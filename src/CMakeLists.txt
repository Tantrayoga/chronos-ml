find_package(Threads REQUIRED)

add_library(chronos_impl STATIC
  telemetry.cpp
  ml_inference.cpp
  thread_utils.cpp
)
target_link_libraries(chronos_impl PUBLIC chronos_core)

add_executable(chronos_replay replay_harness.cpp)
target_link_libraries(chronos_replay PRIVATE chronos_impl Threads::Threads)
