#pragma once

#include <memory>
#include <string>
#include <vector>

#include "parser/ast.h"
#include "types/value.h"

namespace db {

class IAggregateFunction {
 public:
  virtual ~IAggregateFunction() = default;
  virtual void reset() = 0;
  virtual void accumulate(const Value &value) = 0;
  virtual Value finalize() const = 0;
};

/** COUNT(*), COUNT(expr), SUM, AVG, MIN, MAX. */
std::unique_ptr<IAggregateFunction> make_aggregate_function(
    const std::string &name, const std::vector<ExpressionPtr> &args);

bool is_count_star(const FunctionCallExpression &fn);

}  // namespace db
