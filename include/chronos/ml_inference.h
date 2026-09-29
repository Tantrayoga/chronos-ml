#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace chronos {

// Number of price levels on each side folded into the imbalance calculation.
// Fixed at compile time so BookFeatures stays a flat, stack-resident value
// type — no heap, no dynamic sizing anywhere in the inference path.
inline constexpr size_t kObiDepth = 5;

// Pre-extracted, model-ready features. Decoupled from OrderBook<> itself
// (rather than templated on its parameters) so InferenceEngine has a stable,
// non-template ABI that can live in a .cpp file — callers fill this from
// whatever book shape they have.
struct BookFeatures {
    std::array<uint32_t, kObiDepth> bid_qty{};
    std::array<uint32_t, kObiDepth> ask_qty{};
    uint64_t spread = 0;  // best_ask - best_bid, in the book's fixed-point ticks
};

enum class PricePrediction : int8_t {
    Down = -1,
    Flat = 0,
    Up = 1,
};

class InferenceEngine {
public:
    // Order Book Imbalance over the top kObiDepth levels:
    //   (sum(bid_qty) - sum(ask_qty)) / (sum(bid_qty) + sum(ask_qty))
    // Bounded in [-1, 1]; positive means resting buy-side size dominates the
    // visible book, which historically leads short-horizon upward drift.
    static float compute_obi(const BookFeatures& features) noexcept;

    // Static-weight linear model over {obi, spread} thresholded into a
    // three-way call. Weights are fixed constants baked in at compile time —
    // this is inference-only, deterministic, and O(1): no training loop, no
    // heap-backed parameter store, no dynamic dispatch.
    PricePrediction predict(const BookFeatures& features) const noexcept;

private:
    // [obi_weight, spread_weight, bias]. Offline-fit constants; kept as a
    // plain array rather than a container so the whole model is a handful
    // of registers, not a pointer chase.
    static constexpr std::array<float, 3> kWeights{2.5f, -0.15f, 0.0f};
    static constexpr float kUpThreshold = 0.15f;
    static constexpr float kDownThreshold = -0.15f;
};

}  // namespace chronos
