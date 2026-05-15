#include "network/server.h"
#include "platform/process.h"
#include "platform/tcp_client.h"
#include "tests/test_util.hh"

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

class NetworkIntegrationTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    g_tmp = std::make_unique<test_util::TempDbDir>();
    int base = 30000 + static_cast<int>(platform::current_process_id() % 15000);
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
  std::string r =
      platform::tcp_exchange("127.0.0.1", static_cast<uint16_t>(g_port), "PING|\n");
  EXPECT_EQ(r, "PONG\n");
}

TEST_F(NetworkIntegrationTest, QueryReturnsOkPrefix) {
  ASSERT_GT(g_port, 0);
  std::string r = platform::tcp_exchange(
      "127.0.0.1", static_cast<uint16_t>(g_port),
      "QUERY|CREATE TABLE tcp_t (id INT PRIMARY KEY)\n");
  ASSERT_FALSE(r.empty());
  EXPECT_NE(r.find("OK|"), std::string::npos);
}

TEST_F(NetworkIntegrationTest, InvalidProtocolReturnsError) {
  ASSERT_GT(g_port, 0);
  std::string r = platform::tcp_exchange(
      "127.0.0.1", static_cast<uint16_t>(g_port), "NOTPIPE\n");
  ASSERT_FALSE(r.empty());
  EXPECT_NE(r.find("ERROR|"), std::string::npos);
}

}  // namespace
}  // namespace db
