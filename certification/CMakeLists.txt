# certification — Client certification harness (Milestone 11)
#
# The cert-harness library acts as an external FIX client using raw
# POSIX sockets. It does NOT link to engine internals.
# The test executable wires the harness to a real platform stack.

add_library(cert-harness
    src/cert_harness.cpp
    src/cert_report_writer.cpp
)

target_include_directories(cert-harness
    PUBLIC include
)

# The harness itself has no engine dependencies — it's a pure FIX TCP client.
# No target_link_libraries needed for the library.

# ─── Certification Tests ─────────────────────────────────────────
# The test wires the harness to a real FIX TCP acceptor + engine stack.
add_executable(cert-harness-tests
    test/cert_harness_test.cpp
    test/cert_report_test.cpp
)

target_link_libraries(cert-harness-tests
    PRIVATE cert-harness
    PRIVATE fix-gateway-adapter
    PRIVATE risk-service
    PRIVATE refdata-service
    PRIVATE ops-controller
    PRIVATE engine-core
    PRIVATE GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(cert-harness-tests)
