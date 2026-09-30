#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace chronos {

// The three checkpoints an order passes through on the hot path. Kept as a
// flat enum (not a bitmask) since events are logged sequentially, not
// combined.
enum class TelemetryStage : uint8_t {
    OrderReceived = 0,   // T1: order handed to the book
    SpreadCrossed = 1,   // T2: matching determined the order crosses
    OrdersUnlinked = 2,  // T3: resting orders removed from the queue/pool
    SignalEmitted = 3,   // T4: inference ran against the post-update book
};

struct TelemetryEvent {
    uint64_t order_id;
    uint64_t timestamp_ns;
    TelemetryStage stage;
    // Only meaningful when stage == SignalEmitted (chronos::PricePrediction
    // cast to int8_t); zero for every other stage. Kept as a raw int8_t
    // rather than including ml_inference.h here, so telemetry stays
    // decoupled from the inference module's type.
    int8_t prediction = 0;
};

// Monotonic nanosecond timestamp. On ARM64 this reads the CPU's virtual
// counter register directly instead of going through a syscall-backed clock
// — a measurement mechanism has to be cheaper than the thing it measures, or
// the overhead of measuring dominates the sample itself.
uint64_t now_ns() noexcept;

// Single-producer/single-consumer lock-free ring buffer for latency events.
// Capacity must be a power of two: wraparound then reduces to a bitmask
// (`& kMask`) instead of a modulo, which keeps push() a fixed handful of
// integer ops with no division on the hot path.
template <size_t Capacity>
class LatencyRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    // Called from the matching-engine thread. Never blocks and never
    // allocates: a fetch_add plus a mask is the entire cost of a write. On a
    // full buffer, the oldest slot is silently overwritten — we care about a
    // recent trailing window of samples for tail-latency analysis, not a
    // permanent audit log, so we deliberately don't pay for overflow
    // handling here.
    void push(uint64_t order_id, TelemetryStage stage, int8_t prediction = 0) noexcept {
        const size_t index = write_idx_.fetch_add(1, std::memory_order_relaxed) & kMask;
        events_[index] = TelemetryEvent{order_id, now_ns(), stage, prediction};
    }

    // Snapshot accessors for offline/benchmark analysis after the hot loop
    // has finished. Not meant to be read concurrently with an in-flight
    // push() from another thread.
    const std::array<TelemetryEvent, Capacity>& events() const noexcept { return events_; }
    size_t count() const noexcept { return write_idx_.load(std::memory_order_relaxed); }

private:
    static constexpr size_t kMask = Capacity - 1;
    std::array<TelemetryEvent, Capacity> events_{};
    std::atomic<size_t> write_idx_{0};
};

struct LatencyStats {
    double mean_ns;
    double median_ns;
    double p99_ns;
};

// Computes mean/median/p99 over `samples_ns`, sorting it in place. This is
// an O(n log n) offline analysis step — it is never called from the hot
// matching path, only after a benchmark run has collected its samples.
LatencyStats compute_latency_stats(std::vector<uint64_t>& samples_ns) noexcept;

// Fixed-capacity ring buffer used to sink hot-path signals (order lifecycle
// events, inference predictions) for asynchronous/offline consumption. A
// single concrete alias — rather than letting every OrderBook<> instantiate
// its own template capacity — keeps the sink's type stable regardless of the
// book's own size parameters, so it can be injected as a plain pointer.
using SignalRingBuffer = LatencyRingBuffer<4096>;

}  // namespace chronos
