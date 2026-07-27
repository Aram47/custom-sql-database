#include "core/database.h"

#include "gtest/gtest.h"
#include "tests/test_util.hh"

namespace db {
namespace {

TEST(ViewTest, CreateViewAndSelectStar) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY, name STRING)")
          .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1, 'a')").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (2, 'b')").success);
  ASSERT_TRUE(
      db.execute_query("CREATE VIEW v AS SELECT id, name FROM t WHERE id > 0")
          .success);
  auto expected = db.execute_query("SELECT id, name FROM t WHERE id > 0");
  auto actual = db.execute_query("SELECT * FROM v");
  ASSERT_TRUE(actual.success) << actual.message;
  ASSERT_TRUE(expected.success) << expected.message;
  ASSERT_EQ(actual.rows.size(), expected.rows.size());
  EXPECT_EQ(actual.column_names, expected.column_names);
  for (size_t i = 0; i < actual.rows.size(); ++i) {
    EXPECT_EQ(actual.rows[i], expected.rows[i]);
  }
}

TEST(ViewTest, ViewWithJoinAndWhere) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE users (id INT PRIMARY KEY, name STRING)")
          .success);
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE orders (id INT PRIMARY KEY, user_id INT)")
                  .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO users VALUES (1, 'alice')").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO orders VALUES (10, 1)").success);
  ASSERT_TRUE(db.execute_query(
                      "CREATE VIEW user_orders AS SELECT users.name, orders.id "
                      "FROM users JOIN orders ON users.id = orders.user_id "
                      "WHERE users.id = 1")
                  .success);
  auto result = db.execute_query("SELECT * FROM user_orders");
  ASSERT_TRUE(result.success) << result.message;
  ASSERT_EQ(result.rows.size(), 1u);
  EXPECT_EQ(result.rows[0][0].as_string(), "alice");
  EXPECT_EQ(result.rows[0][1].as_int(), 10);
}

TEST(ViewTest, DropViewThenSelectFails) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("CREATE VIEW v AS SELECT id FROM t").success);
  ASSERT_TRUE(db.execute_query("DROP VIEW v").success);
  auto result = db.execute_query("SELECT * FROM v");
  EXPECT_FALSE(result.success);
  ASSERT_TRUE(db.execute_query("DROP VIEW IF EXISTS v").success);
}

TEST(ViewTest, NameClashWithTable) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
  auto view_clash =
      db.execute_query("CREATE VIEW t AS SELECT id FROM t");
  EXPECT_FALSE(view_clash.success);
  ASSERT_TRUE(db.execute_query("CREATE VIEW v AS SELECT id FROM t").success);
  auto table_clash =
      db.execute_query("CREATE TABLE v (id INT PRIMARY KEY)");
  EXPECT_FALSE(table_clash.success);
}

TEST(ViewTest, NestedViewAndCycle) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (7)").success);
  ASSERT_TRUE(db.execute_query("CREATE VIEW v1 AS SELECT id FROM t").success);
  ASSERT_TRUE(db.execute_query("CREATE VIEW v2 AS SELECT id FROM v1").success);
  auto nested = db.execute_query("SELECT * FROM v2");
  ASSERT_TRUE(nested.success) << nested.message;
  ASSERT_EQ(nested.rows.size(), 1u);
  EXPECT_EQ(nested.rows[0][0].as_int(), 7);
  ASSERT_TRUE(db.execute_query("CREATE VIEW v3 AS SELECT id FROM v2").success);
  ASSERT_TRUE(
      db.execute_query("CREATE VIEW cycle_a AS SELECT id FROM t").success);
  // Replace cycle_a body indirectly: create cycle_b -> cycle_a, then we need
  // mutual cycle. Register cycle_b referencing cycle_a, then we cannot change
  // cycle_a. Create cycle via two views that reference each other at create
  // time — second create succeeds; cycle fails at query time.
  ASSERT_TRUE(
      db.execute_query("CREATE VIEW cycle_b AS SELECT id FROM cycle_a")
          .success);
  // Drop and recreate cycle_a to point at cycle_b (not supported as REPLACE).
  // Instead create cycle_c -> cycle_d and cycle_d -> cycle_c by creating empty
  // stubs is not possible. Direct: CREATE VIEW ca AS SELECT * FROM cb fails
  // if cb missing. So create ca on t, cb on ca, drop ca, recreate ca on cb.
  ASSERT_TRUE(db.execute_query("DROP VIEW cycle_a").success);
  ASSERT_TRUE(
      db.execute_query("CREATE VIEW cycle_a AS SELECT id FROM cycle_b")
          .success);
  auto cyclic = db.execute_query("SELECT * FROM cycle_a");
  EXPECT_FALSE(cyclic.success);
  EXPECT_NE(cyclic.message.find("Circular"), std::string::npos);
}

TEST(ViewTest, PersistAcrossRestart) {
  test_util::TempDbDir tmp;
  {
    Database db(tmp.path_string());
    ASSERT_TRUE(
        db.execute_query("CREATE TABLE t (id INT PRIMARY KEY, score INT)")
            .success);
    ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1, 42)").success);
    ASSERT_TRUE(
        db.execute_query("CREATE VIEW scores AS SELECT id, score FROM t")
            .success);
  }
  Database reloaded(tmp.path_string());
  reloaded.load_from_disk();
  auto result = reloaded.execute_query("SELECT * FROM scores");
  ASSERT_TRUE(result.success) << result.message;
  ASSERT_EQ(result.rows.size(), 1u);
  EXPECT_EQ(result.rows[0][0].as_int(), 1);
  EXPECT_EQ(result.rows[0][1].as_int(), 42);
}

TEST(ViewTest, ExplainShowsViewScan) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("CREATE VIEW v AS SELECT id FROM t").success);
  auto result = db.execute_query("EXPLAIN SELECT * FROM v");
  ASSERT_TRUE(result.success) << result.message;
  ASSERT_FALSE(result.rows.empty());
  bool found = false;
  for (const auto &row : result.rows) {
    if (!row.empty() && row[0].as_string().find("ViewScan(v)") !=
                            std::string::npos) {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found);
}

}  // namespace
}  // namespace db
