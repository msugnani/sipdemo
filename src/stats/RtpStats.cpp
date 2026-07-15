#include "sipdemo/RtpStats.h"

#include <cmath>

namespace sipdemo {

int64_t RtpStats::extend(uint16_t seq, int64_t reference) {
    int64_t base = reference - (reference % 65536);
    int64_t candidate = base + seq;
    while (candidate - reference > 32768) candidate -= 65536;
    while (reference - candidate > 32768) candidate += 65536;
    return candidate;
}

void RtpStats::update(uint16_t seq, uint32_t rtpTimestamp, double arrivalSec) {
    int64_t ext;
    if (!started_) {
        started_ = true;
        ext = seq;
        baseExt_ = ext;
        maxExt_ = ext;
    } else {
        ext = extend(seq, maxExt_);
        if (ext < maxExt_) ++reordered_;
        if (ext > maxExt_) maxExt_ = ext;
    }
    ++received_;

    // RFC 3550 interarrival jitter. Transit = arrival - sender timestamp, both
    // in RTP timestamp units. D is the difference of transit times; the jitter
    // estimate is a first-order low-pass (gain 1/16) of |D|.
    double transit = arrivalSec * clockRate_ - static_cast<double>(rtpTimestamp);
    if (haveTransit_) {
        double d = std::fabs(transit - lastTransit_);
        jitter_ += (d - jitter_) / 16.0;
    }
    lastTransit_ = transit;
    haveTransit_ = true;
}

uint64_t RtpStats::expected() const {
    if (!started_) return 0;
    return static_cast<uint64_t>(maxExt_ - baseExt_ + 1);
}

int64_t RtpStats::lost() const {
    int64_t l = static_cast<int64_t>(expected()) - static_cast<int64_t>(received_);
    return l > 0 ? l : 0;
}

double RtpStats::lossFraction() const {
    uint64_t exp = expected();
    if (exp == 0) return 0.0;
    return static_cast<double>(lost()) / static_cast<double>(exp);
}

void RtpStats::reset() {
    started_ = false;
    baseExt_ = maxExt_ = 0;
    received_ = reordered_ = 0;
    lastTransit_ = 0.0;
    haveTransit_ = false;
    jitter_ = 0.0;
}

}  // namespace sipdemo
