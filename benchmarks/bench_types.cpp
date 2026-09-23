#include <benchmark/benchmark.h>
#include "chronos/types.h"

static void BM_OrderSize(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(sizeof(chronos::Order));
    }
}
BENCHMARK(BM_OrderSize);
