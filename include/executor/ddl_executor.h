#pragma once

#include <map>
#include <memory>
#include <string>

#include "core/table.h"
#include "executor/query_executor.h"
#include "parser/ast.h"

namespace db {

class DropTableExecutor : public QueryExecutor {
 public:
  DropTableExecutor(std::shared_ptr<DropTableStatement> stmt,
                    std::map<std::string, std::unique_ptr<Table>> *tables,
                    const std::string &storage_directory);
  QueryResult execute() override;

 private:
  std::shared_ptr<DropTableStatement> stmt_;
  std::map<std::string, std::unique_ptr<Table>> *tables_;
  std::string storage_directory_;
};

class AlterTableExecutor : public QueryExecutor {
 public:
  AlterTableExecutor(std::shared_ptr<AlterTableStatement> stmt,
                     std::map<std::string, std::unique_ptr<Table>> *tables,
                     const std::string &storage_directory);
  QueryResult execute() override;

 private:
  std::shared_ptr<AlterTableStatement> stmt_;
  std::map<std::string, std::unique_ptr<Table>> *tables_;
  std::string storage_directory_;
};

}  // namespace db
