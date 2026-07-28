#pragma once

#include <optional>
#include <string>

#include "core/row.h"
#include "core/session_context.h"
#include "core/trigger.h"
#include "executor/query_executor.h"

namespace db {

class Database;
class Table;

/**
 * Fires FOR EACH ROW triggers around a single DML mutation.
 * BEFORE may mutate NEW via SET NEW; AFTER is read-only side effects.
 */
class TriggerExecutor {
 public:
  TriggerExecutor(Database *database, SessionContext *session);

  QueryResult executeBeforeInsert(const std::string &table_name, Row &new_row);
  QueryResult executeAfterInsert(const std::string &table_name,
                                 const Row &new_row);
  QueryResult executeBeforeUpdate(const std::string &table_name,
                                  const Row &old_row, Row &new_row);
  QueryResult executeAfterUpdate(const std::string &table_name,
                                 const Row &old_row, const Row &new_row);
  QueryResult executeBeforeDelete(const std::string &table_name,
                                  const Row &old_row);
  QueryResult executeAfterDelete(const std::string &table_name,
                                 const Row &old_row);

 private:
  Database *database_;
  SessionContext *session_;

  QueryResult fireTriggers(const std::string &table_name, TriggerTiming timing,
                           TriggerEvent event, const Row *old_row,
                           Row *new_row, Table *schema_table);
  QueryResult runTriggerBody(const TriggerDefinition &trigger,
                             const Row *old_row, Row *new_row,
                             Table *schema_table, bool allow_set_new);
};

}  // namespace db
