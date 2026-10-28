# simulator — FIX participant simulator (market-making client)
#
# Standalone program that connects to the venue over TCP/TLS and
# exercises the full FIX 4.4 order lifecycle.

add_library(participant-simulator
    src/participant_simulator.cpp
)

target_include_directories(participant-simulator PUBLIC include)

# No engine dependencies — this is a pure FIX TCP client (like cert-harness).
# Needs OpenSSL for TLS support.
target_link_libraries(participant-simulator PUBLIC OpenSSL::SSL OpenSSL::Crypto)

# ─── Standalone executable ────────────────────────────────────────
add_executable(fix-simulator src/main.cpp)
target_link_libraries(fix-simulator PRIVATE participant-simulator)

# ─── Tests ────────────────────────────────────────────────────────
# Tests wire the simulator to a real FIX TCP acceptor + engine stack.
add_executable(simulator-tests
    test/simulator_test.cpp
)

target_link_libraries(simulator-tests
    PRIVATE participant-simulator
    PRIVATE fix-gateway-adapter
    PRIVATE risk-service
    PRIVATE refdata-service
    PRIVATE ops-controller
    PRIVATE engine-core
    PRIVATE GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(simulator-tests)
