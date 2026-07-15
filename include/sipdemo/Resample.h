#pragma once

#include <cstdint>
#include <vector>

namespace sipdemo {

// Linear upsample from 8 kHz mono PCM to 16 kHz (insert one interpolated
// sample between each pair). Output length is 2 * input.size() when input is
// non-empty; empty in → empty out.
std::vector<int16_t> upsample8kTo16k(const std::vector<int16_t>& in);

}  // namespace sipdemo
