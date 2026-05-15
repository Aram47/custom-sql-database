#pragma once

#include <memory>

#include "executor/query_executor.h"
#include "parser/ast.h"

namespace db {

class Database;

/**
 * Multi-table SELECT using nested-loop joins (INNER / LEFT / RIGHT / FULL /
 * CROSS). INNER or JOIN without ON behaves as a cross join; CROSS JOIN forbids
 * ON at parse time; LEFT / RIGHT / FULL require ON. WHERE / DISTINCT follow
 * join semantics.
 */
class JoinSelectExecutor : public QueryExecutor {
 public:
  JoinSelectExecutor(std::shared_ptr<SelectStatement> stmt, Database *database);
  QueryResult execute() override;

 private:
  std::shared_ptr<SelectStatement> stmt_;
  Database *database_;
};

}  // namespace db
