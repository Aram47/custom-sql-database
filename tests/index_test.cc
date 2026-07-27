#include "core/database.h"

#include "gtest/gtest.h"
#include "tests/test_util.hh"

namespace db {
namespace {

TEST(IndexTest, CreateIndexUsedInWhere) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE t (id INT PRIMARY KEY, score INT)")
          .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1, 10)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (2, 20)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (3, 30)").success);
  ASSERT_TRUE(db.execute_query("CREATE INDEX idx_score ON t(score)").success);
  auto r = db.execute_query("SELECT id FROM t WHERE score = 20");
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 1u);
  EXPECT_EQ(r.rows[0][0].as_int(), 2);
}

TEST(IndexTest, DropIndexAndReload) {
  test_util::TempDbDir tmp;
  {
    Database db(tmp.path_string());
    ASSERT_TRUE(
        db.execute_query("CREATE TABLE t (id INT PRIMARY KEY, score INT)")
            .success);
    ASSERT_TRUE(db.execute_query("CREATE INDEX idx_score ON t(score)").success);
    ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1, 5)").success);
  }
  Database reloaded(tmp.path_string());
  reloaded.load_from_disk();
  auto *table = reloaded.get_table("t");
  ASSERT_NE(table, nullptr);
  EXPECT_TRUE(table->has_secondary_index("idx_score"));
  EXPECT_TRUE(table->has_index("score"));
  ASSERT_TRUE(reloaded.execute_query("DROP INDEX idx_score").success);
  EXPECT_FALSE(table->has_secondary_index("idx_score"));
}

TEST(IndexTest, CompositeIndexEqualityAndPrefix) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE t (id INT PRIMARY KEY, a INT, b INT, c INT)")
                  .success);
  ASSERT_TRUE(db.execute_query("CREATE INDEX idx_ab ON t(a, b)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (1, 10, 1, 0)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (2, 10, 2, 0)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO t VALUES (3, 20, 1, 0)").success);
  auto both = db.execute_query("SELECT id FROM t WHERE a = 10 AND b = 2");
  ASSERT_TRUE(both.success) << both.message;
  ASSERT_EQ(both.rows.size(), 1u);
  EXPECT_EQ(both.rows[0][0].as_int(), 2);
  auto prefix = db.execute_query("SELECT id FROM t WHERE a = 10");
  ASSERT_TRUE(prefix.success) << prefix.message;
  EXPECT_EQ(prefix.rows.size(), 2u);
}

}  // namespace
}  // namespace db
