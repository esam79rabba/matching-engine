# risk — pre-trade risk service
# Generic: thresholds from configuration, no asset-class-specific logic.

add_library(risk-service
    src/risk_service.cpp
    src/rate_limiter.cpp
    src/position_tracker.cpp
    src/credit_tracker.cpp
)

target_include_directories(risk-service
    PUBLIC include
)

target_link_libraries(risk-service
    PUBLIC engine-core    # For engine::types.h
)

# ─── Tests ─────────────────────────────────────────────────────
add_executable(risk-service-tests
    test/risk_service_test.cpp
    test/rate_limiter_test.cpp
    test/rate_limiter_integration_test.cpp
    test/position_tracker_test.cpp
    test/credit_tracker_test.cpp
    test/reduce_only_test.cpp
)

target_link_libraries(risk-service-tests
    PRIVATE risk-service
    PRIVATE GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(risk-service-tests)
