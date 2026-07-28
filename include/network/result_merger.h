#pragma once

#include <string>
#include <vector>

#include "executor/query_executor.h"

namespace db {

/**
 * Merges column-aligned QueryResult sets from scatter-gather.
 */
class ResultMerger {
 public:
  /**
   * Unions rows from successful results. Columns must match the first result.
   * @return error if any input failed or columns disagree.
   */
  static QueryResult merge(const std::vector<QueryResult> &results);
};

}  // namespace db
