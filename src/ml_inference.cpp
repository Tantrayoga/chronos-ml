#include "chronos/ml_inference.h"

namespace chronos {

float InferenceEngine::compute_obi(const BookFeatures& features) noexcept {
    uint32_t bid_sum = 0;
    uint32_t ask_sum = 0;

    // Fixed trip count known at compile time (kObiDepth is constexpr), so
    // the compiler fully unrolls this: no loop-exit branch to mispredict and
    // no runtime bounds check beyond what's already guaranteed by the array
    // type.
    for (size_t i = 0; i < kObiDepth; ++i) {
        bid_sum += features.bid_qty[i];
        ask_sum += features.ask_qty[i];
    }

    const uint32_t total = bid_sum + ask_sum;
    if (total == 0) {
        // Genuine boundary case (empty book), not a hot-path branch — this
        // only fires when there is no liquidity to compute an imbalance
        // from at all.
        return 0.0f;
    }

    return (static_cast<float>(bid_sum) - static_cast<float>(ask_sum)) / static_cast<float>(total);
}

PricePrediction InferenceEngine::predict(const BookFeatures& features) const noexcept {
    const float obi = compute_obi(features);
    const float score = kWeights[0] * obi + kWeights[1] * static_cast<float>(features.spread) + kWeights[2];

    // Branchless three-way decision: each comparison lowers to a compare
    // plus a set-on-condition (e.g. `cset` on ARM64), not a conditional
    // jump, so the result is a fixed-latency arithmetic expression rather
    // than a data-dependent branch the predictor has to guess.
    const int is_up = static_cast<int>(score > kUpThreshold);
    const int is_down = static_cast<int>(score < kDownThreshold);
    return static_cast<PricePrediction>(is_up - is_down);
}

}  // namespace chronos
