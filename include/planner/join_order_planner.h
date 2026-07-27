#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "planner/cost_model.h"
#include "planner/table_statistics.h"

namespace db {

struct JoinRelation {
  std::string table_name;
  size_t estimated_rows{0};
  bool has_indexed_equi_join{false};
};

/** Left-deep join order by nested-loop cost for small relation sets. */
class JoinOrderPlanner {
 public:
  static std::vector<size_t> planLeftDeepOrder(
      const std::vector<JoinRelation> &relations);
};

}  // namespace db
