add_library(dropcopy-service
    src/drop_copy_service.cpp
)
target_include_directories(dropcopy-service PUBLIC include)
target_link_libraries(dropcopy-service PUBLIC engine-core)

add_executable(dropcopy-tests test/drop_copy_test.cpp)
target_link_libraries(dropcopy-tests PRIVATE dropcopy-service PRIVATE GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(dropcopy-tests)
