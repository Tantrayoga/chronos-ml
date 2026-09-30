# Chronos-ML

A zero-allocation, low-latency limit order book (LOB) matching engine in C++20, with an integrated branchless ML inference signal, built and benchmarked on Apple Silicon (ARM64).

## Architecture

### Zero-allocation object pool

`FixedObjectPool<T, Capacity>` ([include/chronos/memory_pool.h](include/chronos/memory_pool.h)) allocates a `std::array<T, Capacity>` once at construction and hands out slots via an index-based free-list stack. `allocate()` and `deallocate()` are both O(1) and never touch the heap after startup — there is no `malloc`/`free` anywhere on the order lifecycle path.

### Intrusive doubly-linked queues

Orders are 40-byte, 8-byte-aligned structs (`include/chronos/types.h`) that link to each other by **pool index**, not by pointer:

```cpp
struct Order {
    uint64_t order_id;
    uint64_t price;
    uint32_t quantity;
    uint32_t filled_qty;
    uint32_t next_idx;   // intrusive doubly-linked list
    uint32_t prev_idx;
    char side;
    uint8_t flags;
    uint8_t padding[6];
};
```

Each price level's FIFO queue is threaded through these indices directly into the object pool's backing array. There's no separate node allocation for the queue structure — the order *is* the node, and traversal is index math into an array that's already resident and cache-friendly, rather than pointer-chasing through independently allocated heap nodes.

### Flat, sorted price-level array

`OrderBook<MaxPriceLevels, MaxOrders>` ([include/chronos/order_book.h](include/chronos/order_book.h)) stores all price levels in a single `std::array<PriceLevel, MaxPriceLevels>`, sorted by `(side, price)` ascending. This gives two properties the matching engine leans on directly:

- **Binary search** (`lower_bound_level`) finds any price level in O(log n) without a tree structure.
- **Best bid/ask are adjacent in memory** — Buy levels occupy a contiguous front block, Sell levels a contiguous back block, so the best bid (last Buy entry) and best ask (first Sell entry) sit at a single boundary index. Sweeping the book outward from the best price is just walking that boundary index in one direction, with no re-search per level.

Each `PriceLevel` also tracks `resting_qty` — the sum of remaining quantity across every order resting at that level — maintained incrementally on every append, fill, and cancel. This is what keeps top-of-book feature extraction O(depth) instead of requiring a queue walk on every book update.

### Matching engine

`add_order()`, `cancel_order()`, and `match()` implement price-time priority matching: an aggressive order crosses the spread against the best opposite price levels, filling resting orders in FIFO order (oldest first) via `std::min(taker_remaining, resting_remaining)`. Fully-filled resting orders are unlinked and returned to the pool; fully-drained price levels are compacted out of the array the same way they were inserted — an insert shifts entries up to make room, a removal shifts them back down to close the gap.

## Telemetry & benchmark methodology

### Lock-free ring buffer

`LatencyRingBuffer<Capacity>` ([include/chronos/telemetry.h](include/chronos/telemetry.h)) is a single-producer/single-consumer ring buffer for logging pipeline events. With exactly one writer, a push is a single relaxed atomic increment (`fetch_add`) plus a bitmask for wraparound (`Capacity` must be a power of two) — no compare-and-swap retry loop, no mutex, no syscall.

Four checkpoints are recorded per order lifecycle:

| Stage | Meaning |
|---|---|
| `OrderReceived` (T1) | Order handed to the book |
| `SpreadCrossed` (T2) | Matching determined the order crosses the opposite book |
| `OrdersUnlinked` (T3) | Resting orders removed from the queue/pool during a sweep |
| `SignalEmitted` (T4) | Post-update inference ran and produced a prediction |

### Timestamping

`now_ns()` reads the ARM64 virtual counter register (`CNTVCT_EL0`) directly via inline assembly, converting ticks to nanoseconds using the counter frequency (`CNTFRQ_EL0`) — avoiding the syscall overhead a portable `clock_gettime`-backed clock would add on the very path being measured. A `std::chrono::high_resolution_clock` fallback is used on non-ARM64 targets.

### Latency profile (from `tests/test_benchmark.cpp`, flooding 10,000 resting orders across 500 price levels)

```
[bench] insertion latency (ns) over 10000 samples: mean=56.6 median=42.0 p99=84.0
[bench] single sweeping taker match: 51994 ns (requested fill 50000 units)
```

Percentiles (p50/p99), not the arithmetic mean, are the metric that matters: a strategy's edge is threatened by its worst-case behavior, not its typical case, since that's precisely when the market has moved fast enough that a slow fill costs money. `compute_latency_stats()` sorts the sample set and indexes directly into the tail (`0.99 * n`) rather than approximating it from the mean.

## ML inference integration

### Order Book Imbalance (OBI)

`InferenceEngine::compute_obi()` ([include/chronos/ml_inference.h](include/chronos/ml_inference.h)) computes:

```
OBI = (Σ bid_qty - Σ ask_qty) / (Σ bid_qty + Σ ask_qty)
```

over the top `kObiDepth` (5) price levels on each side, bounded in `[-1, 1]`. Positive values indicate resting buy-side pressure dominates the visible book.

### Branchless prediction

`predict()` applies a fixed, compile-time linear model over `{obi, spread}` and thresholds the result into `{Down, Flat, Up}` without an `if/else` chain:

```cpp
const int is_up   = static_cast<int>(score > kUpThreshold);
const int is_down = static_cast<int>(score < kDownThreshold);
return static_cast<PricePrediction>(is_up - is_down);
```

Each comparison lowers to a compare-and-set instruction, not a conditional branch, so the three-way decision has no data-dependent branch for the predictor to mispredict. All weights (`kWeights`) are `constexpr`, and `BookFeatures` is a flat, fixed-size value type — the entire inference call is a handful of registers and multiply-adds, with zero heap allocation and deterministic O(1) latency.

### Hot-path wiring

`OrderBook` holds an `InferenceEngine` by value (stateless, zero overhead) and an optional `SignalRingBuffer* telemetry_sink`. Every state-changing operation — a resting insert, a match, a cancel — calls `on_book_state_changed()`, which recomputes top-of-book features and runs inference in O(kObiDepth) time. When `telemetry_sink` is `nullptr`, every hook is a single pointer check: callers who don't need telemetry pay nothing for it.

## Building & testing

```sh
cmake -S . -B build
cmake --build build --target chronos_tests -j
./build/tests/chronos_tests
```
