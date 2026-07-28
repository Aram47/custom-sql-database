#pragma once

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace db {

/** When a row-level trigger fires relative to the mutation. */
enum class TriggerTiming { Before, After };

/** DML event that activates a trigger. */
enum class TriggerEvent { Insert, Update, Delete };

/** Maximum nested trigger firing depth (guard against recursion storms). */
constexpr int MAX_TRIGGER_DEPTH = 16;

/** Named FOR EACH ROW trigger definition. */
class TriggerDefinition {
 public:
  TriggerDefinition(std::string name, std::string table_name,
                    TriggerTiming timing, TriggerEvent event,
                    std::vector<std::string> statement_sqls,
                    std::string source_sql);

  const std::string &getName() const;
  const std::string &getTableName() const;
  TriggerTiming getTiming() const;
  TriggerEvent getEvent() const;
  const std::vector<std::string> &getStatementSqls() const;
  const std::string &getSourceSql() const;

 private:
  std::string name_;
  std::string table_name_;
  TriggerTiming timing_;
  TriggerEvent event_;
  std::vector<std::string> statement_sqls_;
  std::string source_sql_;
};

/**
 * Catalog of triggers with persistence under `_triggers/`.
 * Definitions are keyed by name; firing order is registration order per
 * (table, timing, event).
 */
class TriggerCatalog {
 public:
  static constexpr const char *TRIGGERS_SUBDIR = "_triggers";
  static constexpr const char *TRIGGER_EXTENSION = ".trig";

  bool hasTrigger(const std::string &name) const;
  const TriggerDefinition *getTrigger(const std::string &name) const;
  std::vector<const TriggerDefinition *> listBy(const std::string &table_name,
                                                TriggerTiming timing,
                                                TriggerEvent event) const;

  void registerTrigger(std::unique_ptr<TriggerDefinition> definition);
  void unregisterTrigger(const std::string &name);

  void saveTrigger(const std::string &storage_directory,
                   const std::string &name) const;
  void removeTriggerFile(const std::string &storage_directory,
                         const std::string &name) const;
  void loadAll(const std::string &storage_directory);

 private:
  std::map<std::string, std::unique_ptr<TriggerDefinition>> triggers_;
  std::vector<std::string> order_;

  static std::string buildTriggerPath(const std::string &storage_directory,
                                      const std::string &name);
};

}  // namespace db
