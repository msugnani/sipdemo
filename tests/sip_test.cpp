#include <gtest/gtest.h>

#include "sipdemo/SipParser.h"
#include "sipdemo/SipUas.h"

using namespace sipdemo;

namespace {

std::string crlf(std::string s) {
    // Convert bare \n in test literals to CRLF for wire-accurate messages.
    std::string out;
    for (char c : s) {
        if (c == '\n') out += "\r\n";
        else out += c;
    }
    return out;
}

const char* kInvite =
    "INVITE sip:bob@10.0.0.5 SIP/2.0\n"
    "Via: SIP/2.0/UDP 10.0.0.9:5060;branch=z9hG4bK776asdhds\n"
    "Max-Forwards: 70\n"
    "From: Alice <sip:alice@10.0.0.9>;tag=1928301774\n"
    "To: Bob <sip:bob@10.0.0.5>\n"
    "Call-ID: a84b4c76e66710@10.0.0.9\n"
    "CSeq: 314159 INVITE\n"
    "Contact: <sip:alice@10.0.0.9>\n"
    "Content-Type: application/sdp\n"
    "Content-Length: 129\n"
    "\n"
    "v=0\n"
    "o=alice 2890844526 2890844526 IN IP4 10.0.0.9\n"
    "s=call\n"
    "c=IN IP4 10.0.0.9\n"
    "t=0 0\n"
    "m=audio 49170 RTP/AVP 0 8\n"
    "a=rtpmap:0 PCMU/8000\n";

}  // namespace

TEST(SipParser, ParsesRequestLineAndHeaders) {
    auto msg = parseSip(crlf(kInvite));
    ASSERT_TRUE(msg.has_value());
    EXPECT_TRUE(msg->isRequest);
    EXPECT_EQ(msg->method, "INVITE");
    EXPECT_EQ(msg->requestUri, "sip:bob@10.0.0.5");
    EXPECT_EQ(msg->version, "SIP/2.0");
    EXPECT_EQ(*msg->get("call-id"), "a84b4c76e66710@10.0.0.9");  // case-insensitive
    EXPECT_EQ(msg->getAll("Via").size(), 1u);
}

TEST(SipParser, ExtractsBodyByContentLength) {
    auto msg = parseSip(crlf(kInvite));
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->body.size(), 129u);
    EXPECT_NE(msg->body.find("m=audio 49170"), std::string::npos);
}

TEST(SipParser, ParsesStatusLine) {
    auto msg = parseSip(crlf("SIP/2.0 200 OK\nCSeq: 1 INVITE\n\n"));
    ASSERT_TRUE(msg.has_value());
    EXPECT_FALSE(msg->isRequest);
    EXPECT_EQ(msg->statusCode, 200);
    EXPECT_EQ(msg->reasonPhrase, "OK");
}

TEST(SipParser, HandlesHeaderFolding) {
    auto msg = parseSip(crlf("OPTIONS sip:x SIP/2.0\nSubject: hello\n world\n\n"));
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(*msg->get("Subject"), "hello world");
}

TEST(SipParser, RejectsMalformed) {
    EXPECT_FALSE(parseSip("garbage without crlfcrlf").has_value());
    EXPECT_FALSE(parseSip(crlf("NObodyExpects\n\n")).has_value());          // bad start line
    EXPECT_FALSE(parseSip(crlf("INVITE sip:x SIP/2.0\nBadHeader\n\n")).has_value());  // no colon
    EXPECT_FALSE(parseSip(crlf("SIP/2.0 20 OK\n\n")).has_value());          // bad status code
}

TEST(SipParser, RejectsTruncatedBody) {
    // Content-Length claims more than is present.
    EXPECT_FALSE(parseSip(crlf("INVITE sip:x SIP/2.0\nContent-Length: 50\n\nshort")).has_value());
}

TEST(SipParser, HeaderParamAndCSeq) {
    EXPECT_EQ(*headerParam("Alice <sip:a@h>;tag=abc", "tag"), "abc");
    EXPECT_FALSE(headerParam("Alice <sip:a@h>", "tag").has_value());
    auto c = parseCSeq("314159 INVITE");
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->number, 314159);
    EXPECT_EQ(c->method, "INVITE");
}

TEST(SipParser, SerializeRoundTrips) {
    auto msg = parseSip(crlf(kInvite));
    ASSERT_TRUE(msg.has_value());
    std::string wire = msg->serialize();
    auto again = parseSip(wire);
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(again->method, "INVITE");
    EXPECT_EQ(again->body, msg->body);
    EXPECT_EQ(*again->get("CSeq"), "314159 INVITE");
}

// --------------------------- UAS state machine -----------------------------

TEST(SipUas, InviteProducesRingingAndOkWithSdp) {
    SipUas uas(SipUas::Config{"127.0.0.1", 5060, 40000, "sipdemo-test"});
    uas.setTagForTest("uastag");
    auto invite = parseSip(crlf(kInvite));
    ASSERT_TRUE(invite.has_value());

    auto res = uas.handle(*invite);
    ASSERT_EQ(res.responses.size(), 3u);  // 100, 180, 200
    EXPECT_EQ(res.responses[0].statusCode, 100);
    EXPECT_EQ(res.responses[1].statusCode, 180);

    const SipMessage& ok = res.responses[2];
    EXPECT_EQ(ok.statusCode, 200);
    EXPECT_EQ(*ok.get("CSeq"), "314159 INVITE");        // echoed
    EXPECT_EQ(*ok.get("Call-ID"), *invite->get("Call-ID"));
    EXPECT_TRUE(ok.has("Contact"));
    EXPECT_NE(ok.get("To")->find("tag=uastag"), std::string::npos);  // To-tag added
    EXPECT_NE(ok.body.find("a=rtpmap:0 PCMU/8000"), std::string::npos);

    EXPECT_EQ(res.media, SipUas::MediaAction::Start);
    EXPECT_EQ(res.remoteRtp.ip, "10.0.0.9");
    EXPECT_EQ(res.remoteRtp.port, 49170);
    EXPECT_EQ(res.payloadType, 0);
    EXPECT_EQ(res.dtmfPayloadType, -1);
    EXPECT_TRUE(uas.inCall());
}

TEST(SipUas, DuplicateInviteResendsOkWithoutRestartingMedia) {
    SipUas uas({});
    auto invite = parseSip(crlf(kInvite));
    uas.handle(*invite);
    auto res = uas.handle(*invite);  // retransmission
    ASSERT_EQ(res.responses.size(), 1u);
    EXPECT_EQ(res.responses[0].statusCode, 200);
    EXPECT_EQ(res.media, SipUas::MediaAction::None);  // media not restarted
}

TEST(SipUas, ByeEndsDialogAndStopsMedia) {
    SipUas uas({});
    auto invite = parseSip(crlf(kInvite));
    uas.handle(*invite);

    auto bye = parseSip(crlf(
        "BYE sip:bob@10.0.0.5 SIP/2.0\n"
        "Via: SIP/2.0/UDP 10.0.0.9:5060;branch=z9hG4bKnashds\n"
        "From: Alice <sip:alice@10.0.0.9>;tag=1928301774\n"
        "To: Bob <sip:bob@10.0.0.5>;tag=xyz\n"
        "Call-ID: a84b4c76e66710@10.0.0.9\n"
        "CSeq: 314160 BYE\n\n"));
    ASSERT_TRUE(bye.has_value());
    auto res = uas.handle(*bye);
    ASSERT_EQ(res.responses.size(), 1u);
    EXPECT_EQ(res.responses[0].statusCode, 200);
    EXPECT_EQ(res.media, SipUas::MediaAction::Stop);
    EXPECT_FALSE(uas.inCall());
}

TEST(SipUas, OptionsAnsweredOutOfDialog) {
    SipUas uas({});
    auto opt = parseSip(crlf(
        "OPTIONS sip:bob@10.0.0.5 SIP/2.0\n"
        "Via: SIP/2.0/UDP 10.0.0.9:5060;branch=z9hG4bKopt\n"
        "From: Alice <sip:alice@10.0.0.9>;tag=1\n"
        "To: Bob <sip:bob@10.0.0.5>\n"
        "Call-ID: opt-call\n"
        "CSeq: 1 OPTIONS\n\n"));
    auto res = uas.handle(*opt);
    ASSERT_EQ(res.responses.size(), 1u);
    EXPECT_EQ(res.responses[0].statusCode, 200);
    EXPECT_TRUE(res.responses[0].has("Allow"));
    EXPECT_FALSE(uas.inCall());
}

TEST(SipUas, ByeForUnknownDialogGives481) {
    SipUas uas({});
    auto bye = parseSip(crlf(
        "BYE sip:x SIP/2.0\nVia: SIP/2.0/UDP h;branch=z\n"
        "From: <sip:a>;tag=1\nTo: <sip:b>;tag=2\nCall-ID: nope\nCSeq: 2 BYE\n\n"));
    auto res = uas.handle(*bye);
    ASSERT_EQ(res.responses.size(), 1u);
    EXPECT_EQ(res.responses[0].statusCode, 481);
}
