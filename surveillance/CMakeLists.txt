# surveillance — market surveillance and abuse detection

add_library(surveillance
    src/surveillance_engine.cpp
    src/rules/wash_trade_rule.cpp
    src/rules/layering_rule.cpp
    src/rules/order_trade_ratio_rule.cpp
    src/rules/quote_stuffing_rule.cpp
)

target_include_directories(surveillance PUBLIC include)
target_link_libraries(surveillance PUBLIC engine-core)

# ─── Tests ─────────────────────────────────────────────────────
add_executable(surveillance-tests test/surveillance_test.cpp)
target_link_libraries(surveillance-tests
    PRIVATE surveillance
    PRIVATE GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(surveillance-tests)
