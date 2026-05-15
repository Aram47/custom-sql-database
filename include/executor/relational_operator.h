#pragma once

#include "executor/query_executor.h"
#include "executor/select_expression_evaluator.h"
#include "parser/ast.h"

namespace db {

struct SelectPipelineContext {
  std::shared_ptr<SelectStatement> statement;
  SelectExpressionEvaluator row_evaluator;
};

class IRelationalOperator {
 public:
  virtual ~IRelationalOperator() = default;
  virtual QueryResult apply(QueryResult input,
                            const SelectPipelineContext &ctx) const = 0;
};

}  // namespace db
