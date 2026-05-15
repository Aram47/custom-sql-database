#pragma once

#include "executor/relational_operator.h"

namespace db {

class LimitOffsetOperator : public IRelationalOperator {
 public:
  QueryResult apply(QueryResult input,
                    const SelectPipelineContext &ctx) const override;
};

}  // namespace db
