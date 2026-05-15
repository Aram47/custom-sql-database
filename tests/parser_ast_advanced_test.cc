#include "parser/parser.h"

#include <variant>

#include "gtest/gtest.h"
#include "parser/ast.h"
#include "utils/exceptions.h"

namespace db {
namespace {

TEST(ParserAstAdvancedTest, SelectColumnAlias) {
  Parser parser("SELECT id AS uid FROM users");
  auto v = parser.parse_statement();
  auto *sel = std::get_if<std::shared_ptr<SelectStatement>>(&v);
  ASSERT_NE(sel, nullptr);
  const auto &cols = (*sel)->get_select_columns();
  ASSERT_EQ(cols.size(), 1u);
  EXPECT_EQ(cols[0].second, "uid");
  EXPECT_EQ((*sel)->get_from_table(), "users");
}

TEST(ParserAstAdvancedTest, InnerJoinParsed) {
  Parser parser(
      "SELECT * FROM orders INNER JOIN customers ON orders.cust_id = "
      "customers.id");
  auto v = parser.parse_statement();
  auto *sel = std::get_if<std::shared_ptr<SelectStatement>>(&v);
  ASSERT_NE(sel, nullptr);
  const auto &joins = (*sel)->get_joins();
  ASSERT_EQ(joins.size(), 1u);
  EXPECT_EQ(std::get<0>(joins[0]), "INNER");
  EXPECT_EQ(std::get<1>(joins[0]), "customers");
  EXPECT_NE(std::get<3>(joins[0]), nullptr);
}

TEST(ParserAstAdvancedTest, CrossJoinParsed) {
  Parser parser("SELECT * FROM x CROSS JOIN y");
  auto v = parser.parse_statement();
  auto *sel = std::get_if<std::shared_ptr<SelectStatement>>(&v);
  ASSERT_NE(sel, nullptr);
  const auto &joins = (*sel)->get_joins();
  ASSERT_EQ(joins.size(), 1u);
  EXPECT_EQ(std::get<0>(joins[0]), "CROSS");
  EXPECT_EQ(std::get<1>(joins[0]), "y");
  EXPECT_EQ(std::get<3>(joins[0]), nullptr);
}

TEST(ParserAstAdvancedTest, FullOuterJoinParsed) {
  Parser parser(
      "SELECT * FROM fa FULL OUTER JOIN fb ON fa.id = fb.id");
  auto v = parser.parse_statement();
  auto *sel = std::get_if<std::shared_ptr<SelectStatement>>(&v);
  ASSERT_NE(sel, nullptr);
  const auto &joins = (*sel)->get_joins();
  ASSERT_EQ(joins.size(), 1u);
  EXPECT_EQ(std::get<0>(joins[0]), "FULL");
  EXPECT_EQ(std::get<1>(joins[0]), "fb");
  EXPECT_NE(std::get<3>(joins[0]), nullptr);
}

TEST(ParserAstAdvancedTest, LeftOuterJoinParsed) {
  Parser parser("SELECT * FROM a LEFT OUTER JOIN b ON a.k = b.k");
  auto v = parser.parse_statement();
  auto *sel = std::get_if<std::shared_ptr<SelectStatement>>(&v);
  ASSERT_NE(sel, nullptr);
  const auto &joins = (*sel)->get_joins();
  ASSERT_EQ(joins.size(), 1u);
  EXPECT_EQ(std::get<0>(joins[0]), "LEFT");
  EXPECT_NE(std::get<3>(joins[0]), nullptr);
}

TEST(ParserAstAdvancedTest, CrossJoinWithOnThrows) {
  Parser parser("SELECT * FROM x CROSS JOIN y ON x.i = y.j");
  EXPECT_THROW(parser.parse_statement(), ParseException);
}

TEST(ParserAstAdvancedTest, GroupByHavingParsed) {
  Parser parser("SELECT id FROM t GROUP BY id HAVING id > 0");
  auto v = parser.parse_statement();
  auto *sel = std::get_if<std::shared_ptr<SelectStatement>>(&v);
  ASSERT_NE(sel, nullptr);
  ASSERT_FALSE((*sel)->get_group_by_columns().empty());
  EXPECT_NE((*sel)->get_having_condition(), nullptr);
}

TEST(ParserAstAdvancedTest, OrderByDescLimitOffsetParsed) {
  Parser parser("SELECT x FROM data ORDER BY x DESC LIMIT 10 OFFSET 20");
  auto v = parser.parse_statement();
  auto *sel = std::get_if<std::shared_ptr<SelectStatement>>(&v);
  ASSERT_NE(sel, nullptr);
  const auto &ob = (*sel)->get_order_by_columns();
  ASSERT_EQ(ob.size(), 1u);
  EXPECT_FALSE(ob[0].second);  // descending
  EXPECT_EQ((*sel)->get_limit(), 10);
  EXPECT_EQ((*sel)->get_offset(), 20);
}

}  // namespace
}  // namespace db
