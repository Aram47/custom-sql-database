#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/database.h"
#include "core/session_context.h"
#include "core/shard_router.h"
#include "executor/query_executor.h"
#include "network/rpc_client.h"

namespace db {

/**
 * Thin coordinator routing layer: parse, analyze shards, proxy or scatter.
 * Uses a local meta Database for partition catalog; does not run user DML locally.
 */
class CoordinatorQueryRouter {
 public:
  CoordinatorQueryRouter(Database *metaDatabase, ShardRouter shardRouter,
                         std::unique_ptr<IRpcClient> rpcClient,
                         std::string rpcSecret);

  /** Routes or rejects a client SQL statement. */
  QueryResult executeQuery(const std::string &sql, SessionContext *session);

 private:
  Database *meta_database_;
  ShardRouter shard_router_;
  std::unique_ptr<IRpcClient> rpc_client_;
  std::string rpc_secret_;

  QueryResult handleBegin(SessionContext *session);
  QueryResult handleCommit(SessionContext *session);
  QueryResult handleRollback(SessionContext *session);
  QueryResult handleBroadcast(const std::string &sql, SessionContext *session);
  QueryResult handleSelect(const std::shared_ptr<SelectStatement> &stmt,
                           const std::string &sql, SessionContext *session);
  QueryResult handleInsert(const std::shared_ptr<InsertStatement> &stmt,
                           const std::string &sql, SessionContext *session);
  QueryResult handleUpdate(const std::shared_ptr<UpdateStatement> &stmt,
                           const std::string &sql, SessionContext *session);
  QueryResult handleDelete(const std::shared_ptr<DeleteStatement> &stmt,
                           const std::string &sql, SessionContext *session);
  QueryResult proxyToEndpoints(const std::vector<ShardEndpoint> &endpoints,
                               const std::string &sql, SessionContext *session,
                               bool allowScatter);
  QueryResult ensurePinned(SessionContext *session, int shardId);
  QueryResult forwardWithTransaction(const ShardEndpoint &endpoint,
                                     const std::string &sql,
                                     SessionContext *session);
  bool ensureWorkersHealthy(const std::vector<ShardEndpoint> &endpoints,
                            std::string *error) const;
  std::optional<std::vector<ShardEndpoint>> resolveTableShards(
      const std::string &tableName, const ExpressionPtr &whereExpr,
      std::string *error) const;
  std::optional<Value> extractInsertPartitionKey(
      const InsertStatement &stmt, const Table &table,
      const std::string &keyColumn, size_t rowIndex,
      std::string *error) const;
};

}  // namespace db
