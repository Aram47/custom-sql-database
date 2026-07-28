#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "storage/page_format.h"
#include "utils/exceptions.h"

namespace db {

/**
 * Mutable fixed-size page with header, tuple area, and slot directory.
 * Owns a kPageSize byte buffer.
 */
class Page {
 public:
  Page();
  explicit Page(uint32_t page_id);
  /** Copies kPageSize bytes from raw_bytes into a new page buffer. */
  static Page from_bytes(const uint8_t *raw_bytes);

  Page(const Page &) = default;
  Page &operator=(const Page &) = default;
  Page(Page &&) noexcept = default;
  Page &operator=(Page &&) noexcept = default;

  /** Initializes an empty page with the given id. */
  void initialize(uint32_t page_id);

  uint32_t get_page_id() const;
  uint16_t get_slot_count() const;
  size_t get_free_space() const;
  const uint8_t *data() const;
  uint8_t *mutable_data();

  /**
   * Inserts a tuple into free space; returns slot index.
   * @throws StorageException if the tuple does not fit.
   */
  uint16_t insert_tuple(const uint8_t *tuple_bytes, uint16_t tuple_length);

  /** Returns tuple bytes for a live slot, or nullopt if deleted/invalid. */
  std::optional<std::vector<uint8_t>> read_tuple(uint16_t slot) const;

  /** Marks a slot deleted (offset=0); does not compact payload. */
  void delete_tuple(uint16_t slot);

  /** Overwrites an existing live slot in place if length matches, else fails. */
  bool update_tuple_in_place(uint16_t slot, const uint8_t *tuple_bytes,
                             uint16_t tuple_length);

  bool is_slot_live(uint16_t slot) const;

 private:
  std::vector<uint8_t> bytes_;

  PageHeader &header();
  const PageHeader &header() const;
  SlotEntry *slot_at(uint16_t index);
  const SlotEntry *slot_at(uint16_t index) const;
};

}  // namespace db
