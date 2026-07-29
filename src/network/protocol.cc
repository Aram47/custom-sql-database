#include "network/protocol.h"

#include <sstream>
#include <vector>

#include "types/value.h"

namespace db {
namespace {

// Textual wire NULL marker (PostgreSQL COPY-compatible). Empty STRING stays "".
constexpr const char *kWireNull = "\\N";

std::vector<std::string> split_tabs(const std::string &line) {
  std::vector<std::string> parts;
  std::string current;
  for (char ch : line) {
    if (ch == '\t') {
      parts.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(ch);
  }
  parts.push_back(current);
  return parts;
}

std::string encode_wire_cell(const Value &val) {
  if (val.is_null()) {
    return kWireNull;
  }
  return val.to_string();
}

Value decode_wire_cell(const std::string &cell) {
  if (cell == kWireNull) {
    return Value();
  }
  return Value(cell);
}

}  // namespace

Protocol::Request Protocol::parse_request(const std::string &message) {
  size_t pipePos = message.find('|');
  if (pipePos == std::string::npos) {
    throw std::runtime_error("Invalid request format");
  }
  Request req;
  req.type = message.substr(0, pipePos);
  req.data = message.substr(pipePos + 1);
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
  for (size_t i = 0; i < response.column_names.size(); ++i) {
    if (i > 0) {
      oss << "\t";
    }
    oss << response.column_names[i];
  }
  if (!response.rows.empty()) {
    oss << "\n";
    for (const auto &row : response.rows) {
      for (size_t i = 0; i < row.size(); ++i) {
        if (i > 0) {
          oss << "\t";
        }
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
  for (const auto &row : result.rows) {
    std::vector<std::string> strRow;
    strRow.reserve(row.size());
    for (const auto &val : row) {
      // NULL -> "\N"; empty STRING -> "" (distinct wire values).
      strRow.push_back(encode_wire_cell(val));
    }
    resp.rows.push_back(strRow);
  }
  return format_response(resp);
}

std::string Protocol::format_rpc_query(const std::string &rpcSecret,
                                       const std::string &sql) {
  return "RPC_QUERY|" + rpcSecret + "|" + sql + "\n";
}

bool Protocol::split_rpc_query(const std::string &data, std::string *secret,
                               std::string *sql) {
  if (!secret || !sql) {
    return false;
  }
  const size_t pipePos = data.find('|');
  if (pipePos == std::string::npos) {
    return false;
  }
  *secret = data.substr(0, pipePos);
  *sql = data.substr(pipePos + 1);
  return !secret->empty() && !sql->empty();
}

QueryResult Protocol::parse_query_result(const std::string &message) {
  if (message.size() >= 6 && message.compare(0, 6, "ERROR|") == 0) {
    std::string err = message.substr(6);
    if (!err.empty() && err.back() == '\n') {
      err.pop_back();
    }
    return QueryResult::error_result(err);
  }
  if (message.size() < 3 || message.compare(0, 3, "OK|") != 0) {
    return QueryResult::error_result("invalid RPC response");
  }
  std::string body = message.substr(3);
  // Strip at most one trailing response newline so an empty-string data row
  // (e.g. "OK|s\n\n") is preserved.
  if (!body.empty() && (body.back() == '\n' || body.back() == '\r')) {
    body.pop_back();
  }
  if (!body.empty() && body.back() == '\r') {
    body.pop_back();
  }
  QueryResult result = QueryResult::success_result("OK");
  if (body.empty()) {
    return result;
  }
  std::vector<std::string> lines;
  std::string current;
  for (char ch : body) {
    if (ch == '\n') {
      lines.push_back(current);
      current.clear();
      continue;
    }
    if (ch != '\r') {
      current.push_back(ch);
    }
  }
  if (!current.empty() || (!body.empty() && body.back() == '\n')) {
    lines.push_back(current);
  }
  if (lines.empty()) {
    return result;
  }
  result.column_names = split_tabs(lines[0]);
  if (result.column_names.size() == 1 && result.column_names[0].empty()) {
    result.column_names.clear();
  }
  for (size_t i = 1; i < lines.size(); ++i) {
    // Preserve empty data lines as empty-string rows; only skip when there
    // are no columns (status-like body) — otherwise empty cell rows matter.
    if (lines[i].empty() && result.column_names.empty()) {
      continue;
    }
    const std::vector<std::string> cells = split_tabs(lines[i]);
    std::vector<Value> row;
    row.reserve(cells.size());
    for (const std::string &cell : cells) {
      row.push_back(decode_wire_cell(cell));
    }
    result.rows.push_back(std::move(row));
  }
  return result;
}

}  // namespace db
