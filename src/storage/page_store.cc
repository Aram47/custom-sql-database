#include "storage/page_store.h"

#include <cstring>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace db {

void MemoryPageStore::read_page(PageId page_id, uint8_t *out_bytes) {
  if (out_bytes == nullptr) {
    throw StorageException("Null read buffer");
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (page_id >= pages_.size()) {
    throw StorageException("Page id out of range");
  }
  std::memcpy(out_bytes, pages_[page_id].data(), kPageSize);
}

void MemoryPageStore::write_page(PageId page_id, const uint8_t *bytes) {
  if (bytes == nullptr) {
    throw StorageException("Null write buffer");
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (page_id >= pages_.size()) {
    throw StorageException("Page id out of range");
  }
  std::memcpy(pages_[page_id].data(), bytes, kPageSize);
}

PageId MemoryPageStore::allocate_page() {
  std::lock_guard<std::mutex> lock(mutex_);
  const PageId id = static_cast<PageId>(pages_.size());
  pages_.emplace_back(kPageSize, 0);
  return id;
}

uint32_t MemoryPageStore::page_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<uint32_t>(pages_.size());
}

void MemoryPageStore::replace_pages(
    const std::vector<std::vector<uint8_t>> &pages) {
  std::lock_guard<std::mutex> lock(mutex_);
  pages_.clear();
  pages_.reserve(pages.size());
  for (const auto &page : pages) {
    if (page.size() != kPageSize) {
      throw StorageException("Invalid page size in replace_pages");
    }
    pages_.push_back(page);
  }
}

std::vector<std::vector<uint8_t>> MemoryPageStore::export_pages() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pages_;
}

FilePageStore::FilePageStore(std::string path, uint64_t pages_offset,
                             uint32_t page_count)
    : path_(std::move(path)),
      pages_offset_(pages_offset),
      page_count_(page_count) {}

void FilePageStore::ensure_file_size(uint32_t min_pages) {
  const uint64_t needed =
      pages_offset_ + static_cast<uint64_t>(min_pages) * kPageSize;
  fs::path parent = fs::path(path_).parent_path();
  if (!parent.empty()) {
    fs::create_directories(parent);
  }
  std::fstream file(path_, std::ios::binary | std::ios::in | std::ios::out);
  if (!file) {
    std::ofstream create(path_, std::ios::binary | std::ios::trunc);
    if (!create) {
      throw StorageException("Cannot create page file: " + path_);
    }
    create.close();
    file.open(path_, std::ios::binary | std::ios::in | std::ios::out);
  }
  if (!file) {
    throw StorageException("Cannot open page file: " + path_);
  }
  file.seekg(0, std::ios::end);
  const auto current = static_cast<uint64_t>(file.tellg());
  if (current < needed) {
    file.seekp(static_cast<std::streamoff>(needed - 1));
    char zero = 0;
    file.write(&zero, 1);
  }
}

void FilePageStore::read_page(PageId page_id, uint8_t *out_bytes) {
  if (out_bytes == nullptr) {
    throw StorageException("Null read buffer");
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (page_id >= page_count_) {
    throw StorageException("Page id out of range");
  }
  std::ifstream file(path_, std::ios::binary);
  if (!file) {
    throw StorageException("Cannot open page file for read: " + path_);
  }
  const uint64_t offset =
      pages_offset_ + static_cast<uint64_t>(page_id) * kPageSize;
  file.seekg(static_cast<std::streamoff>(offset));
  file.read(reinterpret_cast<char *>(out_bytes),
            static_cast<std::streamsize>(kPageSize));
  if (!file) {
    throw StorageException("Failed to read page from " + path_);
  }
}

void FilePageStore::write_page(PageId page_id, const uint8_t *bytes) {
  if (bytes == nullptr) {
    throw StorageException("Null write buffer");
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (page_id >= page_count_) {
    throw StorageException("Page id out of range");
  }
  ensure_file_size(page_count_);
  std::fstream file(path_, std::ios::binary | std::ios::in | std::ios::out);
  if (!file) {
    throw StorageException("Cannot open page file for write: " + path_);
  }
  const uint64_t offset =
      pages_offset_ + static_cast<uint64_t>(page_id) * kPageSize;
  file.seekp(static_cast<std::streamoff>(offset));
  file.write(reinterpret_cast<const char *>(bytes),
             static_cast<std::streamsize>(kPageSize));
  if (!file) {
    throw StorageException("Failed to write page to " + path_);
  }
}

PageId FilePageStore::allocate_page() {
  std::lock_guard<std::mutex> lock(mutex_);
  const PageId id = page_count_;
  page_count_ += 1;
  ensure_file_size(page_count_);
  std::vector<uint8_t> zero(kPageSize, 0);
  std::fstream file(path_, std::ios::binary | std::ios::in | std::ios::out);
  if (!file) {
    throw StorageException("Cannot open page file to allocate: " + path_);
  }
  const uint64_t offset =
      pages_offset_ + static_cast<uint64_t>(id) * kPageSize;
  file.seekp(static_cast<std::streamoff>(offset));
  file.write(reinterpret_cast<const char *>(zero.data()),
             static_cast<std::streamsize>(kPageSize));
  return id;
}

uint32_t FilePageStore::page_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return page_count_;
}

void FilePageStore::replace_pages(
    const std::vector<std::vector<uint8_t>> &pages) {
  std::lock_guard<std::mutex> lock(mutex_);
  page_count_ = static_cast<uint32_t>(pages.size());
  ensure_file_size(page_count_);
  std::fstream file(path_, std::ios::binary | std::ios::in | std::ios::out);
  if (!file) {
    throw StorageException("Cannot open page file for replace: " + path_);
  }
  for (uint32_t i = 0; i < page_count_; ++i) {
    if (pages[i].size() != kPageSize) {
      throw StorageException("Invalid page size in replace_pages");
    }
    const uint64_t offset =
        pages_offset_ + static_cast<uint64_t>(i) * kPageSize;
    file.seekp(static_cast<std::streamoff>(offset));
    file.write(reinterpret_cast<const char *>(pages[i].data()),
               static_cast<std::streamsize>(kPageSize));
  }
}

std::vector<std::vector<uint8_t>> FilePageStore::export_pages() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::vector<uint8_t>> pages;
  pages.reserve(page_count_);
  std::ifstream file(path_, std::ios::binary);
  if (!file) {
    throw StorageException("Cannot open page file for export: " + path_);
  }
  for (uint32_t i = 0; i < page_count_; ++i) {
    std::vector<uint8_t> page(kPageSize);
    const uint64_t offset =
        pages_offset_ + static_cast<uint64_t>(i) * kPageSize;
    file.seekg(static_cast<std::streamoff>(offset));
    file.read(reinterpret_cast<char *>(page.data()),
              static_cast<std::streamsize>(kPageSize));
    if (!file) {
      throw StorageException("Failed to export page from " + path_);
    }
    pages.push_back(std::move(page));
  }
  return pages;
}

const std::string &FilePageStore::get_path() const { return path_; }

uint64_t FilePageStore::get_pages_offset() const { return pages_offset_; }

}  // namespace db
