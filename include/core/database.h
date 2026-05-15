#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/table.h"
#include "executor/query_executor.h"
#include "parser/parser.h"
#include "utils/exceptions.h"

namespace db {

class Database {
 public:
  explicit Database(std::string storage_directory = "data");

  Database(const Database &) = delete;
  Database &operator=(const Database &) = delete;

  void load_from_disk();

  QueryResult execute_query(const std::string &sql);

  void create_table(const std::string &table_name);
  void drop_table(const std::string &table_name);
  Table *get_table(const std::string &table_name);

  std::vector<std::string> list_tables() const;
  bool has_table(const std::string &table_name) const;

 private:
  std::string storage_directory_;
  mutable std::recursive_mutex db_mutex_;
  std::map<std::string, std::unique_ptr<Table>> tables_;

  QueryResult persist_after_mutation(QueryResult result);
  QueryResult execute_select_statement(std::shared_ptr<SelectStatement> stmt);
  QueryResult execute_insert_statement(std::shared_ptr<InsertStatement> stmt);
  QueryResult execute_update_statement(std::shared_ptr<UpdateStatement> stmt);
  QueryResult execute_delete_statement(std::shared_ptr<DeleteStatement> stmt);
  QueryResult execute_create_table_statement(
      std::shared_ptr<CreateTableStatement> stmt);
};

}  // namespace db
