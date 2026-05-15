#include "core/row.h"

#include "gtest/gtest.h"
#include "types/value.h"

namespace db {
namespace {

TEST(RowTest, ConstructSetCompareAndToString) {
  Row r(std::vector<Value>{Value(int64_t{1}), Value(std::string("z"))});
  ASSERT_EQ(r.get_column_count(), 2u);
  EXPECT_EQ(r.get_value(0).as_int(), 1);
  EXPECT_EQ(r.get_value(1).as_string(), "z");

  Row copy = r;
  EXPECT_TRUE(r == copy);
  EXPECT_FALSE(r != copy);

  copy.set_value(0, Value(int64_t{2}));
  EXPECT_FALSE(r == copy);

  EXPECT_THROW(static_cast<void>(r.set_value(5, Value{})), std::out_of_range);
  EXPECT_NE(r.to_string().find("Row("), std::string::npos);
  EXPECT_NE(r.to_string().find("1"), std::string::npos);
  EXPECT_NE(r.to_string().find("z"), std::string::npos);
}

TEST(RowTest, EmptyRow) {
  Row r;
  EXPECT_TRUE(r.is_empty());
  EXPECT_EQ(r.get_column_count(), 0u);
}

}  // namespace
}  // namespace db
