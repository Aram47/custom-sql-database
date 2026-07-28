#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "core/database.h"
#include "executor/hash_join_executor.h"
#include "planner/cost_model.h"
#include "types/value.h"

#include "gtest/gtest.h"
#include "tests/test_util.hh"

namespace db {
namespace {

std::string joinPlanText(const QueryResult &result) {
  std::string text;
  for (const auto &row : result.rows) {
    if (!row.empty()) {
      if (!text.empty()) {
        text += "\n";
      }
      text += row[0].to_string();
    }
  }
  return text;
}

void sortIntPairs(std::vector<std::pair<int, int>> *rows) {
  std::sort(rows->begin(), rows->end());
}

TEST(HashJoinTest, ExecutorMatchesNestedLoopSemantics) {
  std::vector<std::vector<Value>> left = {
      {Value(1), Value(10)},
      {Value(2), Value(20)},
      {Value(3), Value(10)},
  };
  std::vector<std::vector<Value>> right = {
      {Value(10), Value(100)},
      {Value(20), Value(200)},
      {Value(30), Value(300)},
  };
  std::vector<std::vector<Value>> hash_rows =
      HashJoinExecutor::executeInnerEqui(left, right, 1, 0, true);
  std::vector<std::pair<int, int>> got;
  for (const auto &row : hash_rows) {
    ASSERT_EQ(row.size(), 4u);
    got.emplace_back(row[0].as_int(), row[2].as_int());
  }
  sortIntPairs(&got);
  ASSERT_EQ(got.size(), 3u);
  EXPECT_EQ(got[0], (std::pair<int, int>{1, 10}));
  EXPECT_EQ(got[1], (std::pair<int, int>{2, 20}));
  EXPECT_EQ(got[2], (std::pair<int, int>{3, 10}));
}

TEST(HashJoinTest, NullJoinKeysDoNotMatch) {
  std::vector<std::vector<Value>> left = {
      {Value(1), Value()},
      {Value(2), Value(20)},
  };
  std::vector<std::vector<Value>> right = {
      {Value(), Value(100)},
      {Value(20), Value(200)},
  };
  std::vector<std::vector<Value>> hash_rows =
      HashJoinExecutor::executeInnerEqui(left, right, 1, 0, false);
  ASSERT_EQ(hash_rows.size(), 1u);
  EXPECT_EQ(hash_rows[0][0].as_int(), 2);
  EXPECT_EQ(hash_rows[0][2].as_int(), 20);
}

TEST(HashJoinTest, ExplainPrefersHashJoinWithoutIndex) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string(), 0);
  ASSERT_TRUE(db.execute_query("CREATE TABLE left_t (id INT, k INT)").success);
  ASSERT_TRUE(db.execute_query("CREATE TABLE right_t (id INT, k INT)").success);
  for (int i = 0; i < 40; ++i) {
    ASSERT_TRUE(db.execute_query("INSERT INTO left_t VALUES (" +
                                 std::to_string(i) + ", " +
                                 std::to_string(i % 10) + ")")
                    .success);
    ASSERT_TRUE(db.execute_query("INSERT INTO right_t VALUES (" +
                                 std::to_string(i) + ", " +
                                 std::to_string(i % 10) + ")")
                    .success);
  }
  ASSERT_TRUE(db.execute_query("VACUUM").success);
  QueryResult explain = db.execute_query(
      "EXPLAIN SELECT left_t.id, right_t.id FROM left_t INNER JOIN right_t ON "
      "left_t.k = right_t.k");
  ASSERT_TRUE(explain.success) << explain.message;
  const std::string plan = joinPlanText(explain);
  EXPECT_NE(plan.find("HashJoin"), std::string::npos) << plan;
  QueryResult result = db.execute_query(
      "SELECT left_t.id, right_t.id FROM left_t INNER JOIN right_t ON "
      "left_t.k = right_t.k");
  ASSERT_TRUE(result.success) << result.message;
  EXPECT_EQ(result.rows.size(), 160u);
}

TEST(HashJoinTest, ExplainMayUseIndexNestedLoopOnPk) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string(), 0);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE orders (id INT PRIMARY KEY, cust_id INT)")
          .success);
  ASSERT_TRUE(
      db.execute_query(
           "CREATE TABLE customers (id INT PRIMARY KEY, name STRING)")
          .success);
  for (int i = 0; i < 40; ++i) {
    ASSERT_TRUE(db.execute_query("INSERT INTO customers VALUES (" +
                                 std::to_string(i) + ", 'n')")
                    .success);
    ASSERT_TRUE(db.execute_query("INSERT INTO orders VALUES (" +
                                 std::to_string(i) + ", " +
                                 std::to_string(i) + ")")
                    .success);
  }
  ASSERT_TRUE(db.execute_query("VACUUM").success);
  QueryResult explain = db.execute_query(
      "EXPLAIN SELECT orders.id, customers.name FROM orders INNER JOIN "
      "customers ON orders.cust_id = customers.id");
  ASSERT_TRUE(explain.success) << explain.message;
  const std::string plan = joinPlanText(explain);
  EXPECT_TRUE(plan.find("IndexNestedLoop") != std::string::npos ||
              plan.find("HashJoin") != std::string::npos ||
              plan.find("NestedLoop") != std::string::npos)
      << plan;
}

TEST(HashJoinTest, SqlNullJoinKeyProducesNoMatch) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string(), 0);
  ASSERT_TRUE(db.execute_query("CREATE TABLE a (id INT, k INT)").success);
  ASSERT_TRUE(db.execute_query("CREATE TABLE b (id INT, k INT)").success);
  for (int i = 0; i < 32; ++i) {
    const std::string k =
        (i == 0) ? "NULL" : std::to_string(i);
    ASSERT_TRUE(db.execute_query("INSERT INTO a VALUES (" + std::to_string(i) +
                                 ", " + k + ")")
                    .success);
    ASSERT_TRUE(db.execute_query("INSERT INTO b VALUES (" + std::to_string(i) +
                                 ", " + k + ")")
                    .success);
  }
  ASSERT_TRUE(db.execute_query("VACUUM").success);
  QueryResult result = db.execute_query(
      "SELECT a.id FROM a INNER JOIN b ON a.k = b.k");
  ASSERT_TRUE(result.success) << result.message;
  EXPECT_EQ(result.rows.size(), 31u);
}

}  // namespace
}  // namespace db
