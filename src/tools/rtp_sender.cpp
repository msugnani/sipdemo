// rtp_sender - a local G.711 RTP source with a "chaos" switch.
//
// Streams 20 ms G.711 packets to a destination, optionally injecting packet
// loss, reordering, and random delay so you can visibly demonstrate the jitter
// buffer smoothing playback. Audio is a generated sine tone (no external WAV
// dependency), or a 16-bit mono 8 kHz PCM WAV via --wav.
//
// Example:
//   rtp_sender --dest 127.0.0.1:40000 --secs 10 --loss 5 --reorder 5 --delay 30

#define _USE_MATH_DEFINES
#include <algorithm>
#include <chrono>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "sipdemo/G711.h"
#include "sipdemo/RtpPacket.h"
#include "sipdemo/UdpSocket.h"

using namespace sipdemo;

namespace {

constexpr int kSampleRate = 8000;
constexpr int kPtimeMs = 20;
constexpr int kSamplesPerPacket = kSampleRate * kPtimeMs / 1000;  // 160

struct Args {
    std::string destIp = "127.0.0.1";
    uint16_t destPort = 40000;
    int seconds = 10;
    int payloadType = 0;  // 0 PCMU, 8 PCMA
    double lossPct = 0.0;
    double reorderPct = 0.0;
    int maxDelayMs = 0;
    std::string wavPath;
};

uint16_t parsePort(const std::string& s) { return static_cast<uint16_t>(std::stoi(s)); }

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (k == "--dest") {
            std::string v = next();
            auto colon = v.find(':');
            if (colon != std::string::npos) {
                a.destIp = v.substr(0, colon);
                a.destPort = parsePort(v.substr(colon + 1));
            }
        } else if (k == "--secs") {
            a.seconds = std::stoi(next());
        } else if (k == "--pt") {
            a.payloadType = std::stoi(next());
        } else if (k == "--loss") {
            a.lossPct = std::stod(next());
        } else if (k == "--reorder") {
            a.reorderPct = std::stod(next());
        } else if (k == "--delay") {
            a.maxDelayMs = std::stoi(next());
        } else if (k == "--wav") {
            a.wavPath = next();
        } else if (k == "--help" || k == "-h") {
            std::printf(
                "Usage: rtp_sender [--dest ip:port] [--secs N] [--pt 0|8]\n"
                "                  [--loss %%] [--reorder %%] [--delay ms] [--wav file]\n");
            std::exit(0);
        }
    }
    return a;
}

// Load 16-bit mono PCM samples from a minimal RIFF/WAVE file. Returns empty on
// any problem (caller falls back to a generated tone).
std::vector<int16_t> loadWavPcm16(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::vector<char> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (buf.size() < 44 || std::memcmp(buf.data(), "RIFF", 4) != 0 ||
        std::memcmp(buf.data() + 8, "WAVE", 4) != 0) {
        return {};
    }
    // Walk chunks to find "data".
    size_t pos = 12;
    while (pos + 8 <= buf.size()) {
        uint32_t sz;
        std::memcpy(&sz, buf.data() + pos + 4, 4);
        if (std::memcmp(buf.data() + pos, "data", 4) == 0) {
            size_t start = pos + 8;
            size_t bytes = std::min<size_t>(sz, buf.size() - start);
            std::vector<int16_t> pcm(bytes / 2);
            std::memcpy(pcm.data(), buf.data() + start, pcm.size() * 2);
            return pcm;
        }
        pos += 8 + sz + (sz & 1);
    }
    return {};
}

// A 440 Hz sine tone, `seconds` long, as int16 PCM at 8 kHz.
std::vector<int16_t> generateTone(int seconds) {
    std::vector<int16_t> pcm(static_cast<size_t>(seconds) * kSampleRate);
    const double freq = 440.0;
    for (size_t i = 0; i < pcm.size(); ++i) {
        double t = static_cast<double>(i) / kSampleRate;
        pcm[i] = static_cast<int16_t>(8000.0 * std::sin(2.0 * M_PI * freq * t));
    }
    return pcm;
}

}  // namespace

int main(int argc, char** argv) {
    Args args = parseArgs(argc, argv);

    std::vector<int16_t> pcm;
    if (!args.wavPath.empty()) {
        pcm = loadWavPcm16(args.wavPath);
        if (pcm.empty()) {
            std::fprintf(stderr, "warning: could not read WAV '%s', using tone\n",
                         args.wavPath.c_str());
        }
    }
    if (pcm.empty()) pcm = generateTone(args.seconds);

    UdpSocket sock;
    sock.bind("0.0.0.0", 0);
    Endpoint dest{args.destIp, args.destPort};

    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> pct(0.0, 100.0);
    std::uniform_int_distribution<int> delayDist(0, std::max(0, args.maxDelayMs));

    std::random_device rd;
    uint32_t ssrc = rd();
    uint16_t seq = static_cast<uint16_t>(rd());
    uint32_t timestamp = rd();

    std::printf("rtp_sender -> %s  pt=%d  loss=%.1f%% reorder=%.1f%% delay<=%dms\n",
                dest.str().c_str(), args.payloadType, args.lossPct, args.reorderPct,
                args.maxDelayMs);

    // A pending packet held back to create reordering.
    std::vector<uint8_t> heldPacket;
    bool holding = false;

    size_t totalPackets = pcm.size() / kSamplesPerPacket;
    auto nextSend = std::chrono::steady_clock::now();
    uint64_t sent = 0, dropped = 0, reordered = 0;

    for (size_t p = 0; p < totalPackets; ++p) {
        // Slice + encode one 20 ms frame.
        std::vector<int16_t> frame(pcm.begin() + p * kSamplesPerPacket,
                                   pcm.begin() + (p + 1) * kSamplesPerPacket);
        std::vector<uint8_t> codes = (args.payloadType == 8) ? g711::encodeALaw(frame)
                                                             : g711::encodeMuLaw(frame);
        RtpPacket rtp;
        rtp.payloadType = static_cast<uint8_t>(args.payloadType);
        rtp.marker = (p == 0);
        rtp.sequence = seq++;
        rtp.timestamp = timestamp;
        rtp.ssrc = ssrc;
        rtp.payload = std::move(codes);
        timestamp += kSamplesPerPacket;
        std::vector<uint8_t> wire = rtp.serialize();

        // Pace to real time (20 ms per packet).
        nextSend += std::chrono::milliseconds(kPtimeMs);
        std::this_thread::sleep_until(nextSend);

        // --- Chaos injection -----------------------------------------------
        if (pct(rng) < args.lossPct) {
            ++dropped;
            continue;  // drop this packet entirely
        }
        if (args.maxDelayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delayDist(rng)));
        }
        if (holding) {
            // Release the previously held packet *after* this one -> reorder.
            sock.sendTo(wire, dest);
            sock.sendTo(heldPacket, dest);
            holding = false;
            ++reordered;
            sent += 2;
            continue;
        }
        if (pct(rng) < args.reorderPct && p + 1 < totalPackets) {
            heldPacket = std::move(wire);  // hold it back one slot
            holding = true;
            continue;
        }
        sock.sendTo(wire, dest);
        ++sent;
    }
    if (holding) {
        sock.sendTo(heldPacket, dest);
        ++sent;
    }

    std::printf("done: sent=%llu dropped=%llu reordered=%llu\n",
                static_cast<unsigned long long>(sent),
                static_cast<unsigned long long>(dropped),
                static_cast<unsigned long long>(reordered));
    return 0;
}
