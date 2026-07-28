#include <string>

#include "core/database.h"
#include "parser/parser.h"

#include "gtest/gtest.h"
#include "tests/test_util.hh"

namespace db {
namespace {

void setup_two_tables(Database *db) {
  ASSERT_TRUE(db->execute_query("CREATE TABLE left_t (id INT, name STRING)")
                  .success);
  ASSERT_TRUE(db->execute_query("CREATE TABLE right_t (id INT, name STRING)")
                  .success);
  ASSERT_TRUE(
      db->execute_query(
           "INSERT INTO left_t VALUES (1, 'a'), (2, 'b'), (3, 'c')")
          .success);
  ASSERT_TRUE(
      db->execute_query(
           "INSERT INTO right_t VALUES (2, 'b'), (3, 'c'), (4, 'd')")
          .success);
}

TEST(SetOperationTest, UnionAllKeepsDuplicates) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  setup_two_tables(&db);
  auto actual = db.execute_query(
      "SELECT id FROM left_t UNION ALL SELECT id FROM right_t");
  ASSERT_TRUE(actual.success) << actual.message;
  EXPECT_EQ(actual.rows.size(), 6u);
}

TEST(SetOperationTest, UnionRemovesDuplicates) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  setup_two_tables(&db);
  auto actual = db.execute_query(
      "SELECT id FROM left_t UNION SELECT id FROM right_t ORDER BY id");
  ASSERT_TRUE(actual.success) << actual.message;
  ASSERT_EQ(actual.rows.size(), 4u);
  EXPECT_EQ(actual.rows[0][0].as_int(), 1);
  EXPECT_EQ(actual.rows[1][0].as_int(), 2);
  EXPECT_EQ(actual.rows[2][0].as_int(), 3);
  EXPECT_EQ(actual.rows[3][0].as_int(), 4);
}

TEST(SetOperationTest, IntersectReturnsCommonRows) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  setup_two_tables(&db);
  auto actual = db.execute_query(
      "SELECT id, name FROM left_t INTERSECT SELECT id, name FROM right_t "
      "ORDER BY id");
  ASSERT_TRUE(actual.success) << actual.message;
  ASSERT_EQ(actual.rows.size(), 2u);
  EXPECT_EQ(actual.rows[0][0].as_int(), 2);
  EXPECT_EQ(actual.rows[0][1].as_string(), "b");
  EXPECT_EQ(actual.rows[1][0].as_int(), 3);
}

TEST(SetOperationTest, ExceptReturnsLeftMinusRight) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  setup_two_tables(&db);
  auto actual = db.execute_query(
      "SELECT id FROM left_t EXCEPT SELECT id FROM right_t ORDER BY id");
  ASSERT_TRUE(actual.success) << actual.message;
  ASSERT_EQ(actual.rows.size(), 1u);
  EXPECT_EQ(actual.rows[0][0].as_int(), 1);
}

TEST(SetOperationTest, ColumnCountMismatchReturnsError) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  setup_two_tables(&db);
  auto actual = db.execute_query(
      "SELECT id FROM left_t UNION SELECT id, name FROM right_t");
  EXPECT_FALSE(actual.success);
  EXPECT_NE(actual.message.find("column count"), std::string::npos);
}

TEST(SetOperationTest, TypeMismatchReturnsError) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query("CREATE TABLE ints (v INT)").success);
  ASSERT_TRUE(db.execute_query("CREATE TABLE strs (v STRING)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO ints VALUES (1)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO strs VALUES ('x')").success);
  auto actual =
      db.execute_query("SELECT v FROM ints UNION SELECT v FROM strs");
  EXPECT_FALSE(actual.success);
  EXPECT_NE(actual.message.find("type mismatch"), std::string::npos);
}

TEST(SetOperationTest, OrderByAfterUnion) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  setup_two_tables(&db);
  auto actual = db.execute_query(
      "SELECT id FROM left_t UNION ALL SELECT id FROM right_t ORDER BY id DESC "
      "LIMIT 3");
  ASSERT_TRUE(actual.success) << actual.message;
  ASSERT_EQ(actual.rows.size(), 3u);
  EXPECT_EQ(actual.rows[0][0].as_int(), 4);
  EXPECT_EQ(actual.rows[1][0].as_int(), 3);
  EXPECT_EQ(actual.rows[2][0].as_int(), 3);
}

TEST(SetOperationTest, ParenthesizedNesting) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  setup_two_tables(&db);
  auto actual = db.execute_query(
      "(SELECT id FROM left_t UNION SELECT id FROM right_t) EXCEPT "
      "SELECT id FROM right_t WHERE id = 4 ORDER BY id");
  ASSERT_TRUE(actual.success) << actual.message;
  ASSERT_EQ(actual.rows.size(), 3u);
  EXPECT_EQ(actual.rows[0][0].as_int(), 1);
  EXPECT_EQ(actual.rows[1][0].as_int(), 2);
  EXPECT_EQ(actual.rows[2][0].as_int(), 3);
}

TEST(SetOperationTest, ParseChainProducesSetOperation) {
  Parser parser(
      "SELECT id FROM left_t UNION SELECT id FROM right_t INTERSECT "
      "SELECT id FROM left_t");
  ParsedStatement stmt = parser.parse_statement();
  ASSERT_TRUE(
      std::holds_alternative<std::shared_ptr<SetOperationStatement>>(stmt));
}

TEST(SetOperationTest, ExplainMentionsUnion) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  setup_two_tables(&db);
  auto actual = db.execute_query(
      "EXPLAIN SELECT id FROM left_t UNION SELECT id FROM right_t");
  ASSERT_TRUE(actual.success) << actual.message;
  ASSERT_FALSE(actual.rows.empty());
  std::string plan;
  for (const auto &row : actual.rows) {
    plan += row[0].as_string();
    plan += "\n";
  }
  EXPECT_NE(plan.find("Union"), std::string::npos);
}

TEST(SetOperationTest, IntersectAllRejected) {
  Parser parser("SELECT id FROM t INTERSECT ALL SELECT id FROM u");
  EXPECT_THROW(parser.parse_statement(), ParseException);
}

}  // namespace
}  // namespace db
