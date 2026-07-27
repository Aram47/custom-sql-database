#include "core/check_constraint.h"

#include <algorithm>
#include <cctype>

#include "core/table.h"
#include "executor/select_column_binding.h"
#include "executor/select_expression_evaluator.h"
#include "parser/ast.h"
#include "parser/parser.h"
#include "utils/exceptions.h"

namespace db {
namespace {

std::string to_upper_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::toupper(ch));
                 });
  return value;
}

bool is_aggregate_name(const std::string &name) {
  const std::string upper = to_upper_copy(name);
  return upper == "COUNT" || upper == "SUM" || upper == "AVG" ||
         upper == "MIN" || upper == "MAX";
}

std::string allocate_check_constraint_name(const Table &table) {
  size_t index = 1;
  while (true) {
    const std::string candidate = "ck_" + std::to_string(index);
    bool is_used = false;
    for (const CheckConstraintDefinition &existing : table.get_checks()) {
      if (existing.name == candidate) {
        is_used = true;
        break;
      }
    }
    if (!is_used) {
      return candidate;
    }
    ++index;
  }
}

}  // namespace

bool is_check_expression_allowed(const ExpressionPtr &predicate) {
  if (!predicate) {
    return false;
  }
  if (std::dynamic_pointer_cast<ExistsExpression>(predicate) ||
      std::dynamic_pointer_cast<SubqueryExpression>(predicate)) {
    return false;
  }
  auto in_expr = std::dynamic_pointer_cast<InExpression>(predicate);
  if (in_expr) {
    if (in_expr->has_subquery()) {
      return false;
    }
    if (!is_check_expression_allowed(in_expr->get_left())) {
      return false;
    }
    for (const ExpressionPtr &value : in_expr->get_values()) {
      if (!is_check_expression_allowed(value)) {
        return false;
      }
    }
    return true;
  }
  auto fn_expr = std::dynamic_pointer_cast<FunctionCallExpression>(predicate);
  if (fn_expr) {
    if (is_aggregate_name(fn_expr->get_function_name())) {
      return false;
    }
    for (const ExpressionPtr &arg : fn_expr->get_arguments()) {
      if (!is_check_expression_allowed(arg)) {
        return false;
      }
    }
    return true;
  }
  auto bin_op = std::dynamic_pointer_cast<BinaryOpExpression>(predicate);
  if (bin_op) {
    return is_check_expression_allowed(bin_op->get_left()) &&
           is_check_expression_allowed(bin_op->get_right());
  }
  auto unary_op = std::dynamic_pointer_cast<UnaryOpExpression>(predicate);
  if (unary_op) {
    return is_check_expression_allowed(unary_op->get_expression());
  }
  auto case_expr = std::dynamic_pointer_cast<CaseExpression>(predicate);
  if (case_expr) {
    for (const auto &when_then : case_expr->get_when_then_pairs()) {
      if (!is_check_expression_allowed(when_then.first) ||
          !is_check_expression_allowed(when_then.second)) {
        return false;
      }
    }
    if (case_expr->get_else_expression() &&
        !is_check_expression_allowed(case_expr->get_else_expression())) {
      return false;
    }
    return true;
  }
  return true;
}

ExpressionPtr parse_check_expression(const std::string &expression_text) {
  try {
    Parser parser(expression_text);
    return parser.parse_standalone_expression();
  } catch (const std::exception &e) {
    throw ParseException(std::string("Invalid CHECK expression: ") + e.what());
  }
}

void prepare_check_constraint(Table *table, CheckConstraintDefinition &check) {
  if (!table) {
    throw ConstraintException("Internal error: no table for CHECK");
  }
  if (!check.predicate) {
    throw ConstraintException("CHECK constraint requires an expression");
  }
  if (!is_check_expression_allowed(check.predicate)) {
    throw ConstraintException(
        "CHECK expression must not contain subqueries or aggregates");
  }
  if (check.expression_text.empty()) {
    check.expression_text = check.predicate->to_string();
  }
  if (check.name.empty()) {
    check.name = allocate_check_constraint_name(*table);
  }
  std::vector<SelectColumnBinding> bindings;
  const std::string &table_name = table->get_name();
  for (const Column &col : table->get_columns()) {
    bindings.push_back({table_name, table_name, col.get_name()});
  }
  SelectExpressionEvaluator evaluator(std::move(bindings));
  if (auto err = evaluator.validate_expression_tree(check.predicate)) {
    throw ConstraintException(*err);
  }
}

}  // namespace db
