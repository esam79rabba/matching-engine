#include "ops/http/http_server.h"
#include "ops/auth_service.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <sstream>

namespace ops {

HttpServer::HttpServer(uint16_t port) : port_(port) {}

HttpServer::~HttpServer() { stop(); }

void HttpServer::set_auth(const AuthMiddleware& auth) {
    auth_ = auth;
}

void HttpServer::wire(OpsController& ops_ctrl, refdata::RefDataHttpHandler& refdata_handler,
                       AuthService* auth_service) {
    handler_ = [&ops_ctrl, &refdata_handler, auth_service](
            const std::string& method, const std::string& path,
            const std::string& body, const std::string& raw_request) -> std::pair<int, std::string> {

        // Extract headers for OpsRequest
        auto hdrs = AuthMiddleware::parse_headers(raw_request);

        // Route: instrument ops actions (halt/resume/close) → ops controller
        if (path.rfind("/api/v1/instruments", 0) == 0 && method == "POST" &&
            (path.find("/halt") != std::string::npos ||
             path.find("/resume") != std::string::npos ||
             path.find("/close") != std::string::npos)) {
            OpsRequest ops_req{method, path, body, {}, hdrs};
            auto resp = ops_ctrl.handle(ops_req);
            return {resp.status, resp.body};
        }

        // RBAC: refdata write endpoints (POST/PUT) require admin role
        if ((method == "POST" || method == "PUT") &&
            (path.rfind("/api/v1/instruments", 0) == 0 ||
             path.rfind("/api/v1/participants", 0) == 0 ||
             path.rfind("/api/v1/sessions", 0) == 0)) {
            if (auth_service) {
                auto auth_it = hdrs.find("Authorization");
                bool authorized = false;
                if (auth_it != hdrs.end()) {
                    std::string token = auth_it->second;
                    const std::string bearer_prefix = "Bearer ";
                    if (token.size() > bearer_prefix.size() &&
                        token.substr(0, bearer_prefix.size()) == bearer_prefix) {
                        token = token.substr(bearer_prefix.size());
                    }
                    authorized = auth_service->has_role(token, "admin");
                }
                if (!authorized) {
                    return {403, R"({"error":"admin role required"})"};
                }
            }
        }

        // Route: /api/v1/instruments (read + create/update) → ref data handler
        if (path.rfind("/api/v1/instruments", 0) == 0) {
            auto resp = refdata_handler.handle({method, path, body});
            return {resp.status, resp.body};
        }
        if (path.rfind("/api/v1/symbols", 0) == 0 ||
            path.rfind("/api/v1/participants", 0) == 0 ||
            path.rfind("/api/v1/sessions", 0) == 0) {
            auto resp = refdata_handler.handle({method, path, body});
            return {resp.status, resp.body};
        }

        // Route: everything else → ops controller
        OpsRequest ops_req{method, path, body, {}, hdrs};
        auto resp = ops_ctrl.handle(ops_req);
        return {resp.status, resp.body};
    };
}

bool HttpServer::start() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return false;

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (listen(listen_fd_, 16) < 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    running_ = true;

    while (running_) {
        struct timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_fd_, &read_fds);

        int ready = select(listen_fd_ + 1, &read_fds, nullptr, nullptr, &tv);
        if (ready <= 0) continue;

        int client_fd = accept(listen_fd_, nullptr, nullptr);
        if (client_fd < 0) continue;

        handle_request(client_fd);
        close(client_fd);
    }

    return true;
}

void HttpServer::stop() {
    running_ = false;
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
}

void HttpServer::handle_request(int client_fd) {
    // Read headers + possibly partial body
    char buf[8192];
    ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return;
    buf[n] = '\0';

    std::string raw_request(buf, static_cast<size_t>(n));

    // Check if we need to read more body based on Content-Length
    auto hdr_end = raw_request.find("\r\n\r\n");
    if (hdr_end != std::string::npos) {
        // Look for Content-Length header
        auto cl_pos = raw_request.find("Content-Length:");
        if (cl_pos == std::string::npos) cl_pos = raw_request.find("content-length:");
        if (cl_pos != std::string::npos && cl_pos < hdr_end) {
            auto val_start = raw_request.find_first_of("0123456789", cl_pos);
            auto val_end = raw_request.find_first_not_of("0123456789", val_start);
            if (val_start != std::string::npos) {
                size_t content_length = std::stoull(raw_request.substr(val_start, val_end - val_start));
                size_t body_start = hdr_end + 4;
                size_t body_received = raw_request.size() - body_start;
                // Read remaining body if incomplete — set recv timeout
                struct timeval body_tv{};
                body_tv.tv_sec = 1;
                body_tv.tv_usec = 0;
                setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &body_tv, sizeof(body_tv));
                while (body_received < content_length) {
                    ssize_t more = recv(client_fd, buf, std::min(sizeof(buf) - 1, content_length - body_received), 0);
                    if (more <= 0) break;
                    buf[more] = '\0';
                    raw_request.append(buf, static_cast<size_t>(more));
                    body_received += static_cast<size_t>(more);
                }
            }
        }
    }

    std::string method, path, body;
    if (!parse_http_request(raw_request, method, path, body)) {
        std::string resp = build_http_response(400, R"({"error":"bad request"})");
        ::send(client_fd, resp.c_str(), resp.size(), 0);
        return;
    }

    // CORS preflight (OPTIONS)
    if (method == "OPTIONS") {
        std::string resp = build_http_response(200, "");
        ::send(client_fd, resp.c_str(), resp.size(), 0);
        return;
    }

    // Auth check
    if (auth_.is_enabled() && !auth_.check_raw(raw_request, path)) {
        std::string resp = build_http_response(401, R"({"error":"unauthorized"})");
        ::send(client_fd, resp.c_str(), resp.size(), 0);
        return;
    }

    if (!handler_) {
        std::string resp = build_http_response(500, R"({"error":"no handler"})");
        ::send(client_fd, resp.c_str(), resp.size(), 0);
        return;
    }

    auto [status, resp_body] = handler_(method, path, body, raw_request);
    std::string resp = build_http_response(status, resp_body);
    ::send(client_fd, resp.c_str(), resp.size(), 0);
}

bool HttpServer::parse_http_request(const std::string& raw,
                                      std::string& method, std::string& path,
                                      std::string& body) {
    // Parse: "GET /api/v1/health HTTP/1.1\r\n..."
    auto first_line_end = raw.find("\r\n");
    if (first_line_end == std::string::npos) return false;

    std::string first_line = raw.substr(0, first_line_end);
    auto sp1 = first_line.find(' ');
    auto sp2 = first_line.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) return false;

    method = first_line.substr(0, sp1);
    path = first_line.substr(sp1 + 1, sp2 - sp1 - 1);

    // Extract body (after \r\n\r\n)
    auto body_start = raw.find("\r\n\r\n");
    if (body_start != std::string::npos) {
        body = raw.substr(body_start + 4);
    }

    return true;
}

std::string HttpServer::build_http_response(int status, const std::string& body) const {
    std::ostringstream oss;
    std::string status_text = (status == 200) ? "OK" :
                               (status == 201) ? "Created" :
                               (status == 400) ? "Bad Request" :
                               (status == 401) ? "Unauthorized" :
                               (status == 403) ? "Forbidden" :
                               (status == 404) ? "Not Found" :
                               (status == 429) ? "Too Many Requests" :
                               (status == 500) ? "Internal Server Error" : "Unknown";

    std::string origin = cors_origin_.empty() ? "*" : cors_origin_;

    oss << "HTTP/1.1 " << status << " " << status_text << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Access-Control-Allow-Origin: " << origin << "\r\n"
        << "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
        << "Access-Control-Allow-Headers: Authorization, X-API-Key, Content-Type\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << body;
    return oss.str();
}

}  // namespace ops
