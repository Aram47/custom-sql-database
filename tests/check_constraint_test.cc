#include "core/database.h"

#include "gtest/gtest.h"
#include "tests/test_util.hh"

namespace db {
namespace {

TEST(CheckConstraintTest, CreateAndInsertOk) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query(
                     "CREATE TABLE people (id INT PRIMARY KEY, "
                     "age INT, CHECK (age >= 0))")
                  .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO people VALUES (1, 1)").success);
}

TEST(CheckConstraintTest, InsertViolationFails) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query(
                     "CREATE TABLE people (id INT PRIMARY KEY, "
                     "age INT, CHECK (age >= 0))")
                  .success);
  auto bad = db.execute_query("INSERT INTO people VALUES (1, -1)");
  EXPECT_FALSE(bad.success);
  EXPECT_NE(bad.message.find("CHECK"), std::string::npos);
}

TEST(CheckConstraintTest, UpdateViolationFails) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query(
                     "CREATE TABLE people (id INT PRIMARY KEY, "
                     "age INT, CHECK (age >= 0))")
                  .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO people VALUES (1, 10)").success);
  auto bad = db.execute_query("UPDATE people SET age = -5 WHERE id = 1");
  EXPECT_FALSE(bad.success);
}

TEST(CheckConstraintTest, NullSatisfiesCheck) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query(
                     "CREATE TABLE people (id INT PRIMARY KEY, "
                     "age INT, CHECK (age > 0))")
                  .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO people VALUES (1, NULL)").success);
}

TEST(CheckConstraintTest, AlterAddAndDropCheck) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query(
                     "CREATE TABLE people (id INT PRIMARY KEY, age INT)")
                  .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO people VALUES (1, 5)").success);
  ASSERT_TRUE(db.execute_query(
                     "ALTER TABLE people ADD CONSTRAINT age_pos CHECK (age >= 0)")
                  .success);
  auto bad = db.execute_query("INSERT INTO people VALUES (2, -1)");
  EXPECT_FALSE(bad.success);
  ASSERT_TRUE(db.execute_query("ALTER TABLE people DROP CHECK age_pos").success);
  ASSERT_TRUE(db.execute_query("INSERT INTO people VALUES (2, -1)").success);
}

TEST(CheckConstraintTest, PersistRoundTrip) {
  test_util::TempDbDir tmp;
  {
    Database db(tmp.path_string());
    ASSERT_TRUE(db.execute_query(
                       "CREATE TABLE people (id INT PRIMARY KEY, "
                       "age INT, CONSTRAINT age_nonneg CHECK (age >= 0))")
                    .success);
    ASSERT_TRUE(db.execute_query("INSERT INTO people VALUES (1, 3)").success);
  }
  Database reloaded(tmp.path_string());
  reloaded.load_from_disk();
  ASSERT_TRUE(reloaded.has_table("people"));
  auto bad = reloaded.execute_query("INSERT INTO people VALUES (2, -1)");
  EXPECT_FALSE(bad.success);
  ASSERT_TRUE(reloaded.execute_query("INSERT INTO people VALUES (2, 4)").success);
}

TEST(CheckConstraintTest, RejectSubqueryCheck) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE other (id INT PRIMARY KEY)").success);
  auto bad = db.execute_query(
      "CREATE TABLE people (id INT PRIMARY KEY, age INT, "
      "CHECK (EXISTS (SELECT 1 FROM other)))");
  EXPECT_FALSE(bad.success);
}

TEST(CheckConstraintTest, ColumnLevelCheckSugar) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query(
                     "CREATE TABLE people (id INT PRIMARY KEY, "
                     "age INT CHECK (age >= 0))")
                  .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO people VALUES (1, 0)").success);
  EXPECT_FALSE(db.execute_query("INSERT INTO people VALUES (2, -1)").success);
}

TEST(CheckConstraintTest, AlterAddRejectsExistingViolators) {
  test_util::TempDbDir tmp;
  Database db(tmp.path_string());
  ASSERT_TRUE(db.execute_query(
                     "CREATE TABLE people (id INT PRIMARY KEY, age INT)")
                  .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO people VALUES (1, -1)").success);
  auto bad =
      db.execute_query("ALTER TABLE people ADD CHECK (age >= 0)");
  EXPECT_FALSE(bad.success);
}

}  // namespace
}  // namespace db
