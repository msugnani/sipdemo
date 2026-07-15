#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace sipdemo {

// An RTP packet (RFC 3550). We model the fixed 12-byte header plus optional
// CSRC list and the payload. Header extension and padding are parsed enough to
// be skipped correctly, but we don't interpret extension contents.
struct RtpPacket {
    uint8_t version = 2;
    bool padding = false;
    bool extension = false;
    bool marker = false;
    uint8_t payloadType = 0;  // 0 = PCMU, 8 = PCMA (RFC 3551)
    uint16_t sequence = 0;
    uint32_t timestamp = 0;
    uint32_t ssrc = 0;
    std::vector<uint32_t> csrc;
    std::vector<uint8_t> payload;

    // Parse a raw datagram. Returns nullopt if too short or malformed
    // (bad version, truncated CSRC/extension/padding).
    static std::optional<RtpPacket> parse(const uint8_t* data, size_t len);
    static std::optional<RtpPacket> parse(const std::vector<uint8_t>& buf) {
        return parse(buf.data(), buf.size());
    }

    // Serialize to the wire (network byte order). CSRC count is derived from
    // csrc.size() (clamped to 15).
    std::vector<uint8_t> serialize() const;
};

// 16-bit RTP sequence-number comparison that respects wraparound (RFC 1982
// serial-number arithmetic). Returns true iff `a` precedes `b`.
bool seqLess(uint16_t a, uint16_t b);

// Signed forward distance b - a in modular 16-bit space, in (-32768, 32768].
int seqDiff(uint16_t a, uint16_t b);

}  // namespace sipdemo
