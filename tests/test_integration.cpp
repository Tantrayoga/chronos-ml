#include <catch2/catch_test_macros.hpp>

#include "chronos/order_book.h"
#include "chronos/telemetry.h"

namespace {
using IntegrationBook = chronos::OrderBook<16, 32>;
}

TEST_CASE("aggressive sweep matches, records telemetry, and emits a valid signal", "[integration]") {
    IntegrationBook book;
    chronos::SignalRingBuffer telemetry;
    book.telemetry_sink = &telemetry;

    // Market makers post resting bids/asks. Each add_order() triggers a T1
    // receipt event and a T4 inference signal (non-crossing, so no T2/T3).
    REQUIRE(book.add_order(1, chronos::Side::Buy, 99, 20) != chronos::kInvalidIndex);
    REQUIRE(book.add_order(2, chronos::Side::Sell, 100, 15) != chronos::kInvalidIndex);
    REQUIRE(book.add_order(3, chronos::Side::Sell, 101, 15) != chronos::kInvalidIndex);

    const size_t events_before_sweep = telemetry.count();
    REQUIRE(events_before_sweep == 6);  // 3 orders x (T1 + T4)

    // Aggressive taker sweeps both ask levels: fully fills order 2, partially
    // fills order 3.
    const uint32_t taker_idx = book.add_order(4, chronos::Side::Buy, 101, 20);
    REQUIRE(taker_idx != chronos::kInvalidIndex);

    const uint32_t level_101 = book.find_price_level(chronos::Side::Sell, 101);
    REQUIRE(level_101 != chronos::kInvalidIndex);
    REQUIRE(book.orders.get(book.price_levels[level_101].head_idx).order_id == 3);
    REQUIRE(book.price_levels[level_101].resting_qty == 10);  // 15 - 5 filled

    // The sweep adds: T1 (taker received), T2 (spread crossed), T3 (orders
    // unlinked), T4 (post-update signal) — exactly one crossing episode.
    const size_t events_after_sweep = telemetry.count();
    REQUIRE(events_after_sweep == events_before_sweep + 4);

    const auto& events = telemetry.events();
    const chronos::TelemetryEvent& t1 = events[events_before_sweep + 0];
    const chronos::TelemetryEvent& t2 = events[events_before_sweep + 1];
    const chronos::TelemetryEvent& t3 = events[events_before_sweep + 2];
    const chronos::TelemetryEvent& t4 = events[events_before_sweep + 3];

    REQUIRE(t1.order_id == 4);
    REQUIRE(t1.stage == chronos::TelemetryStage::OrderReceived);
    REQUIRE(t2.order_id == 4);
    REQUIRE(t2.stage == chronos::TelemetryStage::SpreadCrossed);
    REQUIRE(t3.order_id == 4);
    REQUIRE(t3.stage == chronos::TelemetryStage::OrdersUnlinked);
    REQUIRE(t4.order_id == 4);
    REQUIRE(t4.stage == chronos::TelemetryStage::SignalEmitted);

    // Timestamps are monotonic across the pipeline: T1 <= T2 <= T3 <= T4.
    REQUIRE(t1.timestamp_ns <= t2.timestamp_ns);
    REQUIRE(t2.timestamp_ns <= t3.timestamp_ns);
    REQUIRE(t3.timestamp_ns <= t4.timestamp_ns);

    // Prediction is one of the three valid PricePrediction values.
    REQUIRE(t4.prediction >= -1);
    REQUIRE(t4.prediction <= 1);

    // No latency spike: the whole sweep-plus-inference episode should stay
    // within a coarse sanity bound (microseconds, not milliseconds) even
    // under test-harness overhead. This is a smoke bound, not a strict perf
    // gate — hardware- and load-dependent numbers belong in test_benchmark.cpp.
    const uint64_t episode_ns = t4.timestamp_ns - t1.timestamp_ns;
    REQUIRE(episode_ns < 1'000'000);  // < 1ms end-to-end for a two-level sweep
}

TEST_CASE("canceling a resting order triggers a book-state signal", "[integration]") {
    IntegrationBook book;
    chronos::SignalRingBuffer telemetry;
    book.telemetry_sink = &telemetry;

    const uint32_t order_idx = book.add_order(1, chronos::Side::Buy, 100, 10);
    REQUIRE(order_idx != chronos::kInvalidIndex);
    const size_t events_after_add = telemetry.count();

    book.cancel_order(chronos::Side::Buy, 100, order_idx);

    REQUIRE(telemetry.count() == events_after_add + 1);
    const chronos::TelemetryEvent& cancel_event = telemetry.events()[events_after_add];
    REQUIRE(cancel_event.order_id == 1);
    REQUIRE(cancel_event.stage == chronos::TelemetryStage::SignalEmitted);
    REQUIRE(book.find_price_level(chronos::Side::Buy, 100) == chronos::kInvalidIndex);
}

TEST_CASE("a book with no telemetry sink incurs no telemetry side effects", "[integration]") {
    IntegrationBook book;  // telemetry_sink left null

    REQUIRE(book.add_order(1, chronos::Side::Buy, 99, 20) != chronos::kInvalidIndex);
    REQUIRE(book.add_order(2, chronos::Side::Sell, 99, 20) != chronos::kInvalidIndex);
    // No crash, no telemetry access attempted — the null check makes every
    // hook a no-op. Correctness of matching itself is unaffected.
    REQUIRE(book.find_price_level(chronos::Side::Buy, 99) == chronos::kInvalidIndex);
    REQUIRE(book.find_price_level(chronos::Side::Sell, 99) == chronos::kInvalidIndex);
}
