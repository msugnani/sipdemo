#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sipdemo {

// Minimal RFC 6455 WebSocket *client* for localhost demos (no TLS).
// Spawns a reader thread after connect to drain server frames and answer
// ping with pong — without that, uvicorn keepalives fill the TCP buffer and
// the connection dies mid-call (STT stops while RTP echo keeps working).
class WsClient {
public:
    WsClient() = default;
    ~WsClient();

    WsClient(const WsClient&) = delete;
    WsClient& operator=(const WsClient&) = delete;

    bool connect(const std::string& url);
    void close();
    bool connected() const;

    bool sendBinary(const uint8_t* data, size_t len);
    bool sendBinary(const std::vector<uint8_t>& data) {
        return sendBinary(data.data(), data.size());
    }
    bool sendText(const std::string& text);

private:
    bool sendFrameUnlocked(uint8_t opcode, const uint8_t* data, size_t len);
    void closeFdUnlocked();
    void readerLoop();
    bool readExact(uint8_t* buf, size_t len);

#ifdef _WIN32
    uintptr_t fd_ = ~static_cast<uintptr_t>(0);
#else
    int fd_ = -1;
#endif
    mutable std::mutex mu_;
    bool connected_ = false;
    std::string url_;
    std::atomic<bool> readerStop_{false};
    std::thread readerThread_;
};

}  // namespace sipdemo
