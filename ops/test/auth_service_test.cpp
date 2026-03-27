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

}  // namespace
}  // namespace ops
