#pragma once

#include <cstddef>
#include <vector>

#include "types/value.h"

namespace db {

/**
 * In-memory hash join for INNER equi joins.
 * NULL join keys never match (SQL three-valued logic).
 */
class HashJoinExecutor {
 public:
  /**
   * Joins left_rows and right_rows on equality of key columns.
   * @param left_rows Outer/left input rows (already projected to values).
   * @param right_rows Inner/right input rows.
   * @param left_key_index Column index of the join key in left rows.
   * @param right_key_index Column index of the join key in right rows.
   * @param build_on_left When true, hash left side; otherwise hash right.
   * @return Concatenated matching rows (left columns then right columns).
   */
  static std::vector<std::vector<Value>> executeInnerEqui(
      const std::vector<std::vector<Value>> &left_rows,
      const std::vector<std::vector<Value>> &right_rows, size_t left_key_index,
      size_t right_key_index, bool build_on_left);
};

}  // namespace db
