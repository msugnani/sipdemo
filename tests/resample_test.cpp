#include <gtest/gtest.h>

#include "sipdemo/Resample.h"

using namespace sipdemo;

TEST(Resample, EmptyStaysEmpty) {
    EXPECT_TRUE(upsample8kTo16k({}).empty());
}

TEST(Resample, DoublesLength) {
    std::vector<int16_t> in = {0, 1000, 2000, -1000};
    auto out = upsample8kTo16k(in);
    EXPECT_EQ(out.size(), in.size() * 2);
    EXPECT_EQ(out[0], 0);
    EXPECT_EQ(out[2], 1000);
    // Midpoint between 0 and 1000.
    EXPECT_EQ(out[1], 500);
}
