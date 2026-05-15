#pragma once

#include <memory>
#include <string>
#include <vector>

#include "executor/query_executor.h"

namespace db {

class Protocol {
 public:
  // Request format: "QUERY|<sql>\n"
  // Response format: "OK|<result_data>\n" or "ERROR|<error_msg>\n"

  struct Request {
    std::string type;  // "QUERY", "PING", etc.
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
};

}  // namespace db
