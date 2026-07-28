#pragma once

#include "executor/relational_operator.h"

namespace db {

/**
 * Evaluates window functions in the SELECT list on a projected QueryResult.
 */
class WindowOperator : public IRelationalOperator {
 public:
  QueryResult apply(QueryResult input,
                    const SelectPipelineContext &ctx) const override;
};

}  // namespace db
