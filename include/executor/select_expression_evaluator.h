#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/row.h"
#include "parser/ast.h"

namespace db {

struct SelectColumnBinding {
  std::string alias;
  std::string physical_table;
  std::string column_name;
};

/**
 * Shared expression semantics for SELECT / JOIN / UPDATE-style row contexts.
 * Binds logical column refs (bare or alias.table) against a flattened row slice.
 */
class SelectExpressionEvaluator {
 public:
  explicit SelectExpressionEvaluator(std::vector<SelectColumnBinding> bindings);

  /** Returns binding count (equals expected Row column count). */
  size_t binding_count() const;

  /** Header label for wildcard expansion during JOIN queries. */
  std::string qualified_header(size_t binding_index) const;

  /**
   * Resolves ColumnRefExpression; -1 on unknown column.
   * Ambiguous bare column yields -1 with error text.
   */
  int resolve_column_index(const ColumnRefExpression &cref,
                           std::string *error_msg) const;

  /** Validates all reachable column refs; std::nullopt on success. */
  std::optional<std::string> validate_expression_tree(
      const ExpressionPtr &expr) const;

  bool evaluate_condition(const Row &row,
                          const ExpressionPtr &condition) const;
  Value evaluate_expression(const Row &row, const ExpressionPtr &expr,
                            std::string *error_msg = nullptr) const;

  /**
   * UPDATE SET rhs: only literals and bare column references (no arithmetic).
   * Other expression shapes yield NULL semantics, matching legacy UpdateExecutor.
   */
  Value evaluate_dml_assignment_rhs(const Row &row,
                                    const ExpressionPtr &expr) const;

 private:
  std::vector<SelectColumnBinding> bindings_;
};

}  // namespace db
