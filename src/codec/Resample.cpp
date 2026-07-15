#include "sipdemo/Resample.h"

namespace sipdemo {

std::vector<int16_t> upsample8kTo16k(const std::vector<int16_t>& in) {
    if (in.empty()) return {};
    std::vector<int16_t> out;
    out.reserve(in.size() * 2);
    for (size_t i = 0; i < in.size(); ++i) {
        out.push_back(in[i]);
        int16_t next = (i + 1 < in.size()) ? in[i + 1] : in[i];
        // Midpoint interpolation (bias toward zero on odd sums is fine for STT).
        out.push_back(static_cast<int16_t>((static_cast<int32_t>(in[i]) + next) / 2));
    }
    return out;
}

}  // namespace sipdemo
