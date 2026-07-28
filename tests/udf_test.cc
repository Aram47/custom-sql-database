#include "core/database.h"
#include "parser/parser.h"

#include "gtest/gtest.h"
#include "tests/test_util.hh"
#include "utils/exceptions.h"

namespace db {
namespace {

TEST(UdfTest, CreateDollarBodyAndSelect) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string(), 0);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (21)").success);
  QueryResult create = db.execute_query(
      "CREATE FUNCTION double_it(x INT) RETURNS INT AS $$\n"
      "  RETURN x * 2;\n"
      "$$");
  ASSERT_TRUE(create.success) << create.message;
  QueryResult result = db.execute_query("SELECT double_it(id) FROM t");
  ASSERT_TRUE(result.success) << result.message;
  ASSERT_EQ(result.rows.size(), 1u);
  EXPECT_EQ(result.rows[0][0].as_int(), 42);
}

TEST(UdfTest, ShortAsForm) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string(), 0);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (7)").success);
  ASSERT_TRUE(db.execute_query(
                     "CREATE FUNCTION triple_it(x INT) RETURNS INT AS (x * 3)")
                  .success);
  QueryResult result = db.execute_query("SELECT triple_it(id) FROM t");
  ASSERT_TRUE(result.success) << result.message;
  EXPECT_EQ(result.rows[0][0].as_int(), 21);
}

TEST(UdfTest, DropAndUnknownArity) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string(), 0);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1)").success);
  ASSERT_TRUE(db.execute_query(
                     "CREATE FUNCTION add1(x INT) RETURNS INT AS (x + 1)")
                  .success);
  EXPECT_FALSE(db.execute_query("SELECT add1(id, 2) FROM t").success);
  ASSERT_TRUE(db.execute_query("DROP FUNCTION add1").success);
  EXPECT_FALSE(db.execute_query("SELECT add1(id) FROM t").success);
}

TEST(UdfTest, PersistAcrossReload) {
  test_util::TempDbDir tmp;
  {
    Database db(tmp.path_string(), 0);
    ASSERT_TRUE(db.execute_query(
                       "CREATE FUNCTION inc(x INT) RETURNS INT AS $$\n"
                       "RETURN x + 1;\n"
                       "$$")
                    .success);
  }
  Database db2(tmp.path_string(), 0);
  db2.load_from_disk();
  ASSERT_TRUE(
      db2.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db2.execute_query("INSERT INTO t VALUES (9)").success);
  QueryResult result = db2.execute_query("SELECT inc(id) FROM t");
  ASSERT_TRUE(result.success) << result.message;
  EXPECT_EQ(result.rows[0][0].as_int(), 10);
}

TEST(UdfTest, UnclosedDollarQuoteFails) {
  EXPECT_THROW(
      {
        Parser parser("CREATE FUNCTION f(x INT) RETURNS INT AS $$ RETURN x");
        parser.parse_statement();
      },
      ParseException);
}

}  // namespace
}  // namespace db
