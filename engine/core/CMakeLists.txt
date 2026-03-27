# engine/core — matching engine kernel
# Generic multi-asset core. No asset-class-specific logic here.

add_library(engine-core
    src/order_book.cpp
    src/order_pool.cpp
    src/matching.cpp
    src/partition.cpp
    src/journal_stub.cpp
    src/snapshot.cpp
    src/cancel_on_disconnect.cpp
    src/instrument_state.cpp
    src/journal_file.cpp
    src/session_scheduler.cpp
    src/kill_switch.cpp
    src/session_lifecycle.cpp
    src/gtd_expiry_checker.cpp
    src/instrument_lifecycle.cpp
    src/journal_segmented.cpp
    src/journal_direct_io.cpp
    src/startup.cpp
)

target_include_directories(engine-core
    PUBLIC include
)

# Disable exceptions and RTTI on the engine core (hot-path constraint)
target_compile_options(engine-core PRIVATE
    -fno-exceptions
    -fno-rtti
)

# ─── Tests ─────────────────────────────────────────────────────
add_executable(engine-core-tests
    test/order_book_test.cpp
    test/order_pool_test.cpp
    test/matching_test.cpp
    test/partition_test.cpp
    test/journal_stub_test.cpp
    test/snapshot_test.cpp
    test/replay_determinism_test.cpp
    test/cancel_on_disconnect_test.cpp
    test/instrument_state_test.cpp
    test/multi_partition_test.cpp
    test/journal_file_test.cpp
    test/state_enforcement_test.cpp
    test/session_scheduler_test.cpp
    test/kill_switch_test.cpp
    test/session_lifecycle_test.cpp
    test/gtd_expiry_test.cpp
    test/instrument_lifecycle_test.cpp
    test/stress_test.cpp
    test/snapshot_restore_test.cpp
    test/journal_segmented_test.cpp
    test/journal_direct_io_test.cpp
    test/startup_test.cpp
)

target_link_libraries(engine-core-tests
    PRIVATE engine-core
    PRIVATE risk-service
    PRIVATE market-data-publisher
    PRIVATE dropcopy-service
    PRIVATE GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(engine-core-tests)
