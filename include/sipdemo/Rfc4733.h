#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace sipdemo {

// One RFC 4733 (telephone-event) payload (minimum 4 bytes).
struct DtmfEvent {
    uint8_t event = 0;   // 0-9, *, #, A-D (0-15)
    bool end = false;    // E bit
    uint8_t volume = 0;  // 0-63 (dBm0 magnitude; ignored by us)
    uint16_t duration = 0;
};

// Parse a telephone-event RTP payload. Returns nullopt if shorter than 4 bytes.
std::optional<DtmfEvent> parseDtmfPayload(const uint8_t* data, size_t len);
inline std::optional<DtmfEvent> parseDtmfPayload(const std::vector<uint8_t>& buf) {
    return parseDtmfPayload(buf.data(), buf.size());
}

// Map event code to a printable digit character ("0"-"9", "*", "#", "A"-"D").
// Returns nullopt for out-of-range events.
std::optional<char> dtmfEventToChar(uint8_t event);

// Stateful decoder: emits once per key *press* on the end bit. Retransmitted
// end packets are suppressed; pressing the same digit again is a new press
// (detected via a non-end packet or a duration restart).
class DtmfDecoder {
public:
    std::optional<char> feed(const std::vector<uint8_t>& payload);
    void reset();

private:
    int lastEvent_ = -1;
    bool emittedForCurrent_ = false;
    uint16_t lastDuration_ = 0;
};

}  // namespace sipdemo
