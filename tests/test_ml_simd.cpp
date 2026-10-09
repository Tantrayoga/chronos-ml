#include <cstdint>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "chronos/ml_inference.h"
#include "chronos/ml_simd.h"

namespace {

// Straight-line scalar reference: integer sums, one conversion at the end.
float reference_obi(const chronos::BookFeatures& features) {
    uint64_t bid_sum = 0;
    uint64_t ask_sum = 0;
    for (size_t i = 0; i < chronos::kObiDepth; ++i) {
        bid_sum += features.bid_qty[i];
        ask_sum += features.ask_qty[i];
    }
    const uint64_t total = bid_sum + ask_sum;
    if (total == 0) {
        return 0.0f;
    }
    return (static_cast<float>(bid_sum) - static_cast<float>(ask_sum)) / static_cast<float>(total);
}

}  // namespace

TEST_CASE("SIMD OBI matches the scalar reference across randomized books", "[ml_simd]") {
    uint64_t state = 0xC0FFEE1234ULL;
    auto next = [&state]() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1DULL;
    };

    for (int trial = 0; trial < 10'000; ++trial) {
        chronos::BookFeatures features{};
        for (size_t i = 0; i < chronos::kObiDepth; ++i) {
            // Per-level volumes kept under 2^20 so every partial sum is an
            // exactly representable float, and the two paths must agree.
            features.bid_qty[i] = static_cast<uint32_t>(next() % (1u << 20));
            features.ask_qty[i] = static_cast<uint32_t>(next() % (1u << 20));
        }
        REQUIRE_THAT(chronos::compute_obi_simd(features),
                     Catch::Matchers::WithinAbs(reference_obi(features), 1e-6));
    }
}

TEST_CASE("SIMD OBI handles empty, one-sided, and tail-only books", "[ml_simd]") {
    chronos::BookFeatures empty{};
    REQUIRE(chronos::compute_obi_simd(empty) == 0.0f);

    chronos::BookFeatures bids_only{};
    bids_only.bid_qty = {10, 20, 30, 40, 50};
    REQUIRE(chronos::compute_obi_simd(bids_only) == 1.0f);

    chronos::BookFeatures asks_only{};
    asks_only.ask_qty = {10, 20, 30, 40, 50};
    REQUIRE(chronos::compute_obi_simd(asks_only) == -1.0f);

    // Liquidity only in the 5th level exercises the scalar tail past the
    // 4-wide NEON chunk.
    chronos::BookFeatures tail_only{};
    tail_only.bid_qty[4] = 30;
    tail_only.ask_qty[4] = 10;
    REQUIRE_THAT(chronos::compute_obi_simd(tail_only), Catch::Matchers::WithinAbs(0.5, 1e-6));
}

TEST_CASE("InferenceEngine::compute_obi routes through the SIMD path", "[ml_simd]") {
    chronos::BookFeatures features{};
    features.bid_qty = {100, 0, 0, 0, 0};
    features.ask_qty = {0, 0, 0, 0, 300};
    REQUIRE(chronos::InferenceEngine::compute_obi(features) == chronos::compute_obi_simd(features));
    REQUIRE_THAT(chronos::InferenceEngine::compute_obi(features), Catch::Matchers::WithinAbs(-0.5, 1e-6));
}
