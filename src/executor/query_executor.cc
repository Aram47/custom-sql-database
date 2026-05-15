#include "executor/query_executor.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "types/type_converter.h"

namespace db {

// ==================== SelectExecutor ====================

SelectExecutor::SelectExecutor(std::shared_ptr<SelectStatement> stmt,
                               Table *table)
    : stmt_(stmt), table_(table) {}

QueryResult SelectExecutor::execute() {
  QueryResult result;
  result.success = true;

  if (!table_) {
    return QueryResult::error_result("Table not found");
  }

  std::vector<Row> all_rows = table_->get_all_rows();
  std::vector<std::string> col_names;
  for (const auto &col : table_->get_columns()) {
    col_names.push_back(col.get_name());
  }

  // Filter rows by WHERE clause
  std::vector<Row> filtered_rows;
  for (const auto &row : all_rows) {
    if (!stmt_->get_where_condition() ||
        evaluate_condition(row, col_names, stmt_->get_where_condition())) {
      filtered_rows.push_back(row);
    }
  }

  // Extract selected columns
  const auto &select_cols = stmt_->get_select_columns();
  if (select_cols.empty()) {
    return QueryResult::error_result("No columns selected");
  }

  const auto isWildcardColumn = [](const ExpressionPtr &e) -> bool {
    auto col_ref = std::dynamic_pointer_cast<ColumnRefExpression>(e);
    if (col_ref && col_ref->get_column() == "*") return true;
    auto ident = std::dynamic_pointer_cast<IdentifierExpression>(e);
    return ident && ident->get_name() == "*";
  };

  // Build result column names
  for (const auto &[expr, alias] : select_cols) {
    if (isWildcardColumn(expr)) {
      for (const auto &col : table_->get_columns()) {
        result.column_names.push_back(col.get_name());
      }
    } else if (!alias.empty()) {
      result.column_names.push_back(alias);
    } else if (expr) {
      result.column_names.push_back(expr->to_string());
    }
  }

  // Build result rows
  for (const auto &row : filtered_rows) {
    std::vector<Value> resultRow;

    for (const auto &[expr, alias] : select_cols) {
      if (isWildcardColumn(expr)) {
        for (size_t i = 0; i < row.get_column_count(); ++i) {
          resultRow.push_back(row.get_value(i));
        }
      } else {
        resultRow.push_back(evaluate_expression(row, col_names, expr));
      }
    }

    result.rows.push_back(resultRow);
  }

  // Apply DISTINCT (sort so std::unique removes all duplicates)
  if (stmt_->is_distinct()) {
    const auto rowLess = [](const std::vector<Value> &a,
                            const std::vector<Value> &b) {
      const size_t n = std::min(a.size(), b.size());
      for (size_t i = 0; i < n; ++i) {
        if (!(a[i] == b[i])) return a[i] < b[i];
      }
      return a.size() < b.size();
    };
    const auto rowEqual = [](const std::vector<Value> &a,
                             const std::vector<Value> &b) {
      if (a.size() != b.size()) return false;
      for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) return false;
      }
      return true;
    };
    std::sort(result.rows.begin(), result.rows.end(), rowLess);
    const auto last =
        std::unique(result.rows.begin(), result.rows.end(), rowEqual);
    result.rows.erase(last, result.rows.end());
  }

  result.affected_rows = result.rows.size();
  result.message = "SELECT OK";

  return result;
}

bool SelectExecutor::evaluate_condition(
    const Row &row, const std::vector<std::string> &col_names,
    const ExpressionPtr &condition) const {
  if (!condition) return true;

  auto bin_op = std::dynamic_pointer_cast<BinaryOpExpression>(condition);
  if (bin_op) {
    Value left = evaluate_expression(row, col_names, bin_op->get_left());
    Value right = evaluate_expression(row, col_names, bin_op->get_right());

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
        return evaluate_condition(row, col_names, bin_op->get_left()) &&
               evaluate_condition(row, col_names, bin_op->get_right());
      case BinaryOpExpression::Operator::OR:
        return evaluate_condition(row, col_names, bin_op->get_left()) ||
               evaluate_condition(row, col_names, bin_op->get_right());
      default:
        return true;
    }
  }

  auto unaryOp = std::dynamic_pointer_cast<UnaryOpExpression>(condition);
  if (unaryOp) {
    if (unaryOp->get_operator() == UnaryOpExpression::Operator::NOT) {
      return !evaluate_condition(row, col_names, unaryOp->get_expression());
    }
  }

  return true;
}

Value SelectExecutor::evaluate_expression(
    const Row &row, const std::vector<std::string> &col_names,
    const ExpressionPtr &expr) const {
  auto literal = std::dynamic_pointer_cast<LiteralExpression>(expr);
  if (literal) return literal->get_value();

  auto col_ref = std::dynamic_pointer_cast<ColumnRefExpression>(expr);
  if (col_ref) {
    int idx = -1;
    for (size_t i = 0; i < col_names.size(); ++i) {
      if (col_names[i] == col_ref->get_column()) {
        idx = i;
        break;
      }
    }
    if (idx >= 0 && idx < (int)row.get_column_count()) {
      return row.get_value(idx);
    }
    return Value();
  }

  auto bin_op = std::dynamic_pointer_cast<BinaryOpExpression>(expr);
  if (bin_op) {
    Value left = evaluate_expression(row, col_names, bin_op->get_left());
    Value right = evaluate_expression(row, col_names, bin_op->get_right());

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
      return Value((int64_t)1);  // Simplified count (used in SELECT context)
    } else if (func_name == "upper" && !args.empty()) {
      Value val = evaluate_expression(row, col_names, args[0]);
      std::string str = val.as_string();
      std::transform(str.begin(), str.end(), str.begin(), ::toupper);
      return Value(str);
    } else if (func_name == "lower" && !args.empty()) {
      Value val = evaluate_expression(row, col_names, args[0]);
      std::string str = val.as_string();
      std::transform(str.begin(), str.end(), str.begin(), ::tolower);
      return Value(str);
    } else if (func_name == "length" && !args.empty()) {
      Value val = evaluate_expression(row, col_names, args[0]);
      return Value((int64_t)val.as_string().length());
    }
  }

  return Value();
}

// ==================== InsertExecutor ====================

InsertExecutor::InsertExecutor(std::shared_ptr<InsertStatement> stmt,
                               Table *table)
    : stmt_(stmt), table_(table) {}

QueryResult InsertExecutor::execute() {
  if (!table_) {
    return QueryResult::error_result("Table not found");
  }

  const auto &columns = stmt_->get_columns();
  const auto &values = stmt_->get_values();

  if (values.empty()) {
    return QueryResult::error_result("No values to insert");
  }

  int inserted_count = 0;

  for (const auto &valueRow : values) {
    try {
      Row row;

      if (!columns.empty()) {
        // Map column names to values
        std::vector<Value> rowValues(table_->get_column_count(), Value());
        for (size_t i = 0; i < columns.size() && i < valueRow.size(); ++i) {
          int colIdx = table_->get_column_index(columns[i]);
          if (colIdx >= 0) {
            rowValues[colIdx] = valueRow[i];
          }
        }
        row = Row(rowValues);
      } else {
        // All columns in order
        row = Row(valueRow);
      }

      table_->insert_row(row);
      inserted_count++;
    } catch (const std::exception &e) {
      return QueryResult::error_result(std::string("Insert failed: ") +
                                       e.what());
    }
  }

  QueryResult result = QueryResult::success_result("INSERT OK");
  result.affected_rows = inserted_count;
  return result;
}

// ==================== UpdateExecutor ====================

UpdateExecutor::UpdateExecutor(std::shared_ptr<UpdateStatement> stmt,
                               Table *table)
    : stmt_(stmt), table_(table) {}

QueryResult UpdateExecutor::execute() {
  if (!table_) {
    return QueryResult::error_result("Table not found");
  }

  std::vector<std::string> col_names;
  for (const auto &col : table_->get_columns()) {
    col_names.push_back(col.get_name());
  }

  int updated_count = 0;
  std::vector<Row> &rows = table_->get_mutable_rows();

  for (size_t i = 0; i < rows.size(); ++i) {
    if (!stmt_->get_where_condition() ||
        evaluate_condition(rows[i], col_names, stmt_->get_where_condition())) {
      Row newRow = rows[i];

      for (const auto &[colName, expr] : stmt_->get_set_clauses()) {
        int colIdx = table_->get_column_index(colName);
        if (colIdx >= 0) {
          Value newValue = evaluate_expression(rows[i], col_names, expr);
          newRow.set_value(colIdx, newValue);
        }
      }

      try {
        table_->update_row(i, newRow);
        updated_count++;
      } catch (const std::exception &e) {
        return QueryResult::error_result(std::string("Update failed: ") +
                                         e.what());
      }
    }
  }

  QueryResult result = QueryResult::success_result("UPDATE OK");
  result.affected_rows = updated_count;
  return result;
}

bool UpdateExecutor::evaluate_condition(
    const Row &row, const std::vector<std::string> &col_names,
    const ExpressionPtr &condition) const {
  if (!condition) return true;

  auto bin_op = std::dynamic_pointer_cast<BinaryOpExpression>(condition);
  if (bin_op) {
    Value left = evaluate_expression(row, col_names, bin_op->get_left());
    Value right = evaluate_expression(row, col_names, bin_op->get_right());

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
        return evaluate_condition(row, col_names, bin_op->get_left()) &&
               evaluate_condition(row, col_names, bin_op->get_right());
      case BinaryOpExpression::Operator::OR:
        return evaluate_condition(row, col_names, bin_op->get_left()) ||
               evaluate_condition(row, col_names, bin_op->get_right());
      default:
        return true;
    }
  }

  return true;
}

Value UpdateExecutor::evaluate_expression(
    const Row &row, const std::vector<std::string> &col_names,
    const ExpressionPtr &expr) const {
  auto literal = std::dynamic_pointer_cast<LiteralExpression>(expr);
  if (literal) return literal->get_value();

  auto col_ref = std::dynamic_pointer_cast<ColumnRefExpression>(expr);
  if (col_ref) {
    int idx = -1;
    for (size_t i = 0; i < col_names.size(); ++i) {
      if (col_names[i] == col_ref->get_column()) {
        idx = i;
        break;
      }
    }
    if (idx >= 0 && idx < (int)row.get_column_count()) {
      return row.get_value(idx);
    }
    return Value();
  }

  return Value();
}

// ==================== DeleteExecutor ====================

DeleteExecutor::DeleteExecutor(std::shared_ptr<DeleteStatement> stmt,
                               Table *table)
    : stmt_(stmt), table_(table) {}

QueryResult DeleteExecutor::execute() {
  if (!table_) {
    return QueryResult::error_result("Table not found");
  }

  std::vector<std::string> col_names;
  for (const auto &col : table_->get_columns()) {
    col_names.push_back(col.get_name());
  }

  int deleted_count = 0;
  const std::vector<Row> &rows = table_->get_all_rows();

  // Collect indices to delete (in reverse order to avoid index shifting)
  std::vector<int> indicesToDelete;
  for (int i = rows.size() - 1; i >= 0; --i) {
    if (!stmt_->get_where_condition() ||
        evaluate_condition(rows[i], col_names, stmt_->get_where_condition())) {
      indicesToDelete.push_back(i);
    }
  }

  // Delete in reverse order
  for (int idx : indicesToDelete) {
    try {
      table_->delete_row(idx);
      deleted_count++;
    } catch (const std::exception &e) {
      return QueryResult::error_result(std::string("Delete failed: ") +
                                       e.what());
    }
  }

  QueryResult result = QueryResult::success_result("DELETE OK");
  result.affected_rows = deleted_count;
  return result;
}

bool DeleteExecutor::evaluate_condition(
    const Row &row, const std::vector<std::string> &col_names,
    const ExpressionPtr &condition) const {
  if (!condition) return true;

  auto bin_op = std::dynamic_pointer_cast<BinaryOpExpression>(condition);
  if (bin_op) {
    Value left = evaluate_expression(row, col_names, bin_op->get_left());
    Value right = evaluate_expression(row, col_names, bin_op->get_right());

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
        return evaluate_condition(row, col_names, bin_op->get_left()) &&
               evaluate_condition(row, col_names, bin_op->get_right());
      case BinaryOpExpression::Operator::OR:
        return evaluate_condition(row, col_names, bin_op->get_left()) ||
               evaluate_condition(row, col_names, bin_op->get_right());
      default:
        return true;
    }
  }

  return true;
}

Value DeleteExecutor::evaluate_expression(
    const Row &row, const std::vector<std::string> &col_names,
    const ExpressionPtr &expr) const {
  auto literal = std::dynamic_pointer_cast<LiteralExpression>(expr);
  if (literal) return literal->get_value();

  auto col_ref = std::dynamic_pointer_cast<ColumnRefExpression>(expr);
  if (col_ref) {
    int idx = -1;
    for (size_t i = 0; i < col_names.size(); ++i) {
      if (col_names[i] == col_ref->get_column()) {
        idx = i;
        break;
      }
    }
    if (idx >= 0 && idx < (int)row.get_column_count()) {
      return row.get_value(idx);
    }
    return Value();
  }

  return Value();
}

// ==================== CreateTableExecutor ====================

CreateTableExecutor::CreateTableExecutor(
    std::shared_ptr<CreateTableStatement> stmt,
    std::map<std::string, std::unique_ptr<Table>> *tables)
    : stmt_(stmt), tables_(tables) {}

QueryResult CreateTableExecutor::execute() {
  if (!tables_) {
    return QueryResult::error_result("Internal error: no tables map");
  }

  if (tables_->count(stmt_->get_table_name())) {
    return QueryResult::error_result("Table '" + stmt_->get_table_name() +
                                     "' already exists");
  }

  try {
    auto table = std::make_unique<Table>(stmt_->get_table_name());

    for (const auto &colDef : stmt_->get_columns()) {
      DataType dataType = string_to_data_type(colDef.get_type());
      const bool nullable = !colDef.is_not_null() && !colDef.is_primary_key();
      Column col(colDef.get_name(), dataType, nullable, colDef.is_primary_key(),
                 colDef.is_unique());
      table->add_column(col);
    }

    (*tables_)[stmt_->get_table_name()] = std::move(table);

    QueryResult result = QueryResult::success_result("CREATE TABLE OK");
    return result;
  } catch (const std::exception &e) {
    return QueryResult::error_result(std::string("CREATE TABLE failed: ") +
                                     e.what());
  }
}

}  // namespace db
