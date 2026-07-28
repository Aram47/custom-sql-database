#include "storage/page_format.h"

#include <cstring>

#include "types/type_converter.h"
#include "utils/exceptions.h"

namespace db {
namespace {

void append_u32(std::vector<uint8_t> &out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xFF));
  out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

void append_u64(std::vector<uint8_t> &out, uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
  }
}

uint32_t read_u32(const uint8_t *data, size_t &offset, size_t length) {
  if (offset + 4 > length) {
    throw StorageException("Truncated tuple while reading u32");
  }
  const uint32_t value =
      static_cast<uint32_t>(data[offset]) |
      (static_cast<uint32_t>(data[offset + 1]) << 8) |
      (static_cast<uint32_t>(data[offset + 2]) << 16) |
      (static_cast<uint32_t>(data[offset + 3]) << 24);
  offset += 4;
  return value;
}

uint64_t read_u64(const uint8_t *data, size_t &offset, size_t length) {
  if (offset + 8 > length) {
    throw StorageException("Truncated tuple while reading u64");
  }
  uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<uint64_t>(data[offset + static_cast<size_t>(i)])
             << (8 * i);
  }
  offset += 8;
  return value;
}

}  // namespace

std::vector<uint8_t> serialize_tuple(const Row &row) {
  std::vector<uint8_t> out;
  out.reserve(measure_tuple_size(row));
  append_u64(out, row.get_xmin());
  append_u64(out, row.get_xmax());
  for (size_t i = 0; i < row.get_column_count(); ++i) {
    const std::vector<uint8_t> blob =
        TypeConverter::serialize_value(row.get_value(i));
    append_u32(out, static_cast<uint32_t>(blob.size()));
    out.insert(out.end(), blob.begin(), blob.end());
  }
  return out;
}

size_t measure_tuple_size(const Row &row) {
  size_t size = 16;
  for (size_t i = 0; i < row.get_column_count(); ++i) {
    const std::vector<uint8_t> blob =
        TypeConverter::serialize_value(row.get_value(i));
    size += 4 + blob.size();
  }
  return size;
}

Row deserialize_tuple(const uint8_t *data, size_t length,
                      const std::vector<DataType> &column_types) {
  if (data == nullptr) {
    throw StorageException("Null tuple data");
  }
  size_t offset = 0;
  const uint64_t xmin = read_u64(data, offset, length);
  const uint64_t xmax = read_u64(data, offset, length);
  Row row;
  row.set_xmin(xmin);
  row.set_xmax(xmax);
  for (size_t i = 0; i < column_types.size(); ++i) {
    const uint32_t value_len = read_u32(data, offset, length);
    if (offset + value_len > length) {
      throw StorageException("Truncated tuple column payload");
    }
    std::vector<uint8_t> blob(data + offset, data + offset + value_len);
    offset += value_len;
    row.add_value(TypeConverter::deserialize_value(blob, column_types[i]));
  }
  return row;
}

}  // namespace db
