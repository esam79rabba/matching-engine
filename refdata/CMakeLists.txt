add_library(refdata-service
    src/ref_data_service.cpp
    src/ref_data_http.cpp
)
target_include_directories(refdata-service PUBLIC include)
target_link_libraries(refdata-service PUBLIC engine-core PUBLIC risk-service)

add_executable(refdata-tests
    test/ref_data_test.cpp
    test/ref_data_http_test.cpp
)
target_link_libraries(refdata-tests PRIVATE refdata-service PRIVATE GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(refdata-tests)
