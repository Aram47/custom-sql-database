#include <gtest/gtest.h>

#include "core/database.h"
#include "parser/parser.h"
#include "tests/test_util.hh"

using namespace db;

TEST(KeysConstraintTest, ParseCompositePrimaryKeyAndUnique) {
  Parser parser(
      "CREATE TABLE order_items (order_id UUID, product_id UUID, qty INT, "
      "PRIMARY KEY (order_id, product_id), UNIQUE (order_id, product_id))");
  auto stmt = parser.parse_create_table_statement();
  ASSERT_NE(stmt, nullptr);
  EXPECT_EQ(stmt->get_primary_key_columns().size(), 2u);
  EXPECT_EQ(stmt->get_primary_key_columns()[0], "order_id");
  EXPECT_EQ(stmt->get_primary_key_columns()[1], "product_id");
  ASSERT_EQ(stmt->get_unique_constraints().size(), 1u);
  EXPECT_EQ(stmt->get_unique_constraints()[0].second.size(), 2u);
}

TEST(KeysConstraintTest, ParseAlterMultiColumnUnique) {
  Parser parser("ALTER TABLE t ADD UNIQUE (a, b)");
  auto stmt = parser.parse_alter_table_statement();
  ASSERT_NE(stmt, nullptr);
  EXPECT_EQ(stmt->get_action().type, AlterTableActionType::AddUnique);
  ASSERT_EQ(stmt->get_action().columns.size(), 2u);
  EXPECT_EQ(stmt->get_action().columns[0], "a");
  EXPECT_EQ(stmt->get_action().columns[1], "b");
}

TEST(KeysConstraintTest, CompositePrimaryKeyEnforced) {
  test_util::TempDbDir dir;
  Database db(dir.path_string());
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE order_items ("
                      "order_id INT, product_id INT, qty INT, "
                      "PRIMARY KEY (order_id, product_id))")
                  .success);
  ASSERT_TRUE(
      db.execute_query("INSERT INTO order_items VALUES (1, 10, 2)").success);
  EXPECT_FALSE(
      db.execute_query("INSERT INTO order_items VALUES (1, 10, 3)").success);
  ASSERT_TRUE(
      db.execute_query("INSERT INTO order_items VALUES (1, 11, 1)").success);
}

TEST(KeysConstraintTest, CompositeUniqueEnforced) {
  test_util::TempDbDir dir;
  Database db(dir.path_string());
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE tags (id INT PRIMARY KEY, a INT, b INT, "
                      "UNIQUE (a, b))")
                  .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO tags VALUES (1, 1, 2)").success);
  EXPECT_FALSE(db.execute_query("INSERT INTO tags VALUES (2, 1, 2)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO tags VALUES (3, 1, 3)").success);
}

TEST(KeysConstraintTest, AlterAddCompositeUnique) {
  test_util::TempDbDir dir;
  Database db(dir.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY, a INT, b INT)")
          .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1, 1, 2)").success);
  ASSERT_TRUE(db.execute_query("ALTER TABLE t ADD UNIQUE (a, b)").success);
  EXPECT_FALSE(db.execute_query("INSERT INTO t VALUES (2, 1, 2)").success);
  ASSERT_TRUE(db.execute_query("ALTER TABLE t DROP UNIQUE (a, b)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (3, 1, 2)").success);
}

TEST(KeysConstraintTest, ForeignKeyOnCompositePrimaryKey) {
  test_util::TempDbDir dir;
  Database db(dir.path_string());
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE parent (a INT, b INT, PRIMARY KEY (a, b))")
                  .success);
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE child (id INT PRIMARY KEY, a INT, b INT, "
                      "FOREIGN KEY (a, b) REFERENCES parent (a, b))")
                  .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO parent VALUES (1, 2)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO child VALUES (10, 1, 2)").success);
  EXPECT_FALSE(db.execute_query("INSERT INTO child VALUES (11, 9, 9)").success);
}

TEST(KeysConstraintTest, PersistCompositeKeys) {
  test_util::TempDbDir dir;
  {
    Database db(dir.path_string());
    ASSERT_TRUE(db.execute_query(
                        "CREATE TABLE order_items ("
                        "order_id INT, product_id INT, qty INT, "
                        "PRIMARY KEY (order_id, product_id), "
                        "UNIQUE (qty, product_id))")
                    .success);
    ASSERT_TRUE(
        db.execute_query("INSERT INTO order_items VALUES (1, 10, 2)").success);
  }
  Database reloaded(dir.path_string());
  reloaded.load_from_disk();
  const Table *table = reloaded.get_table("order_items");
  ASSERT_NE(table, nullptr);
  ASSERT_EQ(table->get_primary_key_columns().size(), 2u);
  EXPECT_EQ(table->get_primary_key_columns()[0], "order_id");
  EXPECT_EQ(table->get_unique_constraints().size(), 1u);
  EXPECT_FALSE(reloaded.execute_query("INSERT INTO order_items VALUES (1, 10, 5)")
                   .success);
}
