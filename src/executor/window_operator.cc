#include "executor/window_operator.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "core/row.h"
#include "executor/select_analysis.h"
#include "executor/select_pipeline.h"
#include "executor/window_function.h"

namespace db {

namespace {

struct WindowColumn {
  size_t column_index;
  std::shared_ptr<FunctionCallExpression> function;
};

int compareOrderValues(const Value &a, const Value &b, bool ascending) {
  if (a.is_null() && b.is_null()) return 0;
  if (a.is_null()) return 1;
  if (b.is_null()) return -1;
  if (a == b) return 0;
  if (ascending) return a < b ? -1 : 1;
  return a > b ? -1 : 1;
}

int compareExpressionList(
    const SelectExpressionEvaluator &eval, const Row &rowA, const Row &rowB,
    const std::vector<std::pair<ExpressionPtr, bool>> &orderBy) {
  for (const auto &[expr, ascending] : orderBy) {
    const Value va = eval.evaluate_expression(rowA, expr, nullptr);
    const Value vb = eval.evaluate_expression(rowB, expr, nullptr);
    const int cmp = compareOrderValues(va, vb, ascending);
    if (cmp != 0) return cmp;
  }
  return 0;
}

int comparePartitionKeys(const SelectExpressionEvaluator &eval, const Row &rowA,
                         const Row &rowB,
                         const std::vector<ExpressionPtr> &partitionBy) {
  for (const ExpressionPtr &expr : partitionBy) {
    const Value va = eval.evaluate_expression(rowA, expr, nullptr);
    const Value vb = eval.evaluate_expression(rowB, expr, nullptr);
    if (va == vb) continue;
    if (va.is_null()) return -1;
    if (vb.is_null()) return 1;
    return va < vb ? -1 : 1;
  }
  return 0;
}

bool isSamePartition(const SelectExpressionEvaluator &eval, const Row &rowA,
                     const Row &rowB,
                     const std::vector<ExpressionPtr> &partitionBy) {
  return comparePartitionKeys(eval, rowA, rowB, partitionBy) == 0;
}

bool isPeerOrder(const SelectExpressionEvaluator &eval, const Row &rowA,
                 const Row &rowB,
                 const std::vector<std::pair<ExpressionPtr, bool>> &orderBy) {
  return compareExpressionList(eval, rowA, rowB, orderBy) == 0;
}

std::vector<WindowColumn> collectWindowColumns(
    const std::shared_ptr<SelectStatement> &stmt) {
  std::vector<WindowColumn> columns;
  if (!stmt) return columns;
  const auto &selectCols = stmt->get_select_columns();
  size_t columnIndex = 0;
  for (const auto &[expr, alias] : selectCols) {
    (void)alias;
    if (is_wildcard_select_expression(expr)) {
      continue;
    }
    if (auto fn = std::dynamic_pointer_cast<FunctionCallExpression>(expr)) {
      if (fn->is_windowed()) {
        columns.push_back({columnIndex, fn});
      }
    }
    ++columnIndex;
  }
  return columns;
}

Value evaluateWindowArgument(const SelectExpressionEvaluator &eval,
                             const Row &row,
                             const FunctionCallExpression &fn) {
  if (fn.get_arguments().empty()) {
    return Value();
  }
  return eval.evaluate_expression(row, fn.get_arguments()[0], nullptr);
}

void applyOneWindow(QueryResult &result, const SelectExpressionEvaluator &eval,
                    const WindowColumn &column) {
  const auto &spec = column.function->get_window_spec();
  if (!spec) return;
  std::unique_ptr<IWindowFunction> windowFn =
      make_window_function(column.function->get_function_name());
  if (!windowFn) return;
  std::vector<size_t> indices(result.rows.size());
  for (size_t i = 0; i < indices.size(); ++i) {
    indices[i] = i;
  }
  const auto &partitionBy = spec->get_partition_by();
  const auto &orderBy = spec->get_order_by();
  std::stable_sort(indices.begin(), indices.end(),
                   [&](size_t left, size_t right) {
                     Row rowLeft(result.rows[left]);
                     Row rowRight(result.rows[right]);
                     const int partCmp =
                         comparePartitionKeys(eval, rowLeft, rowRight,
                                              partitionBy);
                     if (partCmp != 0) return partCmp < 0;
                     return compareExpressionList(eval, rowLeft, rowRight,
                                                  orderBy) < 0;
                   });
  for (size_t pos = 0; pos < indices.size(); ++pos) {
    const size_t rowIndex = indices[pos];
    Row current(result.rows[rowIndex]);
    const bool isPartitionStart =
        pos == 0 ||
        !isSamePartition(eval, Row(result.rows[indices[pos - 1]]), current,
                         partitionBy);
    if (isPartitionStart) {
      windowFn->resetPartition();
    }
    const bool isPeer =
        !isPartitionStart &&
        isPeerOrder(eval, Row(result.rows[indices[pos - 1]]), current, orderBy);
    const Value arg =
        evaluateWindowArgument(eval, current, *column.function);
    windowFn->consumeRow(arg, isPeer);
    if (column.column_index < result.rows[rowIndex].size()) {
      result.rows[rowIndex][column.column_index] = windowFn->currentValue();
    }
  }
}

}  // namespace

QueryResult WindowOperator::apply(QueryResult input,
                                  const SelectPipelineContext &ctx) const {
  if (!input.success) return input;
  if (!ctx.statement || !select_has_window(ctx.statement)) return input;
  const std::vector<WindowColumn> windows =
      collectWindowColumns(ctx.statement);
  if (windows.empty()) return input;
  const SelectExpressionEvaluator eval =
      evaluator_for_result_columns(input);
  for (const WindowColumn &column : windows) {
    applyOneWindow(input, eval, column);
  }
  return input;
}

}  // namespace db
