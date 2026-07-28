#include "executor/procedure_executor.h"

#include "core/database.h"
#include "core/routine_catalog.h"
#include "parser/parser.h"

namespace db {

ProcedureExecutor::ProcedureExecutor(Database *database, SessionContext *session)
    : database_(database), session_(session) {}

QueryResult ProcedureExecutor::executeCall(
    const ProcedureDefinition &procedure,
    const std::vector<Value> &arguments) {
  if (!database_) {
    return QueryResult::error_result("Internal error: no database");
  }
  if (arguments.size() != procedure.getParams().size()) {
    return QueryResult::error_result(
        "Wrong number of arguments for procedure " + procedure.getName());
  }
  std::unordered_map<std::string, Value> locals;
  for (size_t i = 0; i < procedure.getParams().size(); ++i) {
    locals[procedure.getParams()[i].name] = arguments[i];
  }
  QueryResult last = QueryResult::success_result("CALL OK");
  for (const std::string &stmt_sql : procedure.getStatementSqls()) {
    Parser parser(stmt_sql);
    ParsedStatement parsed = parser.parse_statement();
    database_->set_active_local_variables(locals);
    QueryResult step =
        database_->run_parsed_statement(parsed, session_, stmt_sql);
    database_->clear_active_local_variables();
    if (!step.success) {
      return step;
    }
    last = std::move(step);
  }
  last.message = "CALL OK";
  return last;
}

}  // namespace db
