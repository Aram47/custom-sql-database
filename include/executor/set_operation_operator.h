#pragma once

#include "executor/query_executor.h"
#include "parser/ast.h"

namespace db {

/**
 * Applies UNION / UNION ALL / INTERSECT / EXCEPT on two QueryResults.
 */
class SetOperationOperator {
 public:
  /**
   * Combines left and right results according to kind.
   * @param left Left operand result (column names preserved on success).
   * @param right Right operand result.
   * @param kind Set operation kind.
   * @param isAll When true and kind is Union, keeps duplicates (UNION ALL).
   * @return Combined QueryResult or an error result.
   */
  QueryResult execute(const QueryResult &left, const QueryResult &right,
                      SetOperationKind kind, bool isAll) const;
};

}  // namespace db
