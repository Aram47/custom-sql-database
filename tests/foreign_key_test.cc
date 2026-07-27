#include "core/database.h"

#include "gtest/gtest.h"
#include "tests/test_util.hh"

namespace db {
namespace {

TEST(ForeignKeyTest, InsertRequiresParent) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE parent (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query(
                     "CREATE TABLE child (id INT PRIMARY KEY, "
                     "pid INT REFERENCES parent(id))")
                  .success);
  auto bad = db.execute_query("INSERT INTO child VALUES (1, 99)");
  EXPECT_FALSE(bad.success);
  ASSERT_TRUE(db.execute_query("INSERT INTO parent VALUES (1)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO child VALUES (1, 1)").success);
}

TEST(ForeignKeyTest, DeleteParentRestricted) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE parent (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query(
                     "CREATE TABLE child (id INT PRIMARY KEY, "
                     "pid INT REFERENCES parent(id))")
                  .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO parent VALUES (1)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO child VALUES (10, 1)").success);
  auto del = db.execute_query("DELETE FROM parent WHERE id = 1");
  EXPECT_FALSE(del.success);
  ASSERT_TRUE(db.execute_query("DELETE FROM child WHERE id = 10").success);
  ASSERT_TRUE(db.execute_query("DELETE FROM parent WHERE id = 1").success);
}

TEST(ForeignKeyTest, UpdateChildAndParent) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE parent (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query(
                     "CREATE TABLE child (id INT PRIMARY KEY, "
                     "pid INT REFERENCES parent(id))")
                  .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO parent VALUES (1)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO parent VALUES (2)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO child VALUES (10, 1)").success);
  ASSERT_TRUE(db.execute_query("UPDATE child SET pid = 2 WHERE id = 10").success);
  auto bad = db.execute_query("UPDATE parent SET id = 3 WHERE id = 2");
  EXPECT_FALSE(bad.success);
}

TEST(ForeignKeyTest, DeleteCascade) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE parent (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query(
                     "CREATE TABLE child (id INT PRIMARY KEY, "
                     "pid INT REFERENCES parent(id) ON DELETE CASCADE)")
                  .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO parent VALUES (1)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO child VALUES (10, 1)").success);
  ASSERT_TRUE(db.execute_query("DELETE FROM parent WHERE id = 1").success);
  auto r = db.execute_query("SELECT id FROM child");
  ASSERT_TRUE(r.success);
  EXPECT_TRUE(r.rows.empty());
}

TEST(ForeignKeyTest, DeleteSetNull) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE parent (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query(
                     "CREATE TABLE child (id INT PRIMARY KEY, "
                     "pid INT REFERENCES parent(id) ON DELETE SET NULL)")
                  .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO parent VALUES (1)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO child VALUES (10, 1)").success);
  ASSERT_TRUE(db.execute_query("DELETE FROM parent WHERE id = 1").success);
  auto r = db.execute_query("SELECT pid FROM child WHERE id = 10");
  ASSERT_TRUE(r.success);
  ASSERT_EQ(r.rows.size(), 1u);
  EXPECT_TRUE(r.rows[0][0].is_null());
}

TEST(ForeignKeyTest, UpdateCascadeAndSetNull) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE parent (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query(
                     "CREATE TABLE child (id INT PRIMARY KEY, "
                     "pid INT REFERENCES parent(id) ON UPDATE CASCADE)")
                  .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO parent VALUES (1)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO child VALUES (10, 1)").success);
  ASSERT_TRUE(db.execute_query("UPDATE parent SET id = 5 WHERE id = 1").success);
  auto r = db.execute_query("SELECT pid FROM child WHERE id = 10");
  ASSERT_TRUE(r.success);
  ASSERT_EQ(r.rows.size(), 1u);
  EXPECT_EQ(r.rows[0][0].as_int(), 5);
  ASSERT_TRUE(db.execute_query("DROP TABLE child").success);
  ASSERT_TRUE(db.execute_query("DROP TABLE parent").success);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE parent (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query(
                     "CREATE TABLE child (id INT PRIMARY KEY, "
                     "pid INT REFERENCES parent(id) ON UPDATE SET NULL)")
                  .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO parent VALUES (1)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO child VALUES (10, 1)").success);
  ASSERT_TRUE(db.execute_query("UPDATE parent SET id = 7 WHERE id = 1").success);
  auto r2 = db.execute_query("SELECT pid FROM child WHERE id = 10");
  ASSERT_TRUE(r2.success);
  ASSERT_EQ(r2.rows.size(), 1u);
  EXPECT_TRUE(r2.rows[0][0].is_null());
}

TEST(ForeignKeyTest, ColumnDefaultAndSetDefault) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE parent (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO parent VALUES (0)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO parent VALUES (1)").success);
  ASSERT_TRUE(db.execute_query(
                     "CREATE TABLE child (id INT PRIMARY KEY, "
                     "pid INT DEFAULT 0 REFERENCES parent(id) ON DELETE SET "
                     "DEFAULT)")
                  .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO child (id, pid) VALUES (10, 1)")
                  .success);
  ASSERT_TRUE(db.execute_query("DELETE FROM parent WHERE id = 1").success);
  auto r = db.execute_query("SELECT pid FROM child WHERE id = 10");
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 1u);
  EXPECT_EQ(r.rows[0][0].as_int(), 0);
}

TEST(ForeignKeyTest, MultiColumnForeignKey) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query("CREATE TABLE parent (a INT, b INT)").success);
  ASSERT_TRUE(
      db.execute_query("CREATE INDEX parent_ab ON parent(a, b)").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO parent VALUES (1, 2)").success);
  ASSERT_TRUE(db.execute_query(
                     "CREATE TABLE child (id INT PRIMARY KEY, ca INT, cb INT, "
                     "FOREIGN KEY (ca, cb) REFERENCES parent(a, b))")
                  .success);
  auto bad = db.execute_query("INSERT INTO child VALUES (1, 9, 9)");
  EXPECT_FALSE(bad.success);
  ASSERT_TRUE(db.execute_query("INSERT INTO child VALUES (1, 1, 2)").success);
}

}  // namespace
}  // namespace db
