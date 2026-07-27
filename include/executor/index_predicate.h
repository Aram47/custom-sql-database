#pragma once

#include <optional>
#include <string>
#include <vector>

#include "parser/ast.h"
#include "types/value.h"

namespace db {

enum class IndexCompareOp { Equal, Less, LessEqual, Greater, GreaterEqual };

/** Single sargable column predicate usable with a B-tree index. */
struct IndexColumnPredicate {
  std::string column_name;
  std::string table_qualifier;
  IndexCompareOp op{IndexCompareOp::Equal};
  Value literal;
  bool has_upper{false};
  Value upper_literal;
  bool lower_inclusive{true};
  bool upper_inclusive{true};
};

/**
 * Extracts AND-connected column OP literal predicates from an expression.
 * Returns nullopt if the expression contains OR or non-sargable forms that
 * prevent partial index use (caller should full-scan).
 * AND of extractable and non-extractable parts: returns extractable parts only
 * when the whole tree is AND of comparisons (non-sargable leaf => nullopt).
 */
std::optional<std::vector<IndexColumnPredicate>> extract_index_predicates(
    const ExpressionPtr &expr);

/** True when ON is a simple equi-join between two column references. */
bool try_extract_equi_join_columns(const ExpressionPtr &on_expr,
                                   std::string &left_table,
                                   std::string &left_column,
                                   std::string &right_table,
                                   std::string &right_column);

}  // namespace db
