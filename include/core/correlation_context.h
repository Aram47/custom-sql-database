#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/row.h"
#include "executor/select_column_binding.h"
#include "parser/ast.h"
#include "types/value.h"

namespace db {

/**
 * Stack of outer-query row bindings for correlated subquery evaluation.
 */
class CorrelationContext {
 public:
  void pushFrame(const std::vector<SelectColumnBinding> &bindings,
                 const Row &row);
  void popFrame();
  bool empty() const;
  std::optional<Value> lookup(const ColumnRefExpression &cref) const;

 private:
  struct Frame {
    std::map<std::string, Value> by_qualified;
    std::map<std::string, Value> by_column;
  };
  std::vector<Frame> frames_;
};

}  // namespace db
