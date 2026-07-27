#include "core/row.h"

namespace db {

Row::Row() = default;

Row::Row(const std::vector<Value> &values) : values_(values) {}

void Row::add_value(const Value &value) { values_.push_back(value); }

void Row::set_value(size_t index, const Value &value) {
  if (index >= values_.size()) {
    throw std::out_of_range("Row index out of range");
  }
  values_[index] = value;
}

void Row::remove_value(size_t index) {
  if (index >= values_.size()) {
    throw std::out_of_range("Row index out of range");
  }
  values_.erase(values_.begin() + static_cast<long>(index));
}

const Value &Row::get_value(size_t index) const {
  if (index >= values_.size()) {
    throw std::out_of_range("Row index out of range");
  }
  return values_[index];
}

Value &Row::get_mutable_value(size_t index) {
  if (index >= values_.size()) {
    throw std::out_of_range("Row index out of range");
  }
  return values_[index];
}

size_t Row::get_column_count() const { return values_.size(); }

bool Row::is_empty() const { return values_.empty(); }

uint64_t Row::get_xmin() const { return xmin_; }

uint64_t Row::get_xmax() const { return xmax_; }

void Row::set_xmin(uint64_t xmin) { xmin_ = xmin; }

void Row::set_xmax(uint64_t xmax) { xmax_ = xmax; }

bool Row::operator==(const Row &other) const {
  if (values_.size() != other.values_.size()) return false;
  for (size_t i = 0; i < values_.size(); ++i) {
    if (values_[i] != other.values_[i]) return false;
  }
  return true;
}

bool Row::operator!=(const Row &other) const { return !(*this == other); }

std::vector<Value>::const_iterator Row::begin() const {
  return values_.begin();
}

std::vector<Value>::const_iterator Row::end() const { return values_.end(); }

std::vector<Value>::iterator Row::begin() { return values_.begin(); }

std::vector<Value>::iterator Row::end() { return values_.end(); }

std::string Row::to_string() const {
  std::string str = "Row(";
  for (size_t i = 0; i < values_.size(); ++i) {
    if (i > 0) str += ", ";
    str += values_[i].to_string();
  }
  str += ")";
  return str;
}

}  // namespace db
