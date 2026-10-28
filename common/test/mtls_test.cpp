/// mTLS integration tests — mutual TLS authentication.
///
/// Tests verify that TlsServerSocket correctly enforces client certificate
/// requirements when config_.require_client_cert is true.
///
/// Generates a self-signed CA, server cert, and client cert at runtime.
#ifdef OPENSSL_AVAILABLE

#include "transport/tls_socket.h"
#include "transport/socket.h"
#include <gtest/gtest.h>
#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <string>

namespace transport {
namespace {

// ─── Helper: generate an RSA key pair ────────────────────

static EVP_PKEY* generate_rsa_key() {
    EVP_PKEY* pkey = EVP_PKEY_new();
    if (!pkey) return nullptr;

    EVP_PKEY_CTX* kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!kctx) { EVP_PKEY_free(pkey); return nullptr; }

    if (EVP_PKEY_keygen_init(kctx) <= 0 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(kctx, 2048) <= 0 ||
        EVP_PKEY_keygen(kctx, &pkey) <= 0) {
        EVP_PKEY_CTX_free(kctx);
        EVP_PKEY_free(pkey);
        return nullptr;
    }
    EVP_PKEY_CTX_free(kctx);
    return pkey;
}

// ─── Helper: create a self-signed CA certificate ─────────

struct CaBundle {
    EVP_PKEY* ca_key = nullptr;
    X509*     ca_cert = nullptr;
    std::string ca_cert_path;
    std::string ca_key_path;
    bool valid = false;

    CaBundle() = default;
    ~CaBundle() {
        if (ca_cert) X509_free(ca_cert);
        if (ca_key) EVP_PKEY_free(ca_key);
    }

    // Move-only to avoid double-free
    CaBundle(const CaBundle&) = delete;
    CaBundle& operator=(const CaBundle&) = delete;
    CaBundle(CaBundle&& o) noexcept
        : ca_key(o.ca_key), ca_cert(o.ca_cert),
          ca_cert_path(std::move(o.ca_cert_path)),
          ca_key_path(std::move(o.ca_key_path)),
          valid(o.valid) {
        o.ca_key = nullptr;
        o.ca_cert = nullptr;
        o.valid = false;
    }
    CaBundle& operator=(CaBundle&& o) noexcept {
        if (this != &o) {
            if (ca_cert) X509_free(ca_cert);
            if (ca_key) EVP_PKEY_free(ca_key);
            ca_key = o.ca_key; ca_cert = o.ca_cert;
            ca_cert_path = std::move(o.ca_cert_path);
            ca_key_path = std::move(o.ca_key_path);
            valid = o.valid;
            o.ca_key = nullptr; o.ca_cert = nullptr; o.valid = false;
        }
        return *this;
    }
};

static CaBundle generate_ca(const std::string& prefix) {
    CaBundle ca;
    ca.ca_cert_path = "/tmp/" + prefix + "_ca_cert.pem";
    ca.ca_key_path  = "/tmp/" + prefix + "_ca_key.pem";

    ca.ca_key = generate_rsa_key();
    if (!ca.ca_key) return ca;

    ca.ca_cert = X509_new();
    if (!ca.ca_cert) return ca;

    ASN1_INTEGER_set(X509_get_serialNumber(ca.ca_cert), 1);
    X509_gmtime_adj(X509_get_notBefore(ca.ca_cert), 0);
    X509_gmtime_adj(X509_get_notAfter(ca.ca_cert), 3600);
    X509_set_pubkey(ca.ca_cert, ca.ca_key);

    // OpenSSL 4.x returns const X509_NAME*; cast away const for the add_entry call,
    // safe when the caller owns the certificate.
    X509_NAME* name = const_cast<X509_NAME*>(X509_get_subject_name(ca.ca_cert));
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                reinterpret_cast<const unsigned char*>("Test CA"),
                                -1, -1, 0);
    X509_set_issuer_name(ca.ca_cert, name);

    if (X509_sign(ca.ca_cert, ca.ca_key, EVP_sha256()) == 0) return ca;

    // Write CA cert
    FILE* fp = fopen(ca.ca_cert_path.c_str(), "w");
    if (!fp) return ca;
    PEM_write_X509(fp, ca.ca_cert);
    fclose(fp);

    // Write CA key
    fp = fopen(ca.ca_key_path.c_str(), "w");
    if (!fp) return ca;
    PEM_write_PrivateKey(fp, ca.ca_key, nullptr, nullptr, 0, nullptr, nullptr);
    fclose(fp);

    ca.valid = true;
    return ca;
}

// ─── Helper: generate a cert signed by a CA ──────────────

struct SignedCert {
    std::string cert_path;
    std::string key_path;
    bool valid = false;
};

static SignedCert generate_signed_cert(const CaBundle& ca,
                                        const std::string& cn,
                                        const std::string& prefix,
                                        long serial) {
    SignedCert result;
    result.cert_path = "/tmp/" + prefix + "_cert.pem";
    result.key_path  = "/tmp/" + prefix + "_key.pem";

    EVP_PKEY* key = generate_rsa_key();
    if (!key) return result;

    X509* cert = X509_new();
    if (!cert) { EVP_PKEY_free(key); return result; }

    ASN1_INTEGER_set(X509_get_serialNumber(cert), serial);
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), 3600);
    X509_set_pubkey(cert, key);

    X509_NAME* subj = const_cast<X509_NAME*>(X509_get_subject_name(cert));
    X509_NAME_add_entry_by_txt(subj, "CN", MBSTRING_ASC,
                                reinterpret_cast<const unsigned char*>(cn.c_str()),
                                -1, -1, 0);

    // Issuer is the CA
    X509_set_issuer_name(cert, X509_get_subject_name(ca.ca_cert));

    // Sign with CA key
    if (X509_sign(cert, ca.ca_key, EVP_sha256()) == 0) {
        X509_free(cert);
        EVP_PKEY_free(key);
        return result;
    }

    // Write cert
    FILE* fp = fopen(result.cert_path.c_str(), "w");
    if (!fp) { X509_free(cert); EVP_PKEY_free(key); return result; }
    PEM_write_X509(fp, cert);
    fclose(fp);

    // Write key
    fp = fopen(result.key_path.c_str(), "w");
    if (!fp) { X509_free(cert); EVP_PKEY_free(key); return result; }
    PEM_write_PrivateKey(fp, key, nullptr, nullptr, 0, nullptr, nullptr);
    fclose(fp);

    X509_free(cert);
    EVP_PKEY_free(key);
    result.valid = true;
    return result;
}

// ─── Cleanup helper ──────────────────────────────────────

static void cleanup_files(const std::vector<std::string>& paths) {
    for (const auto& p : paths) {
        std::remove(p.c_str());
    }
}

// ─── Test fixture ────────────────────────────────────────

class MtlsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Generate CA
        ca_ = generate_ca("mtls_test");
        ASSERT_TRUE(ca_.valid) << "Failed to generate test CA";

        // Generate server cert signed by CA
        server_cert_ = generate_signed_cert(ca_, "localhost", "mtls_server", 10);
        ASSERT_TRUE(server_cert_.valid) << "Failed to generate server cert";

        // Generate client cert signed by same CA
        client_cert_ = generate_signed_cert(ca_, "test-client", "mtls_client", 20);
        ASSERT_TRUE(client_cert_.valid) << "Failed to generate client cert";

        // Generate a second CA (untrusted) and client cert signed by it
        rogue_ca_ = generate_ca("mtls_rogue");
        ASSERT_TRUE(rogue_ca_.valid) << "Failed to generate rogue CA";

        rogue_client_cert_ = generate_signed_cert(rogue_ca_, "rogue-client", "mtls_rogue_client", 30);
        ASSERT_TRUE(rogue_client_cert_.valid) << "Failed to generate rogue client cert";
    }

    void TearDown() override {
        cleanup_files({
            ca_.ca_cert_path, ca_.ca_key_path,
            server_cert_.cert_path, server_cert_.key_path,
            client_cert_.cert_path, client_cert_.key_path,
            rogue_ca_.ca_cert_path, rogue_ca_.ca_key_path,
            rogue_client_cert_.cert_path, rogue_client_cert_.key_path,
        });
    }

    /// Start a TLS server with mTLS enabled, return the bound port.
    TlsServerSocket create_mtls_server() {
        TlsConfig cfg;
        cfg.cert_file = server_cert_.cert_path;
        cfg.key_file  = server_cert_.key_path;
        cfg.ca_file   = ca_.ca_cert_path;
        cfg.require_client_cert = true;
        cfg.min_tls_version = "TLSv1.2";
        return TlsServerSocket(0, cfg);
    }

    CaBundle   ca_;
    SignedCert server_cert_;
    SignedCert client_cert_;
    CaBundle   rogue_ca_;
    SignedCert rogue_client_cert_;
};

// ─── Test: reject connection without client cert ─────────

TEST_F(MtlsTest, RejectsWithoutClientCert) {
    auto server = create_mtls_server();
    ASSERT_TRUE(server.init_context());
    ASSERT_TRUE(server.listen());
    uint16_t port = server.bound_port();
    ASSERT_GT(port, 0u);

    std::atomic<bool> handshake_succeeded{false};

    std::thread client_thread([port, &handshake_succeeded, this] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return;

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);
        if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            return;
        }

        // Client context WITHOUT client certificate
        SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) { ::close(fd); return; }

        // Trust the server's CA so we attempt the handshake
        SSL_CTX_load_verify_locations(ctx, ca_.ca_cert_path.c_str(), nullptr);

        SSL* ssl = SSL_new(ctx);
        SSL_set_fd(ssl, fd);
        int rc = SSL_connect(ssl);
        handshake_succeeded.store(rc == 1);

        SSL_shutdown(ssl);
        SSL_free(ssl);
        ::close(fd);
        SSL_CTX_free(ctx);
    });

    // Server accept should fail — SSL_accept returns error when client has no cert
    ISocket* client = server.accept();
    EXPECT_EQ(client, nullptr)
        << "Server should reject TLS handshake when client presents no certificate";
    if (client) {
        client->close();
        delete client;
    }

    client_thread.join();
    server.close();
}

// ─── Test: accept connection with valid client cert ──────

TEST_F(MtlsTest, AcceptsWithValidClientCert) {
    auto server = create_mtls_server();
    ASSERT_TRUE(server.init_context());
    ASSERT_TRUE(server.listen());
    uint16_t port = server.bound_port();
    ASSERT_GT(port, 0u);

    std::atomic<bool> handshake_succeeded{false};

    std::thread client_thread([port, &handshake_succeeded, this] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return;

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);
        if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            return;
        }

        SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) { ::close(fd); return; }

        // Load client certificate and key
        SSL_CTX_use_certificate_file(ctx, client_cert_.cert_path.c_str(), SSL_FILETYPE_PEM);
        SSL_CTX_use_PrivateKey_file(ctx, client_cert_.key_path.c_str(), SSL_FILETYPE_PEM);

        // Trust the server's CA
        SSL_CTX_load_verify_locations(ctx, ca_.ca_cert_path.c_str(), nullptr);

        SSL* ssl = SSL_new(ctx);
        SSL_set_fd(ssl, fd);
        int rc = SSL_connect(ssl);
        handshake_succeeded.store(rc == 1);

        if (rc == 1) {
            // Exchange data to prove the connection works
            const char* msg = "MTLS_HELLO";
            SSL_write(ssl, msg, 10);

            char buf[32]{};
            SSL_read(ssl, buf, sizeof(buf));
        }

        SSL_shutdown(ssl);
        SSL_free(ssl);
        ::close(fd);
        SSL_CTX_free(ctx);
    });

    ISocket* client = server.accept();
    ASSERT_NE(client, nullptr) << "Server should accept mTLS client with valid cert";
    EXPECT_TRUE(client->is_open());

    // Read client message
    char buf[32]{};
    int n = client->recv(buf, sizeof(buf));
    ASSERT_GT(n, 0);
    EXPECT_EQ(std::string(buf, static_cast<size_t>(n)), "MTLS_HELLO");

    // Send response
    const char* reply = "MTLS_WORLD";
    int sent = client->send(reply, 10);
    EXPECT_EQ(sent, 10);

    client->close();
    delete client;

    client_thread.join();
    EXPECT_TRUE(handshake_succeeded.load())
        << "Handshake should succeed with valid client certificate";

    server.close();
}

// ─── Test: reject connection with cert from untrusted CA ─

TEST_F(MtlsTest, RejectsWithInvalidClientCert) {
    auto server = create_mtls_server();
    ASSERT_TRUE(server.init_context());
    ASSERT_TRUE(server.listen());
    uint16_t port = server.bound_port();
    ASSERT_GT(port, 0u);

    std::atomic<bool> handshake_succeeded{false};

    std::thread client_thread([port, &handshake_succeeded, this] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return;

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);
        if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            return;
        }

        SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) { ::close(fd); return; }

        // Load client cert signed by the ROGUE CA (untrusted by server)
        SSL_CTX_use_certificate_file(ctx, rogue_client_cert_.cert_path.c_str(), SSL_FILETYPE_PEM);
        SSL_CTX_use_PrivateKey_file(ctx, rogue_client_cert_.key_path.c_str(), SSL_FILETYPE_PEM);

        // Trust the server's real CA for server verification
        SSL_CTX_load_verify_locations(ctx, ca_.ca_cert_path.c_str(), nullptr);

        SSL* ssl = SSL_new(ctx);
        SSL_set_fd(ssl, fd);
        int rc = SSL_connect(ssl);
        handshake_succeeded.store(rc == 1);

        SSL_shutdown(ssl);
        SSL_free(ssl);
        ::close(fd);
        SSL_CTX_free(ctx);
    });

    // Server accept should fail — SSL_accept rejects cert from untrusted CA
    ISocket* client = server.accept();
    EXPECT_EQ(client, nullptr)
        << "Server should reject TLS handshake when client cert is from untrusted CA";
    if (client) {
        client->close();
        delete client;
    }

    client_thread.join();
    server.close();
}

}  // namespace
}  // namespace transport

#endif  // OPENSSL_AVAILABLE
