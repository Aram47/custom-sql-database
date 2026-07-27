#include "core/database.h"
#include "core/session_context.h"

#include "gtest/gtest.h"
#include "tests/test_util.hh"

namespace db {
namespace {

TEST(PreparedStatementTest, PrepareExecuteDeallocate) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  SessionContext session;
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)", &session).success);
  ASSERT_TRUE(
      db.execute_query("INSERT INTO t VALUES (1)", &session).success);
  ASSERT_TRUE(db.execute_query("PREPARE q AS SELECT id FROM t WHERE id = 1",
                               &session)
                  .success);
  auto r = db.execute_query("EXECUTE q", &session);
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 1u);
  EXPECT_EQ(r.rows[0][0].as_int(), 1);
  ASSERT_TRUE(db.execute_query("DEALLOCATE PREPARE q", &session).success);
  EXPECT_FALSE(db.execute_query("EXECUTE q", &session).success);
}

TEST(PreparedStatementTest, BindParameters) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  SessionContext session;
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY, name STRING)",
                       &session)
          .success);
  ASSERT_TRUE(
      db.execute_query("INSERT INTO t VALUES (1, 'a')", &session).success);
  ASSERT_TRUE(
      db.execute_query("INSERT INTO t VALUES (2, 'b')", &session).success);
  ASSERT_TRUE(db.execute_query("PREPARE q AS SELECT name FROM t WHERE id = ?",
                               &session)
                  .success);
  auto r = db.execute_query("EXECUTE q(2)", &session);
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 1u);
  EXPECT_EQ(r.rows[0][0].as_string(), "b");
  auto bad = db.execute_query("EXECUTE q(1, 2)", &session);
  EXPECT_FALSE(bad.success);
}

TEST(PreparedStatementTest, DollarBindParameters) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  SessionContext session;
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY, name STRING)",
                       &session)
          .success);
  ASSERT_TRUE(
      db.execute_query("INSERT INTO t VALUES (1, 'a')", &session).success);
  ASSERT_TRUE(
      db.execute_query("INSERT INTO t VALUES (2, 'b')", &session).success);
  ASSERT_TRUE(
      db.execute_query("PREPARE q AS SELECT name FROM t WHERE id = $1", &session)
          .success);
  auto r = db.execute_query("EXECUTE q(2)", &session);
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 1u);
  EXPECT_EQ(r.rows[0][0].as_string(), "b");
  EXPECT_FALSE(
      db.execute_query("PREPARE bad AS SELECT name FROM t WHERE id = ? AND "
                       "name = $1",
                       &session)
          .success);
}

}  // namespace
}  // namespace db
