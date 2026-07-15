#include "sipdemo/RtpPacket.h"

namespace sipdemo {

namespace {
uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t rd32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}
void wr16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x & 0xff));
}
void wr32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 24));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x & 0xff));
}
}  // namespace

std::optional<RtpPacket> RtpPacket::parse(const uint8_t* data, size_t len) {
    if (len < 12) return std::nullopt;  // fixed header

    RtpPacket p;
    uint8_t b0 = data[0];
    p.version = (b0 >> 6) & 0x03;
    if (p.version != 2) return std::nullopt;
    p.padding = (b0 >> 5) & 0x01;
    p.extension = (b0 >> 4) & 0x01;
    uint8_t cc = b0 & 0x0f;

    uint8_t b1 = data[1];
    p.marker = (b1 >> 7) & 0x01;
    p.payloadType = b1 & 0x7f;

    p.sequence = rd16(data + 2);
    p.timestamp = rd32(data + 4);
    p.ssrc = rd32(data + 8);

    size_t offset = 12;
    if (len < offset + cc * 4u) return std::nullopt;
    for (uint8_t i = 0; i < cc; ++i) {
        p.csrc.push_back(rd32(data + offset));
        offset += 4;
    }

    if (p.extension) {
        if (len < offset + 4) return std::nullopt;
        uint16_t extWords = rd16(data + offset + 2);
        size_t extBytes = 4 + static_cast<size_t>(extWords) * 4;
        if (len < offset + extBytes) return std::nullopt;
        offset += extBytes;  // header extension skipped, not interpreted
    }

    size_t payloadEnd = len;
    if (p.padding) {
        uint8_t padLen = data[len - 1];
        if (padLen == 0 || padLen > len - offset) return std::nullopt;
        payloadEnd = len - padLen;
    }

    p.payload.assign(data + offset, data + payloadEnd);
    return p;
}

std::vector<uint8_t> RtpPacket::serialize() const {
    std::vector<uint8_t> out;
    out.reserve(12 + csrc.size() * 4 + payload.size());

    uint8_t cc = static_cast<uint8_t>(csrc.size() > 15 ? 15 : csrc.size());
    uint8_t b0 = static_cast<uint8_t>((version & 0x03) << 6);
    if (padding) b0 |= 0x20;
    if (extension) b0 |= 0x10;
    b0 |= cc;
    out.push_back(b0);

    uint8_t b1 = static_cast<uint8_t>(payloadType & 0x7f);
    if (marker) b1 |= 0x80;
    out.push_back(b1);

    wr16(out, sequence);
    wr32(out, timestamp);
    wr32(out, ssrc);
    for (uint8_t i = 0; i < cc; ++i) wr32(out, csrc[i]);

    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

bool seqLess(uint16_t a, uint16_t b) {
    // RFC 1982: a < b iff the forward gap is smaller than half the space.
    return a != b && static_cast<uint16_t>(b - a) < 0x8000;
}

int seqDiff(uint16_t a, uint16_t b) {
    return static_cast<int>(static_cast<int16_t>(b - a));
}

}  // namespace sipdemo
