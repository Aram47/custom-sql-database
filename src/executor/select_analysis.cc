#include "executor/select_analysis.h"

#include <algorithm>
#include <cctype>

namespace db {

namespace {

std::string lower_copy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

bool walk_has_aggregate(const ExpressionPtr &expr) {
  if (!expr) return false;
  if (auto fn = std::dynamic_pointer_cast<FunctionCallExpression>(expr)) {
    if (fn->is_windowed()) {
      for (const auto &arg : fn->get_arguments()) {
        if (walk_has_aggregate(arg)) return true;
      }
      return false;
    }
    if (is_aggregate_function_name(fn->get_function_name())) return true;
    for (const auto &arg : fn->get_arguments()) {
      if (walk_has_aggregate(arg)) return true;
    }
    return false;
  }
  if (auto bin = std::dynamic_pointer_cast<BinaryOpExpression>(expr)) {
    return walk_has_aggregate(bin->get_left()) ||
           walk_has_aggregate(bin->get_right());
  }
  if (auto un = std::dynamic_pointer_cast<UnaryOpExpression>(expr)) {
    return walk_has_aggregate(un->get_expression());
  }
  if (auto cs = std::dynamic_pointer_cast<CaseExpression>(expr)) {
    for (const auto &[when, then] : cs->get_when_then_pairs()) {
      if (walk_has_aggregate(when) || walk_has_aggregate(then)) return true;
    }
    return walk_has_aggregate(cs->get_else_expression());
  }
  return false;
}

bool walk_has_window(const ExpressionPtr &expr) {
  if (!expr) return false;
  if (auto fn = std::dynamic_pointer_cast<FunctionCallExpression>(expr)) {
    if (fn->is_windowed()) return true;
    for (const auto &arg : fn->get_arguments()) {
      if (walk_has_window(arg)) return true;
    }
    return false;
  }
  if (auto bin = std::dynamic_pointer_cast<BinaryOpExpression>(expr)) {
    return walk_has_window(bin->get_left()) || walk_has_window(bin->get_right());
  }
  if (auto un = std::dynamic_pointer_cast<UnaryOpExpression>(expr)) {
    return walk_has_window(un->get_expression());
  }
  if (auto cs = std::dynamic_pointer_cast<CaseExpression>(expr)) {
    for (const auto &[when, then] : cs->get_when_then_pairs()) {
      if (walk_has_window(when) || walk_has_window(then)) return true;
    }
    return walk_has_window(cs->get_else_expression());
  }
  return false;
}

bool is_aggregate_argument(const ExpressionPtr &expr) {
  if (!expr) return true;
  if (std::dynamic_pointer_cast<LiteralExpression>(expr)) return true;
  if (is_wildcard_select_expression(expr)) return true;
  if (std::dynamic_pointer_cast<ColumnRefExpression>(expr)) return true;
  if (std::dynamic_pointer_cast<IdentifierExpression>(expr)) return true;
  if (auto bin = std::dynamic_pointer_cast<BinaryOpExpression>(expr)) {
    return is_aggregate_argument(bin->get_left()) &&
           is_aggregate_argument(bin->get_right());
  }
  if (auto un = std::dynamic_pointer_cast<UnaryOpExpression>(expr)) {
    return is_aggregate_argument(un->get_expression());
  }
  return false;
}

bool is_literal_or_aggregate_only(const ExpressionPtr &expr) {
  if (!expr) return true;
  if (std::dynamic_pointer_cast<LiteralExpression>(expr)) return true;
  if (auto fn = std::dynamic_pointer_cast<FunctionCallExpression>(expr)) {
    if (fn->is_windowed()) return true;
    if (is_aggregate_function_name(fn->get_function_name())) {
      for (const auto &arg : fn->get_arguments()) {
        if (!is_aggregate_argument(arg)) return false;
      }
      return true;
    }
    return false;
  }
  if (auto bin = std::dynamic_pointer_cast<BinaryOpExpression>(expr)) {
    return is_literal_or_aggregate_only(bin->get_left()) &&
           is_literal_or_aggregate_only(bin->get_right());
  }
  if (auto un = std::dynamic_pointer_cast<UnaryOpExpression>(expr)) {
    return is_literal_or_aggregate_only(un->get_expression());
  }
  return false;
}

bool matches_group_by(const ExpressionPtr &select_expr,
                      const std::vector<ExpressionPtr> &group_by) {
  for (const auto &gb : group_by) {
    if (expressions_compatible_for_grouping(select_expr, gb)) return true;
  }
  return false;
}

std::optional<std::string> validate_window_function(
    const FunctionCallExpression &fn) {
  if (!fn.is_windowed()) return std::nullopt;
  const std::string name = lower_copy(fn.get_function_name());
  if (!is_window_function_name(name)) {
    return "Unsupported window function: " + fn.get_function_name();
  }
  const auto &spec = fn.get_window_spec();
  if (!spec || spec->get_order_by().empty()) {
    return "Window OVER requires ORDER BY in v1";
  }
  const size_t argCount = fn.get_arguments().size();
  if (name == "row_number" || name == "rank" || name == "dense_rank") {
    if (argCount != 0) {
      return fn.get_function_name() + "() takes no arguments";
    }
    return std::nullopt;
  }
  if (name == "sum" || name == "avg") {
    if (argCount != 1) {
      return fn.get_function_name() + "() OVER requires exactly one argument";
    }
    return std::nullopt;
  }
  return "Unsupported window function: " + fn.get_function_name();
}

std::optional<std::string> walk_validate_windows(const ExpressionPtr &expr) {
  if (!expr) return std::nullopt;
  if (auto fn = std::dynamic_pointer_cast<FunctionCallExpression>(expr)) {
    if (auto err = validate_window_function(*fn)) return err;
    for (const auto &arg : fn->get_arguments()) {
      if (auto err = walk_validate_windows(arg)) return err;
    }
    return std::nullopt;
  }
  if (auto bin = std::dynamic_pointer_cast<BinaryOpExpression>(expr)) {
    if (auto err = walk_validate_windows(bin->get_left())) return err;
    return walk_validate_windows(bin->get_right());
  }
  if (auto un = std::dynamic_pointer_cast<UnaryOpExpression>(expr)) {
    return walk_validate_windows(un->get_expression());
  }
  if (auto cs = std::dynamic_pointer_cast<CaseExpression>(expr)) {
    for (const auto &[when, then] : cs->get_when_then_pairs()) {
      if (auto err = walk_validate_windows(when)) return err;
      if (auto err = walk_validate_windows(then)) return err;
    }
    return walk_validate_windows(cs->get_else_expression());
  }
  return std::nullopt;
}

}  // namespace

bool is_aggregate_function_name(const std::string &name) {
  const std::string n = lower_copy(name);
  return n == "count" || n == "sum" || n == "avg" || n == "min" || n == "max";
}

bool is_window_function_name(const std::string &name) {
  const std::string n = lower_copy(name);
  return n == "row_number" || n == "rank" || n == "dense_rank" || n == "sum" ||
         n == "avg";
}

bool expression_has_aggregate(const ExpressionPtr &expr) {
  return walk_has_aggregate(expr);
}

bool expression_has_window(const ExpressionPtr &expr) {
  return walk_has_window(expr);
}

bool select_has_aggregate(const std::shared_ptr<SelectStatement> &stmt) {
  if (!stmt) return false;
  for (const auto &[expr, alias] : stmt->get_select_columns()) {
    (void)alias;
    if (expression_has_aggregate(expr)) return true;
  }
  if (stmt->get_having_condition() &&
      expression_has_aggregate(stmt->get_having_condition())) {
    return true;
  }
  return false;
}

bool select_has_window(const std::shared_ptr<SelectStatement> &stmt) {
  if (!stmt) return false;
  for (const auto &[expr, alias] : stmt->get_select_columns()) {
    (void)alias;
    if (expression_has_window(expr)) return true;
  }
  return false;
}

bool needs_grouping(const std::shared_ptr<SelectStatement> &stmt) {
  if (!stmt) return false;
  if (!stmt->get_group_by_columns().empty()) return true;
  if (select_has_aggregate(stmt)) return true;
  if (stmt->get_having_condition()) return true;
  return false;
}

bool is_wildcard_select_expression(const ExpressionPtr &expr) {
  if (!expr) return false;
  if (auto col_ref = std::dynamic_pointer_cast<ColumnRefExpression>(expr)) {
    return col_ref->get_column() == "*";
  }
  if (auto ident = std::dynamic_pointer_cast<IdentifierExpression>(expr)) {
    return ident->get_name() == "*";
  }
  return false;
}

bool expressions_compatible_for_grouping(const ExpressionPtr &a,
                                         const ExpressionPtr &b) {
  if (!a || !b) return false;
  if (a->to_string() == b->to_string()) return true;
  auto ca = std::dynamic_pointer_cast<ColumnRefExpression>(a);
  auto cb = std::dynamic_pointer_cast<ColumnRefExpression>(b);
  if (ca && cb) {
    return ca->get_column() == cb->get_column() &&
           ca->get_table() == cb->get_table();
  }
  auto ia = std::dynamic_pointer_cast<IdentifierExpression>(a);
  auto ib = std::dynamic_pointer_cast<IdentifierExpression>(b);
  if (ia && ib) return ia->get_name() == ib->get_name();
  if (ca && ib)
    return ca->get_column() == ib->get_name() && ca->get_table().empty();
  if (ia && cb)
    return ia->get_name() == cb->get_column() && cb->get_table().empty();
  return false;
}

std::optional<std::string> validate_select_for_grouping(
    const std::shared_ptr<SelectStatement> &stmt) {
  if (!stmt) return "Internal error: no SELECT statement";
  const bool has_group_by = !stmt->get_group_by_columns().empty();
  const bool has_aggs = select_has_aggregate(stmt);
  const bool has_having = static_cast<bool>(stmt->get_having_condition());
  if (has_having && !has_group_by && !has_aggs) {
    return "HAVING requires GROUP BY or aggregate in SELECT";
  }
  if (has_group_by) {
    for (const auto &[expr, alias] : stmt->get_select_columns()) {
      (void)alias;
      if (is_wildcard_select_expression(expr)) {
        return "SELECT * is not allowed with GROUP BY";
      }
      if (expression_has_aggregate(expr)) continue;
      if (expression_has_window(expr)) continue;
      if (!matches_group_by(expr, stmt->get_group_by_columns())) {
        return "Column must appear in GROUP BY or be used in an aggregate: " +
               expr->to_string();
      }
    }
  } else if (has_aggs) {
    for (const auto &[expr, alias] : stmt->get_select_columns()) {
      (void)alias;
      if (is_wildcard_select_expression(expr)) {
        return "SELECT * is not allowed with aggregate functions";
      }
      if (!is_literal_or_aggregate_only(expr)) {
        return "Column must appear in GROUP BY or be used in an aggregate: " +
               expr->to_string();
      }
    }
  }
  return std::nullopt;
}

std::optional<std::string> validate_select_for_windows(
    const std::shared_ptr<SelectStatement> &stmt) {
  if (!stmt) return "Internal error: no SELECT statement";
  for (const auto &[expr, alias] : stmt->get_select_columns()) {
    (void)alias;
    if (auto err = walk_validate_windows(expr)) return err;
  }
  return std::nullopt;
}

}  // namespace db
