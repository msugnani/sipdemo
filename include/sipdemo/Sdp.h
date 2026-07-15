#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sipdemo {

// A minimal audio-only SDP session description (RFC 4566), scoped to what an
// offer/answer exchange (RFC 3264) needs for G.711 media + RFC 4733 DTMF.
struct SdpSession {
    // Origin ("o=") and connection ("c=") info.
    std::string username = "-";
    uint64_t sessionId = 0;
    uint64_t sessionVersion = 0;
    std::string connectionAddress = "127.0.0.1";  // c= IN IP4 <addr>

    // Single audio m-line.
    uint16_t audioPort = 0;
    std::vector<int> payloadTypes;  // e.g. {0, 8, 101} for PCMU, PCMA, DTMF
    int ptimeMs = 20;               // a=ptime

    // Dynamic PT for telephone-event/8000 when advertised (-1 = none).
    int dtmfPayloadType = -1;

    // Convenience predicates on the negotiated payload set.
    bool offersPcmu() const;  // PT 0
    bool offersPcma() const;  // PT 8
    bool offersDtmf() const { return dtmfPayloadType >= 0; }

    std::string generate() const;
};

// Parse an SDP body. Returns nullopt if the mandatory audio m-line or
// connection address is missing/garbled.
std::optional<SdpSession> parseSdp(const std::string& body);

// RFC 3264 answer: given a remote offer, choose a codec we support (PCMU
// preferred, then PCMA) and build our answer advertising `localAddr:localPort`.
// Also answers telephone-event when the offer includes it (else we advertise
// PT 101 so softphones that send DTMF after answer still interoperate when
// they offered it — if neither side has it, dtmfPayloadType stays -1).
// Returns nullopt if the offer contains no codec we can speak.
std::optional<SdpSession> makeAnswer(const SdpSession& offer, const std::string& localAddr,
                                     uint16_t localPort);

}  // namespace sipdemo
