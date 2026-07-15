#include <gtest/gtest.h>

#include "sipdemo/Rfc4733.h"

using namespace sipdemo;

TEST(Dtmf, ParsesPayload) {
    // event=5, end=1, volume=10, duration=160
    uint8_t raw[] = {5, static_cast<uint8_t>(0x80 | 10), 0x00, 0xa0};
    auto ev = parseDtmfPayload(raw, sizeof(raw));
    ASSERT_TRUE(ev.has_value());
    EXPECT_EQ(ev->event, 5);
    EXPECT_TRUE(ev->end);
    EXPECT_EQ(ev->volume, 10);
    EXPECT_EQ(ev->duration, 160);
}

TEST(Dtmf, EventToChar) {
    EXPECT_EQ(dtmfEventToChar(0), '0');
    EXPECT_EQ(dtmfEventToChar(5), '5');
    EXPECT_EQ(dtmfEventToChar(10), '*');
    EXPECT_EQ(dtmfEventToChar(11), '#');
    EXPECT_EQ(dtmfEventToChar(12), 'A');
    EXPECT_FALSE(dtmfEventToChar(99).has_value());
}

TEST(Dtmf, DecoderEmitsOnceOnEndBit) {
    DtmfDecoder dec;
    // Start of digit 5 (no end bit).
    std::vector<uint8_t> start = {5, 10, 0x00, 0x50};
    EXPECT_FALSE(dec.feed(start).has_value());

    // End packet.
    std::vector<uint8_t> end = {5, static_cast<uint8_t>(0x80 | 10), 0x00, 0xa0};
    auto d1 = dec.feed(end);
    ASSERT_TRUE(d1.has_value());
    EXPECT_EQ(*d1, '5');

    // Retransmitted end — suppressed.
    EXPECT_FALSE(dec.feed(end).has_value());

    // New digit 9.
    std::vector<uint8_t> nine = {9, static_cast<uint8_t>(0x80 | 10), 0x00, 0x80};
    auto d2 = dec.feed(nine);
    ASSERT_TRUE(d2.has_value());
    EXPECT_EQ(*d2, '9');
}

TEST(Dtmf, DecoderAllowsSameDigitTwice) {
    DtmfDecoder dec;
    std::vector<uint8_t> start = {5, 10, 0x00, 0x40};
    std::vector<uint8_t> end = {5, static_cast<uint8_t>(0x80 | 10), 0x00, 0xa0};

    EXPECT_FALSE(dec.feed(start).has_value());
    auto first = dec.feed(end);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(*first, '5');
    EXPECT_FALSE(dec.feed(end).has_value());  // retransmit

    // Press 5 again: new start clears latch, new end emits again.
    EXPECT_FALSE(dec.feed(start).has_value());
    auto second = dec.feed(end);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*second, '5');
}

