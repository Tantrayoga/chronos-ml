add_executable(chronos_benchmarks
  bench_types.cpp
)
target_link_libraries(chronos_benchmarks PRIVATE chronos_core benchmark::benchmark_main)
