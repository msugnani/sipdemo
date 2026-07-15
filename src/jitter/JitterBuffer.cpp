#include "sipdemo/JitterBuffer.h"

#include <utility>

namespace sipdemo {

int64_t JitterBuffer::extend(uint16_t seq, int64_t reference) {
    // Choose the multiple-of-65536 offset that places `seq` within half a
    // sequence space of the reference (RFC 1982 style unwrapping).
    int64_t base = reference - (reference % 65536);
    int64_t candidate = base + seq;
    while (candidate - reference > 32768) candidate -= 65536;
    while (reference - candidate > 32768) candidate += 65536;
    return candidate;
}

void JitterBuffer::insert(const RtpPacket& pkt) {
    int64_t ext;
    if (!started_) {
        started_ = true;
        ext = pkt.sequence;
        highestExt_ = ext;
        playoutExt_ = ext;
    } else {
        ext = extend(pkt.sequence, highestExt_);
    }

    // Already played past this slot: too late to be useful.
    if (ext < playoutExt_) {
        ++stats_.lateDiscarded;
        return;
    }
    // Duplicate of something already queued.
    if (buf_.count(ext)) {
        ++stats_.lateDiscarded;
        return;
    }

    if (ext < highestExt_) ++stats_.reordered;
    if (ext > highestExt_) highestExt_ = ext;

    buf_.emplace(ext, pkt);
    ++stats_.received;

    // Enforce the hard cap by dropping the oldest queued packet and skipping
    // the playout cursor forward to it.
    while (buf_.size() > cfg_.capacity) {
        auto oldest = buf_.begin();
        if (oldest->first >= playoutExt_) playoutExt_ = oldest->first + 1;
        buf_.erase(oldest);
        ++stats_.overflowDropped;
    }
}

std::optional<RtpPacket> JitterBuffer::pop(bool* wasLost) {
    if (wasLost) *wasLost = false;
    if (!started_) return std::nullopt;

    // Prime: wait until we've accumulated the target depth before draining.
    if (!primed_) {
        if (buf_.size() < cfg_.targetDepth) return std::nullopt;
        primed_ = true;
    }

    if (buf_.empty()) return std::nullopt;  // underrun

    auto it = buf_.find(playoutExt_);
    if (it != buf_.end()) {
        RtpPacket p = std::move(it->second);
        buf_.erase(it);
        ++playoutExt_;
        return p;
    }

    // Expected packet absent but later packets are queued: declare it lost and
    // advance so the stream keeps flowing.
    ++stats_.lost;
    ++playoutExt_;
    if (wasLost) *wasLost = true;
    return std::nullopt;
}

void JitterBuffer::reset() {
    buf_.clear();
    started_ = false;
    primed_ = false;
    highestExt_ = 0;
    playoutExt_ = 0;
    stats_ = JitterStats{};
}

}  // namespace sipdemo
