#pragma once

#include <cstddef>

namespace db {

/** Simple in-memory cost units for access-path and join decisions. */
class CostModel {
 public:
  static constexpr double SEQ_TUPLE_COST = 1.0;
  static constexpr double INDEX_TUPLE_COST = 0.1;
  static constexpr double INDEX_STARTUP_COST = 2.0;

  static double estimateSeqScanCost(size_t row_count);
  static double estimateIndexScanCost(size_t row_count, double selectivity);
  static double estimateNestedLoopCost(size_t outer_rows, size_t inner_rows,
                                       bool has_inner_index);
};

}  // namespace db
