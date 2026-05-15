#pragma once

#include "executor/query_executor.h"
#include "executor/select_expression_evaluator.h"
#include "parser/ast.h"

namespace db {

class SelectPipeline {
 public:
  static QueryResult apply_post_scan(
      QueryResult result, const std::shared_ptr<SelectStatement> &stmt,
      const SelectExpressionEvaluator &row_evaluator);
};

SelectExpressionEvaluator evaluator_for_result_columns(
    const QueryResult &result);

}  // namespace db
