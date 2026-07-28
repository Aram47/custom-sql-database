#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/row.h"
#include "types/data_type.h"

namespace db {

/** Fixed page size in bytes (8 KiB). */
constexpr size_t kPageSize = 8192;

#pragma pack(push, 1)
/** On-disk / in-frame page header. */
struct PageHeader {
  uint32_t page_id{0};
  uint16_t slot_count{0};
  /** First free byte after tuple payload (grows upward). */
  uint16_t lower{0};
  /** Start of slot directory (grows downward from end of page). */
  uint16_t upper{0};
  uint16_t flags{0};
};

/** Slot directory entry; offset 0 means deleted / unused. */
struct SlotEntry {
  uint16_t offset{0};
  uint16_t length{0};
};
#pragma pack(pop)

static_assert(sizeof(PageHeader) == 12, "PageHeader must be packed");
static_assert(sizeof(SlotEntry) == 4, "SlotEntry must be packed");

/**
 * Serializes a row into length-prefixed tuple bytes:
 * xmin, xmax, then per-column (u32 len + TypeConverter blob).
 */
std::vector<uint8_t> serialize_tuple(const Row &row);

/**
 * Deserializes a tuple buffer into a Row using column types for Value decode.
 */
Row deserialize_tuple(const uint8_t *data, size_t length,
                      const std::vector<DataType> &column_types);

/** Returns serialized tuple size without allocating the full buffer twice. */
size_t measure_tuple_size(const Row &row);

}  // namespace db
