#include "executor/sort_operator.h"

#include <algorithm>

#include "executor/select_pipeline.h"

namespace db {

namespace {

int compare_order_values(const Value &a, const Value &b, bool ascending) {
  if (a.is_null() && b.is_null()) return 0;
  if (a.is_null()) return ascending ? 1 : 1;
  if (b.is_null()) return ascending ? -1 : -1;
  if (a == b) return 0;
  if (ascending) return a < b ? -1 : 1;
  return a > b ? -1 : 1;
}

}  // namespace

QueryResult SortOperator::apply(QueryResult input,
                                const SelectPipelineContext &ctx) const {
  if (!input.success) return input;
  if (!ctx.statement || ctx.statement->get_order_by_columns().empty()) {
    return input;
  }

  const SelectExpressionEvaluator result_eval =
      evaluator_for_result_columns(input);

  const auto &order_by = ctx.statement->get_order_by_columns();
  std::stable_sort(
      input.rows.begin(), input.rows.end(),
      [&](const std::vector<Value> &ra, const std::vector<Value> &rb) {
        Row row_a(ra);
        Row row_b(rb);
        for (const auto &[expr, ascending] : order_by) {
          const Value va =
              result_eval.evaluate_expression(row_a, expr, nullptr);
          const Value vb =
              result_eval.evaluate_expression(row_b, expr, nullptr);
          const int cmp = compare_order_values(va, vb, ascending);
          if (cmp != 0) return cmp < 0;
        }
        return false;
      });

  return input;
}

}  // namespace db
