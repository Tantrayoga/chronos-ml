#include <cstdio>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "chronos/order_book.h"
#include "chronos/telemetry.h"

namespace {

constexpr size_t kFloodOrders = 10'000;
constexpr size_t kDistinctPriceLevels = 500;
constexpr size_t kMaxPriceLevels = 4096;

// One taker plus the resting flood, rounded up generously.
using BenchBook = chronos::OrderBook<kMaxPriceLevels, kFloodOrders + 16>;

// BenchBook's inline arrays run into several hundred KB; heap-allocate it
// rather than risk overflowing a thread's default stack.
std::unique_ptr<BenchBook> make_bench_book() {
    return std::make_unique<BenchBook>();
}

}  // namespace

TEST_CASE("flood insertion latency stays sane across 10k resting orders", "[benchmark]") {
    auto book = make_bench_book();
    chronos::LatencyRingBuffer<16384> telemetry;

    std::vector<uint64_t> insertion_latencies_ns;
    insertion_latencies_ns.reserve(kFloodOrders);

    // Spread the flood across kDistinctPriceLevels ascending price levels so
    // the book has real depth (multiple levels, multiple orders per level)
    // rather than one degenerate queue.
    for (uint64_t i = 0; i < kFloodOrders; ++i) {
        const uint64_t price = 1'000 + (i % kDistinctPriceLevels);

        telemetry.push(i, chronos::TelemetryStage::OrderReceived);  // T1
        const uint64_t t_start = chronos::now_ns();

        const uint32_t order_idx = book->add_order(i, chronos::Side::Sell, price, 10);
        REQUIRE(order_idx != chronos::kInvalidIndex);

        const uint64_t t_end = chronos::now_ns();
        telemetry.push(i, chronos::TelemetryStage::OrdersUnlinked);  // T3 (no resting fills here, but marks completion)
        insertion_latencies_ns.push_back(t_end - t_start);
    }

    const chronos::LatencyStats stats = chronos::compute_latency_stats(insertion_latencies_ns);
    std::printf(
        "[bench] insertion latency (ns) over %zu samples: mean=%.1f median=%.1f p99=%.1f\n",
        kFloodOrders, stats.mean_ns, stats.median_ns, stats.p99_ns);

    // We don't gate on an absolute latency number — that's hardware- and
    // load-dependent — only on internal consistency of the computed stats.
    REQUIRE(stats.mean_ns >= 0.0);
    REQUIRE(stats.median_ns >= 0.0);
    REQUIRE(stats.p99_ns >= stats.median_ns);
    REQUIRE(telemetry.count() == kFloodOrders * 2);
}

TEST_CASE("a single sweeping taker crosses many resting levels", "[benchmark]") {
    auto book = make_bench_book();

    for (uint64_t i = 0; i < kFloodOrders; ++i) {
        const uint64_t price = 1'000 + (i % kDistinctPriceLevels);
        REQUIRE(book->add_order(i, chronos::Side::Sell, price, 10) != chronos::kInvalidIndex);
    }

    // One aggressive buy priced above every resting ask, sized to consume
    // roughly half the flooded book. This is the deep-sweep scenario the
    // Day 4 branch-prediction discussion was about: many price levels and
    // many resting orders walked in a single match() call.
    const uint64_t taker_qty = (kFloodOrders / 2) * 10;
    const uint64_t taker_price = 1'000 + kDistinctPriceLevels;  // above every resting ask

    const uint64_t t_start = chronos::now_ns();
    const uint32_t taker_idx = book->add_order(999'999, chronos::Side::Buy, taker_price, static_cast<uint32_t>(taker_qty));
    const uint64_t t_end = chronos::now_ns();

    std::printf(
        "[bench] single sweeping taker match: %llu ns (requested fill %llu units)\n",
        static_cast<unsigned long long>(t_end - t_start),
        static_cast<unsigned long long>(taker_qty));

    // Fully filled: add_order() deallocates the taker's slot, so it still
    // returns a valid (now-recycled) index rather than kInvalidIndex.
    REQUIRE(taker_idx != chronos::kInvalidIndex);
}
