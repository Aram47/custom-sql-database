#include "executor/group_aggregate_operator.h"

#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

#include "executor/aggregate_function.h"
#include "executor/select_analysis.h"

namespace db {

namespace {

std::string group_key_string(const std::vector<Value> &key) {
  std::ostringstream oss;
  for (size_t i = 0; i < key.size(); ++i) {
    if (i > 0) oss << '\x1f';
    oss << key[i].to_string();
  }
  return oss.str();
}

void collect_aggregate_expressions(
    const ExpressionPtr &expr, std::set<std::string> *seen,
    std::vector<ExpressionPtr> *out) {
  if (!expr) return;
  if (auto fn = std::dynamic_pointer_cast<FunctionCallExpression>(expr)) {
    if (fn->is_windowed()) {
      for (const auto &arg : fn->get_arguments()) {
        collect_aggregate_expressions(arg, seen, out);
      }
      return;
    }
    if (is_aggregate_function_name(fn->get_function_name())) {
      const std::string key = fn->to_string();
      if (seen->insert(key).second) out->push_back(expr);
      return;
    }
    for (const auto &arg : fn->get_arguments()) {
      collect_aggregate_expressions(arg, seen, out);
    }
    return;
  }
  if (auto bin = std::dynamic_pointer_cast<BinaryOpExpression>(expr)) {
    collect_aggregate_expressions(bin->get_left(), seen, out);
    collect_aggregate_expressions(bin->get_right(), seen, out);
    return;
  }
  if (auto un = std::dynamic_pointer_cast<UnaryOpExpression>(expr)) {
    collect_aggregate_expressions(un->get_expression(), seen, out);
    return;
  }
  if (auto cs = std::dynamic_pointer_cast<CaseExpression>(expr)) {
    for (const auto &[when, then] : cs->get_when_then_pairs()) {
      collect_aggregate_expressions(when, seen, out);
      collect_aggregate_expressions(then, seen, out);
    }
    collect_aggregate_expressions(cs->get_else_expression(), seen, out);
  }
}

Value evaluate_with_aggregate_map(
    const Row &row, const ExpressionPtr &expr,
    const SelectExpressionEvaluator &row_eval,
    const std::map<std::string, Value> &agg_values) {
  if (!expr) return Value();

  if (auto fn = std::dynamic_pointer_cast<FunctionCallExpression>(expr)) {
    if (is_aggregate_function_name(fn->get_function_name())) {
      auto it = agg_values.find(fn->to_string());
      if (it != agg_values.end()) return it->second;
      return Value();
    }
    std::vector<Value> arg_vals;
    for (const auto &arg : fn->get_arguments()) {
      arg_vals.push_back(
          evaluate_with_aggregate_map(row, arg, row_eval, agg_values));
    }
    return row_eval.evaluate_expression(row, expr, nullptr);
  }

  if (std::dynamic_pointer_cast<LiteralExpression>(expr) ||
      std::dynamic_pointer_cast<ColumnRefExpression>(expr) ||
      std::dynamic_pointer_cast<IdentifierExpression>(expr)) {
    return row_eval.evaluate_expression(row, expr, nullptr);
  }

  if (auto bin = std::dynamic_pointer_cast<BinaryOpExpression>(expr)) {
    Value left = evaluate_with_aggregate_map(row, bin->get_left(), row_eval,
                                             agg_values);
    Value right = evaluate_with_aggregate_map(row, bin->get_right(), row_eval,
                                              agg_values);
    switch (bin->get_operator()) {
      case BinaryOpExpression::Operator::EQ:
        return Value(left == right);
      case BinaryOpExpression::Operator::NE:
        return Value(left != right);
      case BinaryOpExpression::Operator::LT:
        return Value(left < right);
      case BinaryOpExpression::Operator::LE:
        return Value(left <= right);
      case BinaryOpExpression::Operator::GT:
        return Value(left > right);
      case BinaryOpExpression::Operator::GE:
        return Value(left >= right);
      case BinaryOpExpression::Operator::AND:
        return Value(left.as_bool() && right.as_bool());
      case BinaryOpExpression::Operator::OR:
        return Value(left.as_bool() || right.as_bool());
      case BinaryOpExpression::Operator::PLUS:
        return left + right;
      case BinaryOpExpression::Operator::MINUS:
        return left - right;
      case BinaryOpExpression::Operator::MUL:
        return left * right;
      case BinaryOpExpression::Operator::DIV:
        return left / right;
      case BinaryOpExpression::Operator::MOD:
        if (left.is_int() && right.is_int()) {
          return Value(left.as_int() % right.as_int());
        }
        return Value();
      default:
        return Value();
    }
  }

  if (auto un = std::dynamic_pointer_cast<UnaryOpExpression>(expr)) {
    Value inner =
        evaluate_with_aggregate_map(row, un->get_expression(), row_eval,
                                    agg_values);
    if (un->get_operator() == UnaryOpExpression::Operator::NOT) {
      return Value(!inner.as_bool());
    }
    if (un->get_operator() == UnaryOpExpression::Operator::MINUS) {
      return Value() - inner;
    }
  }

  return row_eval.evaluate_expression(row, expr, nullptr);
}

bool having_true(const Row &row, const ExpressionPtr &having,
                 const SelectExpressionEvaluator &row_eval,
                 const std::map<std::string, Value> &agg_values) {
  Value v = evaluate_with_aggregate_map(row, having, row_eval, agg_values);
  return !v.is_null() && v.as_bool();
}

std::string column_header(const ExpressionPtr &expr,
                          const std::string &alias) {
  if (!alias.empty()) return alias;
  if (expr) return expr->to_string();
  return "";
}

Value select_non_aggregate_value(
    const ExpressionPtr &expr, const std::vector<Value> &group_key,
    const std::vector<ExpressionPtr> &group_by_exprs, const Row &first_row,
    const SelectExpressionEvaluator &row_eval) {
  for (size_t i = 0; i < group_by_exprs.size(); ++i) {
    if (expressions_compatible_for_grouping(expr, group_by_exprs[i])) {
      if (i < group_key.size()) return group_key[i];
    }
  }
  return row_eval.evaluate_expression(first_row, expr, nullptr);
}

}  // namespace

GroupAggregateOperator::GroupAggregateOperator(
    std::shared_ptr<SelectStatement> stmt,
    SelectExpressionEvaluator row_evaluator)
    : stmt_(std::move(stmt)), row_evaluator_(std::move(row_evaluator)) {}

QueryResult GroupAggregateOperator::apply(
    const std::vector<Row> &input_rows) const {
  if (!stmt_) {
    return QueryResult::error_result("Internal error: no statement");
  }

  const auto &group_by = stmt_->get_group_by_columns();
  const auto &select_cols = stmt_->get_select_columns();
  if (select_cols.empty()) {
    return QueryResult::error_result("No columns selected");
  }

  std::set<std::string> seen_aggs;
  std::vector<ExpressionPtr> aggregate_exprs;
  for (const auto &[expr, alias] : select_cols) {
    (void)alias;
    collect_aggregate_expressions(expr, &seen_aggs, &aggregate_exprs);
  }
  collect_aggregate_expressions(stmt_->get_having_condition(), &seen_aggs,
                              &aggregate_exprs);

  struct GroupState {
    std::vector<Row> rows;
    std::map<std::string, std::unique_ptr<IAggregateFunction>> aggregates;
  };

  std::unordered_map<std::string, GroupState> groups;

  for (const auto &row : input_rows) {
    std::vector<Value> key;
    key.reserve(group_by.size());
    for (const auto &gb_expr : group_by) {
      key.push_back(row_evaluator_.evaluate_expression(row, gb_expr, nullptr));
    }
    const std::string key_str = group_key_string(key);
    GroupState &state = groups[key_str];
    if (state.rows.empty()) {
      state.rows.push_back(row);
      for (const auto &agg_expr : aggregate_exprs) {
        auto fn = std::dynamic_pointer_cast<FunctionCallExpression>(agg_expr);
        if (!fn) continue;
        state.aggregates[fn->to_string()] =
            make_aggregate_function(fn->get_function_name(),
                                    fn->get_arguments());
        state.aggregates[fn->to_string()]->reset();
      }
    } else {
      state.rows.push_back(row);
    }

    for (const auto &agg_expr : aggregate_exprs) {
      auto fn = std::dynamic_pointer_cast<FunctionCallExpression>(agg_expr);
      if (!fn) continue;
      auto &agg = state.aggregates[fn->to_string()];
      if (!agg) continue;
      if (is_count_star(*fn)) {
        agg->accumulate(Value(static_cast<int64_t>(1)));
      } else if (fn->get_arguments().empty()) {
        agg->accumulate(Value(static_cast<int64_t>(1)));
      } else {
        Value arg_val = row_evaluator_.evaluate_expression(
            row, fn->get_arguments()[0], nullptr);
        agg->accumulate(arg_val);
      }
    }
  }

  if (groups.empty() && !aggregate_exprs.empty()) {
    std::vector<Value> empty_key;
    const std::string key_str = group_key_string(empty_key);
    GroupState &state = groups[key_str];
    for (const auto &agg_expr : aggregate_exprs) {
      auto fn = std::dynamic_pointer_cast<FunctionCallExpression>(agg_expr);
      if (!fn) continue;
      state.aggregates[fn->to_string()] =
          make_aggregate_function(fn->get_function_name(), fn->get_arguments());
      state.aggregates[fn->to_string()]->reset();
    }
  }

  QueryResult result;
  result.success = true;
  result.message = "SELECT OK";

  for (const auto &[expr, alias] : select_cols) {
    result.column_names.push_back(column_header(expr, alias));
  }

  for (auto &[key_str, state] : groups) {
    (void)key_str;
    std::map<std::string, Value> finalized;
    for (auto &[name, agg] : state.aggregates) {
      finalized[name] = agg->finalize();
    }

    const Row &rep = state.rows.empty() ? Row() : state.rows.front();

    if (stmt_->get_having_condition() &&
        !having_true(rep, stmt_->get_having_condition(), row_evaluator_,
                     finalized)) {
      continue;
    }

    std::vector<Value> group_key;
    group_key.reserve(group_by.size());
    for (const auto &gb_expr : group_by) {
      group_key.push_back(
          row_evaluator_.evaluate_expression(rep, gb_expr, nullptr));
    }

    std::vector<Value> out_row;
    for (const auto &[expr, alias] : select_cols) {
      (void)alias;
      if (auto fn = std::dynamic_pointer_cast<FunctionCallExpression>(expr)) {
        if (fn->is_windowed()) {
          out_row.push_back(Value());
          continue;
        }
      }
      if (expression_has_aggregate(expr)) {
        auto fn = std::dynamic_pointer_cast<FunctionCallExpression>(expr);
        if (fn && is_aggregate_function_name(fn->get_function_name())) {
          auto it = finalized.find(fn->to_string());
          out_row.push_back(it != finalized.end() ? it->second : Value());
        } else {
          out_row.push_back(evaluate_with_aggregate_map(rep, expr,
                                                        row_evaluator_,
                                                        finalized));
        }
      } else {
        out_row.push_back(select_non_aggregate_value(
            expr, group_key, group_by, rep, row_evaluator_));
      }
    }
    result.rows.push_back(std::move(out_row));
  }

  result.affected_rows = static_cast<int>(result.rows.size());
  return result;
}

}  // namespace db
