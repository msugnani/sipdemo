#include <gtest/gtest.h>

#include "sipdemo/Sdp.h"

using namespace sipdemo;

namespace {
const char* kOffer =
    "v=0\r\n"
    "o=alice 2890844526 2890844526 IN IP4 10.0.0.9\r\n"
    "s=call\r\n"
    "c=IN IP4 10.0.0.9\r\n"
    "t=0 0\r\n"
    "m=audio 49170 RTP/AVP 0 8\r\n"
    "a=rtpmap:0 PCMU/8000\r\n"
    "a=rtpmap:8 PCMA/8000\r\n"
    "a=ptime:20\r\n";
}  // namespace

TEST(Sdp, ParsesOffer) {
    auto s = parseSdp(kOffer);
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->connectionAddress, "10.0.0.9");
    EXPECT_EQ(s->audioPort, 49170);
    EXPECT_EQ(s->ptimeMs, 20);
    ASSERT_EQ(s->payloadTypes.size(), 2u);
    EXPECT_TRUE(s->offersPcmu());
    EXPECT_TRUE(s->offersPcma());
}

TEST(Sdp, RejectsMissingAudio) {
    EXPECT_FALSE(parseSdp("v=0\r\nc=IN IP4 1.2.3.4\r\n").has_value());
}

TEST(Sdp, AnswerPrefersPcmu) {
    auto offer = parseSdp(kOffer);
    ASSERT_TRUE(offer.has_value());
    auto ans = makeAnswer(*offer, "127.0.0.1", 40000);
    ASSERT_TRUE(ans.has_value());
    EXPECT_EQ(ans->connectionAddress, "127.0.0.1");
    EXPECT_EQ(ans->audioPort, 40000);
    ASSERT_EQ(ans->payloadTypes.size(), 1u);
    EXPECT_EQ(ans->payloadTypes[0], 0);  // PCMU chosen
}

TEST(Sdp, AnswerFallsBackToPcma) {
    auto offer = parseSdp(
        "v=0\r\no=- 1 1 IN IP4 10.0.0.9\r\nc=IN IP4 10.0.0.9\r\n"
        "m=audio 5000 RTP/AVP 8\r\na=rtpmap:8 PCMA/8000\r\n");
    ASSERT_TRUE(offer.has_value());
    auto ans = makeAnswer(*offer, "127.0.0.1", 40000);
    ASSERT_TRUE(ans.has_value());
    EXPECT_EQ(ans->payloadTypes[0], 8);
}

TEST(Sdp, AnswerRejectsNoCommonCodec) {
    auto offer = parseSdp(
        "v=0\r\no=- 1 1 IN IP4 10.0.0.9\r\nc=IN IP4 10.0.0.9\r\n"
        "m=audio 5000 RTP/AVP 96\r\na=rtpmap:96 opus/48000\r\n");
    ASSERT_TRUE(offer.has_value());
    EXPECT_FALSE(makeAnswer(*offer, "127.0.0.1", 40000).has_value());
}

TEST(Sdp, GenerateRoundTrips) {
    auto offer = parseSdp(kOffer);
    auto ans = makeAnswer(*offer, "127.0.0.1", 40000);
    std::string body = ans->generate();
    auto reparsed = parseSdp(body);
    ASSERT_TRUE(reparsed.has_value());
    EXPECT_EQ(reparsed->audioPort, 40000);
    EXPECT_TRUE(reparsed->offersPcmu());
    EXPECT_NE(body.find("a=rtpmap:0 PCMU/8000"), std::string::npos);
}

TEST(Sdp, ParsesTelephoneEvent) {
    auto s = parseSdp(
        "v=0\r\no=- 1 1 IN IP4 10.0.0.9\r\nc=IN IP4 10.0.0.9\r\n"
        "m=audio 5000 RTP/AVP 0 101\r\n"
        "a=rtpmap:0 PCMU/8000\r\n"
        "a=rtpmap:101 telephone-event/8000\r\n"
        "a=fmtp:101 0-15\r\n");
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->dtmfPayloadType, 101);
    EXPECT_TRUE(s->offersDtmf());
}

TEST(Sdp, AnswerIncludesDtmfFromOffer) {
    auto offer = parseSdp(
        "v=0\r\no=- 1 1 IN IP4 10.0.0.9\r\nc=IN IP4 10.0.0.9\r\n"
        "m=audio 5000 RTP/AVP 0 101\r\n"
        "a=rtpmap:0 PCMU/8000\r\n"
        "a=rtpmap:101 telephone-event/8000\r\n");
    ASSERT_TRUE(offer.has_value());
    auto ans = makeAnswer(*offer, "127.0.0.1", 40000);
    ASSERT_TRUE(ans.has_value());
    ASSERT_EQ(ans->payloadTypes.size(), 2u);
    EXPECT_EQ(ans->payloadTypes[0], 0);
    EXPECT_EQ(ans->payloadTypes[1], 101);
    EXPECT_EQ(ans->dtmfPayloadType, 101);
    std::string body = ans->generate();
    EXPECT_NE(body.find("telephone-event/8000"), std::string::npos);
    EXPECT_NE(body.find("a=fmtp:101 0-15"), std::string::npos);
}

