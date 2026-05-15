#include "core/database.h"

#include <atomic>
#include <filesystem>

#include "gtest/gtest.h"

namespace db {
namespace {

namespace fs = std::filesystem;

class TempDbDir {
 public:
  TempDbDir() {
    static std::atomic<uint64_t> seq{0};
    path_ = fs::temp_directory_path() /
            ("custom_sql_db_test_" + std::to_string(++seq));
    fs::create_directories(path_);
  }

  ~TempDbDir() {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }

  const fs::path &path() const { return path_; }
  std::string path_string() const { return path_.string(); }

 private:
  fs::path path_;
};

TEST(DatabaseTest, CrudViaExecuteQuery) {
  TempDbDir tmp;
  Database db(tmp.path_string());

  auto created =
      db.execute_query("CREATE TABLE users (id INT PRIMARY KEY, name STRING)");
  ASSERT_TRUE(created.success) << created.message;

  auto ins = db.execute_query(
      "INSERT INTO users VALUES (1, 'Alice'), (2, 'Bob')");
  ASSERT_TRUE(ins.success) << ins.message;
  EXPECT_EQ(ins.affected_rows, 2);

  auto sel = db.execute_query("SELECT id, name FROM users");
  ASSERT_TRUE(sel.success) << sel.message;
  ASSERT_EQ(sel.rows.size(), 2u);
  ASSERT_GE(sel.column_names.size(), 2u);
  EXPECT_EQ(sel.rows[0][0].as_int(), 1);
  EXPECT_EQ(sel.rows[0][1].as_string(), "Alice");

  auto wildcard = db.execute_query("SELECT * FROM users WHERE id = 2");
  ASSERT_TRUE(wildcard.success) << wildcard.message;
  ASSERT_EQ(wildcard.rows.size(), 1u);
  EXPECT_EQ(wildcard.rows[0][1].as_string(), "Bob");

  auto upd =
      db.execute_query("UPDATE users SET name = 'Robert' WHERE id = 2");
  ASSERT_TRUE(upd.success) << upd.message;
  EXPECT_EQ(upd.affected_rows, 1);

  auto chk = db.execute_query("SELECT name FROM users WHERE id = 2");
  ASSERT_TRUE(chk.success) << chk.message;
  ASSERT_EQ(chk.rows.size(), 1u);
  EXPECT_EQ(chk.rows[0][0].as_string(), "Robert");

  auto del = db.execute_query("DELETE FROM users WHERE id = 1");
  ASSERT_TRUE(del.success) << del.message;
  EXPECT_EQ(del.affected_rows, 1);

  auto left = db.execute_query("SELECT id FROM users");
  ASSERT_TRUE(left.success) << left.message;
  ASSERT_EQ(left.rows.size(), 1u);
  EXPECT_EQ(left.rows[0][0].as_int(), 2);
}

TEST(DatabaseTest, PersistAndReload) {
  TempDbDir tmp;
  {
    Database db(tmp.path_string());
    ASSERT_TRUE(
        db.execute_query(
             "CREATE TABLE items (id INT PRIMARY KEY, qty INT NOT NULL)")
            .success);
    ASSERT_TRUE(db.execute_query("INSERT INTO items VALUES (10, 100)").success);
  }

  Database db2(tmp.path_string());
  db2.load_from_disk();

  auto r = db2.execute_query("SELECT qty FROM items WHERE id = 10");
  ASSERT_TRUE(r.success) << r.message;
  ASSERT_EQ(r.rows.size(), 1u);
  EXPECT_EQ(r.rows[0][0].as_int(), 100);
}

}  // namespace
}  // namespace db
