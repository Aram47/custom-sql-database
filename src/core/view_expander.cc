#include "core/view_expander.h"

#include "core/column.h"
#include "core/database.h"
#include "core/row.h"
#include "core/table.h"
#include "core/view_definition.h"
#include "executor/query_executor.h"
#include "types/data_type.h"

namespace db {

namespace {

DataType infer_column_type(const QueryResult &result, size_t column_index) {
  for (const auto &row : result.rows) {
    if (column_index >= row.size()) {
      continue;
    }
    const Value &value = row[column_index];
    if (!value.is_null()) {
      return value.get_type();
    }
  }
  return DataType::STRING;
}

}  // namespace

ViewExpander::ViewExpander(Database *database) : database_(database) {}

std::shared_ptr<SelectStatement> ViewExpander::clone_select(
    const std::shared_ptr<SelectStatement> &stmt) const {
  auto clone = std::make_shared<SelectStatement>();
  clone->set_distinct(stmt->is_distinct());
  for (const auto &[expr, alias] : stmt->get_select_columns()) {
    clone->add_select_column(expr, alias);
  }
  clone->set_from_table(stmt->get_from_table(), stmt->get_from_alias());
  if (stmt->get_where_condition()) {
    clone->set_where_condition(stmt->get_where_condition());
  }
  for (const auto &expr : stmt->get_group_by_columns()) {
    clone->add_group_by_column(expr);
  }
  if (stmt->get_having_condition()) {
    clone->set_having_condition(stmt->get_having_condition());
  }
  for (const auto &[expr, ascending] : stmt->get_order_by_columns()) {
    clone->add_order_by_column(expr, ascending);
  }
  for (const auto &join : stmt->get_joins()) {
    clone->add_join(std::get<0>(join), std::get<1>(join), std::get<2>(join),
                    std::get<3>(join));
  }
  if (stmt->get_limit() >= 0) {
    clone->set_limit(stmt->get_limit());
  }
  if (stmt->get_offset() > 0) {
    clone->set_offset(stmt->get_offset());
  }
  return clone;
}

bool ViewExpander::materialize_view(const std::string &view_name,
                                    std::string *table_name,
                                    std::set<std::string> *visiting, int depth,
                                    std::string *error_message) {
  if (depth > MAX_VIEW_NESTING_DEPTH) {
    if (error_message) {
      *error_message = "View nesting depth exceeded (max " +
                       std::to_string(MAX_VIEW_NESTING_DEPTH) + ")";
    }
    return false;
  }
  if (visiting->count(view_name)) {
    if (error_message) {
      *error_message = "Circular view reference involving '" + view_name + "'";
    }
    return false;
  }
  const ViewDefinition *view = database_->get_view(view_name);
  if (!view) {
    if (error_message) {
      *error_message = "View '" + view_name + "' not found";
    }
    return false;
  }
  visiting->insert(view_name);
  std::shared_ptr<SelectStatement> body = view->parse_select();
  std::shared_ptr<SelectStatement> expanded_body =
      expand_internal(body, visiting, depth + 1, error_message);
  if (!expanded_body) {
    visiting->erase(view_name);
    return false;
  }
  QueryResult result = database_->run_select(expanded_body);
  visiting->erase(view_name);
  if (!result.success) {
    if (error_message) {
      *error_message = result.message;
    }
    return false;
  }
  const std::string ephemeral_name =
      database_->allocate_ephemeral_table_name(view_name);
  auto table = std::make_unique<Table>(ephemeral_name);
  for (size_t i = 0; i < result.column_names.size(); ++i) {
    const DataType type = infer_column_type(result, i);
    table->add_column(Column(result.column_names[i], type, true, false, false));
  }
  for (const auto &values : result.rows) {
    Row row;
    for (const Value &value : values) {
      row.add_value(value);
    }
    table->insert_row(row);
  }
  table->clear_dirty();
  database_->register_ephemeral_table(ephemeral_name, std::move(table));
  *table_name = ephemeral_name;
  return true;
}

bool ViewExpander::expand_table_ref(std::string *table_name,
                                    std::set<std::string> *visiting, int depth,
                                    std::string *error_message) {
  if (database_->has_table(*table_name) ||
      database_->has_ephemeral_table(*table_name)) {
    return true;
  }
  if (!database_->has_view(*table_name)) {
    if (error_message) {
      *error_message = "Table '" + *table_name + "' not found";
    }
    return false;
  }
  return materialize_view(*table_name, table_name, visiting, depth,
                          error_message);
}

std::shared_ptr<SelectStatement> ViewExpander::expand_internal(
    const std::shared_ptr<SelectStatement> &stmt,
    std::set<std::string> *visiting, int depth, std::string *error_message) {
  if (!stmt || !database_) {
    if (error_message) {
      *error_message = "Invalid SELECT for view expansion";
    }
    return nullptr;
  }
  std::shared_ptr<SelectStatement> clone = clone_select(stmt);
  std::string from_table = clone->get_from_table();
  if (from_table.empty()) {
    if (error_message) {
      *error_message = "SELECT requires FROM clause";
    }
    return nullptr;
  }
  const std::string from_alias = clone->get_from_alias();
  if (!expand_table_ref(&from_table, visiting, depth, error_message)) {
    return nullptr;
  }
  clone->set_from_table(from_table, from_alias);
  for (size_t i = 0; i < clone->get_joins().size(); ++i) {
    std::string join_table = std::get<1>(clone->get_joins()[i]);
    if (!expand_table_ref(&join_table, visiting, depth, error_message)) {
      return nullptr;
    }
    clone->set_join_table(i, join_table);
  }
  return clone;
}

std::shared_ptr<SelectStatement> ViewExpander::expand(
    const std::shared_ptr<SelectStatement> &stmt, std::string *error_message) {
  std::set<std::string> visiting;
  return expand_internal(stmt, &visiting, 1, error_message);
}

}  // namespace db
