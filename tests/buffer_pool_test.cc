#include "core/row.h"
#include "storage/buffer_pool.h"
#include "storage/page_format.h"
#include "storage/page_store.h"
#include "types/value.h"

#include "gtest/gtest.h"

namespace db {
namespace {

TEST(BufferPoolTest, PinUnpinAndDirtyFlush) {
  MemoryPageStore store;
  const PageId page_id = store.allocate_page();
  Page seed(page_id);
  store.write_page(page_id, seed.data());
  BufferPool pool(2);
  pool.register_store(1, &store);
  Page &pinned = pool.pin(1, page_id);
  EXPECT_EQ(pinned.get_page_id(), page_id);
  Row row(std::vector<Value>{Value(int64_t{5})});
  const std::vector<uint8_t> bytes = serialize_tuple(row);
  pinned.insert_tuple(bytes.data(), static_cast<uint16_t>(bytes.size()));
  pool.unpin(1, page_id, true);
  pool.flush_all();
  std::vector<uint8_t> raw(kPageSize);
  store.read_page(page_id, raw.data());
  EXPECT_TRUE(Page::from_bytes(raw.data()).is_slot_live(0));
}

TEST(BufferPoolTest, EvictsUnpinnedFrames) {
  MemoryPageStore store;
  BufferPool pool(2);
  pool.register_store(1, &store);
  const PageId p0 = store.allocate_page();
  const PageId p1 = store.allocate_page();
  const PageId p2 = store.allocate_page();
  Page a(p0);
  Page b(p1);
  Page c(p2);
  store.write_page(p0, a.data());
  store.write_page(p1, b.data());
  store.write_page(p2, c.data());
  pool.pin(1, p0);
  pool.unpin(1, p0, false);
  pool.pin(1, p1);
  pool.unpin(1, p1, false);
  Page &third = pool.pin(1, p2);
  EXPECT_EQ(third.get_page_id(), p2);
  pool.unpin(1, p2, false);
}

TEST(BufferPoolTest, RejectsPinWhenAllPinned) {
  MemoryPageStore store;
  BufferPool pool(1);
  pool.register_store(1, &store);
  const PageId p0 = store.allocate_page();
  const PageId p1 = store.allocate_page();
  Page a(p0);
  Page b(p1);
  store.write_page(p0, a.data());
  store.write_page(p1, b.data());
  pool.pin(1, p0);
  EXPECT_THROW(pool.pin(1, p1), StorageException);
  pool.unpin(1, p0, false);
}

}  // namespace
}  // namespace db
