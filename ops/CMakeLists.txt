add_library(ops-controller
    src/ops_controller.cpp
    src/metrics.cpp
    src/http/http_server.cpp
    src/auth_middleware.cpp
    src/auth_service.cpp
    src/audit_log.cpp
)
target_include_directories(ops-controller PUBLIC include)
target_link_libraries(ops-controller PUBLIC engine-core PUBLIC refdata-service PUBLIC eod-processor)

add_executable(ops-tests
    test/ops_controller_test.cpp
    test/http_server_test.cpp
    test/metrics_test.cpp
    test/auth_middleware_test.cpp
    test/auth_service_test.cpp
    test/audit_log_test.cpp
)
target_link_libraries(ops-tests PRIVATE ops-controller PRIVATE refdata-service PRIVATE GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(ops-tests)
