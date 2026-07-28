#include "executor/select_pipeline.h"

#include "executor/distinct_operator.h"
#include "executor/limit_offset_operator.h"
#include "executor/sort_operator.h"
#include "executor/window_operator.h"

namespace db {

SelectExpressionEvaluator evaluator_for_result_columns(
    const QueryResult &result) {
  std::vector<SelectColumnBinding> bindings;
  bindings.reserve(result.column_names.size());
  for (const auto &name : result.column_names) {
    bindings.push_back({"", "", name});
  }
  return SelectExpressionEvaluator(std::move(bindings));
}

QueryResult SelectPipeline::apply_post_scan(
    QueryResult result, const std::shared_ptr<SelectStatement> &stmt,
    const SelectExpressionEvaluator &row_evaluator) {
  if (!result.success) return result;
  SelectPipelineContext ctx{stmt, evaluator_for_result_columns(result)};
  WindowOperator windowOp;
  result = windowOp.apply(std::move(result), ctx);
  if (!result.success) return result;
  ctx.row_evaluator = evaluator_for_result_columns(result);
  DistinctOperator distinct;
  result = distinct.apply(std::move(result), ctx);
  if (!result.success) return result;
  ctx.row_evaluator = evaluator_for_result_columns(result);
  SortOperator sort;
  result = sort.apply(std::move(result), ctx);
  if (!result.success) return result;
  LimitOffsetOperator limit_offset;
  result = limit_offset.apply(std::move(result), ctx);
  (void)row_evaluator;
  return result;
}

}  // namespace db
