#include "executor/query_executor.h"

#include <algorithm>
#include <functional>
#include <map>
#include <numeric>
#include <set>

#include "core/database.h"
#include "core/session_context.h"
#include "core/index_key.h"
#include "executor/group_aggregate_operator.h"
#include "executor/index_predicate.h"
#include "executor/select_analysis.h"
#include "executor/select_expression_evaluator.h"
#include "executor/select_pipeline.h"
#include "planner/access_path_chooser.h"
#include "types/type_converter.h"

namespace db {

void bind_subquery_evaluators(SelectExpressionEvaluator &eval,
                              Database *database) {
  if (!database) {
    return;
  }
  eval.set_scalar_subquery_fn(
      [database](const std::shared_ptr<SelectStatement> &stmt,
                 std::string *error_msg) {
        return database->evaluate_scalar_subquery(stmt, error_msg);
      });
  eval.set_in_subquery_fn(
      [database](const Value &left, const std::shared_ptr<SelectStatement> &stmt,
                 bool is_not, std::string *error_msg) {
        return database->evaluate_in_subquery(left, stmt, is_not, error_msg);
      });
  eval.set_exists_subquery_fn(
      [database](const std::shared_ptr<SelectStatement> &stmt,
                 std::string *error_msg) {
        return database->evaluate_exists_subquery(stmt, error_msg);
      });
}

namespace {

SelectExpressionEvaluator evaluator_for_table(
    Table *table, const std::string &alias_for_refs) {
  std::vector<SelectColumnBinding> bind;
  bind.reserve(table->get_column_count());
  const std::string phys = table->get_name();
  for (const auto &col : table->get_columns()) {
    bind.push_back({alias_for_refs, phys, col.get_name()});
  }
  return SelectExpressionEvaluator(std::move(bind));
}

SelectExpressionEvaluator evaluator_for_dml_table(Table *table) {
  return evaluator_for_table(table, table->get_name());
}

bool predicate_matches_table(const IndexColumnPredicate &pred, Table *table,
                             const std::string &alias) {
  if (pred.table_qualifier.empty()) {
    return table->get_column_index(pred.column_name) >= 0;
  }
  return (pred.table_qualifier == table->get_name() ||
          pred.table_qualifier == alias) &&
         table->get_column_index(pred.column_name) >= 0;
}

std::vector<size_t> lookup_predicate_rows(Table *table,
                                          const IndexColumnPredicate &pred) {
  if (!table->has_index(pred.column_name)) {
    return {};
  }
  if (pred.op == IndexCompareOp::Equal) {
    return table->find_rows_by_value(pred.column_name, pred.literal);
  }
  std::optional<Value> lower;
  std::optional<Value> upper;
  bool lower_inc = true;
  bool upper_inc = true;
  switch (pred.op) {
    case IndexCompareOp::Less:
      upper = pred.literal;
      upper_inc = false;
      break;
    case IndexCompareOp::LessEqual:
      upper = pred.literal;
      upper_inc = true;
      break;
    case IndexCompareOp::Greater:
      lower = pred.literal;
      lower_inc = false;
      break;
    case IndexCompareOp::GreaterEqual:
      lower = pred.literal;
      lower_inc = true;
      break;
    default:
      break;
  }
  return table->find_rows_by_range(pred.column_name, lower, lower_inc, upper,
                                   upper_inc);
}

std::optional<std::vector<size_t>> collect_candidate_rows(
    Table *table, const std::string &alias, const ExpressionPtr &where_expr) {
  if (!where_expr) {
    return std::nullopt;
  }
  auto preds = extract_index_predicates(where_expr);
  if (!preds || preds->empty()) {
    return std::nullopt;
  }
  std::map<std::string, Value> equal_by_column;
  for (const auto &pred : *preds) {
    if (!predicate_matches_table(pred, table, alias)) {
      continue;
    }
    if (pred.op == IndexCompareOp::Equal) {
      equal_by_column[pred.column_name] = pred.literal;
    }
  }
  std::optional<std::string> best_index;
  IndexKey best_key;
  size_t best_prefix_len = 0;
  for (const auto &[index_name, columns] : table->get_secondary_indexes()) {
    std::vector<Value> components;
    for (const std::string &column_name : columns) {
      auto it = equal_by_column.find(column_name);
      if (it == equal_by_column.end()) {
        break;
      }
      components.push_back(it->second);
    }
    if (components.empty()) {
      continue;
    }
    if (components.size() > best_prefix_len) {
      best_prefix_len = components.size();
      best_index = index_name;
      best_key = IndexKey(std::move(components));
    }
  }
  if (best_index) {
    return table->find_rows_by_index_key(*best_index, best_key);
  }
  std::optional<std::set<size_t>> intersection;
  bool used_index = false;
  for (const auto &pred : *preds) {
    if (!predicate_matches_table(pred, table, alias)) {
      continue;
    }
    if (!table->has_index(pred.column_name)) {
      continue;
    }
    auto rows = lookup_predicate_rows(table, pred);
    used_index = true;
    std::set<size_t> current(rows.begin(), rows.end());
    if (!intersection) {
      intersection = std::move(current);
    } else {
      std::set<size_t> next;
      std::set_intersection(intersection->begin(), intersection->end(),
                            current.begin(), current.end(),
                            std::inserter(next, next.begin()));
      intersection = std::move(next);
    }
  }
  if (!used_index || !intersection) {
    return std::nullopt;
  }
  return std::vector<size_t>(intersection->begin(), intersection->end());
}

std::vector<Row> filter_rows_with_index(
    Table *table, const std::string &alias, const ExpressionPtr &where_expr,
    const SelectExpressionEvaluator &eval, Database *database) {
  std::vector<Row> filtered_rows;
  std::vector<size_t> visible_indices;
  if (database) {
    SessionContext *session = database->get_active_session();
    visible_indices = table->get_visible_row_indices(
        database->get_transaction_manager(),
        database->get_reader_xid(session),
        database->get_reader_snapshot(session));
  } else {
    visible_indices.resize(table->get_row_count());
    std::iota(visible_indices.begin(), visible_indices.end(), 0);
  }
  std::set<size_t> visible_set(visible_indices.begin(), visible_indices.end());
  auto candidates = collect_candidate_rows(table, alias, where_expr);
  const bool has_index_path = candidates.has_value();
  const double selectivity =
      table->get_row_count() == 0
          ? 1.0
          : 1.0 / static_cast<double>(table->get_row_count());
  const AccessPathChoice path = AccessPathChooser::choose(
      table->get_row_count(), has_index_path, selectivity);
  if (candidates && path.kind == AccessPathKind::IndexScan) {
    for (size_t idx : *candidates) {
      if (!visible_set.count(idx)) {
        continue;
      }
      const Row row = table->get_row(idx);
      if (!where_expr || eval.evaluate_condition(row, where_expr)) {
        filtered_rows.push_back(row);
      }
    }
    return filtered_rows;
  }
  for (size_t idx : visible_indices) {
    const Row row = table->get_row(idx);
    if (!where_expr || eval.evaluate_condition(row, where_expr)) {
      filtered_rows.push_back(row);
    }
  }
  return filtered_rows;
}

}  // namespace
// ==================== SelectExecutor ====================

SelectExecutor::SelectExecutor(std::shared_ptr<SelectStatement> stmt,
                               Table *table, Database *database)
    : stmt_(stmt), table_(table), database_(database) {}

QueryResult SelectExecutor::execute() {
  if (!table_) {
    return QueryResult::error_result("Table not found");
  }

  if (auto validation_err = validate_select_for_grouping(stmt_)) {
    return QueryResult::error_result(*validation_err);
  }

  SelectExpressionEvaluator eval =
      evaluator_for_table(table_, stmt_->get_from_alias());
  if (database_) {
    eval.set_correlation_context(database_->get_correlation_context());
    eval.set_bind_context(database_->get_active_bind());
  }
  bind_subquery_evaluators(eval, database_);

  std::vector<Row> filtered_rows = filter_rows_with_index(
      table_, stmt_->get_from_alias(), stmt_->get_where_condition(), eval,
      database_);

  if (needs_grouping(stmt_)) {
    GroupAggregateOperator grouping(stmt_, eval);
    QueryResult grouped = grouping.apply(filtered_rows);
    if (!grouped.success) return grouped;
    return SelectPipeline::apply_post_scan(std::move(grouped), stmt_, eval);
  }

  QueryResult result;
  result.success = true;

  const auto &select_cols = stmt_->get_select_columns();
  if (select_cols.empty()) {
    return QueryResult::error_result("No columns selected");
  }

  const auto isWildcardColumn = [](const ExpressionPtr &e) -> bool {
    return is_wildcard_select_expression(e);
  };

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

  for (const auto &row : filtered_rows) {
    std::vector<Value> resultRow;
    for (const auto &[expr, alias] : select_cols) {
      (void)alias;
      if (isWildcardColumn(expr)) {
        for (size_t i = 0; i < row.get_column_count(); ++i) {
          resultRow.push_back(row.get_value(i));
        }
      } else {
        resultRow.push_back(eval.evaluate_expression(row, expr, nullptr));
      }
    }
    result.rows.push_back(std::move(resultRow));
  }

  result.affected_rows = static_cast<int>(result.rows.size());
  result.message = "SELECT OK";
  return SelectPipeline::apply_post_scan(std::move(result), stmt_, eval);
}

// ==================== InsertExecutor ====================

InsertExecutor::InsertExecutor(std::shared_ptr<InsertStatement> stmt,
                               Table *table, Database *database)
    : stmt_(stmt), table_(table), database_(database) {}

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
        std::vector<Value> rowValues(table_->get_column_count(), Value());
        for (size_t i = 0; i < table_->get_column_count(); ++i) {
          const Column &column = table_->get_column(i);
          if (column.has_default()) {
            rowValues[i] = column.get_default_value();
          }
        }
        for (size_t i = 0; i < columns.size() && i < valueRow.size(); ++i) {
          int colIdx = table_->get_column_index(columns[i]);
          if (colIdx >= 0) {
            rowValues[static_cast<size_t>(colIdx)] = valueRow[i];
          }
        }
        row = Row(rowValues);
      } else {
        std::vector<Value> rowValues = valueRow;
        if (rowValues.size() < table_->get_column_count()) {
          rowValues.resize(table_->get_column_count(), Value());
        }
        for (size_t i = valueRow.size(); i < table_->get_column_count(); ++i) {
          const Column &column = table_->get_column(i);
          if (column.has_default()) {
            rowValues[i] = column.get_default_value();
          }
        }
        row = Row(rowValues);
      }

      if (database_) {
        database_->validate_foreign_keys_on_insert(*table_, row);
      }
      if (database_ && database_->get_active_session() &&
          database_->get_active_session()->is_in_transaction()) {
        table_->insert_row_versioned(
            row, database_->get_reader_xid(database_->get_active_session()));
      } else {
        table_->insert_row(row);
      }
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
                               Table *table, Database *database)
    : stmt_(stmt), table_(table), database_(database) {}

std::vector<size_t> UpdateExecutor::collect_matching_indices() const {
  std::vector<size_t> matching;
  if (!table_) {
    return matching;
  }
  SelectExpressionEvaluator eval = evaluator_for_dml_table(table_);
  if (database_) {
    eval.set_correlation_context(database_->get_correlation_context());
    eval.set_bind_context(database_->get_active_bind());
  }
  bind_subquery_evaluators(eval, database_);
  std::vector<size_t> candidate_indices;
  if (database_) {
    SessionContext *session = database_->get_active_session();
    candidate_indices = table_->get_visible_row_indices(
        database_->get_transaction_manager(),
        database_->get_reader_xid(session),
        database_->get_reader_snapshot(session));
  } else {
    candidate_indices.resize(table_->get_row_count());
    std::iota(candidate_indices.begin(), candidate_indices.end(), 0);
  }
  auto indexed = collect_candidate_rows(table_, table_->get_name(),
                                        stmt_->get_where_condition());
  if (indexed) {
    std::set<size_t> visible(candidate_indices.begin(), candidate_indices.end());
    candidate_indices.clear();
    for (size_t i : *indexed) {
      if (visible.count(i)) {
        candidate_indices.push_back(i);
      }
    }
  }
  for (size_t i : candidate_indices) {
    if (i >= table_->get_row_count()) {
      continue;
    }
    const Row current = table_->get_row(i);
    if (!stmt_->get_where_condition() ||
        eval.evaluate_condition(current, stmt_->get_where_condition())) {
      matching.push_back(i);
    }
  }
  return matching;
}

QueryResult UpdateExecutor::execute() {
  if (!table_) {
    return QueryResult::error_result("Table not found");
  }
  SelectExpressionEvaluator eval = evaluator_for_dml_table(table_);
  if (database_) {
    eval.set_correlation_context(database_->get_correlation_context());
    eval.set_bind_context(database_->get_active_bind());
  }
  bind_subquery_evaluators(eval, database_);
  int updated_count = 0;
  std::vector<size_t> candidate_indices = collect_matching_indices();
  for (size_t i : candidate_indices) {
    if (i >= table_->get_row_count()) {
      continue;
    }
    Row current = table_->get_row(i);
    if (!stmt_->get_where_condition() ||
        eval.evaluate_condition(current, stmt_->get_where_condition())) {
      Row new_row = current;
      for (const auto &[col_name, expr] : stmt_->get_set_clauses()) {
        int col_idx = table_->get_column_index(col_name);
        if (col_idx >= 0) {
          Value new_value = eval.evaluate_dml_assignment_rhs(current, expr);
          new_row.set_value(static_cast<size_t>(col_idx), new_value);
        }
      }
      try {
        if (database_) {
          database_->validate_foreign_keys_on_update(*table_, current, new_row);
        }
        if (database_ && database_->get_active_session() &&
            database_->get_active_session()->is_in_transaction()) {
          table_->update_row_versioned(
              i, new_row,
              database_->get_reader_xid(database_->get_active_session()));
        } else {
          table_->update_row(i, new_row);
        }
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

// ==================== DeleteExecutor ====================

DeleteExecutor::DeleteExecutor(std::shared_ptr<DeleteStatement> stmt,
                               Table *table, Database *database)
    : stmt_(stmt), table_(table), database_(database) {}

std::vector<size_t> DeleteExecutor::collect_matching_indices() const {
  std::vector<size_t> matching;
  if (!table_) {
    return matching;
  }
  SelectExpressionEvaluator eval = evaluator_for_dml_table(table_);
  if (database_) {
    eval.set_correlation_context(database_->get_correlation_context());
    eval.set_bind_context(database_->get_active_bind());
  }
  bind_subquery_evaluators(eval, database_);
  std::vector<size_t> visible_indices;
  if (database_) {
    SessionContext *session = database_->get_active_session();
    visible_indices = table_->get_visible_row_indices(
        database_->get_transaction_manager(),
        database_->get_reader_xid(session),
        database_->get_reader_snapshot(session));
  } else {
    visible_indices.resize(table_->get_row_count());
    std::iota(visible_indices.begin(), visible_indices.end(), 0);
  }
  std::set<size_t> visible(visible_indices.begin(), visible_indices.end());
  auto indexed = collect_candidate_rows(table_, table_->get_name(),
                                        stmt_->get_where_condition());
  std::vector<size_t> candidates;
  if (indexed) {
    for (size_t i : *indexed) {
      if (visible.count(i)) {
        candidates.push_back(i);
      }
    }
  } else {
    candidates = visible_indices;
  }
  for (size_t i : candidates) {
    if (i >= table_->get_row_count()) {
      continue;
    }
    const Row row = table_->get_row(i);
    if (!stmt_->get_where_condition() ||
        eval.evaluate_condition(row, stmt_->get_where_condition())) {
      matching.push_back(i);
    }
  }
  return matching;
}

QueryResult DeleteExecutor::execute() {
  if (!table_) {
    return QueryResult::error_result("Table not found");
  }
  int deleted_count = 0;
  std::vector<size_t> indices_to_delete = collect_matching_indices();
  std::sort(indices_to_delete.begin(), indices_to_delete.end(),
            std::greater<size_t>());
  const bool in_tx = database_ && database_->get_active_session() &&
                     database_->get_active_session()->is_in_transaction();
  for (size_t i : indices_to_delete) {
    try {
      Row row = table_->get_row(i);
      if (database_) {
        database_->validate_foreign_keys_on_delete(*table_, row);
      }
      if (in_tx) {
        table_->delete_row_versioned(
            i, database_->get_reader_xid(database_->get_active_session()));
      } else {
        table_->delete_row(i);
      }
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
      if (colDef.has_default()) {
        col.set_default_value(colDef.get_default_value());
      }
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
