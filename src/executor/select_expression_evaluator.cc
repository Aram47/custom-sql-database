#include "executor/select_expression_evaluator.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "executor/scalar_function.h"
#include "core/routine_catalog.h"
#include "executor/select_analysis.h"
#include "types/type_converter.h"

namespace db {

namespace {

std::optional<std::string> validate_inner(const ExpressionPtr &expr,
                                          const SelectExpressionEvaluator &ev) {
  if (!expr) return std::nullopt;
  if (auto cref = std::dynamic_pointer_cast<ColumnRefExpression>(expr)) {
    std::string err;
    if (ev.resolve_column_index(*cref, &err) < 0) {
      if (ev.lookup_correlated(*cref)) {
        return std::nullopt;
      }
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
  if (auto in_expr = std::dynamic_pointer_cast<InExpression>(expr)) {
    if (auto e = validate_inner(in_expr->get_left(), ev)) return e;
    if (in_expr->has_subquery()) {
      return std::nullopt;
    }
    for (const auto &value_expr : in_expr->get_values()) {
      if (auto e = validate_inner(value_expr, ev)) return e;
    }
    return std::nullopt;
  }
  if (auto exists_expr = std::dynamic_pointer_cast<ExistsExpression>(expr)) {
    (void)exists_expr;
    return std::nullopt;
  }
  if (auto fn = std::dynamic_pointer_cast<FunctionCallExpression>(expr)) {
    for (const auto &arg : fn->get_arguments()) {
      if (auto e = validate_inner(arg, ev)) return e;
    }
    return std::nullopt;
  }
  if (auto cast_expr = std::dynamic_pointer_cast<CastExpression>(expr)) {
    return validate_inner(cast_expr->get_expression(), ev);
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

/** SQL three-valued IN: nullopt means UNKNOWN (treated as false in WHERE). */
std::optional<bool> match_in_list(const Value &left,
                                  const std::vector<Value> &values) {
  if (left.is_null()) {
    return std::nullopt;
  }
  bool saw_null = false;
  for (const Value &candidate : values) {
    if (candidate.is_null()) {
      saw_null = true;
      continue;
    }
    if (candidate == left) {
      return true;
    }
  }
  if (saw_null) {
    return std::nullopt;
  }
  return false;
}

}  // namespace

SelectExpressionEvaluator::SelectExpressionEvaluator(
    std::vector<SelectColumnBinding> bindings)
    : bindings_(std::move(bindings)) {}

void SelectExpressionEvaluator::set_scalar_subquery_fn(ScalarSubqueryFn fn) {
  scalar_subquery_fn_ = std::move(fn);
}

void SelectExpressionEvaluator::set_in_subquery_fn(InSubqueryFn fn) {
  in_subquery_fn_ = std::move(fn);
}

void SelectExpressionEvaluator::set_exists_subquery_fn(ExistsSubqueryFn fn) {
  exists_subquery_fn_ = std::move(fn);
}

void SelectExpressionEvaluator::set_correlation_context(
    CorrelationContext *context) {
  correlation_ = context;
}

void SelectExpressionEvaluator::set_bind_context(const BindContext *context) {
  bind_context_ = context;
}

void SelectExpressionEvaluator::set_routine_catalog(
    const RoutineCatalog *catalog) {
  routine_catalog_ = catalog;
}

void SelectExpressionEvaluator::set_local_variables(
    std::unordered_map<std::string, Value> locals) {
  local_variables_ = std::move(locals);
}

std::optional<Value> SelectExpressionEvaluator::lookup_correlated(
    const ColumnRefExpression &cref) const {
  if (!correlation_) {
    return std::nullopt;
  }
  return correlation_->lookup(cref);
}

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
      if ((pref_ok && e.column_name == col) ||
          e.column_name == prefix + "." + col) {
        matches.push_back(i);
      }
    } else if (e.column_name == col) {
      matches.push_back(i);
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

bool SelectExpressionEvaluator::evaluate_in_predicate(
    const Row &row, const InExpression &in_expr) const {
  Value left = evaluate_expression(row, in_expr.get_left(), nullptr);
  if (in_expr.has_subquery()) {
    auto sub =
        std::dynamic_pointer_cast<SubqueryExpression>(in_expr.get_subquery());
    if (!sub || !in_subquery_fn_) {
      return false;
    }
    if (correlation_) {
      correlation_->pushFrame(bindings_, row);
    }
    std::string err;
    bool ok =
        in_subquery_fn_(left, sub->get_select(), in_expr.is_not(), &err);
    if (correlation_) {
      correlation_->popFrame();
    }
    return ok;
  }
  std::vector<Value> values;
  values.reserve(in_expr.get_values().size());
  for (const auto &value_expr : in_expr.get_values()) {
    values.push_back(evaluate_expression(row, value_expr, nullptr));
  }
  std::optional<bool> matched = match_in_list(left, values);
  if (!matched.has_value()) {
    return false;
  }
  return in_expr.is_not() ? !*matched : *matched;
}

bool SelectExpressionEvaluator::evaluate_exists_predicate(
    const Row &row, const ExistsExpression &exists_expr) const {
  if (!exists_subquery_fn_) {
    return false;
  }
  if (correlation_) {
    correlation_->pushFrame(bindings_, row);
  }
  std::string err;
  bool exists = exists_subquery_fn_(exists_expr.get_select(), &err);
  if (correlation_) {
    correlation_->popFrame();
  }
  return exists_expr.is_not() ? !exists : exists;
}

bool SelectExpressionEvaluator::evaluate_condition(
    const Row &row, const ExpressionPtr &condition) const {
  if (!condition) return true;
  auto in_expr = std::dynamic_pointer_cast<InExpression>(condition);
  if (in_expr) {
    return evaluate_in_predicate(row, *in_expr);
  }
  auto exists_expr = std::dynamic_pointer_cast<ExistsExpression>(condition);
  if (exists_expr) {
    return evaluate_exists_predicate(row, *exists_expr);
  }
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

bool SelectExpressionEvaluator::evaluate_check_condition(
    const Row &row, const ExpressionPtr &condition) const {
  return evaluate_check_tri(row, condition) != TriBool::False;
}

SelectExpressionEvaluator::TriBool SelectExpressionEvaluator::evaluate_check_tri(
    const Row &row, const ExpressionPtr &condition) const {
  if (!condition) {
    return TriBool::True;
  }
  auto bin_op = std::dynamic_pointer_cast<BinaryOpExpression>(condition);
  if (bin_op) {
    const auto op = bin_op->get_operator();
    if (op == BinaryOpExpression::Operator::AND) {
      const TriBool left = evaluate_check_tri(row, bin_op->get_left());
      if (left == TriBool::False) {
        return TriBool::False;
      }
      const TriBool right = evaluate_check_tri(row, bin_op->get_right());
      if (right == TriBool::False) {
        return TriBool::False;
      }
      if (left == TriBool::Unknown || right == TriBool::Unknown) {
        return TriBool::Unknown;
      }
      return TriBool::True;
    }
    if (op == BinaryOpExpression::Operator::OR) {
      const TriBool left = evaluate_check_tri(row, bin_op->get_left());
      if (left == TriBool::True) {
        return TriBool::True;
      }
      const TriBool right = evaluate_check_tri(row, bin_op->get_right());
      if (right == TriBool::True) {
        return TriBool::True;
      }
      if (left == TriBool::Unknown || right == TriBool::Unknown) {
        return TriBool::Unknown;
      }
      return TriBool::False;
    }
    Value left = evaluate_expression(row, bin_op->get_left(), nullptr);
    Value right = evaluate_expression(row, bin_op->get_right(), nullptr);
    if (left.is_null() || right.is_null()) {
      return TriBool::Unknown;
    }
    bool result = false;
    switch (op) {
      case BinaryOpExpression::Operator::EQ:
        result = left == right;
        break;
      case BinaryOpExpression::Operator::NE:
        result = left != right;
        break;
      case BinaryOpExpression::Operator::LT:
        result = left < right;
        break;
      case BinaryOpExpression::Operator::LE:
        result = left <= right;
        break;
      case BinaryOpExpression::Operator::GT:
        result = left > right;
        break;
      case BinaryOpExpression::Operator::GE:
        result = left >= right;
        break;
      default:
        return TriBool::True;
    }
    return result ? TriBool::True : TriBool::False;
  }
  auto unary_op = std::dynamic_pointer_cast<UnaryOpExpression>(condition);
  if (unary_op && unary_op->get_operator() == UnaryOpExpression::Operator::NOT) {
    const TriBool inner = evaluate_check_tri(row, unary_op->get_expression());
    if (inner == TriBool::Unknown) {
      return TriBool::Unknown;
    }
    return inner == TriBool::True ? TriBool::False : TriBool::True;
  }
  auto in_expr = std::dynamic_pointer_cast<InExpression>(condition);
  if (in_expr) {
    return evaluate_in_predicate(row, *in_expr) ? TriBool::True : TriBool::False;
  }
  return TriBool::True;
}

Value SelectExpressionEvaluator::evaluate_expression(
    const Row &row, const ExpressionPtr &expr, std::string *error_msg) const {
  auto literal = std::dynamic_pointer_cast<LiteralExpression>(expr);
  if (literal) return literal->get_value();
  auto subquery = std::dynamic_pointer_cast<SubqueryExpression>(expr);
  if (subquery) {
    if (!scalar_subquery_fn_) {
      if (error_msg) *error_msg = "Subquery evaluation not available";
      return Value();
    }
    if (correlation_) {
      correlation_->pushFrame(bindings_, row);
    }
    Value result = scalar_subquery_fn_(subquery->get_select(), error_msg);
    if (correlation_) {
      correlation_->popFrame();
    }
    return result;
  }
  auto in_expr = std::dynamic_pointer_cast<InExpression>(expr);
  if (in_expr) {
    return Value(evaluate_in_predicate(row, *in_expr));
  }
  auto exists_expr = std::dynamic_pointer_cast<ExistsExpression>(expr);
  if (exists_expr) {
    return Value(evaluate_exists_predicate(row, *exists_expr));
  }
  auto param = std::dynamic_pointer_cast<ParameterExpression>(expr);
  if (param) {
    if (!bind_context_) {
      if (error_msg) *error_msg = "Bind parameter used without EXECUTE args";
      return Value();
    }
    try {
      return bind_context_->getValue(param->get_index());
    } catch (const std::exception &e) {
      if (error_msg) *error_msg = e.what();
      return Value();
    }
  }
  auto col_ref = std::dynamic_pointer_cast<ColumnRefExpression>(expr);
  if (col_ref) {
    if (col_ref->get_table().empty()) {
      auto local_it = local_variables_.find(col_ref->get_column());
      if (local_it != local_variables_.end()) {
        return local_it->second;
      }
    }
    std::string err;
    int idx = resolve_column_index(*col_ref, &err);
    if (idx < 0) {
      auto correlated = lookup_correlated(*col_ref);
      if (correlated) {
        return *correlated;
      }
      if (error_msg) *error_msg = err;
      return Value();
    }
    if (static_cast<size_t>(idx) < row.get_column_count()) {
      return row.get_value(static_cast<size_t>(idx));
    }
    return Value();
  }
  auto ident = std::dynamic_pointer_cast<IdentifierExpression>(expr);
  if (ident) {
    ColumnRefExpression cref(ident->get_name());
    return evaluate_expression(row, std::make_shared<ColumnRefExpression>(cref),
                               error_msg);
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
  auto unary_op = std::dynamic_pointer_cast<UnaryOpExpression>(expr);
  if (unary_op) {
    Value inner = evaluate_expression(row, unary_op->get_expression(), error_msg);
    if (unary_op->get_operator() == UnaryOpExpression::Operator::NOT) {
      if (inner.is_null()) {
        return Value();
      }
      return Value(!inner.as_bool());
    }
    if (unary_op->get_operator() == UnaryOpExpression::Operator::MINUS) {
      if (inner.is_null()) {
        return Value();
      }
      if (inner.is_int()) {
        return Value(-inner.as_int());
      }
      if (inner.is_float()) {
        return Value(-inner.as_float());
      }
      return Value();
    }
  }
  auto cast_expr = std::dynamic_pointer_cast<CastExpression>(expr);
  if (cast_expr) {
    Value inner =
        evaluate_expression(row, cast_expr->get_expression(), error_msg);
    if (inner.is_null()) {
      return Value();
    }
    try {
      return TypeConverter::string_to_value(inner.to_string(),
                                            cast_expr->get_target_type());
    } catch (const std::exception &ex) {
      if (error_msg) {
        *error_msg = ex.what();
      }
      return Value();
    }
  }
  auto func = std::dynamic_pointer_cast<FunctionCallExpression>(expr);
  if (func) {
    if (func->is_windowed()) {
      return Value();
    }
    std::string func_name = func->get_function_name();
    std::transform(func_name.begin(), func_name.end(), func_name.begin(),
                   [](unsigned char c) {
                     return static_cast<char>(std::tolower(c));
                   });
    const auto &args = func->get_arguments();
    if (func_name == "count" || func_name == "sum" || func_name == "avg" ||
        func_name == "min" || func_name == "max") {
      if (error_msg) {
        *error_msg = "Aggregate functions require GROUP BY or grouping context";
      }
      return Value();
    }
    std::vector<Value> values;
    values.reserve(args.size());
    for (const auto &arg : args) {
      values.push_back(evaluate_expression(row, arg, error_msg));
      if (error_msg && !error_msg->empty()) {
        return Value();
      }
    }
    if (ScalarFunctionRegistry::instance().hasFunction(func_name)) {
      return ScalarFunctionRegistry::instance().evaluate(func_name, values,
                                                         error_msg);
    }
    if (routine_catalog_) {
      const FunctionDefinition *udf = routine_catalog_->getFunction(func_name);
      if (!udf) {
        // try original case-sensitive name from AST
        udf = routine_catalog_->getFunction(func->get_function_name());
      }
      if (udf) {
        if (values.size() != udf->getParams().size()) {
          if (error_msg) {
            *error_msg = "Wrong number of arguments for function " +
                         udf->getName();
          }
          return Value();
        }
        std::unordered_map<std::string, Value> locals;
        for (size_t i = 0; i < udf->getParams().size(); ++i) {
          locals[udf->getParams()[i].name] = values[i];
        }
        SelectExpressionEvaluator body_eval({});
        body_eval.set_routine_catalog(routine_catalog_);
        body_eval.set_local_variables(std::move(locals));
        return body_eval.evaluate_expression(Row(), udf->getBody(), error_msg);
      }
    }
    if (error_msg) {
      *error_msg = "Unknown function: " + func_name;
    }
    return Value();
  }
  return Value();
}

Value SelectExpressionEvaluator::evaluate_dml_assignment_rhs(
    const Row &row, const ExpressionPtr &expr) const {
  return evaluate_expression(row, expr, nullptr);
}

}  // namespace db
