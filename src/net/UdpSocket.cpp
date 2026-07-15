#include "sipdemo/UdpSocket.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
static constexpr uintptr_t kInvalidFd = ~static_cast<uintptr_t>(0);  // INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
static constexpr int kInvalidFd = -1;
#endif

namespace sipdemo {

namespace {

#ifdef _WIN32
// Winsock needs global init/teardown once per process. A function-local static
// guarantees thread-safe one-time construction and process-exit cleanup.
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

std::string lastError(const char* what) {
#ifdef _WIN32
    return std::string(what) + " failed: WSA error " + std::to_string(WSAGetLastError());
#else
    return std::string(what) + " failed: " + std::strerror(errno);
#endif
}

}  // namespace

UdpSocket::UdpSocket() : fd_(kInvalidFd) {
    ensureWinsock();
    fd_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd_ == kInvalidFd) {
        throw std::runtime_error(lastError("socket"));
    }
}

UdpSocket::~UdpSocket() { closeSocket(); }

UdpSocket::UdpSocket(UdpSocket&& other) noexcept : fd_(other.fd_), localPort_(other.localPort_) {
    other.fd_ = kInvalidFd;
    other.localPort_ = 0;
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        closeSocket();
        fd_ = other.fd_;
        localPort_ = other.localPort_;
        other.fd_ = kInvalidFd;
        other.localPort_ = 0;
    }
    return *this;
}

void UdpSocket::closeSocket() {
    if (fd_ != kInvalidFd) {
#ifdef _WIN32
        ::closesocket(fd_);
#else
        ::close(fd_);
#endif
        fd_ = kInvalidFd;
    }
}

bool UdpSocket::valid() const { return fd_ != kInvalidFd; }

void UdpSocket::bind(const std::string& ip, uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (ip.empty() || ip == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        throw std::runtime_error("bind: invalid IPv4 address '" + ip + "'");
    }

    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        throw std::runtime_error(lastError("bind"));
    }

    // Discover the actual bound port (matters when caller passed 0).
    sockaddr_in bound{};
    socklen_t len = sizeof(bound);
    if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&bound), &len) == 0) {
        localPort_ = ntohs(bound.sin_port);
    } else {
        localPort_ = port;
    }
}

int UdpSocket::sendTo(const uint8_t* data, size_t len, const Endpoint& to) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(to.port);
    if (::inet_pton(AF_INET, to.ip.c_str(), &addr.sin_addr) != 1) {
        throw std::runtime_error("sendTo: invalid IPv4 address '" + to.ip + "'");
    }

    int sent = ::sendto(fd_, reinterpret_cast<const char*>(data), static_cast<int>(len), 0,
                        reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (sent < 0) {
        throw std::runtime_error(lastError("sendto"));
    }
    return sent;
}

std::optional<Datagram> UdpSocket::recvFrom(int timeoutMs) {
    if (timeoutMs >= 0) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd_, &readfds);
        timeval tv{};
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
#ifdef _WIN32
        int nfds = 0;  // ignored on Windows
#else
        int nfds = fd_ + 1;
#endif
        int ready = ::select(nfds, &readfds, nullptr, nullptr, &tv);
        if (ready == 0) {
            return std::nullopt;  // timed out
        }
        if (ready < 0) {
            throw std::runtime_error(lastError("select"));
        }
    }

    uint8_t buf[2048];
    sockaddr_in from{};
    socklen_t fromLen = sizeof(from);
    int n = ::recvfrom(fd_, reinterpret_cast<char*>(buf), static_cast<int>(sizeof(buf)), 0,
                       reinterpret_cast<sockaddr*>(&from), &fromLen);
    if (n < 0) {
#ifdef _WIN32
        // UDP sendto to an unbound peer often yields ICMP port-unreachable;
        // Winsock surfaces that as WSAECONNRESET on the *next* recvfrom.
        // Treat it as a soft miss so media threads stay alive.
        const int err = WSAGetLastError();
        if (err == WSAECONNRESET || err == WSAECONNREFUSED || err == WSAENETRESET) {
            return std::nullopt;
        }
#endif
        throw std::runtime_error(lastError("recvfrom"));
    }

    Datagram dg;
    dg.data.assign(buf, buf + n);
    char ipbuf[INET_ADDRSTRLEN] = {0};
    ::inet_ntop(AF_INET, &from.sin_addr, ipbuf, sizeof(ipbuf));
    dg.from.ip = ipbuf;
    dg.from.port = ntohs(from.sin_port);
    return dg;
}

}  // namespace sipdemo
