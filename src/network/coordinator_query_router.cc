#include "network/coordinator_query_router.h"

#include <map>
#include <variant>

#include "executor/partition_prune.h"
#include "network/result_merger.h"
#include "parser/ast.h"
#include "parser/parser.h"

namespace db {
namespace {

bool isBroadcastStatement(const ParsedStatement &stmt) {
  return std::holds_alternative<std::shared_ptr<CreateTableStatement>>(stmt) ||
         std::holds_alternative<std::shared_ptr<DropTableStatement>>(stmt) ||
         std::holds_alternative<std::shared_ptr<AlterTableStatement>>(stmt) ||
         std::holds_alternative<std::shared_ptr<CreateIndexStatement>>(stmt) ||
         std::holds_alternative<std::shared_ptr<DropIndexStatement>>(stmt) ||
         std::holds_alternative<std::shared_ptr<CreateViewStatement>>(stmt) ||
         std::holds_alternative<std::shared_ptr<DropViewStatement>>(stmt) ||
         std::holds_alternative<std::shared_ptr<CreateFunctionStatement>>(
             stmt) ||
         std::holds_alternative<std::shared_ptr<DropFunctionStatement>>(stmt) ||
         std::holds_alternative<std::shared_ptr<CreateProcedureStatement>>(
             stmt) ||
         std::holds_alternative<std::shared_ptr<DropProcedureStatement>>(stmt) ||
         std::holds_alternative<std::shared_ptr<CreateTriggerStatement>>(stmt) ||
         std::holds_alternative<std::shared_ptr<DropTriggerStatement>>(stmt) ||
         std::holds_alternative<std::shared_ptr<VacuumStatement>>(stmt);
}

bool tryLiteralFromExpression(const ExpressionPtr &expr, Value *out,
                              std::string *error) {
  auto lit = std::dynamic_pointer_cast<LiteralExpression>(expr);
  if (!lit) {
    if (error) {
      *error = "partition key must be a literal for coordinator routing";
    }
    return false;
  }
  *out = lit->get_value();
  return true;
}

}  // namespace

CoordinatorQueryRouter::CoordinatorQueryRouter(
    Database *metaDatabase, ShardRouter shardRouter,
    std::unique_ptr<IRpcClient> rpcClient, std::string rpcSecret)
    : meta_database_(metaDatabase),
      shard_router_(std::move(shardRouter)),
      rpc_client_(std::move(rpcClient)),
      rpc_secret_(std::move(rpcSecret)) {}

QueryResult CoordinatorQueryRouter::executeQuery(const std::string &sql,
                                                 SessionContext *session) {
  if (!meta_database_ || !rpc_client_) {
    return QueryResult::error_result("coordinator is not configured");
  }
  ParsedStatement stmt;
  try {
    Parser parser(sql);
    stmt = parser.parse_statement();
  } catch (const std::exception &ex) {
    return QueryResult::error_result(ex.what());
  }
  if (std::holds_alternative<std::shared_ptr<BeginStatement>>(stmt)) {
    return handleBegin(session);
  }
  if (std::holds_alternative<std::shared_ptr<CommitStatement>>(stmt)) {
    return handleCommit(session);
  }
  if (std::holds_alternative<std::shared_ptr<RollbackStatement>>(stmt)) {
    return handleRollback(session);
  }
  if (std::holds_alternative<std::shared_ptr<PrepareStatement>>(stmt) ||
      std::holds_alternative<std::shared_ptr<ExecutePreparedStatement>>(stmt) ||
      std::holds_alternative<std::shared_ptr<DeallocatePreparedStatement>>(
          stmt)) {
    return QueryResult::error_result(
        "prepared statements are not supported on coordinator in v1");
  }
  if (isBroadcastStatement(stmt)) {
    return handleBroadcast(sql, session);
  }
  if (auto explain = std::get_if<std::shared_ptr<ExplainStatement>>(&stmt)) {
    const ParsedStatement &inner = (*explain)->get_inner();
    if (auto select =
            std::get_if<std::shared_ptr<SelectStatement>>(&inner)) {
      return handleSelect(*select, sql, session);
    }
    return handleBroadcast(sql, session);
  }
  if (auto select = std::get_if<std::shared_ptr<SelectStatement>>(&stmt)) {
    return handleSelect(*select, sql, session);
  }
  if (std::holds_alternative<std::shared_ptr<SetOperationStatement>>(stmt)) {
    return QueryResult::error_result(
        "set operations are not supported on coordinator in v1");
  }
  if (auto insert = std::get_if<std::shared_ptr<InsertStatement>>(&stmt)) {
    return handleInsert(*insert, sql, session);
  }
  if (auto update = std::get_if<std::shared_ptr<UpdateStatement>>(&stmt)) {
    return handleUpdate(*update, sql, session);
  }
  if (auto del = std::get_if<std::shared_ptr<DeleteStatement>>(&stmt)) {
    return handleDelete(*del, sql, session);
  }
  if (auto call = std::get_if<std::shared_ptr<CallStatement>>(&stmt)) {
    (void)call;
    const auto workers = shard_router_.getShardMap().listWorkers();
    return proxyToEndpoints(workers, sql, session, false);
  }
  return QueryResult::error_result(
      "statement type is not supported on coordinator in v1");
}

QueryResult CoordinatorQueryRouter::handleBegin(SessionContext *session) {
  if (!session) {
    return QueryResult::error_result("BEGIN requires a session");
  }
  if (session->is_in_transaction()) {
    return QueryResult::error_result("Transaction already active");
  }
  session->set_in_transaction(true);
  session->clear_pinned_shard_id();
  return QueryResult::success_result("BEGIN OK");
}

QueryResult CoordinatorQueryRouter::handleCommit(SessionContext *session) {
  if (!session || !session->is_in_transaction()) {
    return QueryResult::error_result("No active transaction");
  }
  if (session->get_pinned_shard_id()) {
    const std::optional<ShardEndpoint> endpoint =
        shard_router_.getShardMap().findEndpoint(*session->get_pinned_shard_id());
    if (!endpoint) {
      session->set_in_transaction(false);
      session->clear_pinned_shard_id();
      return QueryResult::error_result("pinned shard is unavailable");
    }
    QueryResult remote =
        rpc_client_->executeQuery(*endpoint, rpc_secret_, "COMMIT");
    session->set_in_transaction(false);
    session->clear_pinned_shard_id();
    return remote;
  }
  session->set_in_transaction(false);
  session->clear_pinned_shard_id();
  return QueryResult::success_result("COMMIT OK");
}

QueryResult CoordinatorQueryRouter::handleRollback(SessionContext *session) {
  if (!session || !session->is_in_transaction()) {
    return QueryResult::error_result("No active transaction");
  }
  if (session->get_pinned_shard_id()) {
    const std::optional<ShardEndpoint> endpoint =
        shard_router_.getShardMap().findEndpoint(*session->get_pinned_shard_id());
    if (!endpoint) {
      session->set_in_transaction(false);
      session->clear_pinned_shard_id();
      return QueryResult::error_result("pinned shard is unavailable");
    }
    QueryResult remote =
        rpc_client_->executeQuery(*endpoint, rpc_secret_, "ROLLBACK");
    session->set_in_transaction(false);
    session->clear_pinned_shard_id();
    return remote;
  }
  session->set_in_transaction(false);
  session->clear_pinned_shard_id();
  return QueryResult::success_result("ROLLBACK OK");
}

QueryResult CoordinatorQueryRouter::handleBroadcast(const std::string &sql,
                                                    SessionContext *session) {
  if (session && session->is_in_transaction()) {
    return QueryResult::error_result(
        "DDL is not allowed inside a coordinator transaction");
  }
  const std::vector<ShardEndpoint> workers =
      shard_router_.getShardMap().listWorkers();
  std::string healthError;
  if (!ensureWorkersHealthy(workers, &healthError)) {
    return QueryResult::error_result(healthError);
  }
  for (const ShardEndpoint &endpoint : workers) {
    QueryResult remote =
        rpc_client_->executeQuery(endpoint, rpc_secret_, sql);
    if (!remote.success) {
      return QueryResult::error_result("broadcast failed on shard " +
                                       std::to_string(endpoint.shardId) + ": " +
                                       remote.message);
    }
  }
  return meta_database_->execute_query(sql, session);
}

QueryResult CoordinatorQueryRouter::handleSelect(
    const std::shared_ptr<SelectStatement> &stmt, const std::string &sql,
    SessionContext *session) {
  if (!stmt) {
    return QueryResult::error_result("empty SELECT");
  }
  if (!stmt->get_joins().empty()) {
    std::vector<std::string> tables;
    tables.push_back(stmt->get_from_table());
    for (const auto &join : stmt->get_joins()) {
      tables.push_back(std::get<1>(join));
    }
    std::map<int, ShardEndpoint> shardSet;
    for (const std::string &tableName : tables) {
      std::string error;
      auto endpoints = resolveTableShards(tableName, nullptr, &error);
      if (!endpoints) {
        return QueryResult::error_result(error);
      }
      for (const ShardEndpoint &endpoint : *endpoints) {
        shardSet[endpoint.shardId] = endpoint;
      }
    }
    if (shardSet.size() > 1) {
      return QueryResult::error_result(
          "cross-shard JOIN not supported in v1");
    }
    std::vector<ShardEndpoint> single;
    if (!shardSet.empty()) {
      single.push_back(shardSet.begin()->second);
    } else {
      auto first = shard_router_.getShardMap().firstWorker();
      if (!first) {
        return QueryResult::error_result("no workers configured");
      }
      single.push_back(*first);
    }
    return proxyToEndpoints(single, sql, session, false);
  }
  std::string error;
  auto endpoints =
      resolveTableShards(stmt->get_from_table(), stmt->get_where_condition(),
                         &error);
  if (!endpoints) {
    return QueryResult::error_result(error);
  }
  return proxyToEndpoints(*endpoints, sql, session, true);
}

QueryResult CoordinatorQueryRouter::handleInsert(
    const std::shared_ptr<InsertStatement> &stmt, const std::string &sql,
    SessionContext *session) {
  if (!stmt) {
    return QueryResult::error_result("empty INSERT");
  }
  Table *table = meta_database_->get_table(stmt->get_table());
  if (!table) {
    return QueryResult::error_result("Table not found: " + stmt->get_table());
  }
  if (!table->isPartitioned()) {
    auto first = shard_router_.getShardMap().firstWorker();
    if (!first) {
      return QueryResult::error_result("no workers configured");
    }
    return proxyToEndpoints({*first}, sql, session, false);
  }
  const PartitionedTableMetadata *meta = table->getPartitionMetadata();
  auto router = meta->createRouter();
  std::optional<ShardEndpoint> target;
  for (size_t rowIndex = 0; rowIndex < stmt->get_values().size(); ++rowIndex) {
    std::string error;
    std::optional<Value> key = extractInsertPartitionKey(
        *stmt, *table, meta->getKeyColumn(), rowIndex, &error);
    if (!key) {
      return QueryResult::error_result(error);
    }
    std::optional<ShardEndpoint> endpoint =
        shard_router_.resolveKey(*router, *key, &error);
    if (!endpoint) {
      return QueryResult::error_result(error);
    }
    if (!target) {
      target = endpoint;
      continue;
    }
    if (target->shardId != endpoint->shardId) {
      return QueryResult::error_result(
          "multi-shard INSERT is not supported in v1");
    }
  }
  if (!target) {
    return QueryResult::error_result("INSERT has no values");
  }
  return proxyToEndpoints({*target}, sql, session, false);
}

QueryResult CoordinatorQueryRouter::handleUpdate(
    const std::shared_ptr<UpdateStatement> &stmt, const std::string &sql,
    SessionContext *session) {
  if (!stmt) {
    return QueryResult::error_result("empty UPDATE");
  }
  std::string error;
  auto endpoints = resolveTableShards(stmt->get_table(),
                                      stmt->get_where_condition(), &error);
  if (!endpoints) {
    return QueryResult::error_result(error);
  }
  if (endpoints->size() != 1) {
    return QueryResult::error_result(
        "multi-shard UPDATE is not supported in v1");
  }
  return proxyToEndpoints(*endpoints, sql, session, false);
}

QueryResult CoordinatorQueryRouter::handleDelete(
    const std::shared_ptr<DeleteStatement> &stmt, const std::string &sql,
    SessionContext *session) {
  if (!stmt) {
    return QueryResult::error_result("empty DELETE");
  }
  std::string error;
  auto endpoints = resolveTableShards(stmt->get_table(),
                                      stmt->get_where_condition(), &error);
  if (!endpoints) {
    return QueryResult::error_result(error);
  }
  if (endpoints->size() != 1) {
    return QueryResult::error_result(
        "multi-shard DELETE is not supported in v1");
  }
  return proxyToEndpoints(*endpoints, sql, session, false);
}

QueryResult CoordinatorQueryRouter::proxyToEndpoints(
    const std::vector<ShardEndpoint> &endpoints, const std::string &sql,
    SessionContext *session, bool allowScatter) {
  if (endpoints.empty()) {
    return QueryResult::error_result("no target shards for query");
  }
  if (!allowScatter && endpoints.size() > 1) {
    return QueryResult::error_result(
        "statement touches multiple shards which is not allowed");
  }
  if (session && session->is_in_transaction() && endpoints.size() > 1) {
    return QueryResult::error_result(
        "multi-shard transaction is not supported in v1");
  }
  std::string healthError;
  if (!ensureWorkersHealthy(endpoints, &healthError)) {
    return QueryResult::error_result(healthError);
  }
  if (session && session->is_in_transaction()) {
    QueryResult pin = ensurePinned(session, endpoints.front().shardId);
    if (!pin.success) {
      return pin;
    }
    return forwardWithTransaction(endpoints.front(), sql, session);
  }
  if (endpoints.size() == 1) {
    return rpc_client_->executeQuery(endpoints.front(), rpc_secret_, sql);
  }
  std::vector<QueryResult> parts;
  parts.reserve(endpoints.size());
  for (const ShardEndpoint &endpoint : endpoints) {
    parts.push_back(rpc_client_->executeQuery(endpoint, rpc_secret_, sql));
  }
  return ResultMerger::merge(parts);
}

QueryResult CoordinatorQueryRouter::ensurePinned(SessionContext *session,
                                                 int shardId) {
  if (!session->get_pinned_shard_id()) {
    session->set_pinned_shard_id(shardId);
    return QueryResult::success_result("OK");
  }
  if (*session->get_pinned_shard_id() != shardId) {
    return QueryResult::error_result(
        "multi-shard transaction is not supported in v1");
  }
  return QueryResult::success_result("OK");
}

QueryResult CoordinatorQueryRouter::forwardWithTransaction(
    const ShardEndpoint &endpoint, const std::string &sql,
    SessionContext *session) {
  if (!session->has_remote_transaction_started()) {
    QueryResult beginResult =
        rpc_client_->executeQuery(endpoint, rpc_secret_, "BEGIN");
    if (!beginResult.success) {
      return beginResult;
    }
    session->set_remote_transaction_started(true);
  }
  return rpc_client_->executeQuery(endpoint, rpc_secret_, sql);
}

bool CoordinatorQueryRouter::ensureWorkersHealthy(
    const std::vector<ShardEndpoint> &endpoints, std::string *error) const {
  for (const ShardEndpoint &endpoint : endpoints) {
    if (!rpc_client_->ping(endpoint)) {
      if (error) {
        *error = "required worker shard " + std::to_string(endpoint.shardId) +
                 " is down";
      }
      return false;
    }
  }
  return true;
}

std::optional<std::vector<ShardEndpoint>>
CoordinatorQueryRouter::resolveTableShards(const std::string &tableName,
                                           const ExpressionPtr &whereExpr,
                                           std::string *error) const {
  Table *table = meta_database_->get_table(tableName);
  if (!table) {
    if (shard_router_.getShardMap().hasPlacement(tableName)) {
      auto endpoint = shard_router_.resolveChildName(tableName, error);
      if (!endpoint) {
        return std::nullopt;
      }
      return std::vector<ShardEndpoint>{*endpoint};
    }
    auto first = shard_router_.getShardMap().firstWorker();
    if (!first) {
      if (error) {
        *error = "no workers configured";
      }
      return std::nullopt;
    }
    return std::vector<ShardEndpoint>{*first};
  }
  if (!table->isPartitioned()) {
    if (shard_router_.getShardMap().hasPlacement(tableName)) {
      auto endpoint = shard_router_.resolveChildName(tableName, error);
      if (!endpoint) {
        return std::nullopt;
      }
      return std::vector<ShardEndpoint>{*endpoint};
    }
    auto first = shard_router_.getShardMap().firstWorker();
    if (!first) {
      if (error) {
        *error = "no workers configured";
      }
      return std::nullopt;
    }
    return std::vector<ShardEndpoint>{*first};
  }
  const PartitionedTableMetadata *meta = table->getPartitionMetadata();
  auto router = meta->createRouter();
  PartitionPruneRequest request =
      buildPartitionPruneRequest(meta->getKeyColumn(), whereExpr);
  return shard_router_.resolvePrune(*router, request, error);
}

std::optional<Value> CoordinatorQueryRouter::extractInsertPartitionKey(
    const InsertStatement &stmt, const Table &table,
    const std::string &keyColumn, size_t rowIndex,
    std::string *error) const {
  if (rowIndex >= stmt.get_values().size()) {
    if (error) {
      *error = "INSERT row index out of range";
    }
    return std::nullopt;
  }
  const std::vector<ExpressionPtr> &row = stmt.get_values()[rowIndex];
  int keyIndex = -1;
  if (!stmt.get_columns().empty()) {
    for (size_t i = 0; i < stmt.get_columns().size(); ++i) {
      if (stmt.get_columns()[i] == keyColumn) {
        keyIndex = static_cast<int>(i);
        break;
      }
    }
  } else {
    keyIndex = table.get_column_index(keyColumn);
  }
  if (keyIndex < 0 || static_cast<size_t>(keyIndex) >= row.size()) {
    if (error) {
      *error = "INSERT is missing partition key column " + keyColumn;
    }
    return std::nullopt;
  }
  Value key;
  if (!tryLiteralFromExpression(row[static_cast<size_t>(keyIndex)], &key,
                                error)) {
    return std::nullopt;
  }
  return key;
}

}  // namespace db
