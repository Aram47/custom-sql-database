#include "core/database.h"
#include "parser/parser.h"

#include "gtest/gtest.h"
#include "tests/test_util.hh"
#include "utils/exceptions.h"

namespace db {
namespace {

TEST(ProcedureTest, CallInsertVisible) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string(), 0);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
  QueryResult create = db.execute_query(
      "CREATE PROCEDURE add_row(v INT) AS $$\n"
      "  INSERT INTO t VALUES (v);\n"
      "$$");
  ASSERT_TRUE(create.success) << create.message;
  QueryResult call = db.execute_query("CALL add_row(5)");
  ASSERT_TRUE(call.success) << call.message;
  QueryResult select = db.execute_query("SELECT id FROM t");
  ASSERT_TRUE(select.success) << select.message;
  ASSERT_EQ(select.rows.size(), 1u);
  EXPECT_EQ(select.rows[0][0].as_int(), 5);
}

TEST(ProcedureTest, MultiStatementStopsOnError) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string(), 0);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query(
                     "CREATE PROCEDURE bad_proc(v INT) AS $$\n"
                     "  INSERT INTO t VALUES (v);\n"
                     "  INSERT INTO missing VALUES (1);\n"
                     "$$")
                  .success);
  QueryResult call = db.execute_query("CALL bad_proc(1)");
  EXPECT_FALSE(call.success);
  QueryResult select = db.execute_query("SELECT id FROM t");
  ASSERT_TRUE(select.success);
  EXPECT_EQ(select.rows.size(), 1u);
}

TEST(ProcedureTest, DropAndReload) {
  test_util::TempDbDir tmp;
  {
    Database db(tmp.path_string(), 0);
    ASSERT_TRUE(
        db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
    ASSERT_TRUE(db.execute_query(
                       "CREATE PROCEDURE add_row(v INT) AS $$\n"
                       "INSERT INTO t VALUES (v);\n"
                       "$$")
                    .success);
  }
  Database db2(tmp.path_string(), 0);
  db2.load_from_disk();
  ASSERT_TRUE(db2.execute_query("CALL add_row(3)").success);
  QueryResult select = db2.execute_query("SELECT id FROM t");
  ASSERT_TRUE(select.success) << select.message;
  ASSERT_EQ(select.rows.size(), 1u);
  EXPECT_EQ(select.rows[0][0].as_int(), 3);
  ASSERT_TRUE(db2.execute_query("DROP PROCEDURE add_row").success);
  EXPECT_FALSE(db2.execute_query("CALL add_row(4)").success);
}

TEST(ProcedureTest, UnclosedDollarQuoteFails) {
  EXPECT_THROW(
      {
        Parser parser("CREATE PROCEDURE p(v INT) AS $$ INSERT INTO t VALUES (v)");
        parser.parse_statement();
      },
      ParseException);
}

}  // namespace
}  // namespace db
