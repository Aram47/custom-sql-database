#include "network/protocol.h"

#include <algorithm>
#include <sstream>

namespace db {

Protocol::Request Protocol::parse_request(const std::string &message) {
  size_t pipePos = message.find('|');
  if (pipePos == std::string::npos) {
    throw std::runtime_error("Invalid request format");
  }

  Request req;
  req.type = message.substr(0, pipePos);
  req.data = message.substr(pipePos + 1);

  // Remove trailing newline
  if (!req.data.empty() && req.data.back() == '\n') {
    req.data.pop_back();
  }

  return req;
}

std::string Protocol::format_response(const Response &response) {
  std::ostringstream oss;

  if (response.success) {
    oss << "OK|";
  } else {
    oss << "ERROR|" << response.message << "\n";
    return oss.str();
  }

  // Format column names
  for (size_t i = 0; i < response.column_names.size(); ++i) {
    if (i > 0) oss << "\t";
    oss << response.column_names[i];
  }

  // Format rows
  if (!response.rows.empty()) {
    oss << "\n";
    for (const auto &row : response.rows) {
      for (size_t i = 0; i < row.size(); ++i) {
        if (i > 0) oss << "\t";
        oss << row[i];
      }
      oss << "\n";
    }
  } else {
    oss << "\n";
  }

  return oss.str();
}

std::string Protocol::format_query_result(const QueryResult &result) {
  Response resp;
  resp.success = result.success;
  resp.message = result.message;
  resp.column_names = result.column_names;

  // Convert Value to string for rows
  for (const auto &row : result.rows) {
    std::vector<std::string> strRow;
    for (const auto &val : row) {
      strRow.push_back(val.to_string());
    }
    resp.rows.push_back(strRow);
  }

  return format_response(resp);
}

}  // namespace db
