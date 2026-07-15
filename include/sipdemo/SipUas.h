#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sipdemo/SipMessage.h"
#include "sipdemo/UdpSocket.h"

namespace sipdemo {

// A minimal SIP User Agent Server (UAS), RFC 3261 subset.
//
// Handles INVITE / ACK / BYE / OPTIONS / CANCEL for a single call at a time,
// direct IP-to-IP (no REGISTER, no auth, no Record-Route, no full timer
// machinery). It is intentionally transport-agnostic and side-effect-free:
// you feed it a parsed SipMessage and it returns the responses to send plus a
// media event. That makes the whole state machine unit-testable with scripted
// message sequences.
class SipUas {
public:
    struct Config {
        std::string localIp = "127.0.0.1";
        uint16_t sipPort = 5060;
        uint16_t rtpPort = 40000;  // RTP port advertised in our SDP answer
        std::string userAgent = "sipdemo/0.1";
    };

    enum class MediaAction { None, Start, Stop };

    struct Result {
        std::vector<SipMessage> responses;  // send these back to the peer
        MediaAction media = MediaAction::None;
        Endpoint remoteRtp;    // peer's RTP address (valid when media == Start)
        int payloadType = -1;  // negotiated codec PT (0=PCMU, 8=PCMA)
        int dtmfPayloadType = -1;  // telephone-event PT, or -1 if none
    };

    explicit SipUas(Config cfg);

    // Process one inbound message and return the reaction. Deterministic apart
    // from generated tags/branches (which can be overridden for tests).
    Result handle(const SipMessage& in);

    // Test seams: force the random identifiers to fixed values.
    void setTagForTest(std::string tag) { forcedTag_ = std::move(tag); }

    bool inCall() const { return haveDialog_; }
    const std::string& callId() const { return callId_; }

private:
    SipMessage makeResponse(const SipMessage& req, int code, const std::string& reason);
    std::string newTag();

    Config cfg_;

    // --- Active dialog state (single call) ---------------------------------
    bool haveDialog_ = false;
    std::string callId_;
    std::string localTag_;
    long inviteCSeq_ = -1;
    SipMessage lastInvite_;      // cached to build CANCEL's 487
    SipMessage last2xxInvite_;   // cached for duplicate-INVITE retransmit
    bool answered_ = false;

    std::string forcedTag_;
    uint32_t tagCounter_ = 0;
};

}  // namespace sipdemo
