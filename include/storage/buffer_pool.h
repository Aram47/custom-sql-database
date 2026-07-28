#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "storage/item_pointer.h"
#include "storage/page.h"
#include "storage/page_store.h"

namespace db {

/** Buffer pool interface (DIP for HeapFile / tests). */
class IBufferPool {
 public:
  virtual ~IBufferPool() = default;

  /** Pins a page into a frame; caller must unpin. */
  virtual Page &pin(FileId file_id, PageId page_id) = 0;

  /** Releases a pin; marks dirty if is_dirty. */
  virtual void unpin(FileId file_id, PageId page_id, bool is_dirty) = 0;

  /** Writes all dirty frames for all files. */
  virtual void flush_all() = 0;

  /** Writes dirty frames belonging to file_id. */
  virtual void flush_file(FileId file_id) = 0;

  /** Registers a page store for file_id. */
  virtual void register_store(FileId file_id, IPageStore *store) = 0;

  /** Unregisters store and flushes/evicts its frames. */
  virtual void unregister_store(FileId file_id) = 0;

  virtual size_t get_frame_count() const = 0;
};

/**
 * Fixed-frame buffer pool with clock eviction and write-back of dirty pages.
 */
class BufferPool : public IBufferPool {
 public:
  explicit BufferPool(size_t frame_count);

  BufferPool(const BufferPool &) = delete;
  BufferPool &operator=(const BufferPool &) = delete;
  BufferPool(BufferPool &&) = delete;
  BufferPool &operator=(BufferPool &&) = delete;

  ~BufferPool() override;

  Page &pin(FileId file_id, PageId page_id) override;
  void unpin(FileId file_id, PageId page_id, bool is_dirty) override;
  void flush_all() override;
  void flush_file(FileId file_id) override;
  void register_store(FileId file_id, IPageStore *store) override;
  void unregister_store(FileId file_id) override;
  size_t get_frame_count() const override;

 private:
  struct Frame {
    std::unique_ptr<Page> page;
    FileId file_id{0};
    PageId page_id{0};
    int pin_count{0};
    bool is_dirty{false};
    bool is_referenced{false};
    bool is_occupied{false};
  };

  struct PageKey {
    FileId file_id;
    PageId page_id;

    bool operator==(const PageKey &other) const {
      return file_id == other.file_id && page_id == other.page_id;
    }
  };

  struct PageKeyHash {
    size_t operator()(const PageKey &key) const {
      return (static_cast<size_t>(key.file_id) << 32) ^
             static_cast<size_t>(key.page_id);
    }
  };

  size_t frame_count_;
  std::vector<Frame> frames_;
  size_t clock_hand_{0};
  std::unordered_map<PageKey, size_t, PageKeyHash> page_table_;
  std::unordered_map<FileId, IPageStore *> stores_;
  mutable std::mutex mutex_;

  size_t find_victim_frame();
  void evict_frame(size_t frame_index);
  void write_frame_if_dirty(Frame &frame);
  IPageStore *require_store(FileId file_id);
};

}  // namespace db
