/// TLS transport tests — TlsSocket and TlsServerSocket.
///
/// Unit tests work without real certificates.
/// Integration test generates self-signed certs in /tmp for a full handshake.
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
#include <cstring>
#include <cstdio>
#include <fstream>

namespace transport {
namespace {

// ─── Unit tests (no certificates needed) ─────────────────

TEST(TlsContextTest, InitFailsWithMissingCert) {
    TlsConfig cfg;
    cfg.cert_file = "/nonexistent/path/cert.pem";
    cfg.key_file = "/nonexistent/path/key.pem";
    TlsServerSocket server(0, cfg);
    EXPECT_FALSE(server.init_context());
}

TEST(TlsConfigTest, Defaults) {
    TlsConfig cfg;
    EXPECT_TRUE(cfg.cert_file.empty());
    EXPECT_TRUE(cfg.key_file.empty());
    EXPECT_TRUE(cfg.ca_file.empty());
    EXPECT_FALSE(cfg.require_client_cert);
    EXPECT_EQ(cfg.min_tls_version, "TLSv1.2");
    EXPECT_TRUE(cfg.cipher_list.empty());
}

TEST(TlsSocketTest, Polymorphism) {
    // TlsServerSocket is-an IServerSocket
    TlsConfig cfg;
    cfg.cert_file = "dummy.pem";
    cfg.key_file = "dummy.pem";
    TlsServerSocket tls_server(0, cfg);
    IServerSocket* server = &tls_server;

    // Without init_context(), listen() should fail (ctx_ is null)
    EXPECT_FALSE(server->listen());
    EXPECT_FALSE(server->is_listening());
    EXPECT_EQ(tls_server.port(), 0u);
}

TEST(TlsContextTest, ListenFailsWithoutInitContext) {
    TlsConfig cfg;
    cfg.cert_file = "dummy.pem";
    cfg.key_file = "dummy.pem";
    TlsServerSocket server(0, cfg);
    // Skipping init_context() — listen() must return false
    EXPECT_FALSE(server.listen());
    EXPECT_FALSE(server.is_listening());
}

// ─── Helper: generate self-signed cert + key in /tmp ─────

struct TestCert {
    std::string cert_path;
    std::string key_path;
    bool valid = false;
};

static TestCert generate_self_signed_cert() {
    TestCert result;
    result.cert_path = "/tmp/me_test_cert.pem";
    result.key_path = "/tmp/me_test_key.pem";

    // Generate RSA key pair
    EVP_PKEY* pkey = EVP_PKEY_new();
    if (!pkey) return result;

    EVP_PKEY_CTX* kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!kctx) { EVP_PKEY_free(pkey); return result; }

    if (EVP_PKEY_keygen_init(kctx) <= 0 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(kctx, 2048) <= 0 ||
        EVP_PKEY_keygen(kctx, &pkey) <= 0) {
        EVP_PKEY_CTX_free(kctx);
        EVP_PKEY_free(pkey);
        return result;
    }
    EVP_PKEY_CTX_free(kctx);

    // Create self-signed X509 certificate
    X509* x509 = X509_new();
    if (!x509) { EVP_PKEY_free(pkey); return result; }

    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), 3600);  // valid 1 hour
    X509_set_pubkey(x509, pkey);

    X509_NAME* name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                reinterpret_cast<const unsigned char*>("localhost"), -1, -1, 0);
    X509_set_issuer_name(x509, name);

    if (X509_sign(x509, pkey, EVP_sha256()) == 0) {
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return result;
    }

    // Write cert to file
    FILE* cert_fp = fopen(result.cert_path.c_str(), "w");
    if (!cert_fp) { X509_free(x509); EVP_PKEY_free(pkey); return result; }
    PEM_write_X509(cert_fp, x509);
    fclose(cert_fp);

    // Write key to file
    FILE* key_fp = fopen(result.key_path.c_str(), "w");
    if (!key_fp) { X509_free(x509); EVP_PKEY_free(pkey); return result; }
    PEM_write_PrivateKey(key_fp, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    fclose(key_fp);

    X509_free(x509);
    EVP_PKEY_free(pkey);
    result.valid = true;
    return result;
}

// ─── Integration test: full TLS handshake + send/recv ────

TEST(TlsIntegrationTest, HandshakeAndCommunicate) {
    auto certs = generate_self_signed_cert();
    ASSERT_TRUE(certs.valid) << "Failed to generate self-signed test certificates";

    TlsConfig cfg;
    cfg.cert_file = certs.cert_path;
    cfg.key_file = certs.key_path;
    cfg.min_tls_version = "TLSv1.2";

    TlsServerSocket server(0, cfg);
    ASSERT_TRUE(server.init_context());
    ASSERT_TRUE(server.listen());
    uint16_t port = server.bound_port();
    ASSERT_GT(port, 0u);

    // Client thread: connect with TLS
    std::thread client_thread([port] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        // Create TCP connection
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GE(fd, 0);

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);
        int rc = ::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        ASSERT_EQ(rc, 0);

        // Perform TLS client handshake
        SSL_CTX* client_ctx = SSL_CTX_new(TLS_client_method());
        ASSERT_NE(client_ctx, nullptr);

        // Don't verify server cert in test (self-signed)
        SSL_CTX_set_verify(client_ctx, SSL_VERIFY_NONE, nullptr);

        SSL* ssl = SSL_new(client_ctx);
        ASSERT_NE(ssl, nullptr);
        SSL_set_fd(ssl, fd);

        int ssl_rc = SSL_connect(ssl);
        ASSERT_EQ(ssl_rc, 1) << "TLS client handshake failed";

        // Send encrypted message
        const char* msg = "TLS_HELLO";
        int sent = SSL_write(ssl, msg, 9);
        EXPECT_EQ(sent, 9);

        // Receive encrypted response
        char buf[32]{};
        int received = SSL_read(ssl, buf, sizeof(buf));
        EXPECT_GT(received, 0);
        EXPECT_EQ(std::string(buf, static_cast<size_t>(received)), "TLS_WORLD");

        SSL_shutdown(ssl);
        SSL_free(ssl);
        ::close(fd);
        SSL_CTX_free(client_ctx);
    });

    // Server: accept TLS connection
    ISocket* client = server.accept();
    ASSERT_NE(client, nullptr);
    EXPECT_TRUE(client->is_open());
    EXPECT_GE(client->fd(), 0);

    // Read what client sent (through TLS)
    char buf[32]{};
    int n = client->recv(buf, sizeof(buf));
    ASSERT_GT(n, 0);
    EXPECT_EQ(std::string(buf, static_cast<size_t>(n)), "TLS_HELLO");

    // Send TLS response
    const char* reply = "TLS_WORLD";
    int sent = client->send(reply, 9);
    EXPECT_EQ(sent, 9);

    client->close();
    EXPECT_FALSE(client->is_open());
    delete client;

    client_thread.join();
    server.close();

    // Cleanup temp certs
    std::remove(certs.cert_path.c_str());
    std::remove(certs.key_path.c_str());
}

}  // namespace
}  // namespace transport
