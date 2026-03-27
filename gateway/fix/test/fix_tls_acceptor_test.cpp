/// TLS integration tests for FixTcpAcceptor with transport abstraction layer.
///
/// Tests verify that the acceptor works with both plain TCP and TLS server sockets.

#include "gateway/tcp/fix_tcp_acceptor.h"
#include "transport/tcp_socket.h"

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <cstring>
#include <atomic>

#ifdef OPENSSL_AVAILABLE
#include "transport/tls_socket.h"
#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <cstdio>
#endif

namespace gateway {
namespace {

/// Build a minimal FIX Logon message.
static std::string make_logon(const std::string& sender, const std::string& target, int hb = 30) {
    std::string msg;
    msg += "8=FIX.4.4\x01";
    msg += "35=A\x01";
    msg += "49=" + sender + "\x01";
    msg += "56=" + target + "\x01";
    msg += "34=1\x01";
    msg += "108=" + std::to_string(hb) + "\x01";
    msg += "10=000\x01";
    return msg;
}

// ─── Plain TCP test ──────────────────────────────────────

TEST(FixTlsAcceptorTest, PlainTcpStillWorks) {
    // Create a TcpServerSocket on ephemeral port
    auto* tcp_server = new transport::TcpServerSocket(0);
    ASSERT_TRUE(tcp_server->listen());
    uint16_t port = tcp_server->bound_port();
    ASSERT_GT(port, 0u);

    FixTcpAcceptor acceptor(port);
    acceptor.set_server_socket(tcp_server);

    std::atomic<bool> logon_received{false};
    engine::SessionId logon_sid = 0;

    FixTcpCallbacks cbs;
    cbs.on_logon = [&](engine::SessionId sid, engine::ParticipantId) {
        logon_sid = sid;
        logon_received = true;
    };
    acceptor.set_callbacks(cbs);

    // Start acceptor in background thread
    std::thread server_thread([&]() {
        acceptor.start();
    });

    // Give the accept loop a moment to spin up
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Client: connect via plain TCP and send Logon
    int client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(client_fd, 0);

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    int rc = ::connect(client_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    ASSERT_EQ(rc, 0);

    std::string logon_msg = make_logon("CLIENT1", "SERVER1");
    ssize_t sent = ::send(client_fd, logon_msg.c_str(), logon_msg.size(), 0);
    EXPECT_GT(sent, 0);

    // Read Logon ack
    char buf[4096]{};
    ssize_t n = ::recv(client_fd, buf, sizeof(buf) - 1, 0);
    EXPECT_GT(n, 0);

    std::string ack(buf, static_cast<size_t>(n));
    // Verify it's a Logon ack (msg type A)
    EXPECT_NE(ack.find("35=A"), std::string::npos);
    // Verify tag 34 is present
    EXPECT_NE(ack.find("34="), std::string::npos);

    ::close(client_fd);

    // Wait for logon callback
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(logon_received);
    EXPECT_GT(logon_sid, 0u);

    acceptor.stop();
    server_thread.join();

    // Server socket was injected, so acceptor closed it but doesn't delete it.
    // We gave ownership semantics: acceptor closes but we still need to delete.
    delete tcp_server;
}

// ─── TLS test (OpenSSL required) ─────────────────────────

#ifdef OPENSSL_AVAILABLE

struct TestCert {
    std::string cert_path;
    std::string key_path;
    bool valid = false;
};

static TestCert generate_self_signed_cert() {
    TestCert result;
    result.cert_path = "/tmp/me_fix_test_cert.pem";
    result.key_path = "/tmp/me_fix_test_key.pem";

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

    X509* x509 = X509_new();
    if (!x509) { EVP_PKEY_free(pkey); return result; }

    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), 3600);
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

    FILE* cert_fp = fopen(result.cert_path.c_str(), "w");
    if (!cert_fp) { X509_free(x509); EVP_PKEY_free(pkey); return result; }
    PEM_write_X509(cert_fp, x509);
    fclose(cert_fp);

    FILE* key_fp = fopen(result.key_path.c_str(), "w");
    if (!key_fp) { X509_free(x509); EVP_PKEY_free(pkey); return result; }
    PEM_write_PrivateKey(key_fp, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    fclose(key_fp);

    X509_free(x509);
    EVP_PKEY_free(pkey);
    result.valid = true;
    return result;
}

TEST(FixTlsAcceptorTest, TlsConnectionWorks) {
    auto certs = generate_self_signed_cert();
    ASSERT_TRUE(certs.valid) << "Failed to generate self-signed test certificates";

    transport::TlsConfig tls_cfg;
    tls_cfg.cert_file = certs.cert_path;
    tls_cfg.key_file = certs.key_path;
    tls_cfg.min_tls_version = "TLSv1.2";

    // Create TLS server socket
    auto* tls_server = new transport::TlsServerSocket(0, tls_cfg);
    ASSERT_TRUE(tls_server->init_context());
    ASSERT_TRUE(tls_server->listen());
    uint16_t port = tls_server->bound_port();
    ASSERT_GT(port, 0u);

    FixTcpAcceptor acceptor(port);
    acceptor.set_server_socket(tls_server);

    std::atomic<bool> logon_received{false};

    FixTcpCallbacks cbs;
    cbs.on_logon = [&](engine::SessionId, engine::ParticipantId) {
        logon_received = true;
    };
    acceptor.set_callbacks(cbs);

    std::thread server_thread([&]() {
        acceptor.start();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Client: connect via TLS
    int client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(client_fd, 0);

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    int rc = ::connect(client_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    ASSERT_EQ(rc, 0);

    // TLS handshake
    SSL_CTX* client_ctx = SSL_CTX_new(TLS_client_method());
    ASSERT_NE(client_ctx, nullptr);
    SSL_CTX_set_verify(client_ctx, SSL_VERIFY_NONE, nullptr);

    SSL* ssl = SSL_new(client_ctx);
    ASSERT_NE(ssl, nullptr);
    SSL_set_fd(ssl, client_fd);

    int ssl_rc = SSL_connect(ssl);
    ASSERT_EQ(ssl_rc, 1) << "TLS client handshake failed";

    // Send FIX Logon over TLS
    std::string logon_msg = make_logon("TLS_CLIENT", "TLS_SERVER");
    int tls_sent = SSL_write(ssl, logon_msg.c_str(), static_cast<int>(logon_msg.size()));
    EXPECT_GT(tls_sent, 0);

    // Read Logon ack over TLS
    char buf[4096]{};
    int tls_recv = SSL_read(ssl, buf, sizeof(buf) - 1);
    EXPECT_GT(tls_recv, 0);

    std::string ack(buf, static_cast<size_t>(tls_recv));
    EXPECT_NE(ack.find("35=A"), std::string::npos);
    EXPECT_NE(ack.find("34="), std::string::npos);

    SSL_shutdown(ssl);
    SSL_free(ssl);
    ::close(client_fd);
    SSL_CTX_free(client_ctx);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(logon_received);

    acceptor.stop();
    server_thread.join();

    delete tls_server;

    // Cleanup temp certs
    std::remove(certs.cert_path.c_str());
    std::remove(certs.key_path.c_str());
}

#endif  // OPENSSL_AVAILABLE

}  // namespace
}  // namespace gateway
