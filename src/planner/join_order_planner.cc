#include "planner/join_order_planner.h"

#include <algorithm>
#include <limits>

namespace db {

std::vector<size_t> JoinOrderPlanner::planLeftDeepOrder(
    const std::vector<JoinRelation> &relations) {
  const size_t count = relations.size();
  std::vector<size_t> order;
  if (count == 0) {
    return order;
  }
  if (count == 1) {
    return {0};
  }
  if (count > 6) {
    order.resize(count);
    for (size_t i = 0; i < count; ++i) {
      order[i] = i;
    }
    return order;
  }
  std::vector<bool> used(count, false);
  size_t best_start = 0;
  size_t best_start_rows = relations[0].estimated_rows;
  for (size_t i = 1; i < count; ++i) {
    if (relations[i].estimated_rows < best_start_rows) {
      best_start = i;
      best_start_rows = relations[i].estimated_rows;
    }
  }
  order.push_back(best_start);
  used[best_start] = true;
  size_t current_rows = relations[best_start].estimated_rows;
  while (order.size() < count) {
    size_t best_next = count;
    double best_cost = std::numeric_limits<double>::max();
    for (size_t i = 0; i < count; ++i) {
      if (used[i]) {
        continue;
      }
      const double cost = CostModel::estimateNestedLoopCost(
          current_rows, relations[i].estimated_rows,
          relations[i].has_indexed_equi_join);
      if (cost < best_cost) {
        best_cost = cost;
        best_next = i;
      }
    }
    used[best_next] = true;
    order.push_back(best_next);
    current_rows = std::max<size_t>(1, current_rows / 2);
  }
  return order;
}

}  // namespace db
