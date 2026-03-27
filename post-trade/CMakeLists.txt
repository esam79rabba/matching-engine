add_library(post-trade src/trade_store.cpp src/fee_calculator.cpp)
target_include_directories(post-trade PUBLIC include)
target_link_libraries(post-trade PUBLIC engine-core)

add_executable(post-trade-tests test/trade_store_test.cpp test/fee_calculator_test.cpp)
target_link_libraries(post-trade-tests PRIVATE post-trade PRIVATE GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(post-trade-tests)
