#include "planner/cost_model.h"

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

}  // namespace db
