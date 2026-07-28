#pragma once

#include <cstdint>

namespace db {

/** Physical address of a tuple: page number + slot index. */
struct ItemPointer {
  uint32_t page_id{0};
  uint16_t slot{0};

  bool operator==(const ItemPointer &other) const {
    return page_id == other.page_id && slot == other.slot;
  }

  bool operator!=(const ItemPointer &other) const { return !(*this == other); }
};

using FileId = uint32_t;
using PageId = uint32_t;

}  // namespace db
