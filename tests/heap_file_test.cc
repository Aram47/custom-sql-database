#include "core/row.h"
#include "storage/buffer_pool.h"
#include "storage/heap_file.h"
#include "storage/page_store.h"
#include "types/data_type.h"
#include "types/value.h"

#include "gtest/gtest.h"

namespace db {
namespace {

class HeapFileTest : public ::testing::Test {
 protected:
  void SetUp() override {
    store_ = std::make_unique<MemoryPageStore>();
    pool_ = std::make_unique<BufferPool>(8);
    heap_ = std::make_unique<HeapFile>(
        1, *pool_, *store_, std::vector<DataType>{DataType::INT, DataType::STRING});
  }

  std::unique_ptr<MemoryPageStore> store_;
  std::unique_ptr<BufferPool> pool_;
  std::unique_ptr<HeapFile> heap_;
};

TEST_F(HeapFileTest, InsertScanDelete) {
  Row input_row(std::vector<Value>{Value(int64_t{10}), Value(std::string("a"))});
  input_row.set_xmin(1);
  const ItemPointer ptr = heap_->insert_row(input_row);
  const Row loaded = heap_->get_row(ptr);
  EXPECT_EQ(loaded.get_value(0).as_int(), 10);
  EXPECT_EQ(loaded.get_value(1).as_string(), "a");
  EXPECT_EQ(loaded.get_xmin(), 1u);
  size_t count = 0;
  heap_->scan([&](const ItemPointer &, const Row &row) {
    ++count;
    EXPECT_EQ(row.get_value(0).as_int(), 10);
  });
  EXPECT_EQ(count, 1u);
  heap_->delete_slot(ptr);
  count = 0;
  heap_->scan([&](const ItemPointer &, const Row &) { ++count; });
  EXPECT_EQ(count, 0u);
}

TEST_F(HeapFileTest, UpdateInPlaceAndOverflow) {
  Row input_row(std::vector<Value>{Value(int64_t{1}), Value(std::string("x"))});
  const ItemPointer ptr = heap_->insert_row(input_row);
  Row updated(std::vector<Value>{Value(int64_t{2}), Value(std::string("x"))});
  updated.set_xmin(3);
  const ItemPointer same = heap_->update_row(ptr, updated);
  EXPECT_EQ(same.page_id, ptr.page_id);
  EXPECT_EQ(same.slot, ptr.slot);
  EXPECT_EQ(heap_->get_row(same).get_value(0).as_int(), 2);
  Row longer(std::vector<Value>{Value(int64_t{3}),
                                Value(std::string("much-longer-value"))});
  const ItemPointer moved = heap_->update_row(same, longer);
  EXPECT_EQ(heap_->get_row(moved).get_value(1).as_string(),
            "much-longer-value");
}

TEST_F(HeapFileTest, AllocatesNewPageWhenFull) {
  size_t inserted = 0;
  while (store_->page_count() < 2) {
    Row row(std::vector<Value>{
        Value(static_cast<int64_t>(inserted)),
        Value(std::string(200, 'z'))});
    heap_->insert_row(row);
    ++inserted;
    ASSERT_LT(inserted, 1000u);
  }
  size_t count = 0;
  heap_->scan([&](const ItemPointer &, const Row &) { ++count; });
  EXPECT_EQ(count, inserted);
}

}  // namespace
}  // namespace db
