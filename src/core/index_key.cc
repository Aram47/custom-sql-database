#include "core/index_key.h"

#include <sstream>

namespace db {

IndexKey::IndexKey(std::vector<Value> components)
    : components_(std::move(components)) {}

IndexKey::IndexKey(const Value &single) : components_{single} {}

const std::vector<Value> &IndexKey::get_components() const {
  return components_;
}

size_t IndexKey::size() const { return components_.size(); }

bool IndexKey::empty() const { return components_.empty(); }

bool IndexKey::has_null() const {
  for (const Value &component : components_) {
    if (component.is_null()) {
      return true;
    }
  }
  return false;
}

bool IndexKey::starts_with(const IndexKey &prefix) const {
  if (prefix.size() > size()) {
    return false;
  }
  for (size_t i = 0; i < prefix.size(); ++i) {
    if (components_[i] != prefix.components_[i]) {
      return false;
    }
  }
  return true;
}

bool IndexKey::operator==(const IndexKey &other) const {
  return components_ == other.components_;
}

bool IndexKey::operator!=(const IndexKey &other) const {
  return !(*this == other);
}

bool IndexKey::operator<(const IndexKey &other) const {
  const size_t limit =
      size() < other.size() ? size() : other.size();
  for (size_t i = 0; i < limit; ++i) {
    if (components_[i] < other.components_[i]) {
      return true;
    }
    if (other.components_[i] < components_[i]) {
      return false;
    }
  }
  return size() < other.size();
}

bool IndexKey::operator<=(const IndexKey &other) const {
  return !(other < *this);
}

bool IndexKey::operator>(const IndexKey &other) const { return other < *this; }

bool IndexKey::operator>=(const IndexKey &other) const {
  return !(*this < other);
}

std::string IndexKey::to_string() const {
  std::ostringstream stream;
  stream << "(";
  for (size_t i = 0; i < components_.size(); ++i) {
    if (i > 0) {
      stream << ", ";
    }
    stream << components_[i].to_string();
  }
  stream << ")";
  return stream.str();
}

}  // namespace db
