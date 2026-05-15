#include "core/database.h"

#include <set>

#include "gtest/gtest.h"
#include "tests/test_util.hh"

namespace db {
namespace {

TEST(DatabaseExtendedTest, SelectWithoutFromFails) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  auto r = db.execute_query("SELECT 1");
  ASSERT_FALSE(r.success);
  EXPECT_NE(r.message.find("FROM clause"), std::string::npos);
}

TEST(DatabaseExtendedTest, QueryUnknownTableFails) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query("CREATE TABLE a (id INT PRIMARY KEY)").success);
  auto r = db.execute_query("SELECT * FROM missing");
  ASSERT_FALSE(r.success);
}

TEST(DatabaseExtendedTest, DuplicateCreateTableFails) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
  auto r = db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)");
  ASSERT_FALSE(r.success);
}

TEST(DatabaseExtendedTest, DuplicatePrimaryKeyInsertFails) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1)").success);
  auto r = db.execute_query("INSERT INTO t VALUES (1)");
  ASSERT_FALSE(r.success);
}

TEST(DatabaseExtendedTest, UniqueConstraintInsertFails) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query(
                   "CREATE TABLE t (id INT PRIMARY KEY, code STRING UNIQUE)")
                  .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1, 'x')").success);
  auto r = db.execute_query("INSERT INTO t VALUES (2, 'x')");
  ASSERT_FALSE(r.success);
}

TEST(DatabaseExtendedTest, NotNullViolationOnInsertFails) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query(
                   "CREATE TABLE t (id INT PRIMARY KEY, name STRING NOT NULL)")
                  .success);
  auto r = db.execute_query("INSERT INTO t VALUES (1, NULL)");
  ASSERT_FALSE(r.success);
}

TEST(DatabaseExtendedTest, DeleteWithoutWhereRemovesAllRows) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (2)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (3)").success);
  auto del = db.execute_query("DELETE FROM t");
  ASSERT_TRUE(del.success) << del.message;
  EXPECT_EQ(del.affected_rows, 3);
  auto sel = db.execute_query("SELECT * FROM t");
  ASSERT_TRUE(sel.success);
  EXPECT_EQ(sel.rows.size(), 0u);
}

TEST(DatabaseExtendedTest, SelectDistinct) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query(
                   "CREATE TABLE t (id INT PRIMARY KEY, tag STRING)")
                  .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1, 'a')").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (2, 'a')").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (3, 'b')").success);
  auto r = db.execute_query("SELECT DISTINCT tag FROM t");
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 2u);
}

TEST(DatabaseExtendedTest, WhereCompoundAndNotOrComparisons) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
  for (int i = 1; i <= 8; ++i) {
    ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (" + std::to_string(i) +
                                 ")")
                    .success);
  }
  auto r = db.execute_query(
      "SELECT id FROM t WHERE NOT id = 1 AND (id < 4 OR id >= 7)");
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 4u);
  std::set<int64_t> expected({2, 3, 7, 8});
  std::set<int64_t> got;
  for (const auto &row : r.rows) {
    got.insert(row[0].as_int());
  }
  EXPECT_EQ(got, expected);
}

TEST(DatabaseExtendedTest, SelectArithmeticAndMod) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (17)").success);
  auto r = db.execute_query("SELECT id % 5, id + 3 FROM t");
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 1u);
  EXPECT_EQ(r.rows[0][0].as_int(), 2);
  EXPECT_EQ(r.rows[0][1].as_int(), 20);
}

TEST(DatabaseExtendedTest, ScalarFunctionsInSelect) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query(
                   "CREATE TABLE t (id INT PRIMARY KEY, name STRING)")
                  .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1, 'Hello')").success);
  auto upper = db.execute_query("SELECT UPPER(name) FROM t");
  ASSERT_TRUE(upper.success) << upper.message;
  EXPECT_EQ(upper.rows[0][0].as_string(), "HELLO");

  auto lower = db.execute_query("SELECT LOWER(name) FROM t");
  ASSERT_TRUE(lower.success) << lower.message;
  EXPECT_EQ(lower.rows[0][0].as_string(), "hello");

  auto len = db.execute_query("SELECT LENGTH(name) FROM t");
  ASSERT_TRUE(len.success) << len.message;
  EXPECT_EQ(len.rows[0][0].as_int(), 5);

  auto cnt = db.execute_query("SELECT COUNT(id) FROM t");
  ASSERT_TRUE(cnt.success) << cnt.message;
  ASSERT_EQ(cnt.rows.size(), 1u);
  EXPECT_EQ(cnt.rows[0][0].as_int(), 1);  // simplified COUNT semantics
}

TEST(DatabaseExtendedTest, FloatBooleanDateUuidColumns) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query(
                   "CREATE TABLE evt (id INT PRIMARY KEY, price FLOAT, "
                   "active BOOLEAN, d DATE, u UUID)")
                  .success);
  ASSERT_TRUE(
      db.execute_query(
           "INSERT INTO evt VALUES (1, 2.5, 1, '2020-01-15', "
           "'550e8400-e29b-41d4-a716-446655440000')")
          .success);
  auto r = db.execute_query("SELECT price, active, d, u FROM evt WHERE id = 1");
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 1u);
  EXPECT_NEAR(r.rows[0][0].as_float(), 2.5, 1e-9);
  EXPECT_TRUE(r.rows[0][1].as_bool());
  EXPECT_EQ(r.rows[0][2].as_string(), "2020-01-15");
  EXPECT_EQ(r.rows[0][3].as_string(), "550e8400-e29b-41d4-a716-446655440000");
}

TEST(DatabaseExtendedTest, UpdateSetArithmeticNotSupportedFailsConstraint) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query(
                   "CREATE TABLE ut (id INT PRIMARY KEY, x INT NOT NULL)")
                  .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO ut VALUES (1, 10)").success);
  // UpdateExecutor only evaluates literals/column refs — binop yields empty Value (NULL)
  auto r = db.execute_query("UPDATE ut SET x = id + 5 WHERE id = 1");
  ASSERT_FALSE(r.success);
  EXPECT_NE(r.message.find("Update failed"), std::string::npos);
}

}  // namespace
}  // namespace db
