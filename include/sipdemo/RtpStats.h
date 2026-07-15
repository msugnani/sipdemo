#pragma once

#include <cstdint>

namespace sipdemo {

// Receiver-side RTP reception statistics (RFC 3550): packet loss, reordering,
// and the smoothed interarrival jitter estimate used in RTCP receiver reports.
class RtpStats {
public:
    explicit RtpStats(uint32_t clockRateHz = 8000) : clockRate_(clockRateHz) {}

    // Feed one received packet.
    //   seq          : 16-bit RTP sequence number
    //   rtpTimestamp : RTP media timestamp from the packet
    //   arrivalSec   : local arrival time in seconds (monotonic clock)
    void update(uint16_t seq, uint32_t rtpTimestamp, double arrivalSec);

    uint64_t received() const { return received_; }
    // Packets expected across the observed sequence range.
    uint64_t expected() const;
    // expected - received, clamped at zero.
    int64_t lost() const;
    double lossFraction() const;  // 0..1
    uint64_t reordered() const { return reordered_; }

    // Interarrival jitter in RTP timestamp units and in milliseconds.
    double jitter() const { return jitter_; }
    double jitterMs() const { return jitter_ / clockRate_ * 1000.0; }

    void reset();

private:
    static int64_t extend(uint16_t seq, int64_t reference);

    uint32_t clockRate_;
    bool started_ = false;
    int64_t baseExt_ = 0;
    int64_t maxExt_ = 0;
    uint64_t received_ = 0;
    uint64_t reordered_ = 0;

    double lastTransit_ = 0.0;
    bool haveTransit_ = false;
    double jitter_ = 0.0;
};

}  // namespace sipdemo
