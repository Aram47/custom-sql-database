#pragma once

#include <string>
#include <vector>

#include "parser/parser.h"

namespace db {

class Database;
class Table;

/** Builds human-readable query plan lines for EXPLAIN. */
class QueryExplainer {
 public:
  explicit QueryExplainer(Database *database);

  /**
   * Describes how the statement would be executed.
   * @param stmt Parsed statement (must not be ExplainStatement).
   * @return One plan line per vector element.
   */
  std::vector<std::string> buildPlanLines(const ParsedStatement &stmt) const;

 private:
  Database *database_;

  std::vector<std::string> explainSelect(
      const std::shared_ptr<SelectStatement> &stmt) const;
  std::vector<std::string> explainSetOperation(
      const std::shared_ptr<SetOperationStatement> &stmt) const;
  std::vector<std::string> explainInsert(
      const std::shared_ptr<InsertStatement> &stmt) const;
  std::vector<std::string> explainUpdate(
      const std::shared_ptr<UpdateStatement> &stmt) const;
  std::vector<std::string> explainDelete(
      const std::shared_ptr<DeleteStatement> &stmt) const;
  bool hasIndexAccessPath(Table *table, const std::string &alias,
                          const ExpressionPtr &whereExpr) const;
};

}  // namespace db
