#include "executor/select_expression_evaluator.h"

#include <algorithm>
#include <sstream>

namespace db {

namespace {

std::optional<std::string> validate_inner(const ExpressionPtr &expr,
                                          const SelectExpressionEvaluator &ev) {
  if (!expr) return std::nullopt;

  if (auto cref = std::dynamic_pointer_cast<ColumnRefExpression>(expr)) {
    std::string err;
    if (ev.resolve_column_index(*cref, &err) < 0) {
      return err;
    }
    return std::nullopt;
  }

  if (auto bin = std::dynamic_pointer_cast<BinaryOpExpression>(expr)) {
    if (auto e = validate_inner(bin->get_left(), ev)) return e;
    return validate_inner(bin->get_right(), ev);
  }

  if (auto un = std::dynamic_pointer_cast<UnaryOpExpression>(expr)) {
    return validate_inner(un->get_expression(), ev);
  }

  if (auto fn = std::dynamic_pointer_cast<FunctionCallExpression>(expr)) {
    for (const auto &arg : fn->get_arguments()) {
      if (auto e = validate_inner(arg, ev)) return e;
    }
    return std::nullopt;
  }

  if (auto cs = std::dynamic_pointer_cast<CaseExpression>(expr)) {
    for (const auto &[when, then] : cs->get_when_then_pairs()) {
      if (auto e = validate_inner(when, ev)) return e;
      if (auto e = validate_inner(then, ev)) return e;
    }
    if (auto e = validate_inner(cs->get_else_expression(), ev)) {
      return e;
    }
    return std::nullopt;
  }

  return std::nullopt;
}

}  // namespace

SelectExpressionEvaluator::SelectExpressionEvaluator(
    std::vector<SelectColumnBinding> bindings)
    : bindings_(std::move(bindings)) {}

size_t SelectExpressionEvaluator::binding_count() const {
  return bindings_.size();
}

std::string SelectExpressionEvaluator::qualified_header(
    size_t binding_index) const {
  const auto &b = bindings_.at(binding_index);
  return b.alias.empty() ? b.column_name : b.alias + "." + b.column_name;
}

int SelectExpressionEvaluator::resolve_column_index(
    const ColumnRefExpression &cref, std::string *error_msg) const {
  const std::string &prefix = cref.get_table();
  const std::string &col = cref.get_column();

  std::vector<size_t> matches;
  for (size_t i = 0; i < bindings_.size(); ++i) {
    const auto &e = bindings_[i];
    if (!prefix.empty()) {
      const bool pref_ok =
          (e.alias == prefix || e.physical_table == prefix);
      if (pref_ok && e.column_name == col) {
        matches.push_back(i);
      }
    } else {
      if (e.column_name == col) {
        matches.push_back(i);
      }
    }
  }

  if (matches.empty()) {
    if (error_msg) {
      std::ostringstream oss;
      oss << "Unknown column '" << cref.to_string() << "'";
      *error_msg = oss.str();
    }
    return -1;
  }

  if (matches.size() > 1 && prefix.empty()) {
    if (error_msg) {
      *error_msg = "Ambiguous column '" + col + "'";
    }
    return -1;
  }

  return static_cast<int>(matches[0]);
}

std::optional<std::string> SelectExpressionEvaluator::validate_expression_tree(
    const ExpressionPtr &expr) const {
  return validate_inner(expr, *this);
}

bool SelectExpressionEvaluator::evaluate_condition(
    const Row &row, const ExpressionPtr &condition) const {
  if (!condition) return true;

  auto bin_op = std::dynamic_pointer_cast<BinaryOpExpression>(condition);
  if (bin_op) {
    Value left =
        evaluate_expression(row, bin_op->get_left(), nullptr);
    Value right =
        evaluate_expression(row, bin_op->get_right(), nullptr);

    switch (bin_op->get_operator()) {
      case BinaryOpExpression::Operator::EQ:
        return left == right;
      case BinaryOpExpression::Operator::NE:
        return left != right;
      case BinaryOpExpression::Operator::LT:
        return left < right;
      case BinaryOpExpression::Operator::LE:
        return left <= right;
      case BinaryOpExpression::Operator::GT:
        return left > right;
      case BinaryOpExpression::Operator::GE:
        return left >= right;
      case BinaryOpExpression::Operator::AND:
        return evaluate_condition(row, bin_op->get_left()) &&
               evaluate_condition(row, bin_op->get_right());
      case BinaryOpExpression::Operator::OR:
        return evaluate_condition(row, bin_op->get_left()) ||
               evaluate_condition(row, bin_op->get_right());
      default:
        return true;
    }
  }

  auto unary_op = std::dynamic_pointer_cast<UnaryOpExpression>(condition);
  if (unary_op && unary_op->get_operator() == UnaryOpExpression::Operator::NOT) {
    return !evaluate_condition(row, unary_op->get_expression());
  }

  return true;
}

Value SelectExpressionEvaluator::evaluate_expression(
    const Row &row, const ExpressionPtr &expr, std::string *error_msg) const {
  auto literal = std::dynamic_pointer_cast<LiteralExpression>(expr);
  if (literal) return literal->get_value();

  auto col_ref = std::dynamic_pointer_cast<ColumnRefExpression>(expr);
  if (col_ref) {
    std::string err;
    int idx = resolve_column_index(*col_ref, &err);
    if (idx < 0) {
      if (error_msg) *error_msg = err;
      return Value();
    }
    if (static_cast<size_t>(idx) < row.get_column_count()) {
      return row.get_value(static_cast<size_t>(idx));
    }
    return Value();
  }

  auto bin_op = std::dynamic_pointer_cast<BinaryOpExpression>(expr);
  if (bin_op) {
    Value left = evaluate_expression(row, bin_op->get_left(), error_msg);
    Value right = evaluate_expression(row, bin_op->get_right(), error_msg);

    switch (bin_op->get_operator()) {
      case BinaryOpExpression::Operator::PLUS:
        return left + right;
      case BinaryOpExpression::Operator::MINUS:
        return left - right;
      case BinaryOpExpression::Operator::MUL:
        return left * right;
      case BinaryOpExpression::Operator::DIV:
        return left / right;
      case BinaryOpExpression::Operator::MOD: {
        if (left.is_int() && right.is_int()) {
          return Value(left.as_int() % right.as_int());
        }
        return Value();
      }
      default:
        return Value();
    }
  }

  auto func = std::dynamic_pointer_cast<FunctionCallExpression>(expr);
  if (func) {
    auto func_name = func->get_function_name();
    std::transform(func_name.begin(), func_name.end(), func_name.begin(),
                   ::tolower);

    const auto &args = func->get_arguments();
    if (func_name == "count" && !args.empty()) {
      return Value(static_cast<int64_t>(1));
    }
    if (func_name == "upper" && !args.empty()) {
      Value val = evaluate_expression(row, args[0], error_msg);
      std::string str = val.as_string();
      std::transform(str.begin(), str.end(), str.begin(), ::toupper);
      return Value(str);
    }
    if (func_name == "lower" && !args.empty()) {
      Value val = evaluate_expression(row, args[0], error_msg);
      std::string str = val.as_string();
      std::transform(str.begin(), str.end(), str.begin(), ::tolower);
      return Value(str);
    }
    if (func_name == "length" && !args.empty()) {
      Value val = evaluate_expression(row, args[0], error_msg);
      return Value(static_cast<int64_t>(val.as_string().length()));
    }
  }

  return Value();
}

Value SelectExpressionEvaluator::evaluate_dml_assignment_rhs(
    const Row &row, const ExpressionPtr &expr) const {
  auto literal = std::dynamic_pointer_cast<LiteralExpression>(expr);
  if (literal) return literal->get_value();

  auto col_ref = std::dynamic_pointer_cast<ColumnRefExpression>(expr);
  if (col_ref) {
    std::string err;
    const int idx = resolve_column_index(*col_ref, &err);
    if (idx < 0) return Value();
    if (static_cast<size_t>(idx) < row.get_column_count()) {
      return row.get_value(static_cast<size_t>(idx));
    }
    return Value();
  }

  return Value();
}

}  // namespace db
