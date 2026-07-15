#include "sipdemo/WsClient.h"

#include <cstdlib>
#include <cstring>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
static constexpr uintptr_t kInvalidFd = ~static_cast<uintptr_t>(0);
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
static constexpr int kInvalidFd = -1;
#endif

namespace sipdemo {

namespace {

#ifdef _WIN32
struct WinsockGuard {
    WinsockGuard() {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }
    ~WinsockGuard() { WSACleanup(); }
};
void ensureWinsock() { static WinsockGuard guard; }
#else
void ensureWinsock() {}
#endif

struct UrlParts {
    std::string host;
    uint16_t port = 80;
    std::string path = "/";
};

std::optional<UrlParts> parseWsUrl(const std::string& url) {
    const std::string prefix = "ws://";
    if (url.compare(0, prefix.size(), prefix) != 0) return std::nullopt;
    auto rest = url.substr(prefix.size());
    auto slash = rest.find('/');
    std::string hostPort = slash == std::string::npos ? rest : rest.substr(0, slash);
    std::string path = slash == std::string::npos ? "/" : rest.substr(slash);

    UrlParts p;
    p.path = path.empty() ? "/" : path;
    auto colon = hostPort.rfind(':');
    if (colon != std::string::npos && hostPort.find(']') == std::string::npos) {
        p.host = hostPort.substr(0, colon);
        p.port = static_cast<uint16_t>(std::atoi(hostPort.substr(colon + 1).c_str()));
    } else {
        p.host = hostPort;
        p.port = 80;
    }
    if (p.host.empty() || p.port == 0) return std::nullopt;
    return p;
}

std::string randomWsKey() {
    static const char* b64 =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 255);
    uint8_t raw[16];
    for (auto& b : raw) b = static_cast<uint8_t>(dist(gen));

    std::string out;
    out.reserve(24);
    int i = 0;
    for (; i + 2 < 16; i += 3) {
        uint32_t n = (static_cast<uint32_t>(raw[i]) << 16) |
                     (static_cast<uint32_t>(raw[i + 1]) << 8) |
                     static_cast<uint32_t>(raw[i + 2]);
        out.push_back(b64[(n >> 18) & 63]);
        out.push_back(b64[(n >> 12) & 63]);
        out.push_back(b64[(n >> 6) & 63]);
        out.push_back(b64[n & 63]);
    }
    uint32_t n = static_cast<uint32_t>(raw[i]) << 16;
    out.push_back(b64[(n >> 18) & 63]);
    out.push_back(b64[(n >> 12) & 63]);
    out.push_back('=');
    out.push_back('=');
    return out;
}

bool sendAll(decltype(kInvalidFd) fd, const char* data, size_t len) {
    size_t off = 0;
    while (off < len) {
#ifdef _WIN32
        int n = ::send(fd, data + off, static_cast<int>(len - off), 0);
#else
        ssize_t n = ::send(fd, data + off, len - off, 0);
#endif
        if (n <= 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}

}  // namespace

WsClient::~WsClient() { close(); }

void WsClient::closeFdUnlocked() {
    if (fd_ != kInvalidFd) {
#ifdef _WIN32
        ::closesocket(fd_);
#else
        ::close(fd_);
#endif
        fd_ = kInvalidFd;
    }
    connected_ = false;
}

void WsClient::close() {
    readerStop_ = true;
    {
        std::lock_guard<std::mutex> lk(mu_);
        closeFdUnlocked();
    }
    if (readerThread_.joinable()) readerThread_.join();
}

bool WsClient::connected() const {
    std::lock_guard<std::mutex> lk(mu_);
    return connected_;
}

bool WsClient::readExact(uint8_t* buf, size_t len) {
    size_t off = 0;
    while (off < len && !readerStop_) {
#ifdef _WIN32
        int n = ::recv(fd_, reinterpret_cast<char*>(buf + off), static_cast<int>(len - off), 0);
#else
        ssize_t n = ::recv(fd_, buf + off, len - off, 0);
#endif
        if (n <= 0) return false;
        off += static_cast<size_t>(n);
    }
    return off == len;
}

void WsClient::readerLoop() {
    // Drain server→client frames. Answer ping (0x9) with pong (0xA).
    while (!readerStop_) {
        uint8_t hdr[2];
        if (!readExact(hdr, 2)) break;

        const uint8_t opcode = hdr[0] & 0x0f;
        const bool masked = (hdr[1] & 0x80) != 0;
        uint64_t payloadLen = hdr[1] & 0x7f;

        if (payloadLen == 126) {
            uint8_t ext[2];
            if (!readExact(ext, 2)) break;
            payloadLen = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
        } else if (payloadLen == 127) {
            uint8_t ext[8];
            if (!readExact(ext, 8)) break;
            payloadLen = 0;
            for (int i = 0; i < 8; ++i) payloadLen = (payloadLen << 8) | ext[i];
        }

        uint8_t mask[4] = {};
        if (masked) {
            if (!readExact(mask, 4)) break;
        }

        if (payloadLen > 1 << 20) break;  // refuse absurd frames
        std::vector<uint8_t> payload(static_cast<size_t>(payloadLen));
        if (payloadLen > 0 && !readExact(payload.data(), payload.size())) break;
        if (masked) {
            for (size_t i = 0; i < payload.size(); ++i) payload[i] ^= mask[i % 4];
        }

        if (opcode == 0x8) {  // close
            break;
        }
        if (opcode == 0x9) {  // ping → pong
            std::lock_guard<std::mutex> lk(mu_);
            if (connected_) {
                // Pong frames from client must still be masked.
                sendFrameUnlocked(0xA, payload.data(), payload.size());
            }
        }
        // text/binary/pong from server: ignore for this demo client
    }

    std::lock_guard<std::mutex> lk(mu_);
    closeFdUnlocked();
}

bool WsClient::connect(const std::string& url) {
    close();  // stop any previous reader
    readerStop_ = false;
    ensureWinsock();

    auto parts = parseWsUrl(url);
    if (!parts) return false;

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    std::string portStr = std::to_string(parts->port);
    if (::getaddrinfo(parts->host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
        return false;
    }

    decltype(kInvalidFd) fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd == kInvalidFd) {
        ::freeaddrinfo(res);
        return false;
    }

    if (::connect(fd, res->ai_addr, static_cast<socklen_t>(res->ai_addrlen)) != 0) {
        ::freeaddrinfo(res);
#ifdef _WIN32
        ::closesocket(fd);
#else
        ::close(fd);
#endif
        return false;
    }
    ::freeaddrinfo(res);

    std::string key = randomWsKey();
    std::ostringstream req;
    req << "GET " << parts->path << " HTTP/1.1\r\n"
        << "Host: " << parts->host << ":" << parts->port << "\r\n"
        << "Upgrade: websocket\r\n"
        << "Connection: Upgrade\r\n"
        << "Sec-WebSocket-Key: " << key << "\r\n"
        << "Sec-WebSocket-Version: 13\r\n"
        << "\r\n";
    std::string reqStr = req.str();
    if (!sendAll(fd, reqStr.data(), reqStr.size())) {
#ifdef _WIN32
        ::closesocket(fd);
#else
        ::close(fd);
#endif
        return false;
    }

    std::string resp;
    char buf[1024];
    while (resp.find("\r\n\r\n") == std::string::npos) {
#ifdef _WIN32
        int n = ::recv(fd, buf, sizeof(buf), 0);
#else
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
#endif
        if (n <= 0) {
#ifdef _WIN32
            ::closesocket(fd);
#else
            ::close(fd);
#endif
            return false;
        }
        resp.append(buf, buf + n);
        if (resp.size() > 8192) {
#ifdef _WIN32
            ::closesocket(fd);
#else
            ::close(fd);
#endif
            return false;
        }
    }

    if (resp.find("101") == std::string::npos) {
#ifdef _WIN32
        ::closesocket(fd);
#else
        ::close(fd);
#endif
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(mu_);
        fd_ = fd;
        url_ = url;
        connected_ = true;
    }
    // Start reader only after releasing mu_: child may lock for ping→pong;
    // holding mu_ across spawn (or join) is a lock-ordering hazard.
    readerThread_ = std::thread([this] { readerLoop(); });
    return true;
}

bool WsClient::sendFrameUnlocked(uint8_t opcode, const uint8_t* data, size_t len) {
    if (!connected_ || fd_ == kInvalidFd) return false;

    std::vector<uint8_t> frame;
    frame.reserve(14 + len);
    frame.push_back(static_cast<uint8_t>(0x80 | (opcode & 0x0f)));

    uint8_t maskBit = 0x80;
    if (len < 126) {
        frame.push_back(static_cast<uint8_t>(maskBit | len));
    } else if (len <= 0xffff) {
        frame.push_back(static_cast<uint8_t>(maskBit | 126));
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xff));
        frame.push_back(static_cast<uint8_t>(len & 0xff));
    } else {
        return false;
    }

    uint8_t mask[4];
    std::random_device rd;
    for (int i = 0; i < 4; ++i) mask[i] = static_cast<uint8_t>(rd());
    frame.insert(frame.end(), mask, mask + 4);

    for (size_t i = 0; i < len; ++i) {
        frame.push_back(static_cast<uint8_t>(data[i] ^ mask[i % 4]));
    }

    if (!sendAll(fd_, reinterpret_cast<const char*>(frame.data()), frame.size())) {
        closeFdUnlocked();
        return false;
    }
    return true;
}

bool WsClient::sendBinary(const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lk(mu_);
    return sendFrameUnlocked(0x2, data, len);
}

bool WsClient::sendText(const std::string& text) {
    std::lock_guard<std::mutex> lk(mu_);
    return sendFrameUnlocked(0x1, reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

}  // namespace sipdemo
