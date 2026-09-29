add_executable(chronos_tests
  test_types.cpp
  test_memory_pool.cpp
  test_order_book.cpp
  test_benchmark.cpp
  test_ml_inference.cpp
)
target_link_libraries(chronos_tests PRIVATE chronos_core chronos_impl Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(chronos_tests)
