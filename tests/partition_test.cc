#include "core/database.h"

#include <filesystem>
#include <string>

#include "gtest/gtest.h"
#include "tests/test_util.hh"

namespace db {
namespace {

TEST(PartitionTest, RangeInsertRoutesToCorrectChild) {
  test_util::TempDbDir dir;
  Database db(dir.path_string());
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE sales (id INT, amount INT, y INT) "
                      "PARTITION BY RANGE (y)")
                  .success);
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE sales_2024 PARTITION OF sales "
                      "FOR VALUES FROM (2024) TO (2025)")
                  .success);
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE sales_2025 PARTITION OF sales "
                      "FOR VALUES FROM (2025) TO (2026)")
                  .success);
  ASSERT_TRUE(
      db.execute_query("INSERT INTO sales VALUES (1, 10, 2024)").success);
  ASSERT_TRUE(
      db.execute_query("INSERT INTO sales VALUES (2, 20, 2025)").success);
  Table *child2024 = db.get_table("sales_2024");
  Table *child2025 = db.get_table("sales_2025");
  ASSERT_NE(child2024, nullptr);
  ASSERT_NE(child2025, nullptr);
  EXPECT_EQ(child2024->get_row_count(), 1u);
  EXPECT_EQ(child2025->get_row_count(), 1u);
  EXPECT_TRUE(std::filesystem::exists(dir.path() / "sales_2024.db"));
  EXPECT_TRUE(std::filesystem::exists(dir.path() / "sales_2025.db"));
}

TEST(PartitionTest, RangeInsertOutOfRangeErrors) {
  test_util::TempDbDir dir;
  Database db(dir.path_string());
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE sales (id INT, y INT) PARTITION BY RANGE (y)")
                  .success);
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE sales_2024 PARTITION OF sales "
                      "FOR VALUES FROM (2024) TO (2025)")
                  .success);
  auto result = db.execute_query("INSERT INTO sales VALUES (1, 2023)");
  EXPECT_FALSE(result.success);
  EXPECT_NE(result.message.find("No partition"), std::string::npos);
}

TEST(PartitionTest, SelectEqualityPrunesOnePartitionInExplain) {
  test_util::TempDbDir dir;
  Database db(dir.path_string());
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE sales (id INT, y INT) PARTITION BY RANGE (y)")
                  .success);
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE sales_2024 PARTITION OF sales "
                      "FOR VALUES FROM (2024) TO (2025)")
                  .success);
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE sales_2025 PARTITION OF sales "
                      "FOR VALUES FROM (2025) TO (2026)")
                  .success);
  ASSERT_TRUE(
      db.execute_query("INSERT INTO sales VALUES (1, 2024)").success);
  ASSERT_TRUE(
      db.execute_query("INSERT INTO sales VALUES (2, 2025)").success);
  auto explain =
      db.execute_query("EXPLAIN SELECT * FROM sales WHERE y = 2024");
  ASSERT_TRUE(explain.success) << explain.message;
  ASSERT_FALSE(explain.rows.empty());
  std::string plan;
  for (const auto &row : explain.rows) {
    if (!row.empty()) {
      plan += row[0].to_string();
      plan += "\n";
    }
  }
  EXPECT_NE(plan.find("PartitionPrune on sales -> 1 of 2 partitions"),
            std::string::npos)
      << plan;
  EXPECT_NE(plan.find("sales_2024"), std::string::npos) << plan;
  EXPECT_EQ(plan.find("sales_2025"), std::string::npos) << plan;
  auto select = db.execute_query("SELECT id FROM sales WHERE y = 2024");
  ASSERT_TRUE(select.success) << select.message;
  ASSERT_EQ(select.rows.size(), 1u);
  EXPECT_EQ(select.rows[0][0].as_int(), 1);
}

TEST(PartitionTest, SelectFullScansAllPartitions) {
  test_util::TempDbDir dir;
  Database db(dir.path_string());
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE sales (id INT, y INT) PARTITION BY RANGE (y)")
                  .success);
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE sales_2024 PARTITION OF sales "
                      "FOR VALUES FROM (2024) TO (2025)")
                  .success);
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE sales_2025 PARTITION OF sales "
                      "FOR VALUES FROM (2025) TO (2026)")
                  .success);
  ASSERT_TRUE(
      db.execute_query("INSERT INTO sales VALUES (1, 2024)").success);
  ASSERT_TRUE(
      db.execute_query("INSERT INTO sales VALUES (2, 2025)").success);
  auto explain = db.execute_query("EXPLAIN SELECT * FROM sales");
  ASSERT_TRUE(explain.success) << explain.message;
  std::string plan;
  for (const auto &row : explain.rows) {
    if (!row.empty()) {
      plan += row[0].to_string();
      plan += "\n";
    }
  }
  EXPECT_NE(plan.find("PartitionPrune on sales -> 2 of 2 partitions"),
            std::string::npos)
      << plan;
  auto select = db.execute_query("SELECT id FROM sales");
  ASSERT_TRUE(select.success) << select.message;
  EXPECT_EQ(select.rows.size(), 2u);
}

TEST(PartitionTest, HashRoutingStableAcrossRestart) {
  test_util::TempDbDir dir;
  {
    Database db(dir.path_string());
    ASSERT_TRUE(db.execute_query(
                        "CREATE TABLE sales_h (id INT, name STRING) "
                        "PARTITION BY HASH (id)")
                    .success);
    ASSERT_TRUE(db.execute_query(
                        "CREATE TABLE sales_h0 PARTITION OF sales_h "
                        "FOR VALUES WITH (MODULUS 4, REMAINDER 0)")
                    .success);
    ASSERT_TRUE(db.execute_query(
                        "CREATE TABLE sales_h1 PARTITION OF sales_h "
                        "FOR VALUES WITH (MODULUS 4, REMAINDER 1)")
                    .success);
    ASSERT_TRUE(db.execute_query(
                        "CREATE TABLE sales_h2 PARTITION OF sales_h "
                        "FOR VALUES WITH (MODULUS 4, REMAINDER 2)")
                    .success);
    ASSERT_TRUE(db.execute_query(
                        "CREATE TABLE sales_h3 PARTITION OF sales_h "
                        "FOR VALUES WITH (MODULUS 4, REMAINDER 3)")
                    .success);
    ASSERT_TRUE(
        db.execute_query("INSERT INTO sales_h VALUES (42, 'a')").success);
    ASSERT_TRUE(
        db.execute_query("INSERT INTO sales_h VALUES (7, 'b')").success);
  }
  Database reloaded(dir.path_string());
  reloaded.load_from_disk();
  Table *parent = reloaded.get_table("sales_h");
  ASSERT_NE(parent, nullptr);
  ASSERT_TRUE(parent->isPartitioned());
  auto select = reloaded.execute_query("SELECT id, name FROM sales_h");
  ASSERT_TRUE(select.success) << select.message;
  EXPECT_EQ(select.rows.size(), 2u);
  std::string childFor42;
  const char *childNames[] = {"sales_h0", "sales_h1", "sales_h2", "sales_h3"};
  for (const char *name : childNames) {
    Table *child = reloaded.get_table(name);
    ASSERT_NE(child, nullptr);
    for (size_t i = 0; i < child->get_row_count(); ++i) {
      if (child->get_row(i).get_value(0).as_int() == 42) {
        childFor42 = name;
      }
    }
  }
  ASSERT_FALSE(childFor42.empty());
  ASSERT_TRUE(
      reloaded.execute_query("INSERT INTO sales_h VALUES (42, 'c')").success);
  Table *sameChild = reloaded.get_table(childFor42);
  ASSERT_NE(sameChild, nullptr);
  size_t count42 = 0;
  for (size_t i = 0; i < sameChild->get_row_count(); ++i) {
    if (sameChild->get_row(i).get_value(0).as_int() == 42) {
      ++count42;
    }
  }
  EXPECT_EQ(count42, 2u);
}

TEST(PartitionTest, DropPartitionKeepsParent) {
  test_util::TempDbDir dir;
  Database db(dir.path_string());
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE sales (id INT, y INT) PARTITION BY RANGE (y)")
                  .success);
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE sales_2024 PARTITION OF sales "
                      "FOR VALUES FROM (2024) TO (2025)")
                  .success);
  ASSERT_TRUE(
      db.execute_query("INSERT INTO sales VALUES (1, 2024)").success);
  ASSERT_TRUE(db.execute_query("DROP TABLE sales_2024").success);
  EXPECT_FALSE(db.has_table("sales_2024"));
  EXPECT_TRUE(db.has_table("sales"));
  EXPECT_FALSE(std::filesystem::exists(dir.path() / "sales_2024.db"));
  auto insert = db.execute_query("INSERT INTO sales VALUES (2, 2024)");
  EXPECT_FALSE(insert.success);
}

TEST(PartitionTest, DropParentCascadesChildren) {
  test_util::TempDbDir dir;
  Database db(dir.path_string());
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE sales (id INT, y INT) PARTITION BY RANGE (y)")
                  .success);
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE sales_2024 PARTITION OF sales "
                      "FOR VALUES FROM (2024) TO (2025)")
                  .success);
  ASSERT_TRUE(db.execute_query(
                      "CREATE TABLE sales_2025 PARTITION OF sales "
                      "FOR VALUES FROM (2025) TO (2026)")
                  .success);
  ASSERT_TRUE(db.execute_query("DROP TABLE sales").success);
  EXPECT_FALSE(db.has_table("sales"));
  EXPECT_FALSE(db.has_table("sales_2024"));
  EXPECT_FALSE(db.has_table("sales_2025"));
  EXPECT_FALSE(std::filesystem::exists(dir.path() / "sales.db"));
  EXPECT_FALSE(std::filesystem::exists(dir.path() / "sales_2024.db"));
  EXPECT_FALSE(std::filesystem::exists(
      dir.path() / "_partitions" / "sales.part"));
}

}  // namespace
}  // namespace db
