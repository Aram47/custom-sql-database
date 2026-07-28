#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "storage/item_pointer.h"
#include "storage/page_format.h"
#include "utils/exceptions.h"

namespace db {

/** Abstract page I/O backend for a single heap file. */
class IPageStore {
 public:
  virtual ~IPageStore() = default;

  /** Reads page_id into out_bytes (must be kPageSize). */
  virtual void read_page(PageId page_id, uint8_t *out_bytes) = 0;

  /** Writes page_id from bytes (must be kPageSize). */
  virtual void write_page(PageId page_id, const uint8_t *bytes) = 0;

  /** Allocates a new zeroed page and returns its id. */
  virtual PageId allocate_page() = 0;

  virtual uint32_t page_count() const = 0;

  /** Replaces all pages with the given raw page images. */
  virtual void replace_pages(const std::vector<std::vector<uint8_t>> &pages) = 0;

  /** Exports all pages as raw kPageSize buffers. */
  virtual std::vector<std::vector<uint8_t>> export_pages() const = 0;
};

/** In-memory page store for tests and ephemeral tables. */
class MemoryPageStore : public IPageStore {
 public:
  MemoryPageStore() = default;

  void read_page(PageId page_id, uint8_t *out_bytes) override;
  void write_page(PageId page_id, const uint8_t *bytes) override;
  PageId allocate_page() override;
  uint32_t page_count() const override;
  void replace_pages(const std::vector<std::vector<uint8_t>> &pages) override;
  std::vector<std::vector<uint8_t>> export_pages() const override;

 private:
  mutable std::mutex mutex_;
  std::vector<std::vector<uint8_t>> pages_;
};

/**
 * File-backed store for contiguous pages after a fixed byte offset.
 * Used when a table heap lives inside a .db file (v6 page region).
 */
class FilePageStore : public IPageStore {
 public:
  FilePageStore(std::string path, uint64_t pages_offset, uint32_t page_count);

  void read_page(PageId page_id, uint8_t *out_bytes) override;
  void write_page(PageId page_id, const uint8_t *bytes) override;
  PageId allocate_page() override;
  uint32_t page_count() const override;
  void replace_pages(const std::vector<std::vector<uint8_t>> &pages) override;
  std::vector<std::vector<uint8_t>> export_pages() const override;

  const std::string &get_path() const;
  uint64_t get_pages_offset() const;

 private:
  std::string path_;
  uint64_t pages_offset_;
  uint32_t page_count_;
  mutable std::mutex mutex_;

  void ensure_file_size(uint32_t min_pages);
};

}  // namespace db
