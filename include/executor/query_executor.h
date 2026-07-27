#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/row.h"
#include "core/table.h"
#include "executor/select_expression_evaluator.h"
#include "parser/ast.h"
#include "utils/exceptions.h"

namespace db {

class Database;

// Result set for queries
struct QueryResult {
  bool success;
  std::string message;
  std::vector<std::string> column_names;
  std::vector<std::vector<Value>> rows;
  int affected_rows;

  QueryResult() : success(false), affected_rows(0) {}

  static QueryResult success_result(const std::string &msg = "OK") {
    QueryResult r;
    r.success = true;
    r.message = msg;
    return r;
  }

  static QueryResult error_result(const std::string &msg) {
    QueryResult r;
    r.success = false;
    r.message = msg;
    return r;
  }
};

// Abstract executor interface
class QueryExecutor {
 public:
  virtual ~QueryExecutor() = default;
  virtual QueryResult execute() = 0;
};

// SELECT executor
class SelectExecutor : public QueryExecutor {
 public:
  SelectExecutor(std::shared_ptr<SelectStatement> stmt, Table *table,
                 Database *database = nullptr);
  QueryResult execute() override;

 private:
  std::shared_ptr<SelectStatement> stmt_;
  Table *table_;
  Database *database_;
};

// INSERT executor
class InsertExecutor : public QueryExecutor {
 public:
  InsertExecutor(std::shared_ptr<InsertStatement> stmt, Table *table,
                 Database *database = nullptr);
  QueryResult execute() override;

 private:
  std::shared_ptr<InsertStatement> stmt_;
  Table *table_;
  Database *database_;
};

// UPDATE executor
class UpdateExecutor : public QueryExecutor {
 public:
  UpdateExecutor(std::shared_ptr<UpdateStatement> stmt, Table *table,
                 Database *database = nullptr);
  QueryResult execute() override;
  std::vector<size_t> collect_matching_indices() const;

 private:
  std::shared_ptr<UpdateStatement> stmt_;
  Table *table_;
  Database *database_;
};

// DELETE executor
class DeleteExecutor : public QueryExecutor {
 public:
  DeleteExecutor(std::shared_ptr<DeleteStatement> stmt, Table *table,
                 Database *database = nullptr);
  QueryResult execute() override;
  std::vector<size_t> collect_matching_indices() const;

 private:
  std::shared_ptr<DeleteStatement> stmt_;
  Table *table_;
  Database *database_;
};

// CREATE TABLE executor
class CreateTableExecutor : public QueryExecutor {
 public:
  CreateTableExecutor(std::shared_ptr<CreateTableStatement> stmt,
                      std::map<std::string, std::unique_ptr<Table>> *tables);
  QueryResult execute() override;

 private:
  std::shared_ptr<CreateTableStatement> stmt_;
  std::map<std::string, std::unique_ptr<Table>> *tables_;
};

void bind_subquery_evaluators(SelectExpressionEvaluator &eval,
                              Database *database);

}  // namespace db

