#include "storage/heap_file.h"

#include <optional>
#include <vector>

#include "storage/page_format.h"
#include "utils/exceptions.h"

namespace db {

HeapFile::HeapFile(FileId file_id, IBufferPool &buffer_pool,
                   IPageStore &page_store, std::vector<DataType> column_types)
    : file_id_(file_id),
      buffer_pool_(buffer_pool),
      page_store_(page_store),
      column_types_(std::move(column_types)) {
  buffer_pool_.register_store(file_id_, &page_store_);
  if (page_store_.page_count() > 0) {
    insert_page_id_ = page_store_.page_count() - 1;
    has_insert_page_ = true;
  }
}

HeapFile::~HeapFile() {
  try {
    buffer_pool_.flush_file(file_id_);
    buffer_pool_.unregister_store(file_id_);
  } catch (...) {
  }
}

FileId HeapFile::get_file_id() const { return file_id_; }

IPageStore &HeapFile::get_page_store() { return page_store_; }

const IPageStore &HeapFile::get_page_store() const { return page_store_; }

const std::vector<DataType> &HeapFile::get_column_types() const {
  return column_types_;
}

void HeapFile::set_column_types(std::vector<DataType> column_types) {
  column_types_ = std::move(column_types);
}

void HeapFile::ensure_initialized() {
  if (!has_insert_page_) {
    insert_page_id_ = allocate_and_init_page();
    has_insert_page_ = true;
  }
}

PageId HeapFile::allocate_and_init_page() {
  const PageId page_id = page_store_.allocate_page();
  Page blank(page_id);
  page_store_.write_page(page_id, blank.data());
  return page_id;
}

ItemPointer HeapFile::insert_into_page(PageId page_id,
                                       const std::vector<uint8_t> &bytes) {
  Page &page = buffer_pool_.pin(file_id_, page_id);
  try {
    const uint16_t slot =
        page.insert_tuple(bytes.data(), static_cast<uint16_t>(bytes.size()));
    buffer_pool_.unpin(file_id_, page_id, true);
    return ItemPointer{page_id, slot};
  } catch (...) {
    buffer_pool_.unpin(file_id_, page_id, false);
    throw;
  }
}

ItemPointer HeapFile::insert_row(const Row &row) {
  const std::vector<uint8_t> bytes = serialize_tuple(row);
  if (bytes.size() + sizeof(SlotEntry) + sizeof(PageHeader) > kPageSize) {
    throw StorageException("Row does not fit in a single page");
  }
  ensure_initialized();
  try {
    return insert_into_page(insert_page_id_, bytes);
  } catch (const StorageException &) {
    insert_page_id_ = allocate_and_init_page();
    return insert_into_page(insert_page_id_, bytes);
  }
}

Row HeapFile::get_row(const ItemPointer &pointer) const {
  IBufferPool &pool = const_cast<IBufferPool &>(buffer_pool_);
  Page &page = pool.pin(file_id_, pointer.page_id);
  std::optional<std::vector<uint8_t>> bytes;
  try {
    bytes = page.read_tuple(pointer.slot);
  } catch (...) {
    pool.unpin(file_id_, pointer.page_id, false);
    throw;
  }
  pool.unpin(file_id_, pointer.page_id, false);
  if (!bytes.has_value()) {
    throw StorageException("Tuple slot is empty");
  }
  return deserialize_tuple(bytes->data(), bytes->size(), column_types_);
}

ItemPointer HeapFile::update_row(const ItemPointer &pointer, const Row &row) {
  const std::vector<uint8_t> bytes = serialize_tuple(row);
  Page &page = buffer_pool_.pin(file_id_, pointer.page_id);
  bool updated = false;
  try {
    updated = page.update_tuple_in_place(
        pointer.slot, bytes.data(), static_cast<uint16_t>(bytes.size()));
    buffer_pool_.unpin(file_id_, pointer.page_id, updated);
  } catch (...) {
    buffer_pool_.unpin(file_id_, pointer.page_id, false);
    throw;
  }
  if (updated) {
    return pointer;
  }
  delete_slot(pointer);
  return insert_row(row);
}

void HeapFile::delete_slot(const ItemPointer &pointer) {
  Page &page = buffer_pool_.pin(file_id_, pointer.page_id);
  try {
    page.delete_tuple(pointer.slot);
    buffer_pool_.unpin(file_id_, pointer.page_id, true);
  } catch (...) {
    buffer_pool_.unpin(file_id_, pointer.page_id, false);
    throw;
  }
}

void HeapFile::scan(
    const std::function<void(const ItemPointer &, const Row &)> &visitor)
    const {
  IBufferPool &pool = const_cast<IBufferPool &>(buffer_pool_);
  const uint32_t count = page_store_.page_count();
  for (PageId page_id = 0; page_id < count; ++page_id) {
    Page &page = pool.pin(file_id_, page_id);
    try {
      const uint16_t slots = page.get_slot_count();
      for (uint16_t slot = 0; slot < slots; ++slot) {
        if (!page.is_slot_live(slot)) {
          continue;
        }
        auto bytes = page.read_tuple(slot);
        if (!bytes.has_value()) {
          continue;
        }
        const Row row =
            deserialize_tuple(bytes->data(), bytes->size(), column_types_);
        visitor(ItemPointer{page_id, slot}, row);
      }
      pool.unpin(file_id_, page_id, false);
    } catch (...) {
      pool.unpin(file_id_, page_id, false);
      throw;
    }
  }
}

void HeapFile::clear() {
  replace_pages({});
}

void HeapFile::replace_pages(const std::vector<std::vector<uint8_t>> &pages) {
  buffer_pool_.unregister_store(file_id_);
  page_store_.replace_pages(pages);
  buffer_pool_.register_store(file_id_, &page_store_);
  if (pages.empty()) {
    has_insert_page_ = false;
    insert_page_id_ = 0;
    return;
  }
  insert_page_id_ = static_cast<PageId>(pages.size() - 1);
  has_insert_page_ = true;
}

}  // namespace db
