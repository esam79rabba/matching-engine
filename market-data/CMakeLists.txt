add_library(market-data-publisher
    src/publisher.cpp
    src/transport/md_tcp_server.cpp
    src/transport/md_udp_publisher.cpp
    src/depth_publisher.cpp
    src/statistics.cpp
)
target_include_directories(market-data-publisher PUBLIC include)
target_link_libraries(market-data-publisher PUBLIC engine-core)

add_executable(market-data-tests
    test/publisher_test.cpp
    test/md_tcp_server_test.cpp
    test/md_udp_publisher_test.cpp
    test/depth_publisher_test.cpp
    test/statistics_test.cpp
)
target_link_libraries(market-data-tests PRIVATE market-data-publisher PRIVATE GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(market-data-tests)
