#include "network/server.h"
#include "tests/test_util.hh"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "gtest/gtest.h"

namespace db {
namespace {

static std::unique_ptr<Server> g_srv;
static std::unique_ptr<test_util::TempDbDir> g_tmp;
static int g_port = -1;

static std::string tcp_exchange(int port, const std::string &msg) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) return "";
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
    close(sock);
    return "";
  }
  if (connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    close(sock);
    return "";
  }
  send(sock, msg.data(), msg.size(), 0);
  char buf[8192];
  ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
  close(sock);
  if (n <= 0) return "";
  buf[n] = '\0';
  return std::string(buf);
}

class NetworkIntegrationTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    g_tmp = std::make_unique<test_util::TempDbDir>();
    int base = 30000 + static_cast<int>(getpid() % 15000);
    for (int i = 0; i < 50; ++i) {
      int port = base + i;
      try {
        auto srv = std::make_unique<Server>(port, 2, g_tmp->path_string());
        srv->start();
        g_srv = std::move(srv);
        g_port = port;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return;
      } catch (const std::exception &) {
      }
    }
    GTEST_SKIP() << "No free TCP port for integration test";
  }

  static void TearDownTestSuite() {
    if (g_srv) {
      g_srv->stop();
      g_srv.reset();
    }
    g_tmp.reset();
    g_port = -1;
  }
};

TEST_F(NetworkIntegrationTest, PingRepliesPong) {
  ASSERT_GT(g_port, 0);
  std::string r = tcp_exchange(g_port, "PING|\n");
  EXPECT_EQ(r, "PONG\n");
}

TEST_F(NetworkIntegrationTest, QueryReturnsOkPrefix) {
  ASSERT_GT(g_port, 0);
  std::string r =
      tcp_exchange(g_port,
                   "QUERY|CREATE TABLE tcp_t (id INT PRIMARY KEY)\n");
  ASSERT_FALSE(r.empty());
  EXPECT_NE(r.find("OK|"), std::string::npos);
}

TEST_F(NetworkIntegrationTest, InvalidProtocolReturnsError) {
  ASSERT_GT(g_port, 0);
  std::string r = tcp_exchange(g_port, "NOTPIPE\n");
  ASSERT_FALSE(r.empty());
  EXPECT_NE(r.find("ERROR|"), std::string::npos);
}

}  // namespace
}  // namespace db
