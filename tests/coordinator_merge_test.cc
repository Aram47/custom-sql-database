#include "network/coordinator_query_router.h"
#include "network/result_merger.h"
#include "network/rpc_client.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/database.h"
#include "core/session_context.h"
#include "core/shard_map.h"
#include "core/shard_router.h"
#include "gtest/gtest.h"
#include "tests/test_util.hh"
#include "types/value.h"

namespace db {
namespace {

class MockRpcClient : public IRpcClient {
 public:
  using QueryHandler = std::function<QueryResult(const ShardEndpoint &,
                                                 const std::string &)>;

  explicit MockRpcClient(QueryHandler handler) : handler_(std::move(handler)) {}

  void setHealthy(int shardId, bool isHealthy) {
    health_[shardId] = isHealthy;
  }

  QueryResult executeQuery(const ShardEndpoint &endpoint,
                           const std::string &rpcSecret,
                           const std::string &sql) override {
    (void)rpcSecret;
    queries_.push_back({endpoint.shardId, sql});
    if (handler_) {
      return handler_(endpoint, sql);
    }
    return QueryResult::success_result("OK");
  }

  bool ping(const ShardEndpoint &endpoint) override {
    const auto it = health_.find(endpoint.shardId);
    if (it == health_.end()) {
      return true;
    }
    return it->second;
  }

  const std::vector<std::pair<int, std::string>> &queries() const {
    return queries_;
  }

 private:
  QueryHandler handler_;
  std::map<int, bool> health_;
  std::vector<std::pair<int, std::string>> queries_;
};

ShardMap buildTestMap(std::string *error) {
  auto map = ShardMap::build(
      {{0, "127.0.0.1", 9001}, {1, "127.0.0.1", 9002}},
      {{"sales_2024", 0}, {"sales_2025", 1}}, error);
  EXPECT_TRUE(map.has_value()) << (error ? *error : "");
  return *map;
}

void seedPartitionedMeta(Database &db) {
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE sales (id INT, y INT) PARTITION BY RANGE (y)")
                  .success);
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE sales_2024 PARTITION OF sales "
                      "FOR VALUES FROM (2024) TO (2025)")
                  .success);
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE sales_2025 PARTITION OF sales "
                      "FOR VALUES FROM (2025) TO (2026)")
                  .success);
}

TEST(CoordinatorMergeTest, MergeColumnAlignedRows) {
  QueryResult left = QueryResult::success_result("OK");
  left.column_names = {"id", "y"};
  left.rows.push_back({Value(1), Value(2024)});
  QueryResult right = QueryResult::success_result("OK");
  right.column_names = {"id", "y"};
  right.rows.push_back({Value(2), Value(2025)});
  QueryResult merged = ResultMerger::merge({left, right});
  ASSERT_TRUE(merged.success) << merged.message;
  ASSERT_EQ(merged.rows.size(), 2u);
  EXPECT_EQ(merged.rows[0][0].as_int(), 1);
  EXPECT_EQ(merged.rows[1][0].as_int(), 2);
}

TEST(CoordinatorMergeTest, MergeRejectsColumnMismatch) {
  QueryResult left = QueryResult::success_result("OK");
  left.column_names = {"id"};
  QueryResult right = QueryResult::success_result("OK");
  right.column_names = {"y"};
  QueryResult merged = ResultMerger::merge({left, right});
  EXPECT_FALSE(merged.success);
  EXPECT_NE(merged.message.find("column mismatch"), std::string::npos);
}

TEST(CoordinatorMergeTest, InsertProxiesToCorrectShard) {
  test_util::TempDbDir dir;
  Database meta(dir.path_string());
  seedPartitionedMeta(meta);
  std::string error;
  ShardMap map = buildTestMap(&error);
  auto mock = std::make_unique<MockRpcClient>(
      [](const ShardEndpoint &endpoint, const std::string &sql) {
        (void)sql;
        QueryResult result = QueryResult::success_result("OK");
        result.message = "shard-" + std::to_string(endpoint.shardId);
        return result;
      });
  MockRpcClient *mockPtr = mock.get();
  CoordinatorQueryRouter router(&meta, ShardRouter(std::move(map)),
                                std::move(mock), "secret");
  SessionContext session;
  QueryResult result =
      router.executeQuery("INSERT INTO sales VALUES (1, 2024)", &session);
  ASSERT_TRUE(result.success) << result.message;
  ASSERT_EQ(mockPtr->queries().size(), 1u);
  EXPECT_EQ(mockPtr->queries()[0].first, 0);
}

TEST(CoordinatorMergeTest, SelectEqualityHitsSingleShard) {
  test_util::TempDbDir dir;
  Database meta(dir.path_string());
  seedPartitionedMeta(meta);
  std::string error;
  ShardMap map = buildTestMap(&error);
  auto mock = std::make_unique<MockRpcClient>(
      [](const ShardEndpoint &, const std::string &) {
        QueryResult result = QueryResult::success_result("OK");
        result.column_names = {"id", "y"};
        result.rows.push_back({Value(1), Value(2024)});
        return result;
      });
  MockRpcClient *mockPtr = mock.get();
  CoordinatorQueryRouter router(&meta, ShardRouter(std::move(map)),
                                std::move(mock), "secret");
  SessionContext session;
  QueryResult result = router.executeQuery(
      "SELECT * FROM sales WHERE y = 2024", &session);
  ASSERT_TRUE(result.success) << result.message;
  ASSERT_EQ(mockPtr->queries().size(), 1u);
  EXPECT_EQ(mockPtr->queries()[0].first, 0);
}

TEST(CoordinatorMergeTest, SelectWithoutKeyScatterGathers) {
  test_util::TempDbDir dir;
  Database meta(dir.path_string());
  seedPartitionedMeta(meta);
  std::string error;
  ShardMap map = buildTestMap(&error);
  auto mock = std::make_unique<MockRpcClient>(
      [](const ShardEndpoint &endpoint, const std::string &) {
        QueryResult result = QueryResult::success_result("OK");
        result.column_names = {"id", "y"};
        result.rows.push_back(
            {Value(endpoint.shardId), Value(2024 + endpoint.shardId)});
        return result;
      });
  MockRpcClient *mockPtr = mock.get();
  CoordinatorQueryRouter router(&meta, ShardRouter(std::move(map)),
                                std::move(mock), "secret");
  SessionContext session;
  QueryResult result =
      router.executeQuery("SELECT * FROM sales", &session);
  ASSERT_TRUE(result.success) << result.message;
  EXPECT_EQ(mockPtr->queries().size(), 2u);
  EXPECT_EQ(result.rows.size(), 2u);
}

TEST(CoordinatorMergeTest, MultiShardTransactionRejected) {
  test_util::TempDbDir dir;
  Database meta(dir.path_string());
  seedPartitionedMeta(meta);
  std::string error;
  ShardMap map = buildTestMap(&error);
  auto mock = std::make_unique<MockRpcClient>(
      [](const ShardEndpoint &, const std::string &sql) {
        if (sql == "BEGIN" || sql.rfind("INSERT", 0) == 0) {
          return QueryResult::success_result("OK");
        }
        return QueryResult::success_result("OK");
      });
  CoordinatorQueryRouter router(&meta, ShardRouter(std::move(map)),
                                std::move(mock), "secret");
  SessionContext session;
  ASSERT_TRUE(router.executeQuery("BEGIN", &session).success);
  ASSERT_TRUE(
      router.executeQuery("INSERT INTO sales VALUES (1, 2024)", &session)
          .success);
  QueryResult second =
      router.executeQuery("INSERT INTO sales VALUES (2, 2025)", &session);
  EXPECT_FALSE(second.success);
  EXPECT_NE(second.message.find("multi-shard"), std::string::npos);
}

TEST(CoordinatorMergeTest, CrossShardJoinRejected) {
  test_util::TempDbDir dir;
  Database meta(dir.path_string());
  seedPartitionedMeta(meta);
  std::string error;
  ShardMap map = buildTestMap(&error);
  auto mock = std::make_unique<MockRpcClient>(
      [](const ShardEndpoint &, const std::string &) {
        return QueryResult::success_result("OK");
      });
  CoordinatorQueryRouter router(&meta, ShardRouter(std::move(map)),
                                std::move(mock), "secret");
  SessionContext session;
  QueryResult result = router.executeQuery(
      "SELECT * FROM sales JOIN sales_2025 ON sales.id = sales_2025.id",
      &session);
  EXPECT_FALSE(result.success);
  EXPECT_NE(result.message.find("cross-shard JOIN"), std::string::npos);
}

TEST(CoordinatorMergeTest, WorkerDownReturnsError) {
  test_util::TempDbDir dir;
  Database meta(dir.path_string());
  seedPartitionedMeta(meta);
  std::string error;
  ShardMap map = buildTestMap(&error);
  auto mock = std::make_unique<MockRpcClient>(
      [](const ShardEndpoint &, const std::string &) {
        return QueryResult::success_result("OK");
      });
  mock->setHealthy(0, false);
  CoordinatorQueryRouter router(&meta, ShardRouter(std::move(map)),
                                std::move(mock), "secret");
  SessionContext session;
  QueryResult result =
      router.executeQuery("INSERT INTO sales VALUES (1, 2024)", &session);
  EXPECT_FALSE(result.success);
  EXPECT_NE(result.message.find("is down"), std::string::npos);
}

}  // namespace
}  // namespace db
