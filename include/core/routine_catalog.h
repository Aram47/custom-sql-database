#pragma once

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "parser/ast.h"
#include "types/data_type.h"
#include "types/value.h"

namespace db {

/** Single IN parameter of a function or procedure. */
struct RoutineParameter {
  std::string name;
  DataType type{DataType::INT};
};

/** SQL-bodied scalar function definition. */
class FunctionDefinition {
 public:
  FunctionDefinition(std::string name, std::vector<RoutineParameter> params,
                     DataType return_type, ExpressionPtr body,
                     std::string source_sql);

  const std::string &getName() const;
  const std::vector<RoutineParameter> &getParams() const;
  DataType getReturnType() const;
  const ExpressionPtr &getBody() const;
  const std::string &getSourceSql() const;

 private:
  std::string name_;
  std::vector<RoutineParameter> params_;
  DataType return_type_;
  ExpressionPtr body_;
  std::string source_sql_;
};

/** Multi-statement procedure definition (IN params only). */
class ProcedureDefinition {
 public:
  ProcedureDefinition(std::string name, std::vector<RoutineParameter> params,
                      std::vector<std::string> statement_sqls,
                      std::string source_sql);

  const std::string &getName() const;
  const std::vector<RoutineParameter> &getParams() const;
  const std::vector<std::string> &getStatementSqls() const;
  const std::string &getSourceSql() const;

 private:
  std::string name_;
  std::vector<RoutineParameter> params_;
  std::vector<std::string> statement_sqls_;
  std::string source_sql_;
};

/**
 * Catalog of user functions and procedures with persistence under `_routines/`.
 */
class RoutineCatalog {
 public:
  static constexpr const char *ROUTINES_SUBDIR = "_routines";
  static constexpr const char *FUNCTION_EXTENSION = ".func";
  static constexpr const char *PROCEDURE_EXTENSION = ".proc";

  bool hasFunction(const std::string &name) const;
  bool hasProcedure(const std::string &name) const;
  const FunctionDefinition *getFunction(const std::string &name) const;
  const ProcedureDefinition *getProcedure(const std::string &name) const;

  void registerFunction(std::unique_ptr<FunctionDefinition> definition);
  void registerProcedure(std::unique_ptr<ProcedureDefinition> definition);
  void unregisterFunction(const std::string &name);
  void unregisterProcedure(const std::string &name);

  void saveFunction(const std::string &storage_directory,
                    const std::string &name) const;
  void saveProcedure(const std::string &storage_directory,
                     const std::string &name) const;
  void removeFunctionFile(const std::string &storage_directory,
                          const std::string &name) const;
  void removeProcedureFile(const std::string &storage_directory,
                           const std::string &name) const;
  void loadAll(const std::string &storage_directory);

 private:
  std::map<std::string, std::unique_ptr<FunctionDefinition>> functions_;
  std::map<std::string, std::unique_ptr<ProcedureDefinition>> procedures_;

  static std::string buildFunctionPath(const std::string &storage_directory,
                                       const std::string &name);
  static std::string buildProcedurePath(const std::string &storage_directory,
                                        const std::string &name);
};

}  // namespace db
