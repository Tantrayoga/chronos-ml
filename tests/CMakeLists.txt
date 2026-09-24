add_executable(chronos_tests
  test_types.cpp
  test_memory_pool.cpp
)
target_link_libraries(chronos_tests PRIVATE chronos_core Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(chronos_tests)
