#include "executor/distinct_operator.h"

#include <algorithm>

namespace db {

namespace {

bool row_equal(const std::vector<Value> &a, const std::vector<Value> &b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

bool row_less(const std::vector<Value> &a, const std::vector<Value> &b) {
  const size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i) {
    if (a[i] != b[i]) return a[i] < b[i];
  }
  return a.size() < b.size();
}

}  // namespace

QueryResult DistinctOperator::apply(
    QueryResult input, const SelectPipelineContext &ctx) const {
  (void)ctx;
  if (!input.success) return input;
  if (!ctx.statement || !ctx.statement->is_distinct()) return input;

  std::sort(input.rows.begin(), input.rows.end(), row_less);
  const auto last =
      std::unique(input.rows.begin(), input.rows.end(), row_equal);
  input.rows.erase(last, input.rows.end());
  input.affected_rows = static_cast<int>(input.rows.size());
  return input;
}

}  // namespace db
