#include <string>
#include <vector>

#include "core/database.h"

#include "gtest/gtest.h"
#include "tests/test_util.hh"

namespace db {
namespace {

void setup_sales_db(Database *db) {
  ASSERT_TRUE(db->execute_query(
                   "CREATE TABLE sales (id INT PRIMARY KEY, dept STRING, "
                   "amount INT)")
                  .success);
  ASSERT_TRUE(
      db->execute_query(
           "INSERT INTO sales VALUES (1, 'A', 10), (2, 'A', 20), (3, 'B', 5)")
          .success);
}

TEST(SelectRelationalOpsTest, OrderByDesc) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  setup_sales_db(&db);

  auto r = db.execute_query(
      "SELECT id, amount FROM sales ORDER BY amount DESC");
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 3u);
  EXPECT_EQ(r.rows[0][0].as_int(), 2);
  EXPECT_EQ(r.rows[2][0].as_int(), 3);
}

TEST(SelectRelationalOpsTest, OrderByMultiKey) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query("CREATE TABLE t (a INT, b INT)").success);
  ASSERT_TRUE(
      db.execute_query("INSERT INTO t VALUES (1, 2), (1, 1), (2, 1)").success);

  auto r = db.execute_query("SELECT a, b FROM t ORDER BY a, b DESC");
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 3u);
  EXPECT_EQ(r.rows[0][1].as_int(), 2);
  EXPECT_EQ(r.rows[1][1].as_int(), 1);
}

TEST(SelectRelationalOpsTest, LimitAndOffset) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  setup_sales_db(&db);

  auto limited = db.execute_query(
      "SELECT id FROM sales ORDER BY id LIMIT 2");
  ASSERT_TRUE(limited.success) << limited.message;
  ASSERT_EQ(limited.rows.size(), 2u);

  auto offset = db.execute_query(
      "SELECT id FROM sales ORDER BY id OFFSET 1");
  ASSERT_TRUE(offset.success) << offset.message;
  ASSERT_EQ(offset.rows.size(), 2u);
  EXPECT_EQ(offset.rows[0][0].as_int(), 2);
}

TEST(SelectRelationalOpsTest, LimitZeroReturnsEmpty) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  setup_sales_db(&db);

  auto zero = db.execute_query("SELECT id FROM sales ORDER BY id LIMIT 0");
  ASSERT_TRUE(zero.success) << zero.message;
  EXPECT_EQ(zero.rows.size(), 0u);

  auto unlimited = db.execute_query("SELECT id FROM sales ORDER BY id");
  ASSERT_TRUE(unlimited.success) << unlimited.message;
  EXPECT_EQ(unlimited.rows.size(), 3u);
}

TEST(SelectRelationalOpsTest, GroupByCountStar) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  setup_sales_db(&db);

  auto r = db.execute_query(
      "SELECT dept, COUNT(*) FROM sales GROUP BY dept ORDER BY dept");
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 2u);
  EXPECT_EQ(r.rows[0][0].as_string(), "A");
  EXPECT_EQ(r.rows[0][1].as_int(), 2);
  EXPECT_EQ(r.rows[1][0].as_string(), "B");
  EXPECT_EQ(r.rows[1][1].as_int(), 1);
}

TEST(SelectRelationalOpsTest, HavingFiltersGroups) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  setup_sales_db(&db);

  auto r = db.execute_query(
      "SELECT dept FROM sales GROUP BY dept HAVING COUNT(*) > 1");
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 1u);
  EXPECT_EQ(r.rows[0][0].as_string(), "A");
}

TEST(SelectRelationalOpsTest, SumAvgMinMax) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  setup_sales_db(&db);

  auto r = db.execute_query(
      "SELECT dept, SUM(amount), AVG(amount), MIN(amount), MAX(amount) "
      "FROM sales GROUP BY dept ORDER BY dept");
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 2u);
  EXPECT_EQ(r.rows[0][1].as_int(), 30);
  EXPECT_EQ(r.rows[0][2].as_int(), 15);
  EXPECT_EQ(r.rows[0][3].as_int(), 10);
  EXPECT_EQ(r.rows[0][4].as_int(), 20);
}

TEST(SelectRelationalOpsTest, CountWithoutGroupBy) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  setup_sales_db(&db);

  auto star = db.execute_query("SELECT COUNT(*) FROM sales");
  ASSERT_TRUE(star.success) << star.message;
  ASSERT_EQ(star.rows.size(), 1u);
  EXPECT_EQ(star.rows[0][0].as_int(), 3);

  auto col = db.execute_query("SELECT COUNT(amount) FROM sales");
  ASSERT_TRUE(col.success) << col.message;
  EXPECT_EQ(col.rows[0][0].as_int(), 3);
}

TEST(SelectRelationalOpsTest, CountEmptyTable) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE empty_t (id INT PRIMARY KEY)").success);

  auto r = db.execute_query("SELECT COUNT(*) FROM empty_t");
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 1u);
  EXPECT_EQ(r.rows[0][0].as_int(), 0);
}

TEST(SelectRelationalOpsTest, JoinGroupByOrderBy) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE orders (id INT PRIMARY KEY, cid INT)")
          .success);
  ASSERT_TRUE(
      db.execute_query(
           "CREATE TABLE customers (id INT PRIMARY KEY, region STRING)")
          .success);
  ASSERT_TRUE(
      db.execute_query("INSERT INTO orders VALUES (1, 10), (2, 10), (3, 20)")
          .success);
  ASSERT_TRUE(
      db.execute_query(
           "INSERT INTO customers VALUES (10, 'EU'), (20, 'US')")
          .success);

  auto r = db.execute_query(
      "SELECT customers.region, COUNT(*) FROM orders INNER JOIN customers "
      "ON orders.cid = customers.id GROUP BY customers.region "
      "ORDER BY customers.region");
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 2u);
  EXPECT_EQ(r.rows[0][0].as_string(), "EU");
  EXPECT_EQ(r.rows[0][1].as_int(), 2);
  EXPECT_EQ(r.rows[1][0].as_string(), "US");
  EXPECT_EQ(r.rows[1][1].as_int(), 1);
}

TEST(SelectRelationalOpsTest, GroupByValidationErrors) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  setup_sales_db(&db);

  auto bad_select = db.execute_query(
      "SELECT id, COUNT(*) FROM sales GROUP BY dept");
  EXPECT_FALSE(bad_select.success);

  auto bad_having = db.execute_query(
      "SELECT dept FROM sales HAVING COUNT(*) > 0");
  EXPECT_FALSE(bad_having.success);

  auto bad_star = db.execute_query(
      "SELECT * FROM sales GROUP BY dept");
  EXPECT_FALSE(bad_star.success);
}

TEST(SelectRelationalOpsTest, DistinctWithOrderByLimit) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query("CREATE TABLE d (v INT)").success);
  ASSERT_TRUE(
      db.execute_query("INSERT INTO d VALUES (1), (2), (2), (3)").success);

  auto r = db.execute_query(
      "SELECT DISTINCT v FROM d ORDER BY v DESC LIMIT 2");
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 2u);
  EXPECT_EQ(r.rows[0][0].as_int(), 3);
  EXPECT_EQ(r.rows[1][0].as_int(), 2);
}

}  // namespace
}  // namespace db
