#pragma once

#include <vector>

#include "core/row.h"
#include "executor/query_executor.h"
#include "executor/select_expression_evaluator.h"
#include "parser/ast.h"

namespace db {

/**
 * GROUP BY, aggregate evaluation, HAVING filter, and SELECT projection
 * for grouped queries.
 */
class GroupAggregateOperator {
 public:
  GroupAggregateOperator(std::shared_ptr<SelectStatement> stmt,
                         SelectExpressionEvaluator row_evaluator);

  QueryResult apply(const std::vector<Row> &input_rows) const;

 private:
  std::shared_ptr<SelectStatement> stmt_;
  SelectExpressionEvaluator row_evaluator_;
};

}  // namespace db
