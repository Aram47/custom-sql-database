#include "network/result_merger.h"

namespace db {

QueryResult ResultMerger::merge(const std::vector<QueryResult> &results) {
  if (results.empty()) {
    return QueryResult::error_result("scatter-gather produced no results");
  }
  for (const QueryResult &result : results) {
    if (!result.success) {
      return QueryResult::error_result(result.message.empty()
                                           ? "worker query failed"
                                           : result.message);
    }
  }
  QueryResult merged = QueryResult::success_result("OK");
  merged.column_names = results.front().column_names;
  for (size_t i = 0; i < results.size(); ++i) {
    const QueryResult &part = results[i];
    if (part.column_names != merged.column_names) {
      return QueryResult::error_result(
          "scatter-gather column mismatch across shards");
    }
    for (const auto &row : part.rows) {
      merged.rows.push_back(row);
    }
    merged.affected_rows += part.affected_rows;
  }
  return merged;
}

}  // namespace db
