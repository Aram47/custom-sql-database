#include "core/database.h"

#include "gtest/gtest.h"
#include "tests/test_util.hh"

namespace db {
namespace {

TEST(BuiltinFunctionTest, CoalesceReturnsFirstNonNull) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string(), 0);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY, a INT)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1, NULL)").success);
  QueryResult result =
      db.execute_query("SELECT COALESCE(a, 42), COALESCE(NULL, NULL, 7) FROM t");
  ASSERT_TRUE(result.success) << result.message;
  ASSERT_EQ(result.rows.size(), 1u);
  EXPECT_EQ(result.rows[0][0].as_int(), 42);
  EXPECT_EQ(result.rows[0][1].as_int(), 7);
}

TEST(BuiltinFunctionTest, NullIfEqualsReturnsNull) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string(), 0);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1)").success);
  QueryResult result =
      db.execute_query("SELECT NULLIF(1, 1), NULLIF(1, 2) FROM t");
  ASSERT_TRUE(result.success) << result.message;
  ASSERT_EQ(result.rows.size(), 1u);
  EXPECT_TRUE(result.rows[0][0].is_null());
  EXPECT_EQ(result.rows[0][1].as_int(), 1);
}

TEST(BuiltinFunctionTest, CastStringToInt) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string(), 0);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY, s STRING)")
          .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1, '42')").success);
  QueryResult result = db.execute_query("SELECT CAST(s AS INT) FROM t");
  ASSERT_TRUE(result.success) << result.message;
  ASSERT_EQ(result.rows.size(), 1u);
  EXPECT_EQ(result.rows[0][0].as_int(), 42);
}

TEST(BuiltinFunctionTest, CastInvalidReportsError) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string(), 0);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY, s STRING)")
          .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1, 'abc')").success);
  QueryResult result = db.execute_query("SELECT CAST(s AS INT) FROM t");
  EXPECT_FALSE(result.success);
}

TEST(BuiltinFunctionTest, SubstringAndSubstr) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string(), 0);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY, s STRING)")
          .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1, 'abcdef')").success);
  QueryResult result = db.execute_query(
      "SELECT SUBSTRING(s, 2, 3), SUBSTR(s, 4) FROM t");
  ASSERT_TRUE(result.success) << result.message;
  ASSERT_EQ(result.rows.size(), 1u);
  EXPECT_EQ(result.rows[0][0].as_string(), "bcd");
  EXPECT_EQ(result.rows[0][1].as_string(), "def");
}

TEST(BuiltinFunctionTest, CurrentDateReturnsYyyyMmDd) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string(), 0);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1)").success);
  QueryResult result = db.execute_query("SELECT CURRENT_DATE FROM t");
  ASSERT_TRUE(result.success) << result.message;
  ASSERT_EQ(result.rows.size(), 1u);
  const std::string date = result.rows[0][0].as_string();
  ASSERT_EQ(date.size(), 10u);
  EXPECT_EQ(date[4], '-');
  EXPECT_EQ(date[7], '-');
}

TEST(BuiltinFunctionTest, UnknownFunctionErrors) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string(), 0);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1)").success);
  QueryResult result = db.execute_query("SELECT nosuchfn(id) FROM t");
  EXPECT_FALSE(result.success);
  EXPECT_NE(result.message.find("Unknown function"), std::string::npos)
      << result.message;
}

}  // namespace
}  // namespace db
