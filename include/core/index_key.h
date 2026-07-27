#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "types/value.h"

namespace db {

/**
 * Composite B-tree key: lexicographic order over Value components.
 * Single-column indexes use a key of length 1.
 */
class IndexKey {
 public:
  IndexKey() = default;
  explicit IndexKey(std::vector<Value> components);
  explicit IndexKey(const Value &single);

  const std::vector<Value> &get_components() const;
  size_t size() const;
  bool empty() const;
  bool has_null() const;
  bool starts_with(const IndexKey &prefix) const;

  bool operator==(const IndexKey &other) const;
  bool operator!=(const IndexKey &other) const;
  bool operator<(const IndexKey &other) const;
  bool operator<=(const IndexKey &other) const;
  bool operator>(const IndexKey &other) const;
  bool operator>=(const IndexKey &other) const;

  std::string to_string() const;

 private:
  std::vector<Value> components_;
};

}  // namespace db
