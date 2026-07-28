#include "core/trigger.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

#include "parser/parser.h"
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

bool namesEqualIgnoreCase(const std::string &left, const std::string &right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (size_t i = 0; i < left.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(left[i])) !=
        std::tolower(static_cast<unsigned char>(right[i]))) {
      return false;
    }
  }
  return true;
}

}  // namespace

TriggerDefinition::TriggerDefinition(std::string name, std::string table_name,
                                     TriggerTiming timing, TriggerEvent event,
                                     std::vector<std::string> statement_sqls,
                                     std::string source_sql)
    : name_(std::move(name)),
      table_name_(std::move(table_name)),
      timing_(timing),
      event_(event),
      statement_sqls_(std::move(statement_sqls)),
      source_sql_(std::move(source_sql)) {}

const std::string &TriggerDefinition::getName() const { return name_; }

const std::string &TriggerDefinition::getTableName() const {
  return table_name_;
}

TriggerTiming TriggerDefinition::getTiming() const { return timing_; }

TriggerEvent TriggerDefinition::getEvent() const { return event_; }

const std::vector<std::string> &TriggerDefinition::getStatementSqls() const {
  return statement_sqls_;
}

const std::string &TriggerDefinition::getSourceSql() const {
  return source_sql_;
}

bool TriggerCatalog::hasTrigger(const std::string &name) const {
  return getTrigger(name) != nullptr;
}

const TriggerDefinition *TriggerCatalog::getTrigger(
    const std::string &name) const {
  auto it = triggers_.find(name);
  if (it != triggers_.end()) {
    return it->second.get();
  }
  for (const auto &[stored_name, definition] : triggers_) {
    if (namesEqualIgnoreCase(stored_name, name)) {
      return definition.get();
    }
  }
  return nullptr;
}

std::vector<const TriggerDefinition *> TriggerCatalog::listBy(
    const std::string &table_name, TriggerTiming timing,
    TriggerEvent event) const {
  std::vector<const TriggerDefinition *> result;
  for (const std::string &name : order_) {
    auto it = triggers_.find(name);
    if (it == triggers_.end()) {
      continue;
    }
    const TriggerDefinition *definition = it->second.get();
    if (!namesEqualIgnoreCase(definition->getTableName(), table_name)) {
      continue;
    }
    if (definition->getTiming() != timing || definition->getEvent() != event) {
      continue;
    }
    result.push_back(definition);
  }
  return result;
}

void TriggerCatalog::registerTrigger(
    std::unique_ptr<TriggerDefinition> definition) {
  if (hasTrigger(definition->getName())) {
    throw ConstraintException("Trigger '" + definition->getName() +
                              "' already exists");
  }
  const std::string name = definition->getName();
  order_.push_back(name);
  triggers_.emplace(name, std::move(definition));
}

void TriggerCatalog::unregisterTrigger(const std::string &name) {
  const TriggerDefinition *existing = getTrigger(name);
  if (!existing) {
    throw NotFoundException("Trigger '" + name + "' not found");
  }
  const std::string stored_name = existing->getName();
  triggers_.erase(stored_name);
  order_.erase(std::remove(order_.begin(), order_.end(), stored_name),
               order_.end());
}

std::string TriggerCatalog::buildTriggerPath(
    const std::string &storage_directory, const std::string &name) {
  return (fs::path(storage_directory) / TRIGGERS_SUBDIR /
          (name + TRIGGER_EXTENSION))
      .string();
}

void TriggerCatalog::saveTrigger(const std::string &storage_directory,
                                 const std::string &name) const {
  const TriggerDefinition *trigger = getTrigger(name);
  if (!trigger) {
    throw NotFoundException("Trigger '" + name + "' not found");
  }
  fs::create_directories(fs::path(storage_directory) / TRIGGERS_SUBDIR);
  const std::string path = buildTriggerPath(storage_directory, name);
  const std::string tmp_path = path + ".tmp";
  {
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      throw StorageException("Cannot write trigger file: " + tmp_path);
    }
    out << trigger->getSourceSql();
    out.flush();
  }
  fs::rename(tmp_path, path);
}

void TriggerCatalog::removeTriggerFile(const std::string &storage_directory,
                                       const std::string &name) const {
  std::error_code ec;
  fs::remove(buildTriggerPath(storage_directory, name), ec);
}

void TriggerCatalog::loadAll(const std::string &storage_directory) {
  triggers_.clear();
  order_.clear();
  const fs::path dir = fs::path(storage_directory) / TRIGGERS_SUBDIR;
  if (!fs::exists(dir) || !fs::is_directory(dir)) {
    return;
  }
  for (const auto &entry : fs::directory_iterator(dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    std::ifstream in(entry.path(), std::ios::binary);
    if (!in) {
      throw StorageException("Cannot read trigger file: " +
                             entry.path().string());
    }
    std::string sql((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
    sql = trimTrailingNoise(std::move(sql));
    Parser parser(sql);
    ParsedStatement stmt = parser.parse_statement();
    auto create =
        std::get_if<std::shared_ptr<CreateTriggerStatement>>(&stmt);
    if (!create) {
      throw StorageException("Invalid trigger file: " + entry.path().string());
    }
    registerTrigger(std::make_unique<TriggerDefinition>(
        (*create)->get_name(), (*create)->get_table_name(),
        (*create)->get_timing(), (*create)->get_event(),
        (*create)->get_statement_sqls(), (*create)->get_source_sql()));
  }
}

}  // namespace db
