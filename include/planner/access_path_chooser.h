#pragma once

#include <cstddef>

#include "planner/cost_model.h"

namespace db {

enum class AccessPathKind { SeqScan, IndexScan };

struct AccessPathChoice {
  AccessPathKind kind{AccessPathKind::SeqScan};
  double cost{0.0};
};

/** Chooses seq scan vs index scan by estimated cost. */
class AccessPathChooser {
 public:
  static AccessPathChoice choose(size_t row_count, bool has_index_path,
                                 double equality_selectivity = 0.1);
};

}  // namespace db
