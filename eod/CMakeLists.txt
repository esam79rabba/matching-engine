add_library(eod-processor
    src/eod_processor.cpp
    src/eod_file_writer.cpp
)
target_include_directories(eod-processor PUBLIC include)
target_link_libraries(eod-processor PUBLIC engine-core)

add_executable(eod-tests
    test/eod_processor_test.cpp
    test/eod_file_writer_test.cpp
)
target_link_libraries(eod-tests PRIVATE eod-processor PRIVATE GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(eod-tests)
