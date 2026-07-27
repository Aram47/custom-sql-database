#include "core/bind_context.h"

namespace db {

BindContext::BindContext(std::vector<Value> values)
    : values_(std::move(values)) {}

void BindContext::setValues(std::vector<Value> values) {
  values_ = std::move(values);
}

const std::vector<Value> &BindContext::getValues() const { return values_; }

size_t BindContext::size() const { return values_.size(); }

const Value &BindContext::getValue(size_t index) const {
  if (index >= values_.size()) {
    throw InvalidOperationException("Bind parameter index out of range");
  }
  return values_[index];
}

}  // namespace db
