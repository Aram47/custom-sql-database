#pragma once

#include <memory>
#include <string>
#include <vector>

#include "executor/query_executor.h"

namespace db {

class Protocol {
 public:
  // Request format: "QUERY|<sql>\n", "AUTH|<user>|<password>\n", "PING|\n",
  // "QUIT|\n", "RPC_QUERY|<secret>|<sql>\n"
  // Response format: "OK|<result_data>\n" or "ERROR|<error_msg>\n"

  struct Request {
    std::string type;  // "QUERY", "PING", "RPC_QUERY", etc.
    std::string data;
  };

  struct Response {
    bool success;
    std::string message;
    std::vector<std::string> column_names;
    std::vector<std::vector<std::string>> rows;
  };

  static Request parse_request(const std::string &message);
  static std::string format_response(const Response &response);
  static std::string format_query_result(const QueryResult &result);

  /** Builds an internode RPC_QUERY request. */
  static std::string format_rpc_query(const std::string &rpcSecret,
                                      const std::string &sql);

  /**
   * Splits RPC_QUERY payload into secret and SQL.
   * @return false when the payload format is invalid.
   */
  static bool split_rpc_query(const std::string &data, std::string *secret,
                              std::string *sql);

  /** Parses an OK/ERROR wire response into QueryResult. */
  static QueryResult parse_query_result(const std::string &message);
};

}  // namespace db
