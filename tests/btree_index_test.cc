#include "core/btree_index.h"
#include "core/database.h"
#include "core/index_key.h"
#include "core/table.h"

#include "gtest/gtest.h"
#include "tests/test_util.hh"
#include "types/value.h"

namespace db {
namespace {

TEST(BTreeIndexTest, EqualAndRangeLookups) {
  BTreeIndex index;
  index.insert(Value(static_cast<int64_t>(10)), 0);
  index.insert(Value(static_cast<int64_t>(20)), 1);
  index.insert(Value(static_cast<int64_t>(30)), 2);
  index.insert(Value(static_cast<int64_t>(20)), 3);
  auto equal = index.find_equal(Value(static_cast<int64_t>(20)));
  ASSERT_EQ(equal.size(), 2u);
  auto range = index.find_range(Value(static_cast<int64_t>(10)), true,
                                Value(static_cast<int64_t>(20)), true);
  EXPECT_GE(range.size(), 2u);
  index.remove(Value(static_cast<int64_t>(20)), 1);
  equal = index.find_equal(Value(static_cast<int64_t>(20)));
  ASSERT_EQ(equal.size(), 1u);
  EXPECT_EQ(equal[0], 3u);
}

TEST(BTreeIndexTest, TableBuildsIndexOnPrimaryKey) {
  Table table("t");
  table.add_column(Column("id", DataType::INT, false, true, false));
  table.add_column(Column("v", DataType::INT, true, false, false));
  Row r1;
  r1.add_value(Value(static_cast<int64_t>(1)));
  r1.add_value(Value(static_cast<int64_t>(100)));
  table.insert_row(r1);
  Row r2;
  r2.add_value(Value(static_cast<int64_t>(2)));
  r2.add_value(Value(static_cast<int64_t>(200)));
  table.insert_row(r2);
  EXPECT_TRUE(table.has_index("id"));
  auto found = table.find_rows_by_value("id", Value(static_cast<int64_t>(2)));
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0], 1u);
}

TEST(BTreeIndexTest, SelectUsesIndexEqualityAndBetween) {
  test_util::TempDbDir dir;
  Database db(dir.path_string());
  ASSERT_TRUE(
      db.execute_query("CREATE TABLE nums (id INT PRIMARY KEY, n INT)")
          .success);
  for (int i = 1; i <= 20; ++i) {
    ASSERT_TRUE(db.execute_query("INSERT INTO nums VALUES (" +
                                 std::to_string(i) + ", " +
                                 std::to_string(i * 10) + ")")
                    .success);
  }
  auto eq = db.execute_query("SELECT id FROM nums WHERE id = 7");
  ASSERT_TRUE(eq.success) << eq.message;
  ASSERT_EQ(eq.rows.size(), 1u);
  EXPECT_EQ(eq.rows[0][0].as_int(), 7);
  auto between =
      db.execute_query("SELECT id FROM nums WHERE id BETWEEN 5 AND 7");
  ASSERT_TRUE(between.success) << between.message;
  EXPECT_EQ(between.rows.size(), 3u);
}

TEST(BTreeIndexTest, EquiJoinUsesRightIndex) {
  test_util::TempDbDir dir;
  Database db(dir.path_string());
  ASSERT_TRUE(
      db.execute_query(
            "CREATE TABLE orders (id INT PRIMARY KEY, user_id INT NOT NULL)")
          .success);
  ASSERT_TRUE(
      db.execute_query(
            "CREATE TABLE customers (id INT PRIMARY KEY, name STRING NOT NULL)")
          .success);
  ASSERT_TRUE(db.execute_query("INSERT INTO orders VALUES (1, 10)").success);
  ASSERT_TRUE(
      db.execute_query("INSERT INTO customers VALUES (10, 'Carol')").success);
  auto result = db.execute_query(
      "SELECT orders.id, customers.name FROM orders INNER JOIN customers ON "
      "orders.user_id = customers.id");
  ASSERT_TRUE(result.success) << result.message;
  ASSERT_EQ(result.rows.size(), 1u);
  EXPECT_EQ(result.rows[0][1].as_string(), "Carol");
}

TEST(BTreeIndexTest, CompositeKeyLookups) {
  BTreeIndex index;
  index.insert(IndexKey({Value(static_cast<int64_t>(1)),
                         Value(static_cast<int64_t>(10))}),
               0);
  index.insert(IndexKey({Value(static_cast<int64_t>(1)),
                         Value(static_cast<int64_t>(20))}),
               1);
  index.insert(IndexKey({Value(static_cast<int64_t>(2)),
                         Value(static_cast<int64_t>(10))}),
               2);
  auto equal = index.find_equal(IndexKey(
      {Value(static_cast<int64_t>(1)), Value(static_cast<int64_t>(20))}));
  ASSERT_EQ(equal.size(), 1u);
  EXPECT_EQ(equal[0], 1u);
  auto prefix = index.find_prefix(IndexKey(Value(static_cast<int64_t>(1))));
  EXPECT_EQ(prefix.size(), 2u);
}

}  // namespace
}  // namespace db
