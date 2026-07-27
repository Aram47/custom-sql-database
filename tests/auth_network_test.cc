#include "core/auth_manager.h"
#include "network/server.h"
#include "platform/process.h"
#include "platform/tcp_socket.h"
#include "tests/test_util.hh"
#include "utils/sha256.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

namespace db {
namespace {

std::string exchange_keep_alive(platform::TcpSocket &client,
                                const std::string &message) {
  if (client.send(message.data(), message.size()) < 0) {
    return "";
  }
  char buffer[8192];
  const ssize_t received = client.recv(buffer, sizeof(buffer) - 1);
  if (received <= 0) {
    return "";
  }
  buffer[received] = '\0';
  return std::string(buffer, static_cast<size_t>(received));
}

void write_users_file(const std::string &path) {
  const std::vector<uint8_t> admin_salt = AuthManager::generate_salt();
  const std::vector<uint8_t> admin_hash =
      AuthManager::hash_password(admin_salt, "secret");
  const std::vector<uint8_t> reader_salt = AuthManager::generate_salt();
  const std::vector<uint8_t> reader_hash =
      AuthManager::hash_password(reader_salt, "readpass");
  std::ofstream out(path);
  out << "admin:admin:" << to_hex(admin_salt) << ':' << to_hex(admin_hash)
      << '\n';
  out << "viewer:reader:" << to_hex(reader_salt) << ':' << to_hex(reader_hash)
      << '\n';
}

class AuthNetworkTest : public ::testing::Test {
 protected:
  void SetUp() override {
    tmp_ = std::make_unique<test_util::TempDbDir>();
    auth_path_ = tmp_->path_string() + "/users.conf";
    write_users_file(auth_path_);
    ServerAuthConfig auth_config;
    auth_config.auth_file = auth_path_;
    auth_config.require_auth = true;
    int base = 32000 + static_cast<int>(platform::current_process_id() % 15000);
    for (int i = 0; i < 50; ++i) {
      const int port = base + i;
      try {
        auto srv = std::make_unique<Server>(port, 2, tmp_->path_string(),
                                            auth_config);
        srv->start();
        server_ = std::move(srv);
        port_ = port;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return;
      } catch (const std::exception &) {
      }
    }
    GTEST_SKIP() << "No free TCP port for auth integration test";
  }

  void TearDown() override {
    if (server_) {
      server_->stop();
      server_.reset();
    }
    tmp_.reset();
  }

  std::unique_ptr<test_util::TempDbDir> tmp_;
  std::unique_ptr<Server> server_;
  std::string auth_path_;
  int port_{-1};
};

TEST_F(AuthNetworkTest, QueryWithoutAuthRejected) {
  ASSERT_GT(port_, 0);
  platform::TcpSocket::startup();
  platform::TcpSocket client = platform::TcpSocket::create_tcp();
  client.connect("127.0.0.1", static_cast<uint16_t>(port_));
  const std::string response =
      exchange_keep_alive(client, "QUERY|SELECT id FROM t\n");
  EXPECT_NE(response.find("ERROR|authentication required"), std::string::npos);
  client.close();
}

TEST_F(AuthNetworkTest, AuthThenSelectOkReaderInsertDenied) {
  ASSERT_GT(port_, 0);
  platform::TcpSocket::startup();
  platform::TcpSocket admin = platform::TcpSocket::create_tcp();
  admin.connect("127.0.0.1", static_cast<uint16_t>(port_));
  ASSERT_NE(exchange_keep_alive(admin, "AUTH|admin|secret\n").find("OK|"),
            std::string::npos);
  ASSERT_NE(exchange_keep_alive(
                admin, "QUERY|CREATE TABLE auth_net (id INT PRIMARY KEY)\n")
                .find("OK|"),
            std::string::npos);
  ASSERT_NE(
      exchange_keep_alive(admin, "QUERY|INSERT INTO auth_net VALUES (1)\n")
          .find("OK|"),
      std::string::npos);
  admin.close();
  platform::TcpSocket reader = platform::TcpSocket::create_tcp();
  reader.connect("127.0.0.1", static_cast<uint16_t>(port_));
  ASSERT_NE(exchange_keep_alive(reader, "AUTH|viewer|readpass\n").find("OK|"),
            std::string::npos);
  EXPECT_NE(
      exchange_keep_alive(reader, "QUERY|SELECT id FROM auth_net\n").find("OK|"),
      std::string::npos);
  EXPECT_NE(exchange_keep_alive(reader, "QUERY|INSERT INTO auth_net VALUES (2)\n")
                .find("permission denied"),
            std::string::npos);
  reader.close();
}

TEST_F(AuthNetworkTest, BadPasswordKeepsUnauthenticated) {
  ASSERT_GT(port_, 0);
  platform::TcpSocket::startup();
  platform::TcpSocket client = platform::TcpSocket::create_tcp();
  client.connect("127.0.0.1", static_cast<uint16_t>(port_));
  EXPECT_NE(exchange_keep_alive(client, "AUTH|admin|wrong\n")
                .find("ERROR|authentication failed"),
            std::string::npos);
  EXPECT_NE(exchange_keep_alive(client, "QUERY|SELECT id FROM t\n")
                .find("ERROR|authentication required"),
            std::string::npos);
  client.close();
}

}  // namespace
}  // namespace db
