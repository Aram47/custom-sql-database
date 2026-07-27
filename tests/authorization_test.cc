#include "core/authorization.h"
#include "core/session_context.h"
#include "parser/parser.h"
#include "tests/test_util.hh"

#include "core/database.h"

#include "gtest/gtest.h"

namespace db {
namespace {

ParsedStatement parse_sql(const std::string &sql) {
  Parser parser(sql);
  return parser.parse_statement();
}

TEST(AuthorizationTest, AdminAllowsInsert) {
  EXPECT_TRUE(can_execute(Role::Admin, parse_sql("INSERT INTO t VALUES (1)")));
}

TEST(AuthorizationTest, ReaderAllowsSelect) {
  EXPECT_TRUE(can_execute(Role::Reader, parse_sql("SELECT 1")));
}

TEST(AuthorizationTest, ReaderDeniesInsert) {
  EXPECT_FALSE(
      can_execute(Role::Reader, parse_sql("INSERT INTO t VALUES (1)")));
}

TEST(AuthorizationTest, ReaderDeniesDdl) {
  EXPECT_FALSE(can_execute(
      Role::Reader, parse_sql("CREATE TABLE t (id INT PRIMARY KEY)")));
  EXPECT_FALSE(can_execute(Role::Reader, parse_sql("DROP TABLE t")));
  EXPECT_FALSE(can_execute(Role::Reader, parse_sql("VACUUM")));
}

TEST(AuthorizationTest, ReaderAllowsExplainSelect) {
  EXPECT_TRUE(can_execute(Role::Reader, parse_sql("EXPLAIN SELECT 1")));
}

TEST(AuthorizationTest, ReaderDeniesExplainInsert) {
  EXPECT_FALSE(
      can_execute(Role::Reader, parse_sql("EXPLAIN INSERT INTO t VALUES (1)")));
}

TEST(AuthorizationTest, ReaderAllowsTxControl) {
  EXPECT_TRUE(can_execute(Role::Reader, parse_sql("BEGIN")));
  EXPECT_TRUE(can_execute(Role::Reader, parse_sql("COMMIT")));
  EXPECT_TRUE(can_execute(Role::Reader, parse_sql("ROLLBACK")));
}

TEST(AuthorizationTest, ReaderPrepareSelectAllowed) {
  EXPECT_TRUE(can_execute(Role::Reader,
                          parse_sql("PREPARE p AS SELECT 1")));
}

TEST(AuthorizationTest, ReaderPrepareInsertDenied) {
  EXPECT_FALSE(can_execute(
      Role::Reader, parse_sql("PREPARE p AS INSERT INTO t VALUES (1)")));
}

TEST(AuthorizationTest, DatabaseEnforcesReaderRole) {
  test_util::TempDbDir tmp;
  Database database(tmp.path_string());
  ASSERT_TRUE(
      database.execute_query("CREATE TABLE auth_t (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(database.execute_query("INSERT INTO auth_t VALUES (1)").success);
  SessionContext session;
  session.set_authenticated(true);
  session.set_username("viewer");
  session.set_role(Role::Reader);
  auto denied = database.execute_query(
      "INSERT INTO auth_t VALUES (2)", &session);
  EXPECT_FALSE(denied.success);
  EXPECT_NE(denied.message.find("permission denied"), std::string::npos);
  auto allowed = database.execute_query("SELECT id FROM auth_t", &session);
  EXPECT_TRUE(allowed.success) << allowed.message;
}

TEST(AuthorizationTest, DatabaseAllowsAdminDdl) {
  test_util::TempDbDir tmp;
  Database database(tmp.path_string());
  SessionContext session;
  session.set_authenticated(true);
  session.set_username("admin");
  session.set_role(Role::Admin);
  auto result = database.execute_query(
      "CREATE TABLE auth_admin_t (id INT PRIMARY KEY)", &session);
  EXPECT_TRUE(result.success);
}

}  // namespace
}  // namespace db
