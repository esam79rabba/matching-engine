/// Transport abstraction tests — TcpSocket and TcpServerSocket.
#include "transport/tcp_socket.h"
#include "transport/socket.h"
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <cstring>

namespace transport {
namespace {

TEST(TcpServerSocketTest, BindAndListenOnRandomPort) {
    TcpServerSocket server(0);  // port 0 = OS picks
    ASSERT_TRUE(server.listen());
    EXPECT_TRUE(server.is_listening());
    EXPECT_GT(server.bound_port(), 0u);
    server.close();
    EXPECT_FALSE(server.is_listening());
}

TEST(TcpServerSocketTest, AcceptAndCommunicate) {
    TcpServerSocket server(0);
    ASSERT_TRUE(server.listen());
    uint16_t port = server.bound_port();

    // Client thread
    std::thread client_thread([port] {
        // Small delay to ensure server is in accept()
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GE(fd, 0);

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);
        int rc = ::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        ASSERT_EQ(rc, 0);

        // Send a message
        const char* msg = "HELLO";
        ::send(fd, msg, 5, 0);

        // Receive echo
        char buf[16]{};
        ::recv(fd, buf, sizeof(buf), 0);
        EXPECT_EQ(std::string(buf, 5), "WORLD");

        ::close(fd);
    });

    // Accept one client
    ISocket* client = server.accept();
    ASSERT_NE(client, nullptr);
    EXPECT_TRUE(client->is_open());
    EXPECT_GE(client->fd(), 0);

    // Read what client sent
    char buf[16]{};
    int n = client->recv(buf, sizeof(buf));
    ASSERT_GT(n, 0);
    EXPECT_EQ(std::string(buf, static_cast<size_t>(n)), "HELLO");

    // Send response
    const char* reply = "WORLD";
    int sent = client->send(reply, 5);
    EXPECT_EQ(sent, 5);

    client->close();
    EXPECT_FALSE(client->is_open());
    delete client;

    client_thread.join();
    server.close();
}

TEST(TcpSocketTest, MoveSemantics) {
    // Create a socket pair for testing move
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    TcpSocket a(fds[0]);
    EXPECT_TRUE(a.is_open());
    EXPECT_EQ(a.fd(), fds[0]);

    // Move construct
    TcpSocket b(std::move(a));
    EXPECT_FALSE(a.is_open());
    EXPECT_TRUE(b.is_open());
    EXPECT_EQ(b.fd(), fds[0]);

    // Move assign
    TcpSocket c(-1);
    c = std::move(b);
    EXPECT_FALSE(b.is_open());
    EXPECT_TRUE(c.is_open());
    EXPECT_EQ(c.fd(), fds[0]);

    c.close();
    ::close(fds[1]);
}

TEST(TcpSocketTest, SendRecvThroughAbstraction) {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    // Use through ISocket interface
    ISocket* sender = new TcpSocket(fds[0]);
    ISocket* receiver = new TcpSocket(fds[1]);

    const char* msg = "TRANSPORT_OK";
    int sent = sender->send(msg, 12);
    EXPECT_EQ(sent, 12);

    char buf[32]{};
    int received = receiver->recv(buf, sizeof(buf));
    EXPECT_EQ(received, 12);
    EXPECT_EQ(std::string(buf, 12), "TRANSPORT_OK");

    delete sender;
    delete receiver;
}

TEST(TlsConfigTest, DefaultValues) {
    TlsConfig cfg;
    EXPECT_TRUE(cfg.cert_file.empty());
    EXPECT_TRUE(cfg.key_file.empty());
    EXPECT_TRUE(cfg.ca_file.empty());
    EXPECT_FALSE(cfg.require_client_cert);
    EXPECT_EQ(cfg.min_tls_version, "TLSv1.2");
    EXPECT_TRUE(cfg.cipher_list.empty());
}

TEST(TransportPolymorphismTest, ServerAcceptsViaInterface) {
    // Verify IServerSocket interface works polymorphically
    TcpServerSocket tcp_server(0);
    IServerSocket* server = &tcp_server;
    ASSERT_TRUE(server->listen());
    EXPECT_TRUE(server->is_listening());
    // port() returns configured port (0); bound_port() returns actual
    EXPECT_GT(tcp_server.bound_port(), 0u);
    server->close();
}

}  // namespace
}  // namespace transport
