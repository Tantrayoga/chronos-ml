#include <atomic>
#include <cstdint>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "chronos/spsc_queue.h"

TEST_CASE("push/pop preserves FIFO order", "[spsc_queue]") {
    chronos::SPSCQueue<int, 8> queue;
    REQUIRE(queue.push(1));
    REQUIRE(queue.push(2));
    REQUIRE(queue.push(3));

    int value = 0;
    REQUIRE(queue.pop(value));
    REQUIRE(value == 1);
    REQUIRE(queue.pop(value));
    REQUIRE(value == 2);
    REQUIRE(queue.pop(value));
    REQUIRE(value == 3);
}

TEST_CASE("pop on an empty queue fails without blocking", "[spsc_queue]") {
    chronos::SPSCQueue<int, 8> queue;
    int value = 0;
    REQUIRE_FALSE(queue.pop(value));
    REQUIRE(queue.front() == nullptr);
    REQUIRE(queue.empty());
}

TEST_CASE("queue reports full with one slot held back", "[spsc_queue]") {
    chronos::SPSCQueue<int, 4> queue;  // usable capacity = Capacity - 1 = 3
    REQUIRE(queue.push(1));
    REQUIRE(queue.push(2));
    REQUIRE(queue.push(3));
    REQUIRE_FALSE(queue.push(4));  // full: the held-back slot disambiguates from empty
}

TEST_CASE("wraparound preserves order across repeated fill/drain cycles", "[spsc_queue]") {
    chronos::SPSCQueue<int, 4> queue;
    int value = 0;

    for (int cycle = 0; cycle < 5; ++cycle) {
        REQUIRE(queue.push(cycle * 10 + 1));
        REQUIRE(queue.push(cycle * 10 + 2));
        REQUIRE(queue.push(cycle * 10 + 3));
        REQUIRE_FALSE(queue.push(cycle * 10 + 4));  // full every cycle, not just the first

        REQUIRE(queue.pop(value));
        REQUIRE(value == cycle * 10 + 1);
        REQUIRE(queue.pop(value));
        REQUIRE(value == cycle * 10 + 2);
        REQUIRE(queue.pop(value));
        REQUIRE(value == cycle * 10 + 3);
        REQUIRE_FALSE(queue.pop(value));  // empty every cycle
    }
}

TEST_CASE("front()/pop_front() give zero-copy in-place access", "[spsc_queue]") {
    chronos::SPSCQueue<int, 8> queue;
    REQUIRE(queue.push(42));

    int* item = queue.front();
    REQUIRE(item != nullptr);
    REQUIRE(*item == 42);
    queue.pop_front();

    REQUIRE(queue.front() == nullptr);
    REQUIRE(queue.empty());
}

TEST_CASE("emplace constructs directly into the storage slot", "[spsc_queue]") {
    chronos::SPSCQueue<int, 8> queue;
    REQUIRE(queue.emplace(7));

    int value = 0;
    REQUIRE(queue.pop(value));
    REQUIRE(value == 7);
}

TEST_CASE("concurrent producer/consumer moves 1,000,000 items with zero loss and zero reordering",
          "[spsc_queue][stress]") {
    constexpr uint64_t kCount = 1'000'000;
    chronos::SPSCQueue<uint64_t, 4096> queue;

    // Assertion macros are not safe to call from a non-main thread in
    // Catch2, so correctness is tracked via plain atomics here and verified
    // with REQUIRE only after both threads have joined.
    std::atomic<bool> order_violation{false};
    std::atomic<uint64_t> consumed_count{0};

    std::thread producer([&]() {
        for (uint64_t i = 0; i < kCount; ++i) {
            // Never blocks on a lock: a full queue just means the consumer
            // is momentarily behind, so back off and retry rather than
            // stall the thread on a kernel primitive.
            while (!queue.push(i)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&]() {
        uint64_t expected = 0;
        uint64_t value = 0;
        while (expected < kCount) {
            if (queue.pop(value)) {
                if (value != expected) {
                    order_violation.store(true, std::memory_order_relaxed);
                }
                ++expected;
                consumed_count.fetch_add(1, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    REQUIRE_FALSE(order_violation.load());
    REQUIRE(consumed_count.load() == kCount);
}
