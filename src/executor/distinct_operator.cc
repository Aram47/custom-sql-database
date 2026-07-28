#include "executor/distinct_operator.h"

#include <algorithm>

#include "executor/row_key.h"

namespace db {

QueryResult DistinctOperator::apply(
    QueryResult input, const SelectPipelineContext &ctx) const {
  if (!input.success) return input;
  if (!ctx.statement || !ctx.statement->is_distinct()) return input;
  std::sort(input.rows.begin(), input.rows.end(), isRowLess);
  const auto last =
      std::unique(input.rows.begin(), input.rows.end(), isRowEqual);
  input.rows.erase(last, input.rows.end());
  input.affected_rows = static_cast<int>(input.rows.size());
  return input;
}

}  // namespace db
