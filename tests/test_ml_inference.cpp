#include <catch2/catch_test_macros.hpp>

#include "chronos/ml_inference.h"

TEST_CASE("compute_obi returns zero for an empty book", "[ml_inference]") {
    chronos::BookFeatures features{};
    REQUIRE(chronos::InferenceEngine::compute_obi(features) == 0.0f);
}

TEST_CASE("compute_obi is bounded and signed correctly", "[ml_inference]") {
    chronos::BookFeatures heavy_bid{};
    heavy_bid.bid_qty = {100, 100, 100, 100, 100};
    heavy_bid.ask_qty = {10, 10, 10, 10, 10};
    const float obi_bid_heavy = chronos::InferenceEngine::compute_obi(heavy_bid);
    REQUIRE(obi_bid_heavy > 0.0f);
    REQUIRE(obi_bid_heavy <= 1.0f);

    chronos::BookFeatures heavy_ask{};
    heavy_ask.bid_qty = {10, 10, 10, 10, 10};
    heavy_ask.ask_qty = {100, 100, 100, 100, 100};
    const float obi_ask_heavy = chronos::InferenceEngine::compute_obi(heavy_ask);
    REQUIRE(obi_ask_heavy < 0.0f);
    REQUIRE(obi_ask_heavy >= -1.0f);

    chronos::BookFeatures balanced{};
    balanced.bid_qty = {50, 50, 50, 50, 50};
    balanced.ask_qty = {50, 50, 50, 50, 50};
    REQUIRE(chronos::InferenceEngine::compute_obi(balanced) == 0.0f);
}

TEST_CASE("predict thresholds imbalance into Up/Down/Flat", "[ml_inference]") {
    chronos::InferenceEngine engine;

    chronos::BookFeatures strong_bid{};
    strong_bid.bid_qty = {1000, 1000, 1000, 1000, 1000};
    strong_bid.ask_qty = {10, 10, 10, 10, 10};
    strong_bid.spread = 1;
    REQUIRE(engine.predict(strong_bid) == chronos::PricePrediction::Up);

    chronos::BookFeatures strong_ask{};
    strong_ask.bid_qty = {10, 10, 10, 10, 10};
    strong_ask.ask_qty = {1000, 1000, 1000, 1000, 1000};
    strong_ask.spread = 1;
    REQUIRE(engine.predict(strong_ask) == chronos::PricePrediction::Down);

    chronos::BookFeatures balanced{};
    balanced.bid_qty = {50, 50, 50, 50, 50};
    balanced.ask_qty = {50, 50, 50, 50, 50};
    balanced.spread = 1;
    REQUIRE(engine.predict(balanced) == chronos::PricePrediction::Flat);
}
