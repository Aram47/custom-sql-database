#include "executor/index_predicate.h"

namespace db {
namespace {

std::string column_name_of(const ExpressionPtr &expr,
                           std::string &table_qualifier) {
  table_qualifier.clear();
  if (auto col = std::dynamic_pointer_cast<ColumnRefExpression>(expr)) {
    table_qualifier = col->get_table();
    return col->get_column();
  }
  if (auto id = std::dynamic_pointer_cast<IdentifierExpression>(expr)) {
    return id->get_name();
  }
  return {};
}

bool is_literal(const ExpressionPtr &expr, Value &out) {
  if (auto lit = std::dynamic_pointer_cast<LiteralExpression>(expr)) {
    out = lit->get_value();
    return true;
  }
  return false;
}

bool try_comparison(const ExpressionPtr &expr, IndexColumnPredicate &out) {
  auto bin = std::dynamic_pointer_cast<BinaryOpExpression>(expr);
  if (!bin) {
    return false;
  }
  const auto op = bin->get_operator();
  if (op != BinaryOpExpression::Operator::EQ &&
      op != BinaryOpExpression::Operator::LT &&
      op != BinaryOpExpression::Operator::LE &&
      op != BinaryOpExpression::Operator::GT &&
      op != BinaryOpExpression::Operator::GE) {
    return false;
  }
  std::string table;
  std::string col = column_name_of(bin->get_left(), table);
  Value lit;
  bool col_on_left = true;
  if (col.empty() || !is_literal(bin->get_right(), lit)) {
    col = column_name_of(bin->get_right(), table);
    if (col.empty() || !is_literal(bin->get_left(), lit)) {
      return false;
    }
    col_on_left = false;
  }
  out.column_name = col;
  out.table_qualifier = table;
  out.literal = lit;
  out.has_upper = false;
  if (col_on_left) {
    switch (op) {
      case BinaryOpExpression::Operator::EQ:
        out.op = IndexCompareOp::Equal;
        break;
      case BinaryOpExpression::Operator::LT:
        out.op = IndexCompareOp::Less;
        break;
      case BinaryOpExpression::Operator::LE:
        out.op = IndexCompareOp::LessEqual;
        break;
      case BinaryOpExpression::Operator::GT:
        out.op = IndexCompareOp::Greater;
        break;
      case BinaryOpExpression::Operator::GE:
        out.op = IndexCompareOp::GreaterEqual;
        break;
      default:
        return false;
    }
  } else {
    switch (op) {
      case BinaryOpExpression::Operator::EQ:
        out.op = IndexCompareOp::Equal;
        break;
      case BinaryOpExpression::Operator::LT:
        out.op = IndexCompareOp::Greater;
        break;
      case BinaryOpExpression::Operator::LE:
        out.op = IndexCompareOp::GreaterEqual;
        break;
      case BinaryOpExpression::Operator::GT:
        out.op = IndexCompareOp::Less;
        break;
      case BinaryOpExpression::Operator::GE:
        out.op = IndexCompareOp::LessEqual;
        break;
      default:
        return false;
    }
  }
  return true;
}

bool collect_predicates(const ExpressionPtr &expr,
                        std::vector<IndexColumnPredicate> &out) {
  if (!expr) {
    return true;
  }
  auto bin = std::dynamic_pointer_cast<BinaryOpExpression>(expr);
  if (bin && bin->get_operator() == BinaryOpExpression::Operator::AND) {
    return collect_predicates(bin->get_left(), out) &&
           collect_predicates(bin->get_right(), out);
  }
  if (bin && bin->get_operator() == BinaryOpExpression::Operator::OR) {
    return false;
  }
  IndexColumnPredicate pred;
  if (!try_comparison(expr, pred)) {
    return false;
  }
  out.push_back(pred);
  return true;
}

}  // namespace

std::optional<std::vector<IndexColumnPredicate>> extract_index_predicates(
    const ExpressionPtr &expr) {
  if (!expr) {
    return std::vector<IndexColumnPredicate>{};
  }
  std::vector<IndexColumnPredicate> preds;
  if (!collect_predicates(expr, preds)) {
    return std::nullopt;
  }
  return preds;
}

bool try_extract_equi_join_columns(const ExpressionPtr &on_expr,
                                   std::string &left_table,
                                   std::string &left_column,
                                   std::string &right_table,
                                   std::string &right_column) {
  auto bin = std::dynamic_pointer_cast<BinaryOpExpression>(on_expr);
  if (!bin || bin->get_operator() != BinaryOpExpression::Operator::EQ) {
    return false;
  }
  left_column = column_name_of(bin->get_left(), left_table);
  right_column = column_name_of(bin->get_right(), right_table);
  return !left_column.empty() && !right_column.empty();
}

}  // namespace db
