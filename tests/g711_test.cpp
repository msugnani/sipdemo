#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>

#include "sipdemo/G711.h"

using namespace sipdemo;
using namespace sipdemo::g711;

namespace {
// G.711 is lossy (8-bit companded), so round-trip is approximate. The error is
// bounded by the quantization step at a given amplitude.
void expectRoundTripClose(int16_t sample, bool mu) {
    uint8_t code = mu ? encodeMuLaw(sample) : encodeALaw(sample);
    int16_t back = mu ? decodeMuLaw(code) : decodeALaw(code);
    int err = std::abs(static_cast<int>(back) - static_cast<int>(sample));
    int tolerance = 128 + std::abs(sample) / 16;  // grows with amplitude
    EXPECT_LE(err, tolerance) << "sample=" << sample << " mu=" << mu;
}
}  // namespace

TEST(G711, MuLawRoundTrip) {
    for (int s = -32000; s <= 32000; s += 137) expectRoundTripClose(static_cast<int16_t>(s), true);
}

TEST(G711, ALawRoundTrip) {
    for (int s = -32000; s <= 32000; s += 137) expectRoundTripClose(static_cast<int16_t>(s), false);
}

TEST(G711, MuLawZeroAndSignPreserved) {
    EXPECT_LE(std::abs(static_cast<int>(decodeMuLaw(encodeMuLaw(0)))), 128);
    EXPECT_GT(decodeMuLaw(encodeMuLaw(10000)), 0);
    EXPECT_LT(decodeMuLaw(encodeMuLaw(-10000)), 0);
}

TEST(G711, ALawSignPreserved) {
    EXPECT_GT(decodeALaw(encodeALaw(10000)), 0);
    EXPECT_LT(decodeALaw(encodeALaw(-10000)), 0);
}

TEST(G711, MuLawKnownEncodings) {
    // Full-scale positive and negative map to the canonical extremes.
    EXPECT_EQ(encodeMuLaw(32767), 0x80);
    EXPECT_EQ(encodeMuLaw(-32768) & 0xFF, 0x00);
}

TEST(G711, BulkHelpers) {
    std::vector<int16_t> pcm = {0, 1000, -1000, 5000, -5000, 20000};
    auto codes = encodeMuLaw(pcm);
    auto back = decodeMuLaw(codes);
    ASSERT_EQ(back.size(), pcm.size());
    for (size_t i = 0; i < pcm.size(); ++i) {
        int err = std::abs(static_cast<int>(back[i]) - static_cast<int>(pcm[i]));
        EXPECT_LE(err, 128 + std::abs(pcm[i]) / 16);
    }
}
