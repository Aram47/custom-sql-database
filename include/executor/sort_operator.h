#pragma once

#include "executor/relational_operator.h"

namespace db {

class SortOperator : public IRelationalOperator {
 public:
  QueryResult apply(QueryResult input,
                    const SelectPipelineContext &ctx) const override;
};

}  // namespace db
