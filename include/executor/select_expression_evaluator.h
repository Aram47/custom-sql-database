#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/bind_context.h"
#include "core/correlation_context.h"
#include "core/row.h"
#include "executor/select_column_binding.h"
#include "parser/ast.h"

namespace db {

class RoutineCatalog;

/**
 * Shared expression semantics for SELECT / JOIN / UPDATE-style row contexts.
 * Binds logical column refs (bare or alias.table) against a flattened row slice.
 */
class SelectExpressionEvaluator {
 public:
  using ScalarSubqueryFn =
      std::function<Value(const std::shared_ptr<SelectStatement> &,
                          std::string *error_msg)>;
  using InSubqueryFn =
      std::function<bool(const Value &, const std::shared_ptr<SelectStatement> &,
                         bool is_not, std::string *error_msg)>;
  using ExistsSubqueryFn =
      std::function<bool(const std::shared_ptr<SelectStatement> &,
                         std::string *error_msg)>;

  explicit SelectExpressionEvaluator(std::vector<SelectColumnBinding> bindings);

  void set_scalar_subquery_fn(ScalarSubqueryFn fn);
  void set_in_subquery_fn(InSubqueryFn fn);
  void set_exists_subquery_fn(ExistsSubqueryFn fn);
  void set_correlation_context(CorrelationContext *context);
  void set_bind_context(const BindContext *context);
  void set_routine_catalog(const RoutineCatalog *catalog);
  /** Named locals for UDF / procedure parameters (checked before columns). */
  void set_local_variables(std::unordered_map<std::string, Value> locals);
  /** Looks up an outer correlated column value, if available. */
  std::optional<Value> lookup_correlated(
      const ColumnRefExpression &cref) const;

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
  /**
   * CHECK semantics: returns false only when the predicate evaluates to FALSE.
   * TRUE and UNKNOWN (NULL comparisons) both pass.
   */
  bool evaluate_check_condition(const Row &row,
                                const ExpressionPtr &condition) const;
  Value evaluate_expression(const Row &row, const ExpressionPtr &expr,
                            std::string *error_msg = nullptr) const;

  /** UPDATE SET rhs: full expression evaluation (literals, columns, arithmetic). */
  Value evaluate_dml_assignment_rhs(const Row &row,
                                    const ExpressionPtr &expr) const;

 private:
  enum class TriBool { False = 0, True = 1, Unknown = 2 };

  bool evaluate_in_predicate(const Row &row, const InExpression &in_expr) const;
  bool evaluate_exists_predicate(const Row &row,
                                 const ExistsExpression &exists_expr) const;
  TriBool evaluate_check_tri(const Row &row,
                             const ExpressionPtr &condition) const;

  std::vector<SelectColumnBinding> bindings_;
  ScalarSubqueryFn scalar_subquery_fn_;
  InSubqueryFn in_subquery_fn_;
  ExistsSubqueryFn exists_subquery_fn_;
  mutable CorrelationContext *correlation_{nullptr};
  const BindContext *bind_context_{nullptr};
  const RoutineCatalog *routine_catalog_{nullptr};
  std::unordered_map<std::string, Value> local_variables_;
};

}  // namespace db
