#pragma once

#include <cstddef>
#include <vector>

#include "types/value.h"
#include "utils/exceptions.h"

namespace db {

/** Bound positional parameters for PREPARE/EXECUTE (`?` placeholders). */
class BindContext {
 public:
  BindContext() = default;
  explicit BindContext(std::vector<Value> values);

  void setValues(std::vector<Value> values);
  const std::vector<Value> &getValues() const;
  size_t size() const;
  const Value &getValue(size_t index) const;

 private:
  std::vector<Value> values_;
};

}  // namespace db
