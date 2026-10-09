#include "chronos/ml_inference.h"

#include "chronos/ml_simd.h"

namespace chronos {

// Delegates to the NEON path; on non-NEON targets compute_obi_simd falls
// through to its scalar tail, so callers get one implementation everywhere.
float InferenceEngine::compute_obi(const BookFeatures& features) noexcept {
    return compute_obi_simd(features);
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
