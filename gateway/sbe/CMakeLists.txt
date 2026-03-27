add_library(sbe-gateway-codec
    src/sbe_codec.cpp
)
target_include_directories(sbe-gateway-codec PUBLIC include)
target_link_libraries(sbe-gateway-codec PUBLIC engine-core PUBLIC fix-gateway-adapter)

add_executable(sbe-gateway-tests test/sbe_codec_test.cpp)
target_link_libraries(sbe-gateway-tests PRIVATE sbe-gateway-codec PRIVATE GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(sbe-gateway-tests)
