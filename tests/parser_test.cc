#include "parser/parser.h"

#include <variant>

#include "gtest/gtest.h"
#include "parser/ast.h"
#include "utils/exceptions.h"

namespace db {
namespace {

TEST(ParserTest, CreateTable) {
  Parser parser(
      "CREATE TABLE users (id INT PRIMARY KEY, name STRING NOT NULL)");
  auto stmt_var = parser.parse_statement();
  auto *create =
      std::get_if<std::shared_ptr<CreateTableStatement>>(&stmt_var);
  ASSERT_NE(create, nullptr);
  ASSERT_NE(*create, nullptr);
  EXPECT_EQ((*create)->get_table_name(), "users");
  const auto &cols = (*create)->get_columns();
  ASSERT_EQ(cols.size(), 2u);
  EXPECT_EQ(cols[0].get_name(), "id");
  EXPECT_EQ(cols[0].get_type(), "INT");
  EXPECT_TRUE(cols[0].is_primary_key());
  EXPECT_EQ(cols[1].get_name(), "name");
  EXPECT_TRUE(cols[1].is_not_null());
}

TEST(ParserTest, InsertSelectUpdateDelete) {
  {
    Parser parser(
        "INSERT INTO users (id, name) VALUES (1, 'Alice'), (2, 'Bob')");
    auto v = parser.parse_statement();
    auto *ins = std::get_if<std::shared_ptr<InsertStatement>>(&v);
    ASSERT_NE(ins, nullptr);
    EXPECT_EQ((*ins)->get_table(), "users");
    ASSERT_EQ((*ins)->get_values().size(), 2u);
  }
  {
    Parser parser("SELECT id, name FROM users WHERE id = 1");
    auto v = parser.parse_statement();
    auto *sel = std::get_if<std::shared_ptr<SelectStatement>>(&v);
    ASSERT_NE(sel, nullptr);
    EXPECT_EQ((*sel)->get_from_table(), "users");
    EXPECT_NE((*sel)->get_where_condition(), nullptr);
  }
  {
    Parser parser("SELECT * FROM users");
    auto v = parser.parse_statement();
    auto *sel = std::get_if<std::shared_ptr<SelectStatement>>(&v);
    ASSERT_NE(sel, nullptr);
  }
  {
    Parser parser("UPDATE users SET name = 'Carl' WHERE id = 2");
    auto v = parser.parse_statement();
    auto *upd = std::get_if<std::shared_ptr<UpdateStatement>>(&v);
    ASSERT_NE(upd, nullptr);
    EXPECT_EQ((*upd)->get_table(), "users");
    ASSERT_FALSE((*upd)->get_set_clauses().empty());
  }
  {
    Parser parser("DELETE FROM users WHERE id = 1");
    auto v = parser.parse_statement();
    auto *del = std::get_if<std::shared_ptr<DeleteStatement>>(&v);
    ASSERT_NE(del, nullptr);
    EXPECT_EQ((*del)->get_table(), "users");
  }
}

TEST(ParserTest, UnknownStatementThrows) {
  Parser parser("UNKNOWN_STMT foo");
  EXPECT_THROW(static_cast<void>(parser.parse_statement()), ParseException);
}

TEST(ParserTest, DollarParamCountWithTableAlias) {
  {
    Parser p("SELECT id FROM t WHERE id = $1");
    p.parse_statement();
    EXPECT_EQ(p.get_parameter_count(), 1u);
  }
  {
    Parser p("SELECT t0.id AS id FROM t t0 WHERE t0.id = $1");
    p.parse_statement();
    EXPECT_EQ(p.get_parameter_count(), 1u) << "alias+AS should still count $1";
  }
  {
    Parser p("SELECT id FROM t t0 WHERE t0.id = $1");
    p.parse_statement();
    EXPECT_EQ(p.get_parameter_count(), 1u);
  }
}

}  // namespace
}  // namespace db
