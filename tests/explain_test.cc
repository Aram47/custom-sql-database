#include "core/database.h"
#include "parser/parser.h"

#include <variant>

#include "gtest/gtest.h"
#include "tests/test_util.hh"
#include "utils/exceptions.h"

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

TEST(ExplainTest, SelectReturnsQueryPlan) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE users (id INT PRIMARY KEY, name STRING)")
          .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO users VALUES (1, 'Alice')").success);
  QueryResult result =
      db.execute_query("EXPLAIN SELECT * FROM users WHERE id = 1");
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.column_names.size(), 1u);
  EXPECT_EQ(result.column_names[0], "QUERY PLAN");
  ASSERT_FALSE(result.rows.empty());
  const std::string plan = joinPlanText(result);
  EXPECT_TRUE(plan.find("IndexScan") != std::string::npos ||
              plan.find("SeqScan") != std::string::npos);
  EXPECT_TRUE(plan.find("Rows returned:") != std::string::npos);
}

TEST(ExplainTest, InsertExecutesAndExplains) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE users (id INT PRIMARY KEY, name STRING)")
          .success);
  QueryResult explain =
      db.execute_query("EXPLAIN INSERT INTO users VALUES (1, 'Bob')");
  ASSERT_TRUE(explain.success);
  ASSERT_EQ(explain.column_names.size(), 1u);
  EXPECT_EQ(explain.column_names[0], "QUERY PLAN");
  const std::string plan = joinPlanText(explain);
  EXPECT_TRUE(plan.find("Insert on users") != std::string::npos);
  EXPECT_TRUE(plan.find("Affected rows:") != std::string::npos);
  QueryResult select = db.execute_query("SELECT * FROM users");
  ASSERT_TRUE(select.success);
  ASSERT_EQ(select.rows.size(), 1u);
}

TEST(ExplainTest, NestedExplainFails) {
  Parser parser("EXPLAIN EXPLAIN SELECT 1 FROM t");
  EXPECT_THROW(parser.parse_statement(), ParseException);
}

TEST(ExplainTest, ExplainWithoutStatementFails) {
  Parser parser("EXPLAIN");
  EXPECT_THROW(parser.parse_statement(), ParseException);
}

TEST(ExplainTest, ParseExplainSelect) {
  Parser parser("EXPLAIN SELECT id FROM users");
  ParsedStatement stmt = parser.parse_statement();
  auto *explain = std::get_if<std::shared_ptr<ExplainStatement>>(&stmt);
  ASSERT_NE(explain, nullptr);
  ASSERT_NE(*explain, nullptr);
  auto *select =
      std::get_if<std::shared_ptr<SelectStatement>>(&(*explain)->get_inner());
  ASSERT_NE(select, nullptr);
  EXPECT_EQ((*select)->get_from_table(), "users");
}

}  // namespace
}  // namespace db
