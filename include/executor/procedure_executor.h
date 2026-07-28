#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "core/session_context.h"
#include "executor/query_executor.h"
#include "parser/ast.h"
#include "types/value.h"

namespace db {

class Database;
class ProcedureDefinition;

/**
 * Executes CALL by binding IN params as locals and dispatching each
 * statement in the procedure body sequentially.
 */
class ProcedureExecutor {
 public:
  ProcedureExecutor(Database *database, SessionContext *session);

  QueryResult executeCall(const ProcedureDefinition &procedure,
                          const std::vector<Value> &arguments);

 private:
  Database *database_;
  SessionContext *session_;
};

}  // namespace db
