# common/ — shared transport abstractions
# Generic socket interface with plain TCP implementation.
# TLS implementation requires OpenSSL.

add_library(common-transport
    src/tcp_socket.cpp
    src/json_config_loader.cpp
)
target_include_directories(common-transport PUBLIC include)
target_link_libraries(common-transport PUBLIC engine-core)

# ─── OpenSSL / TLS support ─────────────────────────────────────
find_package(OpenSSL QUIET)
if(OPENSSL_FOUND)
    message(STATUS "OpenSSL ${OPENSSL_VERSION} found — TLS transport enabled")
    target_sources(common-transport PRIVATE src/tls_socket.cpp)
    target_link_libraries(common-transport PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(common-transport PUBLIC OPENSSL_AVAILABLE=1)
else()
    message(STATUS "OpenSSL not found — TLS transport disabled (plain TCP only)")
endif()

# ─── Tests ─────────────────────────────────────────────────────
add_executable(transport-tests
    test/transport_test.cpp
)
target_link_libraries(transport-tests PRIVATE common-transport PRIVATE GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(transport-tests)

add_executable(json-config-tests
    test/json_config_test.cpp
)
target_link_libraries(json-config-tests PRIVATE common-transport PRIVATE GTest::gtest_main)
gtest_discover_tests(json-config-tests)

if(OPENSSL_FOUND)
    add_executable(tls-transport-tests
        test/tls_transport_test.cpp
    )
    target_link_libraries(tls-transport-tests PRIVATE common-transport PRIVATE GTest::gtest_main)
    gtest_discover_tests(tls-transport-tests)

    add_executable(mtls-tests
        test/mtls_test.cpp
    )
    target_link_libraries(mtls-tests PRIVATE common-transport PRIVATE GTest::gtest_main)
    gtest_discover_tests(mtls-tests)
endif()
