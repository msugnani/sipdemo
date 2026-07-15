#include <gtest/gtest.h>

#include "sipdemo/JitterBuffer.h"

using namespace sipdemo;

namespace {
RtpPacket pkt(uint16_t seq) {
    RtpPacket p;
    p.sequence = seq;
    p.timestamp = seq * 160u;
    p.payload = {static_cast<uint8_t>(seq & 0xff)};
    return p;
}
}  // namespace

TEST(Jitter, InOrderPlayout) {
    JitterBuffer jb(JitterBuffer::Config{2, 64});
    for (uint16_t s = 100; s < 106; ++s) jb.insert(pkt(s));

    for (uint16_t s = 100; s < 106; ++s) {
        auto p = jb.pop();
        ASSERT_TRUE(p.has_value());
        EXPECT_EQ(p->sequence, s);
    }
    EXPECT_EQ(jb.stats().lost, 0u);
}

TEST(Jitter, ReordersOutOfOrderArrivals) {
    JitterBuffer jb(JitterBuffer::Config{3, 64});
    jb.insert(pkt(10));
    jb.insert(pkt(12));  // arrives before 11
    jb.insert(pkt(11));

    EXPECT_EQ(jb.pop()->sequence, 10);
    EXPECT_EQ(jb.pop()->sequence, 11);  // restored to order
    EXPECT_EQ(jb.pop()->sequence, 12);
    EXPECT_EQ(jb.stats().reordered, 1u);  // seq 11 came after 12
}

TEST(Jitter, DeclaresLossOnGap) {
    JitterBuffer jb(JitterBuffer::Config{1, 64});
    jb.insert(pkt(20));
    jb.insert(pkt(22));  // 21 missing

    EXPECT_EQ(jb.pop()->sequence, 20);
    bool lost = false;
    auto missing = jb.pop(&lost);  // slot for 21
    EXPECT_FALSE(missing.has_value());
    EXPECT_TRUE(lost);
    EXPECT_EQ(jb.pop()->sequence, 22);
    EXPECT_EQ(jb.stats().lost, 1u);
}

TEST(Jitter, DiscardsLatePacket) {
    JitterBuffer jb(JitterBuffer::Config{1, 64});
    jb.insert(pkt(30));
    jb.insert(pkt(31));
    EXPECT_EQ(jb.pop()->sequence, 30);
    EXPECT_EQ(jb.pop()->sequence, 31);
    // Seq 30 shows up again far too late; must be discarded, not replayed.
    jb.insert(pkt(30));
    EXPECT_EQ(jb.stats().lateDiscarded, 1u);
}

TEST(Jitter, HandlesSequenceWraparound) {
    JitterBuffer jb(JitterBuffer::Config{2, 64});
    jb.insert(pkt(65534));
    jb.insert(pkt(65535));
    jb.insert(pkt(0));
    jb.insert(pkt(1));

    EXPECT_EQ(jb.pop()->sequence, 65534);
    EXPECT_EQ(jb.pop()->sequence, 65535);
    EXPECT_EQ(jb.pop()->sequence, 0);  // wrapped correctly
    EXPECT_EQ(jb.pop()->sequence, 1);
    EXPECT_EQ(jb.stats().lost, 0u);
}

TEST(Jitter, WaitsForTargetDepthBeforePlayout) {
    JitterBuffer jb(JitterBuffer::Config{3, 64});
    jb.insert(pkt(1));
    jb.insert(pkt(2));
    EXPECT_FALSE(jb.pop().has_value());  // not primed yet
    jb.insert(pkt(3));
    EXPECT_TRUE(jb.pop().has_value());   // primed
}
