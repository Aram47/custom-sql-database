#include "executor/limit_offset_operator.h"

namespace db {

QueryResult LimitOffsetOperator::apply(
    QueryResult input, const SelectPipelineContext &ctx) const {
  if (!input.success) return input;
  if (!ctx.statement) return input;

  const int offset = ctx.statement->get_offset();
  const int limit = ctx.statement->get_limit();

  if (offset <= 0 && limit <= 0) return input;

  if (offset > 0) {
    const size_t off = static_cast<size_t>(offset);
    if (off >= input.rows.size()) {
      input.rows.clear();
    } else {
      input.rows.erase(input.rows.begin(),
                       input.rows.begin() + static_cast<std::ptrdiff_t>(off));
    }
  }

  if (limit > 0 && static_cast<size_t>(limit) < input.rows.size()) {
    input.rows.resize(static_cast<size_t>(limit));
  }

  input.affected_rows = static_cast<int>(input.rows.size());
  return input;
}

}  // namespace db
