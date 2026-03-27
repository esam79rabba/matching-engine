add_library(replication
    src/journal_replicator.cpp
    src/journal_receiver.cpp
    src/health_probe.cpp
    src/failover_controller.cpp
    src/generation_counter.cpp
    src/auto_promote_controller.cpp
)
target_include_directories(replication PUBLIC include)
target_link_libraries(replication PUBLIC engine-core)

add_executable(replication-tests
    test/replication_test.cpp
    test/failover_detection_test.cpp
    test/auto_promote_test.cpp
)
target_link_libraries(replication-tests PRIVATE replication engine-core GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(replication-tests)
