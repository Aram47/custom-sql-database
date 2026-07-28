#pragma once

#include <string>

#include "core/shard_map.h"
#include "executor/query_executor.h"

namespace db {

/**
 * Abstraction for coordinator → worker RPC.
 * Test doubles implement this without TCP.
 */
class IRpcClient {
 public:
  virtual ~IRpcClient() = default;

  /**
   * Executes SQL on a worker via RPC_QUERY.
   * @param rpcSecret Shared internode secret.
   */
  virtual QueryResult executeQuery(const ShardEndpoint &endpoint,
                                   const std::string &rpcSecret,
                                   const std::string &sql) = 0;

  /** Returns true when the worker answers PING. */
  virtual bool ping(const ShardEndpoint &endpoint) = 0;
};

/** TCP implementation using platform::tcp_exchange. */
class TcpRpcClient : public IRpcClient {
 public:
  QueryResult executeQuery(const ShardEndpoint &endpoint,
                           const std::string &rpcSecret,
                           const std::string &sql) override;
  bool ping(const ShardEndpoint &endpoint) override;
};

}  // namespace db
