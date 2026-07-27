#include "core/database.h"

#include "gtest/gtest.h"
#include "tests/test_util.hh"

namespace db {
namespace {

TEST(SubqueryTest, WhereInSubquery) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE a (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE b (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO a VALUES (1)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO a VALUES (2)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO a VALUES (3)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO b VALUES (2)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO b VALUES (3)").success);
  auto r = db.execute_query("SELECT id FROM a WHERE id IN (SELECT id FROM b)");
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 2u);
}

TEST(SubqueryTest, WhereNotInSubquery) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE a (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE b (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO a VALUES (1)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO a VALUES (2)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO b VALUES (2)").success);
  auto r =
      db.execute_query("SELECT id FROM a WHERE id NOT IN (SELECT id FROM b)");
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 1u);
  EXPECT_EQ(r.rows[0][0].as_int(), 1);
}

TEST(SubqueryTest, LiteralInList) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (2)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (3)").success);
  auto r = db.execute_query("SELECT id FROM t WHERE id IN (1, 3)");
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 2u);
}

TEST(SubqueryTest, ExistsAndNotExists) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE a (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE b (aid INT)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO a VALUES (1)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO a VALUES (2)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO b VALUES (1)").success);
  auto exists_r = db.execute_query(
      "SELECT id FROM a WHERE EXISTS (SELECT 1 FROM b WHERE b.aid = a.id)");
  ASSERT_TRUE(exists_r.success) << exists_r.message;
  ASSERT_EQ(exists_r.rows.size(), 1u);
  EXPECT_EQ(exists_r.rows[0][0].as_int(), 1);
  auto not_exists_r = db.execute_query(
      "SELECT id FROM a WHERE NOT EXISTS (SELECT 1 FROM b WHERE b.aid = a.id)");
  ASSERT_TRUE(not_exists_r.success) << not_exists_r.message;
  ASSERT_EQ(not_exists_r.rows.size(), 1u);
  EXPECT_EQ(not_exists_r.rows[0][0].as_int(), 2);
}

TEST(SubqueryTest, ScalarSubquery) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY, n INT)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1, 10)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (2, 20)").success);
  auto r = db.execute_query(
      "SELECT id FROM t WHERE n = (SELECT n FROM t WHERE id = 2)");
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 1u);
  EXPECT_EQ(r.rows[0][0].as_int(), 2);
}

TEST(SubqueryTest, CorrelatedScalarSubquery) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE outer_t (id INT PRIMARY KEY, n INT)")
                  .success);
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE inner_t (id INT PRIMARY KEY, oid INT, v INT)")
                  .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO outer_t VALUES (1, 100)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO outer_t VALUES (2, 200)").success);
  ASSERT_TRUE(
      db.execute_query("INSERT INTO inner_t VALUES (10, 1, 100)").success);
  ASSERT_TRUE(
      db.execute_query("INSERT INTO inner_t VALUES (20, 2, 999)").success);
  auto r = db.execute_query(
      "SELECT id FROM outer_t WHERE n = (SELECT v FROM inner_t WHERE "
      "inner_t.oid = outer_t.id)");
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 1u);
  EXPECT_EQ(r.rows[0][0].as_int(), 1);
}

}  // namespace
}  // namespace db
