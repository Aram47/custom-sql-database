#include "executor/row_key.h"

#include <algorithm>
#include <cstdint>
#include <functional>

namespace db {

RowKey::RowKey(std::vector<Value> values) : values_(std::move(values)) {}

const std::vector<Value> &RowKey::get_values() const { return values_; }

bool RowKey::operator==(const RowKey &other) const {
  return isRowEqual(values_, other.values_);
}

bool isRowEqual(const std::vector<Value> &left,
                const std::vector<Value> &right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (size_t i = 0; i < left.size(); ++i) {
    if (left[i] != right[i]) {
      return false;
    }
  }
  return true;
}

bool isRowLess(const std::vector<Value> &left,
               const std::vector<Value> &right) {
  const size_t n = std::min(left.size(), right.size());
  for (size_t i = 0; i < n; ++i) {
    if (left[i] != right[i]) {
      return left[i] < right[i];
    }
  }
  return left.size() < right.size();
}

std::size_t hashValue(const Value &value) {
  if (value.is_null()) {
    return 0;
  }
  if (value.is_int()) {
    return std::hash<int64_t>{}(value.as_int());
  }
  if (value.is_float()) {
    return std::hash<double>{}(value.as_float());
  }
  if (value.is_string()) {
    return std::hash<std::string>{}(value.as_string());
  }
  if (value.is_bool()) {
    return std::hash<bool>{}(value.as_bool());
  }
  return 0;
}

void mixHash(std::size_t &seed, std::size_t value) {
  constexpr std::size_t HASH_MIX_CONSTANT =
      static_cast<std::size_t>(0x9e3779b97f4a7c15ULL);
  seed ^= value + HASH_MIX_CONSTANT + (seed << 6) + (seed >> 2);
}

}  // namespace db

namespace std {

size_t hash<db::RowKey>::operator()(const db::RowKey &key) const {
  size_t seed = 0;
  for (const db::Value &value : key.get_values()) {
    db::mixHash(seed, db::hashValue(value));
  }
  return seed;
}

}  // namespace std
