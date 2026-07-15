#pragma once

#include <cstdint>
#include <vector>

namespace sipdemo {

// ITU-T G.711 companding codec: converts between 14/13-bit linear PCM (carried
// in int16 samples) and 8-bit mu-law / A-law code words. This is the audio
// format behind RTP payload types 0 (PCMU) and 8 (PCMA).
namespace g711 {

// mu-law (North America / Japan)
uint8_t encodeMuLaw(int16_t sample);
int16_t decodeMuLaw(uint8_t code);

// A-law (Europe / international)
uint8_t encodeALaw(int16_t sample);
int16_t decodeALaw(uint8_t code);

// Bulk helpers over sample buffers.
std::vector<uint8_t> encodeMuLaw(const std::vector<int16_t>& pcm);
std::vector<int16_t> decodeMuLaw(const std::vector<uint8_t>& codes);
std::vector<uint8_t> encodeALaw(const std::vector<int16_t>& pcm);
std::vector<int16_t> decodeALaw(const std::vector<uint8_t>& codes);

}  // namespace g711
}  // namespace sipdemo
