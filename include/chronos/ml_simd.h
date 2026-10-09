#pragma once

#include <cstddef>
#include <cstdint>

#include "chronos/ml_inference.h"

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace chronos {

// Order Book Imbalance over the top kObiDepth levels, vectorized with ARM
// NEON. Each float32x4_t holds four price levels' volumes, so one vsubq_f32
// and one vaddq_f32 compute the bid-minus-ask difference and the bid-plus-ask
// total for four levels at once, instead of four scalar iterations each.
//
// kObiDepth (5) isn't a multiple of the 4-lane width, so the bulk is handled
// in 4-wide chunks and the remainder by a scalar tail. Padding the feature
// arrays to 8 would remove the tail but cost an extra 16-byte load on every
// call for lanes that are always zero.
inline float compute_obi_simd(const BookFeatures& features) noexcept {
    float diff_sum = 0.0f;
    float total_sum = 0.0f;
    size_t i = 0;

#if defined(__ARM_NEON)
    float32x4_t diff_acc = vdupq_n_f32(0.0f);
    float32x4_t total_acc = vdupq_n_f32(0.0f);

    for (; i + 4 <= kObiDepth; i += 4) {
        // vld1q_u32 has no alignment requirement beyond the element type, so
        // loading straight from std::array storage is safe; vcvtq_f32_u32
        // converts all four lanes in one instruction.
        const float32x4_t bids = vcvtq_f32_u32(vld1q_u32(&features.bid_qty[i]));
        const float32x4_t asks = vcvtq_f32_u32(vld1q_u32(&features.ask_qty[i]));
        diff_acc = vaddq_f32(diff_acc, vsubq_f32(bids, asks));
        total_acc = vaddq_f32(total_acc, vaddq_f32(bids, asks));
    }

    // Horizontal reduction across lanes (FADDP sequence). This is the one
    // cross-lane step and happens once per call, not once per level.
    diff_sum = vaddvq_f32(diff_acc);
    total_sum = vaddvq_f32(total_acc);
#endif

    for (; i < kObiDepth; ++i) {
        const float bid = static_cast<float>(features.bid_qty[i]);
        const float ask = static_cast<float>(features.ask_qty[i]);
        diff_sum += bid - ask;
        total_sum += bid + ask;
    }

    // Empty book: no liquidity to compute an imbalance from. Boundary case,
    // not a hot-path branch in practice.
    if (total_sum == 0.0f) {
        return 0.0f;
    }
    return diff_sum / total_sum;
}

}  // namespace chronos
