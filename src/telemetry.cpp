#include "chronos/telemetry.h"

#include <algorithm>
#include <chrono>
#include <cstddef>

namespace chronos {

uint64_t now_ns() noexcept {
#if defined(__aarch64__)
    // CNTVCT_EL0 is the ARMv8 virtual generic timer counter: a monotonic
    // hardware register read directly by the core, with no syscall, no
    // vDSO indirection, and no risk of the kernel rescheduling us mid-read.
    // CNTFRQ_EL0 gives the counter's tick frequency so we can convert ticks
    // to nanoseconds. Both reads are single MRS instructions.
    uint64_t ticks = 0;
    uint64_t freq_hz = 0;
    asm volatile("mrs %0, cntvct_el0" : "=r"(ticks));
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq_hz));

    // 128-bit intermediate avoids overflow: ticks * 1e9 can exceed 64 bits
    // long before the counter itself wraps.
    const __uint128_t ticks_ns = static_cast<__uint128_t>(ticks) * 1'000'000'000ULL;
    return static_cast<uint64_t>(ticks_ns / freq_hz);
#else
    // Portable fallback for non-ARM64 targets; still monotonic, just paying
    // for whatever indirection the platform's high_resolution_clock has.
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch())
            .count());
#endif
}

LatencyStats compute_latency_stats(std::vector<uint64_t>& samples_ns) noexcept {
    if (samples_ns.empty()) {
        return LatencyStats{0.0, 0.0, 0.0};
    }

    std::sort(samples_ns.begin(), samples_ns.end());
    const size_t n = samples_ns.size();

    double sum = 0.0;
    for (uint64_t sample : samples_ns) {
        sum += static_cast<double>(sample);
    }
    const double mean = sum / static_cast<double>(n);

    const double median = (n % 2 == 0)
        ? (static_cast<double>(samples_ns[n / 2 - 1]) + static_cast<double>(samples_ns[n / 2])) / 2.0
        : static_cast<double>(samples_ns[n / 2]);

    // Tail latency, not the average, is the number that determines whether a
    // strategy holds its edge under load — see the accompanying educational
    // summary for why p99 is what a desk actually gates deployment on.
    size_t p99_index = static_cast<size_t>(0.99 * static_cast<double>(n));
    if (p99_index >= n) {
        p99_index = n - 1;
    }
    const double p99 = static_cast<double>(samples_ns[p99_index]);

    return LatencyStats{mean, median, p99};
}

}  // namespace chronos
