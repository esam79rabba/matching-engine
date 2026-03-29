# benchmark — production-grade latency measurement suite
# Uses HdrHistogram-style percentile tracking across 5 scenarios.

add_executable(matching-benchmark
    matching_benchmark.cpp
)

target_include_directories(matching-benchmark
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(matching-benchmark
    PRIVATE engine-core
    PRIVATE risk-service
)
