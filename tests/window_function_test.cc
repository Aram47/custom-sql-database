#include <string>

#include "core/database.h"
#include "parser/parser.h"
#include "utils/exceptions.h"

#include "gtest/gtest.h"
#include "tests/test_util.hh"

namespace db {
namespace {

void setup_sales_db(Database *db) {
  ASSERT_TRUE(db->execute_query(
                   "CREATE TABLE sales (id INT PRIMARY KEY, dept STRING, "
                   "amount INT)")
                  .success);
  ASSERT_TRUE(db->execute_query(
                      "INSERT INTO sales VALUES (1, 'A', 10), (2, 'A', 20), "
                      "(3, 'A', 20), (4, 'B', 5), (5, 'B', 15)")
                  .success);
}

TEST(WindowFunctionTest, RowNumberPerPartition) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  setup_sales_db(&db);
  auto actual = db.execute_query(
      "SELECT dept, amount, ROW_NUMBER() OVER (PARTITION BY dept ORDER BY "
      "amount, id) AS rn FROM sales ORDER BY dept, rn");
  ASSERT_TRUE(actual.success) << actual.message;
  ASSERT_EQ(actual.rows.size(), 5u);
  EXPECT_EQ(actual.rows[0][0].as_string(), "A");
  EXPECT_EQ(actual.rows[0][2].as_int(), 1);
  EXPECT_EQ(actual.rows[1][2].as_int(), 2);
  EXPECT_EQ(actual.rows[2][2].as_int(), 3);
  EXPECT_EQ(actual.rows[3][0].as_string(), "B");
  EXPECT_EQ(actual.rows[3][2].as_int(), 1);
  EXPECT_EQ(actual.rows[4][2].as_int(), 2);
}

TEST(WindowFunctionTest, RankTiesWithGaps) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  setup_sales_db(&db);
  auto actual = db.execute_query(
      "SELECT amount, RANK() OVER (ORDER BY amount) AS rnk FROM sales "
      "WHERE dept = 'A' ORDER BY amount, id");
  ASSERT_TRUE(actual.success) << actual.message;
  ASSERT_EQ(actual.rows.size(), 3u);
  EXPECT_EQ(actual.rows[0][1].as_int(), 1);
  EXPECT_EQ(actual.rows[1][1].as_int(), 2);
  EXPECT_EQ(actual.rows[2][1].as_int(), 2);
}

TEST(WindowFunctionTest, DenseRankNoGaps) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  setup_sales_db(&db);
  auto actual = db.execute_query(
      "SELECT amount, DENSE_RANK() OVER (ORDER BY amount) AS dr FROM sales "
      "WHERE dept = 'A' ORDER BY amount, id");
  ASSERT_TRUE(actual.success) << actual.message;
  ASSERT_EQ(actual.rows.size(), 3u);
  EXPECT_EQ(actual.rows[0][1].as_int(), 1);
  EXPECT_EQ(actual.rows[1][1].as_int(), 2);
  EXPECT_EQ(actual.rows[2][1].as_int(), 2);
}

TEST(WindowFunctionTest, RunningSumMonotonic) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  setup_sales_db(&db);
  auto actual = db.execute_query(
      "SELECT amount, SUM(amount) OVER (PARTITION BY dept ORDER BY amount, id) "
      "AS running FROM sales WHERE dept = 'A' ORDER BY amount, id");
  ASSERT_TRUE(actual.success) << actual.message;
  ASSERT_EQ(actual.rows.size(), 3u);
  EXPECT_EQ(actual.rows[0][1].as_int(), 10);
  EXPECT_EQ(actual.rows[1][1].as_int(), 30);
  EXPECT_EQ(actual.rows[2][1].as_int(), 50);
}

TEST(WindowFunctionTest, RunningAvg) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  setup_sales_db(&db);
  auto actual = db.execute_query(
      "SELECT amount, AVG(amount) OVER (ORDER BY amount, id) AS avg_amt "
      "FROM sales WHERE dept = 'B' ORDER BY amount, id");
  ASSERT_TRUE(actual.success) << actual.message;
  ASSERT_EQ(actual.rows.size(), 2u);
  EXPECT_DOUBLE_EQ(actual.rows[0][1].as_float(), 5.0);
  EXPECT_DOUBLE_EQ(actual.rows[1][1].as_float(), 10.0);
}

TEST(WindowFunctionTest, MissingOrderByRejected) {
  Parser parser(
      "SELECT ROW_NUMBER() OVER (PARTITION BY dept) FROM sales");
  EXPECT_THROW(parser.parse_statement(), ParseException);
}

TEST(WindowFunctionTest, WindowedSumWithoutGroupBy) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  setup_sales_db(&db);
  auto actual = db.execute_query(
      "SELECT dept, amount, SUM(amount) OVER (PARTITION BY dept ORDER BY id) "
      "AS s FROM sales ORDER BY id");
  ASSERT_TRUE(actual.success) << actual.message;
  ASSERT_EQ(actual.rows.size(), 5u);
}

TEST(WindowFunctionTest, ExplainMentionsWindow) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  setup_sales_db(&db);
  auto actual = db.execute_query(
      "EXPLAIN SELECT dept, ROW_NUMBER() OVER (PARTITION BY dept ORDER BY id) "
      "FROM sales");
  ASSERT_TRUE(actual.success) << actual.message;
  std::string plan;
  for (const auto &row : actual.rows) {
    plan += row[0].as_string();
    plan += "\n";
  }
  EXPECT_NE(plan.find("Window"), std::string::npos);
}

TEST(WindowFunctionTest, UnsupportedWindowFunctionRejected) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  setup_sales_db(&db);
  auto actual = db.execute_query(
      "SELECT LEAD(amount) OVER (ORDER BY id) FROM sales");
  EXPECT_FALSE(actual.success);
}

}  // namespace
}  // namespace db
