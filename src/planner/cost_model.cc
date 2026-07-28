#include "planner/cost_model.h"

#include <algorithm>

namespace db {

double CostModel::estimateSeqScanCost(size_t row_count) {
  return SEQ_TUPLE_COST * static_cast<double>(row_count);
}

double CostModel::estimateIndexScanCost(size_t row_count, double selectivity) {
  const double selected =
      static_cast<double>(row_count) * selectivity;
  return INDEX_STARTUP_COST + INDEX_TUPLE_COST * selected;
}

double CostModel::estimateNestedLoopCost(size_t outer_rows, size_t inner_rows,
                                         bool has_inner_index) {
  const double outer = static_cast<double>(outer_rows);
  if (has_inner_index) {
    return outer * (INDEX_STARTUP_COST + INDEX_TUPLE_COST);
  }
  return outer * SEQ_TUPLE_COST * static_cast<double>(inner_rows);
}

double CostModel::estimateHashJoinCost(size_t build_rows, size_t probe_rows) {
  return HASH_BUILD_TUPLE_COST * static_cast<double>(build_rows) +
         HASH_PROBE_TUPLE_COST * static_cast<double>(probe_rows);
}

double CostModel::estimateEquiJoinSelectivity(size_t left_ndv,
                                              size_t right_ndv) {
  const size_t denom = std::max(size_t{1}, std::max(left_ndv, right_ndv));
  return 1.0 / static_cast<double>(denom);
}

}  // namespace db
