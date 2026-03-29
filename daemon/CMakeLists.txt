add_executable(matching-engine-daemon main.cpp)
target_link_libraries(matching-engine-daemon
    PRIVATE fix-gateway-adapter
    PRIVATE risk-service
    PRIVATE refdata-service
    PRIVATE market-data-publisher
    PRIVATE dropcopy-service
    PRIVATE ops-controller
    PRIVATE eod-processor
    PRIVATE engine-core
    PRIVATE common-transport
)

# ─── Daemon Integration Tests ─────────────────────────────────
add_executable(daemon-integration-tests
    test/daemon_fix_integration_test.cpp
)

target_link_libraries(daemon-integration-tests
    PRIVATE fix-gateway-adapter
    PRIVATE risk-service
    PRIVATE refdata-service
    PRIVATE market-data-publisher
    PRIVATE ops-controller
    PRIVATE eod-processor
    PRIVATE engine-core
    PRIVATE common-transport
    PRIVATE GTest::gtest_main
)

# ─── Lifecycle Regression Tests ───────────────────────────────
add_executable(lifecycle-regression-tests
    test/lifecycle_regression_test.cpp
)

target_link_libraries(lifecycle-regression-tests
    PRIVATE fix-gateway-adapter
    PRIVATE risk-service
    PRIVATE refdata-service
    PRIVATE market-data-publisher
    PRIVATE ops-controller
    PRIVATE eod-processor
    PRIVATE engine-core
    PRIVATE common-transport
    PRIVATE replication
    PRIVATE GTest::gtest_main
)

# ─── Order Session Tracker Unit Tests ────────────────────────
add_executable(order-session-tracker-tests
    test/order_session_tracker_test.cpp
)

target_include_directories(order-session-tracker-tests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(order-session-tracker-tests
    PRIVATE engine-core
    PRIVATE GTest::gtest_main
)

# ─── Passive Fill Integration Tests ──────────────────────────
add_executable(passive-fill-tests
    test/passive_fill_test.cpp
)

target_include_directories(passive-fill-tests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(passive-fill-tests
    PRIVATE fix-gateway-adapter
    PRIVATE risk-service
    PRIVATE refdata-service
    PRIVATE market-data-publisher
    PRIVATE ops-controller
    PRIVATE eod-processor
    PRIVATE engine-core
    PRIVATE common-transport
    PRIVATE GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(daemon-integration-tests)
gtest_discover_tests(lifecycle-regression-tests)
gtest_discover_tests(order-session-tracker-tests)
gtest_discover_tests(passive-fill-tests)
