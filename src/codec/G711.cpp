#include "sipdemo/G711.h"

namespace sipdemo {
namespace g711 {

namespace {
constexpr int kMuBias = 0x84;  // 132
constexpr int kMuClip = 32635;

int seg(int value, const int* table, int size) {
    for (int i = 0; i < size; ++i) {
        if (value <= table[i]) return i;
    }
    return size;
}
}  // namespace

// -------- mu-law -----------------------------------------------------------
uint8_t encodeMuLaw(int16_t sample) {
    int sign = (sample >> 8) & 0x80;
    int mag = sign ? -static_cast<int>(sample) : sample;
    if (mag > kMuClip) mag = kMuClip;
    mag += kMuBias;

    static const int segEnd[8] = {0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF, 0x3FFF, 0x7FFF};
    int segment = seg(mag, segEnd, 8);

    uint8_t code;
    if (segment >= 8) {
        code = 0x7F;
    } else {
        code = static_cast<uint8_t>((segment << 4) | ((mag >> (segment + 3)) & 0x0F));
    }
    return static_cast<uint8_t>(~(code | sign));  // complement per G.711
}

int16_t decodeMuLaw(uint8_t code) {
    code = static_cast<uint8_t>(~code);
    int sign = code & 0x80;
    int segment = (code >> 4) & 0x07;
    int mantissa = code & 0x0F;
    int mag = ((mantissa << 3) + kMuBias) << segment;
    mag -= kMuBias;
    return static_cast<int16_t>(sign ? -mag : mag);
}

// -------- A-law (ITU-T / Sun reference implementation) ---------------------
uint8_t encodeALaw(int16_t sample) {
    int pcm = sample >> 3;  // 16-bit -> 13-bit working range
    uint8_t mask;
    if (pcm >= 0) {
        mask = 0xD5;  // sign bit set for positive, plus the 0x55 even-bit toggle
    } else {
        mask = 0x55;
        pcm = -pcm - 1;
        if (pcm < 0) pcm = 0;
    }

    static const int segEnd[8] = {0x1F, 0x3F, 0x7F, 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF};
    int segment = seg(pcm, segEnd, 8);
    if (segment >= 8) return static_cast<uint8_t>(0x7F ^ mask);

    uint8_t code = static_cast<uint8_t>(segment << 4);
    if (segment < 2) {
        code |= (pcm >> 1) & 0x0F;
    } else {
        code |= (pcm >> segment) & 0x0F;
    }
    return static_cast<uint8_t>(code ^ mask);
}

int16_t decodeALaw(uint8_t code) {
    code ^= 0x55;
    int t = (code & 0x0F) << 4;
    int segment = (code & 0x70) >> 4;
    switch (segment) {
        case 0: t += 8; break;
        case 1: t += 0x108; break;
        default:
            t += 0x108;
            t <<= (segment - 1);
            break;
    }
    return static_cast<int16_t>((code & 0x80) ? t : -t);
}

// -------- bulk helpers -----------------------------------------------------
std::vector<uint8_t> encodeMuLaw(const std::vector<int16_t>& pcm) {
    std::vector<uint8_t> out;
    out.reserve(pcm.size());
    for (int16_t s : pcm) out.push_back(encodeMuLaw(s));
    return out;
}
std::vector<int16_t> decodeMuLaw(const std::vector<uint8_t>& codes) {
    std::vector<int16_t> out;
    out.reserve(codes.size());
    for (uint8_t c : codes) out.push_back(decodeMuLaw(c));
    return out;
}
std::vector<uint8_t> encodeALaw(const std::vector<int16_t>& pcm) {
    std::vector<uint8_t> out;
    out.reserve(pcm.size());
    for (int16_t s : pcm) out.push_back(encodeALaw(s));
    return out;
}
std::vector<int16_t> decodeALaw(const std::vector<uint8_t>& codes) {
    std::vector<int16_t> out;
    out.reserve(codes.size());
    for (uint8_t c : codes) out.push_back(decodeALaw(c));
    return out;
}

}  // namespace g711
}  // namespace sipdemo
