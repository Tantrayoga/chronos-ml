#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace chronos {

// Apple Silicon's cache hierarchy prefetches and coherency-tracks in
// 128-byte lines (vs. the 64-byte line common on x86 and most other ARM
// parts); padding to the wrong size still avoids false sharing but wastes
// half a line on target, so we pick the size for the platform we actually
// ship on.
#if defined(__aarch64__) && defined(__APPLE__)
inline constexpr size_t kCacheLineSize = 128;
#else
inline constexpr size_t kCacheLineSize = 64;
#endif

// Bounded, lock-free, single-producer/single-consumer ring buffer. Intended
// to decouple ingress (market data / order entry, running on its own
// thread) from the matching engine without ever taking a mutex on the path
// between them — a blocked producer or consumer here would reintroduce
// exactly the latency spikes the rest of this engine is built to avoid.
//
// Capacity must be a power of two: index wraparound then reduces to a
// bitmask (`& kMask`) instead of a modulo, which on ARM64 trades the
// integer division/modulo unit (multi-cycle, and not fully pipelined) for a
// single-cycle AND.
//
// One slot is always kept empty by convention (usable capacity is
// Capacity - 1) — this is what lets `head_ == tail_` unambiguously mean
// "empty" without a separate size counter that both threads would need to
// keep in sync.
template <typename T, size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    SPSCQueue() noexcept = default;
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // Producer-thread only. Returns false (never blocks) if the queue is
    // momentarily full.
    bool push(const T& item) noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (tail + 1) & kMask;

        // Acquire: must see every slot the consumer has already finished
        // draining before deciding "full," or we'd refuse a push into a
        // slot that's actually free. Pairs with the consumer's release
        // store to head_ in pop()/pop_front().
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false;
        }

        storage_[tail] = item;

        // Release: everything written to storage_[tail] above must become
        // visible to the consumer before it observes the new tail_ value.
        // This is the synchronizes-with edge that makes the item safe to
        // read cross-thread — acquire/release gives us exactly that edge
        // without paying for a full sequentially-consistent fence, which
        // would also order this queue's traffic against every other atomic
        // in the program.
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    template <typename... Args>
    bool emplace(Args&&... args) noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (tail + 1) & kMask;
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        storage_[tail] = T(std::forward<Args>(args)...);
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    // Consumer-thread only. Returns false (never blocks) if the queue is
    // momentarily empty.
    bool pop(T& item) noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        item = storage_[head];
        head_.store((head + 1) & kMask, std::memory_order_release);
        return true;
    }

    // Zero-copy alternative to pop(): returns a pointer into the slot
    // itself rather than copying out, for callers that want to process the
    // item in place. Must be followed by pop_front() to actually advance
    // the queue — front() alone does not consume.
    T* front() noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) {
            return nullptr;
        }
        return &storage_[head];
    }

    void pop_front() noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        head_.store((head + 1) & kMask, std::memory_order_release);
    }

    bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

private:
    static constexpr size_t kMask = Capacity - 1;

    // head_ and tail_ are each pinned to their own cache line. Without this,
    // the producer's per-push write to tail_ and the consumer's per-pop
    // write to head_ would share one line: every write from either side
    // would force a MESI invalidation of that line in the other core's
    // cache (false sharing) — the two threads never touch the same logical
    // data, but the cache-coherency protocol can't tell that from raw
    // address proximity. Separating them onto distinct lines turns that
    // cross-core traffic into two independent, uncontended lines.
    alignas(kCacheLineSize) std::atomic<size_t> head_{0};
    alignas(kCacheLineSize) std::atomic<size_t> tail_{0};
    alignas(kCacheLineSize) std::array<T, Capacity> storage_{};
};

}  // namespace chronos
