#include "core/routine_catalog.h"

#include <cctype>
#include <filesystem>
#include <fstream>

#include "parser/parser.h"
#include "types/data_type.h"
#include "utils/exceptions.h"

namespace fs = std::filesystem;

namespace db {
namespace {

std::string trimTrailingNoise(std::string text) {
  while (!text.empty() &&
         (text.back() == '\n' || text.back() == '\r' || text.back() == ';' ||
          text.back() == ' ' || text.back() == '\t')) {
    text.pop_back();
  }
  return text;
}

}  // namespace

FunctionDefinition::FunctionDefinition(std::string name,
                                       std::vector<RoutineParameter> params,
                                       DataType return_type, ExpressionPtr body,
                                       std::string source_sql)
    : name_(std::move(name)),
      params_(std::move(params)),
      return_type_(return_type),
      body_(std::move(body)),
      source_sql_(std::move(source_sql)) {}

const std::string &FunctionDefinition::getName() const { return name_; }

const std::vector<RoutineParameter> &FunctionDefinition::getParams() const {
  return params_;
}

DataType FunctionDefinition::getReturnType() const { return return_type_; }

const ExpressionPtr &FunctionDefinition::getBody() const { return body_; }

const std::string &FunctionDefinition::getSourceSql() const {
  return source_sql_;
}

ProcedureDefinition::ProcedureDefinition(
    std::string name, std::vector<RoutineParameter> params,
    std::vector<std::string> statement_sqls, std::string source_sql)
    : name_(std::move(name)),
      params_(std::move(params)),
      statement_sqls_(std::move(statement_sqls)),
      source_sql_(std::move(source_sql)) {}

const std::string &ProcedureDefinition::getName() const { return name_; }

const std::vector<RoutineParameter> &ProcedureDefinition::getParams() const {
  return params_;
}

const std::vector<std::string> &ProcedureDefinition::getStatementSqls() const {
  return statement_sqls_;
}

const std::string &ProcedureDefinition::getSourceSql() const {
  return source_sql_;
}

bool RoutineCatalog::hasFunction(const std::string &name) const {
  return functions_.count(name) > 0;
}

bool RoutineCatalog::hasProcedure(const std::string &name) const {
  return procedures_.count(name) > 0;
}

const FunctionDefinition *RoutineCatalog::getFunction(
    const std::string &name) const {
  auto it = functions_.find(name);
  if (it != functions_.end()) {
    return it->second.get();
  }
  for (const auto &[stored_name, definition] : functions_) {
    if (stored_name.size() != name.size()) {
      continue;
    }
    bool equal = true;
    for (size_t i = 0; i < name.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(stored_name[i])) !=
          std::tolower(static_cast<unsigned char>(name[i]))) {
        equal = false;
        break;
      }
    }
    if (equal) {
      return definition.get();
    }
  }
  return nullptr;
}

const ProcedureDefinition *RoutineCatalog::getProcedure(
    const std::string &name) const {
  auto it = procedures_.find(name);
  if (it != procedures_.end()) {
    return it->second.get();
  }
  for (const auto &[stored_name, definition] : procedures_) {
    if (stored_name.size() != name.size()) {
      continue;
    }
    bool equal = true;
    for (size_t i = 0; i < name.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(stored_name[i])) !=
          std::tolower(static_cast<unsigned char>(name[i]))) {
        equal = false;
        break;
      }
    }
    if (equal) {
      return definition.get();
    }
  }
  return nullptr;
}

void RoutineCatalog::registerFunction(
    std::unique_ptr<FunctionDefinition> definition) {
  if (functions_.count(definition->getName())) {
    throw ConstraintException("Function '" + definition->getName() +
                              "' already exists");
  }
  if (procedures_.count(definition->getName())) {
    throw ConstraintException("Routine name '" + definition->getName() +
                              "' already used by a procedure");
  }
  const std::string name = definition->getName();
  functions_.emplace(name, std::move(definition));
}

void RoutineCatalog::registerProcedure(
    std::unique_ptr<ProcedureDefinition> definition) {
  if (procedures_.count(definition->getName())) {
    throw ConstraintException("Procedure '" + definition->getName() +
                              "' already exists");
  }
  if (functions_.count(definition->getName())) {
    throw ConstraintException("Routine name '" + definition->getName() +
                              "' already used by a function");
  }
  const std::string name = definition->getName();
  procedures_.emplace(name, std::move(definition));
}

void RoutineCatalog::unregisterFunction(const std::string &name) {
  if (!functions_.count(name)) {
    throw NotFoundException("Function '" + name + "' not found");
  }
  functions_.erase(name);
}

void RoutineCatalog::unregisterProcedure(const std::string &name) {
  if (!procedures_.count(name)) {
    throw NotFoundException("Procedure '" + name + "' not found");
  }
  procedures_.erase(name);
}

std::string RoutineCatalog::buildFunctionPath(
    const std::string &storage_directory, const std::string &name) {
  return (fs::path(storage_directory) / ROUTINES_SUBDIR /
          (name + FUNCTION_EXTENSION))
      .string();
}

std::string RoutineCatalog::buildProcedurePath(
    const std::string &storage_directory, const std::string &name) {
  return (fs::path(storage_directory) / ROUTINES_SUBDIR /
          (name + PROCEDURE_EXTENSION))
      .string();
}

void RoutineCatalog::saveFunction(const std::string &storage_directory,
                                  const std::string &name) const {
  const FunctionDefinition *fn = getFunction(name);
  if (!fn) {
    throw NotFoundException("Function '" + name + "' not found");
  }
  fs::create_directories(fs::path(storage_directory) / ROUTINES_SUBDIR);
  const std::string path = buildFunctionPath(storage_directory, name);
  const std::string tmp_path = path + ".tmp";
  {
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      throw StorageException("Cannot write function file: " + tmp_path);
    }
    out << fn->getSourceSql();
    out.flush();
  }
  fs::rename(tmp_path, path);
}

void RoutineCatalog::saveProcedure(const std::string &storage_directory,
                                   const std::string &name) const {
  const ProcedureDefinition *proc = getProcedure(name);
  if (!proc) {
    throw NotFoundException("Procedure '" + name + "' not found");
  }
  fs::create_directories(fs::path(storage_directory) / ROUTINES_SUBDIR);
  const std::string path = buildProcedurePath(storage_directory, name);
  const std::string tmp_path = path + ".tmp";
  {
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      throw StorageException("Cannot write procedure file: " + tmp_path);
    }
    out << proc->getSourceSql();
    out.flush();
  }
  fs::rename(tmp_path, path);
}

void RoutineCatalog::removeFunctionFile(const std::string &storage_directory,
                                        const std::string &name) const {
  std::error_code ec;
  fs::remove(buildFunctionPath(storage_directory, name), ec);
}

void RoutineCatalog::removeProcedureFile(const std::string &storage_directory,
                                         const std::string &name) const {
  std::error_code ec;
  fs::remove(buildProcedurePath(storage_directory, name), ec);
}

void RoutineCatalog::loadAll(const std::string &storage_directory) {
  functions_.clear();
  procedures_.clear();
  const fs::path dir = fs::path(storage_directory) / ROUTINES_SUBDIR;
  if (!fs::exists(dir) || !fs::is_directory(dir)) {
    return;
  }
  for (const auto &entry : fs::directory_iterator(dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    std::ifstream in(entry.path(), std::ios::binary);
    if (!in) {
      throw StorageException("Cannot read routine file: " +
                             entry.path().string());
    }
    std::string sql((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
    sql = trimTrailingNoise(std::move(sql));
    Parser parser(sql);
    ParsedStatement stmt = parser.parse_statement();
    if (auto create_fn =
            std::get_if<std::shared_ptr<CreateFunctionStatement>>(&stmt)) {
      std::vector<RoutineParameter> params;
      for (const RoutineParamAst &param : (*create_fn)->get_params()) {
        params.push_back(
            {param.name, string_to_data_type(param.type_name)});
      }
      registerFunction(std::make_unique<FunctionDefinition>(
          (*create_fn)->get_name(), std::move(params),
          string_to_data_type((*create_fn)->get_return_type()),
          (*create_fn)->get_body(), (*create_fn)->get_source_sql()));
      continue;
    }
    if (auto create_proc =
            std::get_if<std::shared_ptr<CreateProcedureStatement>>(&stmt)) {
      std::vector<RoutineParameter> params;
      for (const RoutineParamAst &param : (*create_proc)->get_params()) {
        params.push_back(
            {param.name, string_to_data_type(param.type_name)});
      }
      registerProcedure(std::make_unique<ProcedureDefinition>(
          (*create_proc)->get_name(), std::move(params),
          (*create_proc)->get_statement_sqls(),
          (*create_proc)->get_source_sql()));
    }
  }
}

}  // namespace db
