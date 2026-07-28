#include "network/rpc_client.h"

#include "network/protocol.h"
#include "platform/tcp_client.h"

namespace db {

QueryResult TcpRpcClient::executeQuery(const ShardEndpoint &endpoint,
                                       const std::string &rpcSecret,
                                       const std::string &sql) {
  const std::string request =
      Protocol::format_rpc_query(rpcSecret, sql);
  const std::string raw = platform::tcp_exchange(endpoint.host, endpoint.port,
                                                 request);
  if (raw.empty()) {
    return QueryResult::error_result("RPC failed: no response from shard " +
                                     std::to_string(endpoint.shardId));
  }
  return Protocol::parse_query_result(raw);
}

bool TcpRpcClient::ping(const ShardEndpoint &endpoint) {
  const std::string raw =
      platform::tcp_exchange(endpoint.host, endpoint.port, "PING|\n");
  return raw.find("PONG") != std::string::npos;
}

}  // namespace db
