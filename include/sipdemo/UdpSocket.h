#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sipdemo {

// A resolved IPv4 transport address (dotted-quad + host-order port).
struct Endpoint {
    std::string ip;      // e.g. "127.0.0.1"
    uint16_t port = 0;

    bool operator==(const Endpoint& o) const { return ip == o.ip && port == o.port; }
    std::string str() const { return ip + ":" + std::to_string(port); }
};

// Result of a datagram receive.
struct Datagram {
    std::vector<uint8_t> data;
    Endpoint from;
};

// A thin, RAII cross-platform UDP socket (Winsock on Windows, BSD elsewhere).
//
// Deliberately tiny: bind, sendTo, recvFrom (with a timeout so threads can shut
// down cleanly). This is the *only* place platform #ifdefs live.
class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    // Bind to a local interface/port. port 0 => OS picks an ephemeral port
    // (query it afterwards with localPort()). Throws std::runtime_error on error.
    void bind(const std::string& ip, uint16_t port);

    // Send a datagram. Returns bytes sent (throws on hard failure).
    int sendTo(const uint8_t* data, size_t len, const Endpoint& to);
    int sendTo(const std::vector<uint8_t>& data, const Endpoint& to) {
        return sendTo(data.data(), data.size(), to);
    }
    int sendTo(const std::string& data, const Endpoint& to) {
        return sendTo(reinterpret_cast<const uint8_t*>(data.data()), data.size(), to);
    }

    // Blocking receive with a timeout. Returns std::nullopt on timeout.
    std::optional<Datagram> recvFrom(int timeoutMs = -1);

    // The actual local port we are bound to (useful after bind(.., 0)).
    uint16_t localPort() const { return localPort_; }

    bool valid() const;

private:
    void closeSocket();

#ifdef _WIN32
    uintptr_t fd_;  // SOCKET
#else
    int fd_;
#endif
    uint16_t localPort_ = 0;
};

}  // namespace sipdemo
