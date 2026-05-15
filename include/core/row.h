#pragma once

#include <memory>
#include <stdexcept>
#include <vector>

#include "types/value.h"

namespace db {

class Row {
 public:
  // Constructors
  Row();
  explicit Row(const std::vector<Value> &values);
  Row(const Row &) = default;
  Row &operator=(const Row &) = default;
  Row(Row &&) = default;
  Row &operator=(Row &&) = default;

  // Data access
  void add_value(const Value &value);
  void set_value(size_t index, const Value &value);
  const Value &get_value(size_t index) const;
  Value &get_mutable_value(size_t index);

  // Row properties
  size_t get_column_count() const;
  bool is_empty() const;

  // Comparison
  bool operator==(const Row &other) const;
  bool operator!=(const Row &other) const;

  // Serialization
  std::vector<Value>::const_iterator begin() const;
  std::vector<Value>::const_iterator end() const;
  std::vector<Value>::iterator begin();
  std::vector<Value>::iterator end();

  // String representation
  std::string to_string() const;

 private:
  std::vector<Value> values_;
};

}  // namespace db
