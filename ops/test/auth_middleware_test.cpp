/// Unit tests for AuthMiddleware — API-key-based authentication for ops endpoints.

#include "ops/auth_middleware.h"
#include <gtest/gtest.h>

namespace ops {
namespace {

TEST(AuthMiddlewareTest, OpenPathAllowed) {
    AuthMiddleware auth;
    auth.add_api_key("secret-key-123");
    auth.set_open_paths({"/health", "/api/v1/health"});

    // Open paths should be allowed even without any auth header
    std::unordered_map<std::string, std::string> no_headers;
    EXPECT_TRUE(auth.check(no_headers, "/health"));
    EXPECT_TRUE(auth.check(no_headers, "/api/v1/health"));
}

TEST(AuthMiddlewareTest, ValidBearerTokenAllowed) {
    AuthMiddleware auth;
    auth.add_api_key("my-secret-key");

    std::unordered_map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer my-secret-key";
    EXPECT_TRUE(auth.check(headers, "/api/v1/instruments"));
}

TEST(AuthMiddlewareTest, ValidApiKeyHeaderAllowed) {
    AuthMiddleware auth;
    auth.add_api_key("my-secret-key");

    std::unordered_map<std::string, std::string> headers;
    headers["X-API-Key"] = "my-secret-key";
    EXPECT_TRUE(auth.check(headers, "/api/v1/instruments"));
}

TEST(AuthMiddlewareTest, InvalidKeyRejected) {
    AuthMiddleware auth;
    auth.add_api_key("correct-key");

    std::unordered_map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer wrong-key";
    EXPECT_FALSE(auth.check(headers, "/api/v1/instruments"));

    headers.clear();
    headers["X-API-Key"] = "wrong-key";
    EXPECT_FALSE(auth.check(headers, "/api/v1/instruments"));
}

TEST(AuthMiddlewareTest, MissingKeyRejected) {
    AuthMiddleware auth;
    auth.add_api_key("my-key");

    // No auth headers at all
    std::unordered_map<std::string, std::string> no_headers;
    EXPECT_FALSE(auth.check(no_headers, "/api/v1/instruments"));
}

TEST(AuthMiddlewareTest, NoKeysConfiguredAllowsAll) {
    AuthMiddleware auth;
    // No keys added — auth is disabled

    std::unordered_map<std::string, std::string> no_headers;
    EXPECT_TRUE(auth.check(no_headers, "/api/v1/instruments"));
    EXPECT_TRUE(auth.check(no_headers, "/any/path"));
    EXPECT_FALSE(auth.is_enabled());
}

TEST(AuthMiddlewareTest, ParseHeadersFromRawHttp) {
    std::string raw =
        "GET /api/v1/health HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "Authorization: Bearer test-key-456\r\n"
        "X-API-Key: another-key\r\n"
        "Content-Type: application/json\r\n"
        "\r\n"
        "{\"body\":true}";

    auto headers = AuthMiddleware::parse_headers(raw);

    EXPECT_EQ(headers["Host"], "localhost:8080");
    EXPECT_EQ(headers["Authorization"], "Bearer test-key-456");
    EXPECT_EQ(headers["X-API-Key"], "another-key");
    EXPECT_EQ(headers["Content-Type"], "application/json");
}

TEST(AuthMiddlewareTest, CheckRawWithBearerToken) {
    AuthMiddleware auth;
    auth.add_api_key("raw-test-key");

    std::string raw =
        "GET /api/v1/instruments HTTP/1.1\r\n"
        "Authorization: Bearer raw-test-key\r\n"
        "\r\n";

    EXPECT_TRUE(auth.check_raw(raw, "/api/v1/instruments"));
}

TEST(AuthMiddlewareTest, CheckRawWithInvalidKey) {
    AuthMiddleware auth;
    auth.add_api_key("correct-key");

    std::string raw =
        "GET /api/v1/instruments HTTP/1.1\r\n"
        "Authorization: Bearer wrong-key\r\n"
        "\r\n";

    EXPECT_FALSE(auth.check_raw(raw, "/api/v1/instruments"));
}

TEST(AuthMiddlewareTest, CheckRawOpenPathNoKey) {
    AuthMiddleware auth;
    auth.add_api_key("some-key");
    auth.set_open_paths({"/health"});

    std::string raw =
        "GET /health HTTP/1.1\r\n"
        "\r\n";

    EXPECT_TRUE(auth.check_raw(raw, "/health"));
}

}  // namespace
}  // namespace ops
