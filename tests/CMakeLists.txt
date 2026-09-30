find_package(Threads REQUIRED)

add_executable(chronos_tests
  test_types.cpp
  test_memory_pool.cpp
  test_order_book.cpp
  test_benchmark.cpp
  test_ml_inference.cpp
  test_integration.cpp
  test_spsc_queue.cpp
)
target_link_libraries(chronos_tests PRIVATE chronos_core chronos_impl Catch2::Catch2WithMain Threads::Threads)

include(CTest)
include(Catch)
catch_discover_tests(chronos_tests)
