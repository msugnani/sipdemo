#include <gtest/gtest.h>

#include "sipdemo/RtpPacket.h"

using namespace sipdemo;

TEST(Rtp, SerializeParseRoundTrip) {
    RtpPacket p;
    p.marker = true;
    p.payloadType = 0;
    p.sequence = 4321;
    p.timestamp = 160000;
    p.ssrc = 0xDEADBEEF;
    p.payload = {1, 2, 3, 4, 5};

    auto bytes = p.serialize();
    ASSERT_EQ(bytes.size(), 12u + 5u);

    auto parsed = RtpPacket::parse(bytes);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->version, 2);
    EXPECT_TRUE(parsed->marker);
    EXPECT_EQ(parsed->payloadType, 0);
    EXPECT_EQ(parsed->sequence, 4321);
    EXPECT_EQ(parsed->timestamp, 160000u);
    EXPECT_EQ(parsed->ssrc, 0xDEADBEEFu);
    EXPECT_EQ(parsed->payload, p.payload);
}

TEST(Rtp, ParsesCsrcList) {
    RtpPacket p;
    p.csrc = {0x11111111, 0x22222222};
    p.payload = {9};
    auto parsed = RtpPacket::parse(p.serialize());
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->csrc.size(), 2u);
    EXPECT_EQ(parsed->csrc[1], 0x22222222u);
    EXPECT_EQ(parsed->payload.size(), 1u);
}

TEST(Rtp, RejectsMalformed) {
    std::vector<uint8_t> tooShort(8, 0);
    EXPECT_FALSE(RtpPacket::parse(tooShort).has_value());

    std::vector<uint8_t> badVersion(12, 0);
    badVersion[0] = 0x40;  // version 1
    EXPECT_FALSE(RtpPacket::parse(badVersion).has_value());

    std::vector<uint8_t> truncatedCsrc(12, 0);
    truncatedCsrc[0] = 0x82;  // version 2, CC=2 but no CSRC bytes present
    EXPECT_FALSE(RtpPacket::parse(truncatedCsrc).has_value());
}

TEST(Rtp, HandlesPadding) {
    RtpPacket p;
    p.payload = {7, 7};
    auto bytes = p.serialize();
    // Append 2 padding bytes and set the padding flag + trailing count.
    bytes.push_back(0);
    bytes.push_back(2);
    bytes[0] |= 0x20;
    auto parsed = RtpPacket::parse(bytes);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->padding);
    EXPECT_EQ(parsed->payload.size(), 2u);  // padding stripped
}

TEST(Rtp, SequenceWraparoundComparison) {
    EXPECT_TRUE(seqLess(10, 20));
    EXPECT_FALSE(seqLess(20, 10));
    // 65535 precedes 0 across the wrap boundary.
    EXPECT_TRUE(seqLess(65535, 0));
    EXPECT_TRUE(seqLess(65530, 3));
    EXPECT_FALSE(seqLess(0, 65535));
    EXPECT_FALSE(seqLess(5, 5));

    EXPECT_EQ(seqDiff(65535, 0), 1);
    EXPECT_EQ(seqDiff(0, 65535), -1);
    EXPECT_EQ(seqDiff(10, 13), 3);
}
