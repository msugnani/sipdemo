// sipdemo - a minimal, fully hand-written SIP/RTP endpoint.
//
// Threads:
//   * signaling : SIP UDP socket -> SipUas state machine -> responses.
//   * rtp-recv  : RTP socket -> RtpPacket parse -> RtpStats -> JitterBuffer
//                 (DTMF telephone-event packets handled here, out of band).
//   * playout   : paced 20 ms pop from JitterBuffer -> BoundedQueue.
//   * output    : BoundedQueue -> G.711 decode -> upsample/WS -> echo encode.
// The main thread prints a live stats panel once per second.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "sipdemo/BoundedQueue.h"
#include "sipdemo/G711.h"
#include "sipdemo/JitterBuffer.h"
#include "sipdemo/Resample.h"
#include "sipdemo/Rfc4733.h"
#include "sipdemo/RtpPacket.h"
#include "sipdemo/RtpStats.h"
#include "sipdemo/SipParser.h"
#include "sipdemo/SipUas.h"
#include "sipdemo/UdpSocket.h"
#include "sipdemo/WsClient.h"

using namespace sipdemo;

namespace {

std::atomic<bool> g_running{true};
void onSignal(int) { g_running = false; }

double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

struct Config {
    std::string ip = "127.0.0.1";
    uint16_t sipPort = 5060;
    uint16_t rtpPort = 40000;
    std::string wsUrl = "ws://127.0.0.1:8080/stream";
};

// One active media session: owns the RTP socket, the receive/playout/output
// pipeline, and the negotiated peer endpoint.
class MediaSession {
public:
    MediaSession(const std::string& localIp, uint16_t rtpPort, std::string wsUrl)
        : queue_(200), jitter_(JitterBuffer::Config{3, 100}), wsUrl_(std::move(wsUrl)) {
        rtpSock_.bind(localIp, rtpPort);
        outSsrc_ = 0x51D0DE70u;  // "SIPDEMO"-ish marker (hex-safe for MSVC)
    }

    void start(const Endpoint& remote, int payloadType, int dtmfPayloadType) {
        {
            std::lock_guard<std::mutex> lk(remoteMu_);
            remote_ = remote;
            remoteLatched_ = false;
        }
        payloadType_ = payloadType;
        dtmfPayloadType_ = dtmfPayloadType;
        dtmf_.reset();
        active_ = true;

        if (!wsUrl_.empty()) {
            if (ws_.connect(wsUrl_)) {
                std::printf("[ws] connected to %s\n", wsUrl_.c_str());
            } else {
                std::printf("[ws] WARNING: could not connect to %s (STT offline)\n",
                            wsUrl_.c_str());
            }
        }

        recvThread_ = std::thread([this] { recvLoop(); });
        playoutThread_ = std::thread([this] { playoutLoop(); });
        outputThread_ = std::thread([this] { outputLoop(); });
        std::printf("[media] started: peer=%s (SDP) pt=%d dtmf_pt=%d\n", remote.str().c_str(),
                    payloadType_, dtmfPayloadType_);
    }

    void stop() {
        if (!active_.exchange(false)) return;
        queue_.close();
        if (recvThread_.joinable()) recvThread_.join();
        if (playoutThread_.joinable()) playoutThread_.join();
        if (outputThread_.joinable()) outputThread_.join();
        ws_.close();
        std::printf("[media] stopped\n");
    }

    ~MediaSession() { stop(); }

    void printStats() {
        std::lock_guard<std::mutex> lk(statsMu_);
        auto js = jitter_.stats();
        std::printf(
            "[stats] recv=%llu loss=%.1f%% reorder=%llu jitter=%.1fms jbuf=%zu q=%zu\n",
            static_cast<unsigned long long>(rtpStats_.received()),
            rtpStats_.lossFraction() * 100.0,
            static_cast<unsigned long long>(rtpStats_.reordered()), rtpStats_.jitterMs(),
            js.currentDepth, queue_.size());
    }

private:
    void onDtmfDigit(char digit) {
        std::printf("[DTMF MATCHED] Key pressed: %c\n", digit);
        std::string json = std::string("{\"type\":\"dtmf\",\"digit\":\"") + digit + "\"}";
        if (!ws_.sendText(json)) {
            std::printf("[ws] DTMF send failed (gateway down?)\n");
        } else {
            std::printf("[SIGNALING] Dispatched '%c' to local AI controller.\n", digit);
        }
    }

    void recvLoop() {
        try {
            while (active_) {
                std::optional<Datagram> dg;
                try {
                    dg = rtpSock_.recvFrom(200);  // 200 ms poll so we can exit
                } catch (const std::exception& ex) {
                    std::printf("[media] recv error: %s\n", ex.what());
                    continue;
                }
                if (!dg) continue;
                auto pkt = RtpPacket::parse(dg->data);
                if (!pkt) continue;

                // Symmetric RTP: softphones often advertise one c=/m= in SDP but
                // actually send (and expect) media from a different UDP source.
                // Latch onto the first real RTP source so echo reaches them.
                latchRemoteIfNeeded(dg->from);

                // RFC 4733 telephone-event: handle out-of-band, never jitter/echo.
                if (dtmfPayloadType_ >= 0 &&
                    static_cast<int>(pkt->payloadType) == dtmfPayloadType_) {
                    if (auto digit = dtmf_.feed(pkt->payload)) {
                        onDtmfDigit(*digit);
                    }
                    continue;
                }

                {
                    std::lock_guard<std::mutex> lk(statsMu_);
                    rtpStats_.update(pkt->sequence, pkt->timestamp, nowSec());
                }
                jitter_.insert(*pkt);
            }
        } catch (const std::exception& ex) {
            std::printf("[media] recvLoop terminated: %s\n", ex.what());
        }
    }

    void latchRemoteIfNeeded(const Endpoint& from) {
        std::lock_guard<std::mutex> lk(remoteMu_);
        if (!remoteLatched_) {
            if (!(from == remote_)) {
                std::printf("[media] symmetric RTP latch: %s -> %s\n", remote_.str().c_str(),
                            from.str().c_str());
            } else {
                std::printf("[media] symmetric RTP latch: %s (matches SDP)\n", from.str().c_str());
            }
            remote_ = from;
            remoteLatched_ = true;
            return;
        }
        // Allow correction if the peer's source port changes.
        if (!(from == remote_)) {
            std::printf("[media] peer RTP source moved: %s -> %s\n", remote_.str().c_str(),
                        from.str().c_str());
            remote_ = from;
        }
    }

    Endpoint currentRemote() {
        std::lock_guard<std::mutex> lk(remoteMu_);
        return remote_;
    }

    // Drain the jitter buffer at the media clock (20 ms) and hand frames to the
    // decode/output stage through the bounded queue.
    void playoutLoop() {
        try {
            auto next = std::chrono::steady_clock::now();
            while (active_) {
                next += std::chrono::milliseconds(20);
                std::this_thread::sleep_until(next);
                bool lost = false;
                auto pkt = jitter_.pop(&lost);
                if (pkt) {
                    queue_.tryPush(std::move(*pkt));  // drop on overflow (backpressure)
                }
                // On loss/underrun we simply emit nothing (a real playout device
                // would insert silence / PLC here).
            }
        } catch (const std::exception& ex) {
            std::printf("[media] playoutLoop terminated: %s\n", ex.what());
        }
    }

    // Decode to PCM, fork to WebSocket/STT, then echo the audio back to the
    // peer as a fresh RTP stream so the caller hears themselves via the buffer.
    void outputLoop() {
        try {
            while (auto item = queue_.pop()) {
                const RtpPacket& in = *item;
                std::vector<int16_t> pcm = (payloadType_ == 8) ? g711::decodeALaw(in.payload)
                                                              : g711::decodeMuLaw(in.payload);

                // Fork PCM to the AI gateway (16 kHz linear16 for Vosk).
                if (ws_.connected() && !pcm.empty()) {
                    auto pcm16 = upsample8kTo16k(pcm);
                    const auto* bytes = reinterpret_cast<const uint8_t*>(pcm16.data());
                    size_t nbytes = pcm16.size() * sizeof(int16_t);
                    if (!ws_.sendBinary(bytes, nbytes)) {
                        // Soft-fail: keep the call alive without STT.
                    }
                }

                std::vector<uint8_t> codes = (payloadType_ == 8) ? g711::encodeALaw(pcm)
                                                                : g711::encodeMuLaw(pcm);
                RtpPacket out;
                out.payloadType = static_cast<uint8_t>(payloadType_);
                out.sequence = outSeq_++;
                out.timestamp = outTs_;
                out.ssrc = outSsrc_;
                out.payload = std::move(codes);
                outTs_ += static_cast<uint32_t>(pcm.size());
                try {
                    rtpSock_.sendTo(out.serialize(), currentRemote());
                } catch (...) {
                    // Peer may have vanished; ignore and keep the pipeline alive.
                }
            }
        } catch (const std::exception& ex) {
            std::printf("[media] outputLoop terminated: %s\n", ex.what());
        }
    }

    UdpSocket rtpSock_;
    Endpoint remote_;
    std::mutex remoteMu_;
    bool remoteLatched_ = false;
    int payloadType_ = 0;
    int dtmfPayloadType_ = -1;
    std::atomic<bool> active_{false};

    BoundedQueue<RtpPacket> queue_;
    JitterBuffer jitter_;
    RtpStats rtpStats_;
    std::mutex statsMu_;
    DtmfDecoder dtmf_;
    WsClient ws_;
    std::string wsUrl_;

    uint32_t outSsrc_ = 0;
    uint16_t outSeq_ = 0;
    uint32_t outTs_ = 0;

    std::thread recvThread_;
    std::thread playoutThread_;
    std::thread outputThread_;
};

Config parseArgs(int argc, char** argv) {
    Config c;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (k == "--ip") c.ip = next();
        else if (k == "--sip-port") c.sipPort = static_cast<uint16_t>(std::stoi(next()));
        else if (k == "--rtp-port") c.rtpPort = static_cast<uint16_t>(std::stoi(next()));
        else if (k == "--ws-url") c.wsUrl = next();
        else if (k == "--help" || k == "-h") {
            std::printf(
                "Usage: sipdemo [--ip 127.0.0.1] [--sip-port 5060] [--rtp-port 40000]\n"
                "               [--ws-url ws://127.0.0.1:8080/stream]\n"
                "On WSL2 NAT, pass --ip <eth0 address> and dial sip:<that-ip>:5060 "
                "from the Windows softphone.\n");
            std::exit(0);
        }
    }
    return c;
}

}  // namespace

int main(int argc, char** argv) {
    Config cfg = parseArgs(argc, argv);
    std::signal(SIGINT, onSignal);
    // Line-buffer logs so Windows redirected consoles show progress promptly.
    setvbuf(stdout, nullptr, _IONBF, 0);

    UdpSocket sipSock;
    sipSock.bind(cfg.ip, cfg.sipPort);

    SipUas uas(SipUas::Config{cfg.ip, cfg.sipPort, cfg.rtpPort, "sipdemo/0.1"});
    std::unique_ptr<MediaSession> media;

    std::printf("sipdemo listening: SIP %s:%u  RTP %s:%u\n", cfg.ip.c_str(), cfg.sipPort,
                cfg.ip.c_str(), cfg.rtpPort);
    std::printf("STT gateway: %s\n", cfg.wsUrl.c_str());
    std::printf("Point a softphone at sip:%s (SIP port %u; MicroSIP: omit :port in the URI).\n",
                cfg.ip.c_str(), cfg.sipPort);

    double lastStats = nowSec();
    while (g_running) {
        auto dg = sipSock.recvFrom(200);
        if (dg) {
            auto msg = parseSip(std::string(dg->data.begin(), dg->data.end()));
            if (msg && msg->isRequest) {
                SipUas::Result res = uas.handle(*msg);
                for (const auto& resp : res.responses) {
                    std::string wire = resp.serialize();
                    sipSock.sendTo(wire, dg->from);
                }
                if (res.media == SipUas::MediaAction::Start) {
                    media = std::make_unique<MediaSession>(cfg.ip, cfg.rtpPort, cfg.wsUrl);
                    media->start(res.remoteRtp, res.payloadType, res.dtmfPayloadType);
                } else if (res.media == SipUas::MediaAction::Stop) {
                    media.reset();
                }
            }
        }

        if (media && nowSec() - lastStats >= 1.0) {
            media->printStats();
            lastStats = nowSec();
        }
    }

    std::printf("\nshutting down...\n");
    media.reset();
    return 0;
}
