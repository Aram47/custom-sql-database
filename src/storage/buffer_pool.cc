#include "storage/buffer_pool.h"

#include <cstring>

namespace db {

BufferPool::BufferPool(size_t frame_count) : frame_count_(frame_count) {
  if (frame_count_ == 0) {
    throw StorageException("Buffer pool must have at least one frame");
  }
  frames_.resize(frame_count_);
  for (Frame &frame : frames_) {
    frame.page = std::make_unique<Page>(0);
  }
}

BufferPool::~BufferPool() {
  try {
    flush_all();
  } catch (...) {
  }
}

size_t BufferPool::get_frame_count() const { return frame_count_; }

void BufferPool::register_store(FileId file_id, IPageStore *store) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (store == nullptr) {
    throw StorageException("Null page store");
  }
  stores_[file_id] = store;
}

void BufferPool::unregister_store(FileId file_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (size_t i = 0; i < frames_.size(); ++i) {
    Frame &frame = frames_[i];
    if (!frame.is_occupied || frame.file_id != file_id) {
      continue;
    }
    if (frame.pin_count > 0) {
      throw StorageException("Cannot unregister store with pinned pages");
    }
    write_frame_if_dirty(frame);
    page_table_.erase(PageKey{frame.file_id, frame.page_id});
    frame.is_occupied = false;
    frame.is_dirty = false;
    frame.is_referenced = false;
  }
  stores_.erase(file_id);
}

IPageStore *BufferPool::require_store(FileId file_id) {
  auto it = stores_.find(file_id);
  if (it == stores_.end() || it->second == nullptr) {
    throw StorageException("No page store registered for file");
  }
  return it->second;
}

void BufferPool::write_frame_if_dirty(Frame &frame) {
  if (!frame.is_occupied || !frame.is_dirty) {
    return;
  }
  IPageStore *store = require_store(frame.file_id);
  store->write_page(frame.page_id, frame.page->data());
  frame.is_dirty = false;
}

void BufferPool::evict_frame(size_t frame_index) {
  Frame &frame = frames_[frame_index];
  if (!frame.is_occupied) {
    return;
  }
  write_frame_if_dirty(frame);
  page_table_.erase(PageKey{frame.file_id, frame.page_id});
  frame.is_occupied = false;
  frame.pin_count = 0;
  frame.is_referenced = false;
}

size_t BufferPool::find_victim_frame() {
  size_t examined = 0;
  const size_t limit = frame_count_ * 2;
  while (examined < limit) {
    Frame &frame = frames_[clock_hand_];
    const size_t victim = clock_hand_;
    clock_hand_ = (clock_hand_ + 1) % frame_count_;
    ++examined;
    if (!frame.is_occupied) {
      return victim;
    }
    if (frame.pin_count > 0) {
      continue;
    }
    if (frame.is_referenced) {
      frame.is_referenced = false;
      continue;
    }
    return victim;
  }
  throw StorageException("No free buffer frame available (all pinned)");
}

Page &BufferPool::pin(FileId file_id, PageId page_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const PageKey key{file_id, page_id};
  auto it = page_table_.find(key);
  if (it != page_table_.end()) {
    Frame &frame = frames_[it->second];
    frame.pin_count += 1;
    frame.is_referenced = true;
    return *frame.page;
  }
  const size_t frame_index = find_victim_frame();
  evict_frame(frame_index);
  Frame &frame = frames_[frame_index];
  IPageStore *store = require_store(file_id);
  std::vector<uint8_t> raw(kPageSize);
  store->read_page(page_id, raw.data());
  frame.page = std::make_unique<Page>(Page::from_bytes(raw.data()));
  frame.file_id = file_id;
  frame.page_id = page_id;
  frame.pin_count = 1;
  frame.is_dirty = false;
  frame.is_referenced = true;
  frame.is_occupied = true;
  page_table_[key] = frame_index;
  return *frame.page;
}

void BufferPool::unpin(FileId file_id, PageId page_id, bool is_dirty) {
  std::lock_guard<std::mutex> lock(mutex_);
  const PageKey key{file_id, page_id};
  auto it = page_table_.find(key);
  if (it == page_table_.end()) {
    throw StorageException("Unpin of page not in pool");
  }
  Frame &frame = frames_[it->second];
  if (frame.pin_count <= 0) {
    throw StorageException("Unpin with zero pin count");
  }
  frame.pin_count -= 1;
  if (is_dirty) {
    frame.is_dirty = true;
  }
}

void BufferPool::flush_all() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (Frame &frame : frames_) {
    write_frame_if_dirty(frame);
  }
}

void BufferPool::flush_file(FileId file_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (Frame &frame : frames_) {
    if (frame.is_occupied && frame.file_id == file_id) {
      write_frame_if_dirty(frame);
    }
  }
}

}  // namespace db
