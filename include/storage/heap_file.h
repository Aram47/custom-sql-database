#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "core/row.h"
#include "storage/buffer_pool.h"
#include "storage/item_pointer.h"
#include "storage/page_store.h"
#include "types/data_type.h"

namespace db {

/**
 * Heap of variable-length tuples stored in fixed-size pages via a buffer pool.
 */
class HeapFile {
 public:
  HeapFile(FileId file_id, IBufferPool &buffer_pool, IPageStore &page_store,
           std::vector<DataType> column_types);

  HeapFile(const HeapFile &) = delete;
  HeapFile &operator=(const HeapFile &) = delete;
  HeapFile(HeapFile &&) = delete;
  HeapFile &operator=(HeapFile &&) = delete;

  ~HeapFile();

  FileId get_file_id() const;
  IPageStore &get_page_store();
  const IPageStore &get_page_store() const;
  const std::vector<DataType> &get_column_types() const;
  void set_column_types(std::vector<DataType> column_types);

  /** Inserts a row; allocates a new page if needed. */
  ItemPointer insert_row(const Row &row);

  /** Reads a live tuple; throws if missing. */
  Row get_row(const ItemPointer &pointer) const;

  /** Updates tuple in place when size matches; otherwise delete+insert. */
  ItemPointer update_row(const ItemPointer &pointer, const Row &row);

  /** Marks the slot deleted. */
  void delete_slot(const ItemPointer &pointer);

  /** Invokes visitor(ItemPointer, Row) for every live slot. */
  void scan(const std::function<void(const ItemPointer &, const Row &)>
                &visitor) const;

  /** Ensures at least one page exists. */
  void ensure_initialized();

  /** Drops all pages and recreates an empty heap. */
  void clear();

  /** Replaces underlying pages and resets the insert cursor. */
  void replace_pages(const std::vector<std::vector<uint8_t>> &pages);

 private:
  FileId file_id_;
  IBufferPool &buffer_pool_;
  IPageStore &page_store_;
  std::vector<DataType> column_types_;
  PageId insert_page_id_{0};
  bool has_insert_page_{false};

  PageId allocate_and_init_page();
  ItemPointer insert_into_page(PageId page_id, const std::vector<uint8_t> &bytes);
};

}  // namespace db
