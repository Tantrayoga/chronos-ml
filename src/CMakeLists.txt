add_library(chronos_impl STATIC
  telemetry.cpp
  ml_inference.cpp
)
target_link_libraries(chronos_impl PUBLIC chronos_core)
