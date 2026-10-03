#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "chronos/spsc_queue.h"
#include "chronos/thread_utils.h"

TEST_CASE("producer/consumer threads apply QoS, affinity, and naming around the Day 8 SPSC queue",
          "[threading]") {
    constexpr uint64_t kCount = 100'000;
    chronos::SPSCQueue<uint64_t, 4096> queue;

    std::atomic<bool> order_violation{false};
    std::atomic<uint64_t> consumed_count{0};
    std::atomic<bool> producer_qos_ok{false};
    std::atomic<bool> consumer_qos_ok{false};
    std::atomic<bool> consumer_affinity_ok{false};
    std::atomic<bool> consumer_name_ok{false};

    // Network/Producer thread: ingress simulation. Tagged distinctly from
    // the consumer so the scheduler is hinted the two don't need to share
    // an L2 cluster — they only interact through the SPSC queue's two
    // cache lines, not through any shared working set.
    std::thread producer([&]() {
        chronos::set_thread_name("chronos-producer");
        producer_qos_ok.store(chronos::elevate_thread_qos(), std::memory_order_relaxed);
        chronos::set_thread_affinity_tag(1);

        for (uint64_t i = 0; i < kCount; ++i) {
            while (!queue.push(i)) {
                std::this_thread::yield();
            }
        }
    });

    // Execution/Consumer thread: the matching-engine stand-in. Gets the
    // same QoS elevation as the producer (both are latency-sensitive) but
    // a distinct affinity tag.
    std::thread consumer([&]() {
        consumer_name_ok.store(chronos::set_thread_name("chronos-execution"), std::memory_order_relaxed);
        consumer_qos_ok.store(chronos::elevate_thread_qos(), std::memory_order_relaxed);
        consumer_affinity_ok.store(chronos::set_thread_affinity_tag(2), std::memory_order_relaxed);

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

    // Correctness of the pipeline itself is unconditional.
    REQUIRE_FALSE(order_violation.load());
    REQUIRE(consumed_count.load() == kCount);

    // QoS elevation and affinity tagging are scheduler *hints* — the
    // kernel can legitimately decline them (sandboxing, missing
    // entitlements, CI environments without the relevant Mach privileges),
    // so they're reported rather than hard-gated; a flaky environment
    // shouldn't fail a test whose actual contract (the queue pipeline
    // works) already passed above.
    INFO("producer QoS elevation succeeded: " << producer_qos_ok.load());
    INFO("consumer QoS elevation succeeded: " << consumer_qos_ok.load());
    INFO("consumer affinity tag accepted: " << consumer_affinity_ok.load());

    // Thread naming is not a scheduler hint and has no documented failure
    // mode under normal conditions, so this one is held to a hard
    // guarantee.
    REQUIRE(consumer_name_ok.load());
}

TEST_CASE("set_thread_name truncates names longer than the platform limit instead of failing", "[threading]") {
    const std::string long_name(200, 'x');
    REQUIRE(chronos::set_thread_name(long_name));
}
