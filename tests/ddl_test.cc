#include "core/database.h"

#include <filesystem>
#include <variant>

#include "gtest/gtest.h"
#include "parser/parser.h"
#include "tests/test_util.hh"

namespace db {
namespace {

TEST(DdlTest, DropTableRemovesFromCatalogAndDisk) {
  test_util::TempDbDir dir;
  Database db(dir.path_string());
  auto create = db.execute_query(
      "CREATE TABLE users (id INT PRIMARY KEY, name STRING)");
  ASSERT_TRUE(create.success) << create.message;
  ASSERT_TRUE(
      std::filesystem::exists(dir.path() / "users.db"));
  auto drop = db.execute_query("DROP TABLE users");
  ASSERT_TRUE(drop.success) << drop.message;
  EXPECT_FALSE(db.has_table("users"));
  EXPECT_FALSE(std::filesystem::exists(dir.path() / "users.db"));
}

TEST(DdlTest, AlterAddDropColumnAndRename) {
  test_util::TempDbDir dir;
  Database db(dir.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY, name STRING)")
          .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1, 'a')").success);
  auto add = db.execute_query("ALTER TABLE t ADD COLUMN age INT");
  ASSERT_TRUE(add.success) << add.message;
  auto sel = db.execute_query("SELECT * FROM t");
  ASSERT_TRUE(sel.success);
  ASSERT_EQ(sel.column_names.size(), 3u);
  EXPECT_EQ(sel.rows[0][2].is_null(), true);
  auto rename_col =
      db.execute_query("ALTER TABLE t RENAME COLUMN name TO full_name");
  ASSERT_TRUE(rename_col.success) << rename_col.message;
  auto rename_tbl = db.execute_query("ALTER TABLE t RENAME TO people");
  ASSERT_TRUE(rename_tbl.success) << rename_tbl.message;
  EXPECT_TRUE(db.has_table("people"));
  EXPECT_FALSE(db.has_table("t"));
  auto drop_col = db.execute_query("ALTER TABLE people DROP COLUMN age");
  ASSERT_TRUE(drop_col.success) << drop_col.message;
  auto sel2 = db.execute_query("SELECT * FROM people");
  ASSERT_TRUE(sel2.success);
  EXPECT_EQ(sel2.column_names.size(), 2u);
}

TEST(DdlTest, AlterConstraints) {
  test_util::TempDbDir dir;
  Database db(dir.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT, email STRING)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1, 'a@b.c')").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (2, 'c@d.e')").success);
  auto pk = db.execute_query("ALTER TABLE t ADD PRIMARY KEY (id)");
  ASSERT_TRUE(pk.success) << pk.message;
  auto uniq = db.execute_query("ALTER TABLE t ADD UNIQUE (email)");
  ASSERT_TRUE(uniq.success) << uniq.message;
  auto nn = db.execute_query("ALTER TABLE t ALTER COLUMN email SET NOT NULL");
  ASSERT_TRUE(nn.success) << nn.message;
  Table *table = db.get_table("t");
  ASSERT_NE(table, nullptr);
  EXPECT_TRUE(table->has_index("id"));
  EXPECT_TRUE(table->has_index("email"));
  ASSERT_TRUE(db.execute_query("ALTER TABLE t DROP UNIQUE (email)").success);
  ASSERT_TRUE(db.execute_query("ALTER TABLE t DROP PRIMARY KEY").success);
  ASSERT_TRUE(
      db.execute_query("ALTER TABLE t ALTER COLUMN email DROP NOT NULL")
          .success);
}

TEST(DdlTest, ParseDropAndAlterStatements) {
  Parser drop_parser("DROP TABLE users");
  auto drop_stmt = drop_parser.parse_statement();
  ASSERT_TRUE(std::holds_alternative<std::shared_ptr<DropTableStatement>>(
      drop_stmt));
  Parser alter_parser("ALTER TABLE t ADD COLUMN x INT");
  auto alter_stmt = alter_parser.parse_statement();
  ASSERT_TRUE(std::holds_alternative<std::shared_ptr<AlterTableStatement>>(
      alter_stmt));
}

TEST(DdlTest, BetweenDesugarsToRange) {
  Parser parser("SELECT * FROM t WHERE age BETWEEN 10 AND 20");
  auto stmt = parser.parse_select_statement();
  ASSERT_NE(stmt->get_where_condition(), nullptr);
  auto and_expr = std::dynamic_pointer_cast<BinaryOpExpression>(
      stmt->get_where_condition());
  ASSERT_NE(and_expr, nullptr);
  EXPECT_EQ(and_expr->get_operator(), BinaryOpExpression::Operator::AND);
}

}  // namespace
}  // namespace db
