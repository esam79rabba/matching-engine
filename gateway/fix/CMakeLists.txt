# gateway/fix — FIX protocol gateway adapter
# Includes real FIX 4.4 parser, and ref-data bridge.

add_library(fix-gateway-adapter
    src/fix_parser.cpp
    src/refdata_gateway_bridge.cpp
    src/tcp/fix_tcp_acceptor.cpp
    src/fix_session_store.cpp
    src/fix_gap_handler.cpp
    src/fix_message_store.cpp
    src/fix_message_store_file.cpp
)

target_include_directories(fix-gateway-adapter
    PUBLIC include
)

target_link_libraries(fix-gateway-adapter
    PUBLIC engine-core
    PUBLIC refdata-service
    PUBLIC common-transport
)

# ─── QuickFIX Session Layer (conditional) ────────────────────
if(QUICKFIX_FOUND)
    target_sources(fix-gateway-adapter PRIVATE
        src/quickfix_application.cpp
    )
    target_link_libraries(fix-gateway-adapter
        PUBLIC quickfix
    )
    target_include_directories(fix-gateway-adapter
        PUBLIC ${quickfix_SOURCE_DIR}/src/C++
    )
endif()

# ─── Unit Tests ──────────────────────────────────────────────
add_executable(fix-gateway-tests
    test/fix_parser_test.cpp
    test/refdata_bridge_test.cpp
    test/fix_session_store_test.cpp
    test/fix_tls_acceptor_test.cpp
    test/fix_session_persistence_test.cpp
    test/fix_gap_handler_test.cpp
    test/fix_gap_integration_test.cpp
    test/fix_message_store_test.cpp
    test/fix_replay_integration_test.cpp
    test/fix_message_store_file_test.cpp
    test/fix_replay_poss_dup_test.cpp
    test/fix_comp_id_resolver_test.cpp
)

target_link_libraries(fix-gateway-tests
    PRIVATE fix-gateway-adapter
    PRIVATE GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(fix-gateway-tests)

# ─── Extended FIX Message Types Tests ────────────────────────
add_executable(fix-message-types-tests
    test/fix_message_types_test.cpp
)

target_link_libraries(fix-message-types-tests
    PRIVATE fix-gateway-adapter
    PRIVATE GTest::gtest_main
)

gtest_discover_tests(fix-message-types-tests)

# ─── E2E Integration Tests ───────────────────────────────────
add_executable(e2e-integration-tests
    test/e2e_integration_test.cpp
)

target_link_libraries(e2e-integration-tests
    PRIVATE fix-gateway-adapter
    PRIVATE risk-service
    PRIVATE engine-core
    PRIVATE GTest::gtest_main
)

gtest_discover_tests(e2e-integration-tests)

# ─── Full Platform Integration Tests ─────────────────────────
add_executable(full-platform-tests
    test/full_platform_test.cpp
)

target_link_libraries(full-platform-tests
    PRIVATE fix-gateway-adapter
    PRIVATE risk-service
    PRIVATE refdata-service
    PRIVATE market-data-publisher
    PRIVATE dropcopy-service
    PRIVATE ops-controller
    PRIVATE engine-core
    PRIVATE GTest::gtest_main
)

gtest_discover_tests(full-platform-tests)

# ─── QuickFIX Integration Tests (conditional) ────────────────
if(QUICKFIX_FOUND)
    add_executable(quickfix-integration-tests
        test/quickfix_integration_test.cpp
    )

    target_link_libraries(quickfix-integration-tests
        PRIVATE fix-gateway-adapter
        PRIVATE GTest::gtest_main
    )

    gtest_discover_tests(quickfix-integration-tests)
endif()
