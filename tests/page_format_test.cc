#include "core/row.h"
#include "storage/page.h"
#include "storage/page_format.h"
#include "types/data_type.h"
#include "types/value.h"

#include "gtest/gtest.h"

namespace db {
namespace {

TEST(PageFormatTest, SerializeDeserializeRoundTrip) {
  Row input_row;
  input_row.add_value(Value(int64_t{42}));
  input_row.add_value(Value(std::string("hello")));
  input_row.set_xmin(7);
  input_row.set_xmax(0);
  const std::vector<uint8_t> bytes = serialize_tuple(input_row);
  const std::vector<DataType> types{DataType::INT, DataType::STRING};
  const Row actual_row =
      deserialize_tuple(bytes.data(), bytes.size(), types);
  EXPECT_EQ(actual_row.get_xmin(), 7u);
  EXPECT_EQ(actual_row.get_xmax(), 0u);
  EXPECT_EQ(actual_row.get_column_count(), 2u);
  EXPECT_EQ(actual_row.get_value(0).as_int(), 42);
  EXPECT_EQ(actual_row.get_value(1).as_string(), "hello");
  EXPECT_EQ(measure_tuple_size(input_row), bytes.size());
}

TEST(PageFormatTest, PackUnpackOnPage) {
  Page page(1);
  Row input_row(std::vector<Value>{Value(int64_t{1}), Value(int64_t{2})});
  input_row.set_xmin(1);
  const std::vector<uint8_t> bytes = serialize_tuple(input_row);
  const uint16_t slot =
      page.insert_tuple(bytes.data(), static_cast<uint16_t>(bytes.size()));
  EXPECT_EQ(slot, 0);
  EXPECT_TRUE(page.is_slot_live(0));
  auto loaded = page.read_tuple(0);
  ASSERT_TRUE(loaded.has_value());
  const std::vector<DataType> types{DataType::INT, DataType::INT};
  const Row actual =
      deserialize_tuple(loaded->data(), loaded->size(), types);
  EXPECT_EQ(actual.get_value(0).as_int(), 1);
  EXPECT_EQ(actual.get_value(1).as_int(), 2);
}

TEST(PageFormatTest, DeleteSlotAndFreeSpace) {
  Page page;
  const size_t free_before = page.get_free_space();
  Row row(std::vector<Value>{Value(int64_t{9})});
  const std::vector<uint8_t> bytes = serialize_tuple(row);
  page.insert_tuple(bytes.data(), static_cast<uint16_t>(bytes.size()));
  EXPECT_LT(page.get_free_space(), free_before);
  page.delete_tuple(0);
  EXPECT_FALSE(page.is_slot_live(0));
  EXPECT_FALSE(page.read_tuple(0).has_value());
}

TEST(PageFormatTest, RejectOversizedTuple) {
  Page page;
  std::vector<uint8_t> huge(kPageSize, 1);
  EXPECT_THROW(
      page.insert_tuple(huge.data(), static_cast<uint16_t>(huge.size())),
      StorageException);
}

}  // namespace
}  // namespace db
