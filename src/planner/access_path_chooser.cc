#include "planner/access_path_chooser.h"

namespace db {

AccessPathChoice AccessPathChooser::choose(size_t row_count,
                                            bool has_index_path,
                                            double equality_selectivity) {
  AccessPathChoice choice;
  choice.kind = AccessPathKind::SeqScan;
  choice.cost = CostModel::estimateSeqScanCost(row_count);
  if (!has_index_path || row_count == 0) {
    return choice;
  }
  const double selectivity =
      equality_selectivity > 0.0
          ? equality_selectivity
          : 1.0 / static_cast<double>(row_count);
  const double index_cost =
      CostModel::estimateIndexScanCost(row_count, selectivity);
  if (index_cost < choice.cost) {
    choice.kind = AccessPathKind::IndexScan;
    choice.cost = index_cost;
  }
  return choice;
}

}  // namespace db
