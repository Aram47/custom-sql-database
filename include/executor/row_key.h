#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#include "types/value.h"

namespace db {

/**
 * Hashable key for a result row (used by DISTINCT and set operations).
 */
class RowKey {
 public:
  explicit RowKey(std::vector<Value> values);

  const std::vector<Value> &get_values() const;
  bool operator==(const RowKey &other) const;

 private:
  std::vector<Value> values_;
};

/** Lexicographic less-than for sorting rows (DISTINCT path). */
bool isRowLess(const std::vector<Value> &left, const std::vector<Value> &right);

/** Equality of two rows by element-wise Value comparison. */
bool isRowEqual(const std::vector<Value> &left, const std::vector<Value> &right);

/** Combines hash of a single Value into an existing seed. */
std::size_t hashValue(const Value &value);

/** Mixes a hash value into a seed (Boost-style combine). */
void mixHash(std::size_t &seed, std::size_t value);

}  // namespace db

namespace std {

template <>
struct hash<db::RowKey> {
  std::size_t operator()(const db::RowKey &key) const;
};

}  // namespace std
