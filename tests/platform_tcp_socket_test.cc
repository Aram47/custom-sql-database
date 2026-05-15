#include "platform/tcp_socket.h"

#include <cstdint>
#include <string>
#include <thread>

#include "gtest/gtest.h"

namespace db {
namespace platform {
namespace {

TEST(PlatformTcpSocketTest, LoopbackSendRecv) {
  TcpSocket::startup();

  TcpSocket server = TcpSocket::create_tcp();
  server.set_reuse_address(true);
  server.bind_loopback(0);
  const uint16_t port = server.local_port();
  server.listen(1);

  TcpSocket accepted;
  std::thread accept_thread([&]() { ASSERT_TRUE(server.try_accept(accepted)); });

  TcpSocket client = TcpSocket::create_tcp();
  client.connect("127.0.0.1", port);

  accept_thread.join();

  const std::string payload = "hello-platform";
  ASSERT_GT(client.send(payload.data(), payload.size()), 0);

  char buffer[64]{};
  ssize_t received = accepted.recv(buffer, sizeof(buffer) - 1);
  ASSERT_GT(received, 0);
  buffer[received] = '\0';
  EXPECT_EQ(std::string(buffer), payload);

  client.close();
  accepted.close();
  server.close();
  TcpSocket::cleanup();
}

}  // namespace
}  // namespace platform
}  // namespace db
