#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

#include "sipdemo/RtpPacket.h"

namespace sipdemo {

struct JitterStats {
    uint64_t received = 0;         // packets accepted into the buffer
    uint64_t lost = 0;             // gaps declared unrecoverable at playout
    uint64_t reordered = 0;        // arrived after a higher sequence number
    uint64_t lateDiscarded = 0;    // arrived after their playout slot passed
    uint64_t overflowDropped = 0;  // dropped because the buffer was full
    size_t currentDepth = 0;       // packets currently queued
};

// A sequence-ordered jitter buffer.
//
// Incoming RTP packets are keyed by an *extended* (unwrapped) 64-bit sequence
// number so 16-bit wraparound is handled transparently. Playout drains packets
// strictly in order once the buffer has been primed to `targetDepth`, trading a
// little latency for smoothness. Missing packets become "lost" (a silence /
// PLC slot) as soon as a later packet is available.
class JitterBuffer {
public:
    struct Config {
        size_t targetDepth = 3;   // packets to buffer before playout starts
        size_t capacity = 64;     // hard cap; oldest is dropped past this
    };

    explicit JitterBuffer(Config cfg = {}) : cfg_(cfg) {}

    // Add a received packet. Late/duplicate packets are dropped (and counted).
    void insert(const RtpPacket& pkt);

    // Pull the next packet for playout, in sequence order.
    //   - a packet, if the next expected sequence number is available;
    //   - std::nullopt with a lost/underrun slot otherwise (play silence/PLC).
    // `wasLost` (if provided) distinguishes a declared loss (true) from a
    // not-yet-primed / underrun state (false).
    std::optional<RtpPacket> pop(bool* wasLost = nullptr);

    void reset();

    JitterStats stats() const {
        JitterStats s = stats_;
        s.currentDepth = buf_.size();
        return s;
    }
    size_t size() const { return buf_.size(); }

private:
    // Map a 16-bit sequence number to the 64-bit value nearest `reference`.
    static int64_t extend(uint16_t seq, int64_t reference);

    Config cfg_;
    std::map<int64_t, RtpPacket> buf_;
    bool started_ = false;
    bool primed_ = false;
    int64_t highestExt_ = 0;   // largest sequence number seen
    int64_t playoutExt_ = 0;   // next sequence number to emit
    JitterStats stats_;
};

}  // namespace sipdemo
