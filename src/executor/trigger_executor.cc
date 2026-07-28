#include "executor/trigger_executor.h"

#include <algorithm>

#include "core/correlation_context.h"
#include "core/database.h"
#include "core/table.h"
#include "executor/select_column_binding.h"
#include "executor/select_expression_evaluator.h"
#include "parser/parser.h"

namespace db {
namespace {

class TriggerDepthGuard {
 public:
  explicit TriggerDepthGuard(Database *database) : database_(database) {
    if (database_) {
      database_->enter_trigger_depth();
    }
  }
  ~TriggerDepthGuard() {
    if (database_) {
      database_->leave_trigger_depth();
    }
  }
  TriggerDepthGuard(const TriggerDepthGuard &) = delete;
  TriggerDepthGuard &operator=(const TriggerDepthGuard &) = delete;

 private:
  Database *database_;
};

std::vector<SelectColumnBinding> buildRowBindings(const Table &table,
                                                  const std::string &alias) {
  std::vector<SelectColumnBinding> bindings;
  bindings.reserve(table.get_column_count());
  for (const Column &column : table.get_columns()) {
    bindings.push_back({alias, table.get_name(), column.get_name()});
  }
  return bindings;
}

void refreshNewFrame(CorrelationContext *ctx, const Table &table,
                     const Row &new_row) {
  if (!ctx) {
    return;
  }
  ctx->popFrame();
  ctx->pushFrame(buildRowBindings(table, "NEW"), new_row);
}

}  // namespace

TriggerExecutor::TriggerExecutor(Database *database, SessionContext *session)
    : database_(database), session_(session) {}

QueryResult TriggerExecutor::executeBeforeInsert(const std::string &table_name,
                                                 Row &new_row) {
  Table *table = database_ ? database_->get_table(table_name) : nullptr;
  return fireTriggers(table_name, TriggerTiming::Before, TriggerEvent::Insert,
                      nullptr, &new_row, table);
}

QueryResult TriggerExecutor::executeAfterInsert(const std::string &table_name,
                                                const Row &new_row) {
  Table *table = database_ ? database_->get_table(table_name) : nullptr;
  Row mutable_copy = new_row;
  return fireTriggers(table_name, TriggerTiming::After, TriggerEvent::Insert,
                      nullptr, &mutable_copy, table);
}

QueryResult TriggerExecutor::executeBeforeUpdate(const std::string &table_name,
                                                 const Row &old_row,
                                                 Row &new_row) {
  Table *table = database_ ? database_->get_table(table_name) : nullptr;
  return fireTriggers(table_name, TriggerTiming::Before, TriggerEvent::Update,
                      &old_row, &new_row, table);
}

QueryResult TriggerExecutor::executeAfterUpdate(const std::string &table_name,
                                                const Row &old_row,
                                                const Row &new_row) {
  Table *table = database_ ? database_->get_table(table_name) : nullptr;
  Row mutable_copy = new_row;
  return fireTriggers(table_name, TriggerTiming::After, TriggerEvent::Update,
                      &old_row, &mutable_copy, table);
}

QueryResult TriggerExecutor::executeBeforeDelete(const std::string &table_name,
                                                 const Row &old_row) {
  Table *table = database_ ? database_->get_table(table_name) : nullptr;
  return fireTriggers(table_name, TriggerTiming::Before, TriggerEvent::Delete,
                      &old_row, nullptr, table);
}

QueryResult TriggerExecutor::executeAfterDelete(const std::string &table_name,
                                                const Row &old_row) {
  Table *table = database_ ? database_->get_table(table_name) : nullptr;
  return fireTriggers(table_name, TriggerTiming::After, TriggerEvent::Delete,
                      &old_row, nullptr, table);
}

QueryResult TriggerExecutor::fireTriggers(const std::string &table_name,
                                          TriggerTiming timing,
                                          TriggerEvent event,
                                          const Row *old_row, Row *new_row,
                                          Table *schema_table) {
  if (!database_) {
    return QueryResult::success_result("OK");
  }
  const auto triggers =
      database_->get_trigger_catalog().listBy(table_name, timing, event);
  if (triggers.empty()) {
    return QueryResult::success_result("OK");
  }
  if (!schema_table) {
    return QueryResult::error_result("Table '" + table_name + "' not found");
  }
  thread_local int local_depth = 0;
  const int current_depth = std::max(database_->get_trigger_depth(), local_depth);
  if (current_depth >= MAX_TRIGGER_DEPTH) {
    return QueryResult::error_result(
        "Trigger recursion depth exceeded (max " +
        std::to_string(MAX_TRIGGER_DEPTH) + ")");
  }
  database_->enter_trigger_depth();
  ++local_depth;
  const bool allow_set_new =
      timing == TriggerTiming::Before &&
      (event == TriggerEvent::Insert || event == TriggerEvent::Update);
  QueryResult result = QueryResult::success_result("OK");
  for (const TriggerDefinition *trigger : triggers) {
    result =
        runTriggerBody(*trigger, old_row, new_row, schema_table, allow_set_new);
    if (!result.success) {
      break;
    }
  }
  --local_depth;
  database_->leave_trigger_depth();
  return result;
}

QueryResult TriggerExecutor::runTriggerBody(const TriggerDefinition &trigger,
                                            const Row *old_row, Row *new_row,
                                            Table *schema_table,
                                            bool allow_set_new) {
  CorrelationContext *ctx = database_->get_correlation_context();
  int frames_pushed = 0;
  if (old_row) {
    ctx->pushFrame(buildRowBindings(*schema_table, "OLD"), *old_row);
    ++frames_pushed;
  }
  if (new_row) {
    ctx->pushFrame(buildRowBindings(*schema_table, "NEW"), *new_row);
    ++frames_pushed;
  }
  auto pop_frames = [&]() {
    for (int i = 0; i < frames_pushed; ++i) {
      ctx->popFrame();
    }
  };
  SessionContext *session = session_;
  if (!session) {
    session = database_->get_active_session();
  }
  for (const std::string &stmt_sql : trigger.getStatementSqls()) {
    Parser parser(stmt_sql);
    ParsedStatement parsed = parser.parse_statement();
    if (auto set_new = std::get_if<std::shared_ptr<SetNewStatement>>(&parsed)) {
      if (!allow_set_new || !new_row) {
        pop_frames();
        return QueryResult::error_result(
            "SET NEW is only valid in BEFORE INSERT/UPDATE triggers");
      }
      const int col_idx =
          schema_table->get_column_index((*set_new)->get_column_name());
      if (col_idx < 0) {
        pop_frames();
        return QueryResult::error_result(
            "Unknown column '" + (*set_new)->get_column_name() +
            "' in SET NEW");
      }
      std::vector<SelectColumnBinding> bindings =
          buildRowBindings(*schema_table, schema_table->get_name());
      SelectExpressionEvaluator eval(std::move(bindings));
      eval.set_correlation_context(ctx);
      eval.set_routine_catalog(&database_->get_routine_catalog());
      bind_subquery_evaluators(eval, database_);
      std::string error_msg;
      Value value = eval.evaluate_expression(*new_row, (*set_new)->get_value(),
                                             &error_msg);
      if (!error_msg.empty()) {
        pop_frames();
        return QueryResult::error_result(error_msg);
      }
      new_row->set_value(static_cast<size_t>(col_idx), value);
      refreshNewFrame(ctx, *schema_table, *new_row);
      continue;
    }
    QueryResult step =
        database_->run_parsed_statement(parsed, session, stmt_sql);
    if (!step.success) {
      pop_frames();
      return step;
    }
  }
  pop_frames();
  return QueryResult::success_result("OK");
}

}  // namespace db
