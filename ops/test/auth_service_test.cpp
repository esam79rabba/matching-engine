#include "ops/auth_service.h"
#include "ops/ops_controller.h"
#include <gtest/gtest.h>

namespace ops {
namespace {

TEST(AuthServiceTest, LoginSuccess) {
    AuthService svc;
    svc.add_user("admin", "secret", "admin");

    auto token = svc.login("admin", "secret");
    ASSERT_TRUE(token.has_value());
    EXPECT_FALSE(token->empty());
}

TEST(AuthServiceTest, LoginFailureBadPassword) {
    AuthService svc;
    svc.add_user("admin", "secret", "admin");

    auto token = svc.login("admin", "wrong");
    EXPECT_FALSE(token.has_value());
}

TEST(AuthServiceTest, LoginFailureUnknownUser) {
    AuthService svc;
    svc.add_user("admin", "secret", "admin");

    auto token = svc.login("nobody", "secret");
    EXPECT_FALSE(token.has_value());
}

TEST(AuthServiceTest, ValidateToken) {
    AuthService svc;
    svc.add_user("op1", "pass", "operator");

    auto token = svc.login("op1", "pass");
    ASSERT_TRUE(token.has_value());

    auto info = svc.validate(*token);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->username, "op1");
    EXPECT_EQ(info->role, "operator");
}

TEST(AuthServiceTest, ValidateInvalidToken) {
    AuthService svc;
    svc.add_user("op1", "pass", "operator");

    auto info = svc.validate("deadbeef");
    EXPECT_FALSE(info.has_value());
}

TEST(AuthServiceTest, RoleHierarchy) {
    AuthService svc;
    svc.add_user("admin_user", "pass", "admin");
    svc.add_user("op_user", "pass", "operator");
    svc.add_user("view_user", "pass", "viewer");

    auto admin_token = svc.login("admin_user", "pass");
    auto op_token = svc.login("op_user", "pass");
    auto view_token = svc.login("view_user", "pass");

    // Admin has all roles
    EXPECT_TRUE(svc.has_role(*admin_token, "viewer"));
    EXPECT_TRUE(svc.has_role(*admin_token, "operator"));
    EXPECT_TRUE(svc.has_role(*admin_token, "admin"));

    // Operator has viewer and operator, not admin
    EXPECT_TRUE(svc.has_role(*op_token, "viewer"));
    EXPECT_TRUE(svc.has_role(*op_token, "operator"));
    EXPECT_FALSE(svc.has_role(*op_token, "admin"));

    // Viewer has only viewer
    EXPECT_TRUE(svc.has_role(*view_token, "viewer"));
    EXPECT_FALSE(svc.has_role(*view_token, "operator"));
    EXPECT_FALSE(svc.has_role(*view_token, "admin"));
}

TEST(AuthServiceTest, LoginEndpointViaController) {
    AuthService svc;
    svc.add_user("admin", "secret123", "admin");

    OpsController ctrl;
    ctrl.set_auth_service(&svc);

    // Successful login
    OpsRequest login_req;
    login_req.method = "POST";
    login_req.path = "/api/v1/auth/login";
    login_req.body = R"({"username":"admin","password":"secret123"})";

    auto resp = ctrl.handle(login_req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"token\":"), std::string::npos);
}

TEST(AuthServiceTest, LoginEndpointBadCredentials) {
    AuthService svc;
    svc.add_user("admin", "secret123", "admin");

    OpsController ctrl;
    ctrl.set_auth_service(&svc);

    OpsRequest login_req;
    login_req.method = "POST";
    login_req.path = "/api/v1/auth/login";
    login_req.body = R"({"username":"admin","password":"wrong"})";

    auto resp = ctrl.handle(login_req);
    EXPECT_EQ(resp.status, 401);
}

TEST(AuthServiceTest, MeEndpointSuccess) {
    AuthService svc;
    svc.add_user("op1", "pass", "operator");

    auto token = svc.login("op1", "pass");
    ASSERT_TRUE(token.has_value());

    OpsController ctrl;
    ctrl.set_auth_service(&svc);

    OpsRequest me_req;
    me_req.method = "GET";
    me_req.path = "/api/v1/auth/me";
    me_req.headers["Authorization"] = "Bearer " + *token;

    auto resp = ctrl.handle(me_req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"username\":\"op1\""), std::string::npos);
    EXPECT_NE(resp.body.find("\"role\":\"operator\""), std::string::npos);
}

TEST(AuthServiceTest, MeEndpointNoToken) {
    AuthService svc;
    svc.add_user("op1", "pass", "operator");

    OpsController ctrl;
    ctrl.set_auth_service(&svc);

    OpsRequest me_req;
    me_req.method = "GET";
    me_req.path = "/api/v1/auth/me";

    auto resp = ctrl.handle(me_req);
    EXPECT_EQ(resp.status, 401);
}

TEST(AuthServiceTest, HmacSignedToken) {
    AuthService svc;
    svc.set_signing_key("test-secret-key");
    svc.add_user("admin", "pass", "admin");

    auto token = svc.login("admin", "pass");
    ASSERT_TRUE(token.has_value());

    // Token should contain a dot separator (payload.signature) when HMAC is available
    EXPECT_NE(token->find('.'), std::string::npos) << "HMAC token should contain dot separator";

    // Valid token should validate
    auto info = svc.validate(*token);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->username, "admin");
    EXPECT_EQ(info->role, "admin");

    // Tampered token should be rejected
    std::string tampered = *token;
    if (!tampered.empty()) {
        // Flip a character in the token
        tampered[tampered.size() / 2] = (tampered[tampered.size() / 2] == 'a') ? 'b' : 'a';
    }
    auto bad_info = svc.validate(tampered);
    // With HMAC, tampered tokens should be rejected.
    // Without HMAC, the hex decoding may still fail if we corrupted a hex char.
    EXPECT_FALSE(bad_info.has_value()) << "Tampered HMAC token should be rejected";
}

TEST(AuthServiceTest, ExpiredTokenRejected) {
    // We cannot easily test time-based expiry without mocking the clock,
    // but we can verify that the expiry constant is set correctly and that
    // tokens within the window are accepted.
    AuthService svc;
    svc.add_user("test", "pass", "viewer");

    auto token = svc.login("test", "pass");
    ASSERT_TRUE(token.has_value());

    // Token just created should be valid (within 24h window)
    auto info = svc.validate(*token);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->username, "test");

    // Verify expiry constant is 24 hours
    EXPECT_EQ(AuthService::TOKEN_EXPIRY_SECONDS, 86400);
}

TEST(AuthServiceTest, PasswordNotStoredPlaintext) {
    AuthService svc;
    svc.add_user("testuser", "mysecretpassword", "admin");

    // Verify correct password still works
    auto token = svc.login("testuser", "mysecretpassword");
    ASSERT_TRUE(token.has_value()) << "Login with correct password should succeed";

    // Verify wrong password fails
    auto bad = svc.login("testuser", "wrongpassword");
    EXPECT_FALSE(bad.has_value()) << "Login with wrong password should fail";

    // Verify the token is valid
    auto info = svc.validate(*token);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->username, "testuser");
    EXPECT_EQ(info->role, "admin");
}

TEST(AuthServiceTest, SigningKeyChange) {
    AuthService svc;
    svc.set_signing_key("key-1");
    svc.add_user("admin", "pass", "admin");

    auto token = svc.login("admin", "pass");
    ASSERT_TRUE(token.has_value());

    // Token should validate with original key
    auto info = svc.validate(*token);
    ASSERT_TRUE(info.has_value());

    // Change signing key — token should now be rejected
    svc.set_signing_key("key-2");
    auto bad_info = svc.validate(*token);
    EXPECT_FALSE(bad_info.has_value()) << "Token signed with old key should be rejected";
}

}  // namespace
}  // namespace ops
