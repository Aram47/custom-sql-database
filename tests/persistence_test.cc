#include "core/database.h"
#include "storage/persistence_manager.h"

#include "gtest/gtest.h"
#include "tests/test_util.hh"

namespace db {
namespace {

TEST(PersistenceTest, AtomicSaveCreatesDbFile) {
  test_util::TempDbDir dir;
  Database db(dir.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE items (id INT PRIMARY KEY, n STRING)")
          .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO items VALUES (1, 'x')").success);
  EXPECT_TRUE(std::filesystem::exists(dir.path() / "items.db"));
  EXPECT_FALSE(std::filesystem::exists(dir.path() / "items.db.tmp"));
}

TEST(PersistenceTest, DirtySaveOnlyTouchesChangedTable) {
  test_util::TempDbDir dir;
  Database db(dir.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE a (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE b (id INT PRIMARY KEY)").success);
  const auto path_a = dir.path() / "a.db";
  const auto path_b = dir.path() / "b.db";
  ASSERT_TRUE(std::filesystem::exists(path_a));
  ASSERT_TRUE(std::filesystem::exists(path_b));
  const auto mtime_b_before = std::filesystem::last_write_time(path_b);
  ASSERT_TRUE(db.execute_query("INSERT INTO a VALUES (1)").success);
  const auto mtime_b_after = std::filesystem::last_write_time(path_b);
  EXPECT_EQ(mtime_b_before, mtime_b_after);
}

TEST(PersistenceTest, ReloadAfterDropDoesNotRestoreTable) {
  test_util::TempDbDir dir;
  {
    Database db(dir.path_string());
    ASSERT_TRUE(
        db.execute_query("CREATE TABLE t (id INT PRIMARY KEY)").success);
    ASSERT_TRUE(db.execute_query("DROP TABLE t").success);
  }
  Database reloaded(dir.path_string());
  reloaded.load_from_disk();
  EXPECT_FALSE(reloaded.has_table("t"));
}

TEST(PersistenceTest, RenameTableRenamesFile) {
  test_util::TempDbDir dir;
  Database db(dir.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE old_name (id INT PRIMARY KEY)").success);
  ASSERT_TRUE(db.execute_query("ALTER TABLE old_name RENAME TO new_name")
                  .success);
  EXPECT_FALSE(std::filesystem::exists(dir.path() / "old_name.db"));
  EXPECT_TRUE(std::filesystem::exists(dir.path() / "new_name.db"));
}

}  // namespace
}  // namespace db
