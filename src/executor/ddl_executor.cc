#include "executor/ddl_executor.h"

#include "core/check_constraint.h"
#include "core/unique_constraint.h"
#include "storage/persistence_manager.h"
#include "types/data_type.h"

namespace db {
namespace {

bool has_live_check_violation(Table *table) {
  for (size_t i = 0; i < table->get_row_count(); ++i) {
    if (table->get_row(i).get_xmax() != 0) {
      continue;
    }
    if (table->find_violated_check(table->get_row(i))) {
      return true;
    }
  }
  return false;
}

}  // namespace

DropTableExecutor::DropTableExecutor(
    std::shared_ptr<DropTableStatement> stmt,
    std::map<std::string, std::unique_ptr<Table>> *tables,
    const std::string &storage_directory)
    : stmt_(std::move(stmt)),
      tables_(tables),
      storage_directory_(storage_directory) {}

QueryResult DropTableExecutor::execute() {
  if (!tables_) {
    return QueryResult::error_result("Internal error: no tables map");
  }
  const std::string &name = stmt_->get_table_name();
  if (!tables_->count(name)) {
    return QueryResult::error_result("Table '" + name + "' not found");
  }
  tables_->erase(name);
  PersistenceManager::remove_table_file(storage_directory_, name);
  return QueryResult::success_result("DROP TABLE OK");
}

AlterTableExecutor::AlterTableExecutor(
    std::shared_ptr<AlterTableStatement> stmt,
    std::map<std::string, std::unique_ptr<Table>> *tables,
    const std::string &storage_directory)
    : stmt_(std::move(stmt)),
      tables_(tables),
      storage_directory_(storage_directory) {}

QueryResult AlterTableExecutor::execute() {
  if (!tables_) {
    return QueryResult::error_result("Internal error: no tables map");
  }
  const std::string &table_name = stmt_->get_table_name();
  auto it = tables_->find(table_name);
  if (it == tables_->end()) {
    return QueryResult::error_result("Table '" + table_name + "' not found");
  }
  Table *table = it->second.get();
  const AlterTableAction &action = stmt_->get_action();
  try {
    switch (action.type) {
      case AlterTableActionType::AddColumn: {
        DataType data_type = string_to_data_type(action.column_def.get_type());
        const bool nullable =
            !action.column_def.is_not_null() && !action.column_def.is_primary_key();
        Column col(action.column_def.get_name(), data_type, nullable,
                   action.column_def.is_primary_key(),
                   action.column_def.is_unique());
        if (action.column_def.has_default()) {
          col.set_default_value(action.column_def.get_default_value());
        }
        table->add_column(col);
        if (action.column_def.has_check_expression()) {
          CheckConstraintDefinition check;
          check.predicate = action.column_def.get_check_expression();
          check.expression_text = check.predicate->to_string();
          prepare_check_constraint(table, check);
          table->add_check(check);
          if (has_live_check_violation(table)) {
            table->drop_check(check.name);
            return QueryResult::error_result(
                "Cannot add CHECK: existing data violates constraint");
          }
        }
        break;
      }
      case AlterTableActionType::DropColumn:
        table->drop_column(action.name);
        break;
      case AlterTableActionType::RenameTable: {
        if (tables_->count(action.new_name)) {
          return QueryResult::error_result("Table '" + action.new_name +
                                           "' already exists");
        }
        auto node = tables_->extract(it);
        node.key() = action.new_name;
        node.mapped()->set_name(action.new_name);
        tables_->insert(std::move(node));
        PersistenceManager::rename_table_file(storage_directory_, table_name,
                                              action.new_name);
        tables_->at(action.new_name)->mark_dirty();
        break;
      }
      case AlterTableActionType::RenameColumn:
        table->rename_column(action.name, action.new_name);
        break;
      case AlterTableActionType::AddPrimaryKey: {
        const std::vector<std::string> &pk_cols =
            action.columns.empty()
                ? std::vector<std::string>{action.name}
                : action.columns;
        try {
          table->apply_primary_key(pk_cols);
          for (size_t i = 0; i < table->get_row_count(); ++i) {
            if (!table->validate_row(table->get_row(i), i)) {
              table->drop_primary_key();
              return QueryResult::error_result(
                  "Cannot add PRIMARY KEY: existing data violates constraint");
            }
          }
        } catch (const std::exception &e) {
          return QueryResult::error_result(e.what());
        }
        table->mark_dirty();
        break;
      }
      case AlterTableActionType::AddUnique: {
        const std::vector<std::string> &uq_cols =
            action.columns.empty()
                ? std::vector<std::string>{action.name}
                : action.columns;
        UniqueConstraintDefinition uq;
        uq.columns = uq_cols;
        try {
          table->apply_unique_constraint(uq);
          for (size_t i = 0; i < table->get_row_count(); ++i) {
            if (!table->validate_unique_constraint(table->get_row(i), i)) {
              table->drop_unique_constraint(uq_cols);
              return QueryResult::error_result(
                  "Cannot add UNIQUE: existing data violates constraint");
            }
          }
        } catch (const std::exception &e) {
          return QueryResult::error_result(e.what());
        }
        table->mark_dirty();
        break;
      }
      case AlterTableActionType::SetNotNull: {
        const int col_idx = table->get_column_index(action.name);
        if (col_idx < 0) {
          return QueryResult::error_result("Column '" + action.name +
                                           "' not found");
        }
        for (size_t i = 0; i < table->get_row_count(); ++i) {
          if (table->get_row(i)
                  .get_value(static_cast<size_t>(col_idx))
                  .is_null()) {
            return QueryResult::error_result(
                "Cannot SET NOT NULL: column contains NULL values");
          }
        }
        table->get_mutable_column(static_cast<size_t>(col_idx))
            .set_nullable(false);
        table->mark_dirty();
        break;
      }
      case AlterTableActionType::DropPrimaryKey: {
        if (!table->drop_primary_key()) {
          return QueryResult::error_result("Table has no primary key");
        }
        table->mark_dirty();
        break;
      }
      case AlterTableActionType::DropUnique: {
        const std::vector<std::string> &uq_cols =
            action.columns.empty()
                ? std::vector<std::string>{action.name}
                : action.columns;
        if (!table->drop_unique_constraint(uq_cols)) {
          return QueryResult::error_result(
              "UNIQUE constraint not found for given columns");
        }
        table->mark_dirty();
        break;
      }
      case AlterTableActionType::DropNotNull: {
        const int col_idx = table->get_column_index(action.name);
        if (col_idx < 0) {
          return QueryResult::error_result("Column '" + action.name +
                                           "' not found");
        }
        Column &col = table->get_mutable_column(static_cast<size_t>(col_idx));
        if (col.is_primary_key()) {
          return QueryResult::error_result(
              "Cannot DROP NOT NULL on primary key column");
        }
        col.set_nullable(true);
        table->mark_dirty();
        break;
      }
      case AlterTableActionType::AddCheck: {
        CheckConstraintDefinition check;
        check.name = action.check_name;
        check.predicate = action.check_expression;
        if (check.predicate) {
          check.expression_text = check.predicate->to_string();
        }
        prepare_check_constraint(table, check);
        table->add_check(check);
        if (has_live_check_violation(table)) {
          table->drop_check(check.name);
          return QueryResult::error_result(
              "Cannot add CHECK: existing data violates constraint");
        }
        break;
      }
      case AlterTableActionType::DropCheck: {
        if (!table->drop_check(action.name)) {
          return QueryResult::error_result("CHECK constraint '" + action.name +
                                           "' not found");
        }
        break;
      }
    }
    return QueryResult::success_result("ALTER TABLE OK");
  } catch (const std::exception &e) {
    return QueryResult::error_result(std::string("ALTER TABLE failed: ") +
                                     e.what());
  }
}

}  // namespace db
