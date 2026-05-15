#include "core/database.h"

#include "executor/query_executor.h"
#include "parser/parser.h"
#include "storage/persistence_manager.h"
#include "utils/logger.h"

namespace db {

Database::Database(std::string storage_directory)
    : storage_directory_(std::move(storage_directory)) {}

void Database::load_from_disk() {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  try {
    tables_ = PersistenceManager::load_database(storage_directory_);
  } catch (const StorageException &e) {
    DB_LOG_ERROR("Failed to load database from disk: ", e.what());
    throw;
  }
}

QueryResult Database::persist_after_mutation(QueryResult result) {
  if (!result.success) {
    return result;
  }
  try {
    PersistenceManager::save_database(tables_, storage_directory_);
    return result;
  } catch (const StorageException &e) {
    DB_LOG_ERROR("Persistence failed: ", e.what());
    return QueryResult::error_result(std::string("Persistence failed: ") +
                                     e.what());
  }
}

QueryResult Database::execute_query(const std::string &sql) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);

  try {
    DB_LOG_DEBUG("Executing query: ", sql);

    Parser parser(sql);
    auto stmt = parser.parse_statement();

    if (auto selectStmt =
            std::get_if<std::shared_ptr<SelectStatement>>(&stmt)) {
      return execute_select_statement(*selectStmt);
    }
    if (auto insertStmt =
            std::get_if<std::shared_ptr<InsertStatement>>(&stmt)) {
      return execute_insert_statement(*insertStmt);
    }
    if (auto updateStmt =
            std::get_if<std::shared_ptr<UpdateStatement>>(&stmt)) {
      return execute_update_statement(*updateStmt);
    }
    if (auto deleteStmt =
            std::get_if<std::shared_ptr<DeleteStatement>>(&stmt)) {
      return execute_delete_statement(*deleteStmt);
    }
    if (auto createStmt =
            std::get_if<std::shared_ptr<CreateTableStatement>>(&stmt)) {
      return execute_create_table_statement(*createStmt);
    }

    return QueryResult::error_result("Unknown statement type");
  } catch (const ParseException &e) {
    DB_LOG_ERROR("Parse error: ", e.what());
    return QueryResult::error_result(e.what());
  } catch (const NotFoundException &e) {
    DB_LOG_ERROR("Not found: ", e.what());
    return QueryResult::error_result(e.what());
  } catch (const ConstraintException &e) {
    DB_LOG_ERROR("Constraint error: ", e.what());
    return QueryResult::error_result(e.what());
  } catch (const DatabaseException &e) {
    DB_LOG_ERROR("Database error: ", e.what());
    return QueryResult::error_result(e.what());
  } catch (const std::exception &e) {
    DB_LOG_ERROR("Unexpected error: ", e.what());
    return QueryResult::error_result(std::string("Unexpected error: ") +
                                     e.what());
  }
}

void Database::create_table(const std::string &table_name) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  if (tables_.count(table_name)) {
    throw ConstraintException("Table '" + table_name + "' already exists");
  }
  tables_[table_name] = std::make_unique<Table>(table_name);
}

void Database::drop_table(const std::string &table_name) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  if (!tables_.count(table_name)) {
    throw NotFoundException("Table '" + table_name + "' not found");
  }
  tables_.erase(table_name);
}

Table *Database::get_table(const std::string &table_name) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  if (!tables_.count(table_name)) {
    return nullptr;
  }
  return tables_[table_name].get();
}

std::vector<std::string> Database::list_tables() const {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  std::vector<std::string> result;
  for (const auto &[name, table] : tables_) {
    result.push_back(name);
  }
  return result;
}

bool Database::has_table(const std::string &table_name) const {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  return tables_.count(table_name) > 0;
}

QueryResult Database::execute_select_statement(
    std::shared_ptr<SelectStatement> stmt) {
  if (stmt->get_from_table().empty()) {
    return QueryResult::error_result("SELECT requires FROM clause");
  }

  Table *table = get_table(stmt->get_from_table());
  if (!table) {
    return QueryResult::error_result("Table '" + stmt->get_from_table() +
                                     "' not found");
  }

  SelectExecutor executor(stmt, table);
  return executor.execute();
}

QueryResult Database::execute_insert_statement(
    std::shared_ptr<InsertStatement> stmt) {
  Table *table = get_table(stmt->get_table());
  if (!table) {
    return QueryResult::error_result("Table '" + stmt->get_table() +
                                     "' not found");
  }

  InsertExecutor executor(stmt, table);
  return persist_after_mutation(executor.execute());
}

QueryResult Database::execute_update_statement(
    std::shared_ptr<UpdateStatement> stmt) {
  Table *table = get_table(stmt->get_table());
  if (!table) {
    return QueryResult::error_result("Table '" + stmt->get_table() +
                                     "' not found");
  }

  UpdateExecutor executor(stmt, table);
  return persist_after_mutation(executor.execute());
}

QueryResult Database::execute_delete_statement(
    std::shared_ptr<DeleteStatement> stmt) {
  Table *table = get_table(stmt->get_table());
  if (!table) {
    return QueryResult::error_result("Table '" + stmt->get_table() +
                                     "' not found");
  }

  DeleteExecutor executor(stmt, table);
  return persist_after_mutation(executor.execute());
}

QueryResult Database::execute_create_table_statement(
    std::shared_ptr<CreateTableStatement> stmt) {
  CreateTableExecutor executor(stmt, &tables_);
  return persist_after_mutation(executor.execute());
}

}  // namespace db
