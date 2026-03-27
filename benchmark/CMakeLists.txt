# benchmark — prototype performance measurement
# Not an exchange-grade certification tool.

add_executable(matching-benchmark
    matching_benchmark.cpp
)

target_link_libraries(matching-benchmark
    PRIVATE engine-core
)
