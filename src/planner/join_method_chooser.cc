#include "planner/join_method_chooser.h"

#include <algorithm>

namespace db {

JoinMethodChoice JoinMethodChooser::choose(const JoinMethodInput &input) {
  JoinMethodChoice choice;
  const size_t left_rows = input.left_rows;
  const size_t right_rows = input.right_rows;
  const size_t left_ndv =
      input.left_ndv == 0 ? std::max(left_rows, size_t{1}) : input.left_ndv;
  const size_t right_ndv =
      input.right_ndv == 0 ? std::max(right_rows, size_t{1}) : input.right_ndv;
  const double selectivity =
      CostModel::estimateEquiJoinSelectivity(left_ndv, right_ndv);
  bool build_on_left = left_rows < right_rows;
  if (left_rows == right_rows) {
    build_on_left = left_ndv <= right_ndv;
  }
  choice.build_on_left = build_on_left;
  if (!input.is_equi_inner) {
    choice.kind = JoinMethodKind::NestedLoop;
    choice.cost = CostModel::estimateNestedLoopCost(left_rows, right_rows,
                                                    false);
    return choice;
  }
  const double nested_loop_cost =
      CostModel::estimateNestedLoopCost(left_rows, right_rows, false);
  choice.kind = JoinMethodKind::NestedLoop;
  choice.cost = nested_loop_cost;
  if (input.has_inner_index) {
    const double index_cost =
        CostModel::estimateNestedLoopCost(left_rows, right_rows, true);
    if (index_cost <= choice.cost) {
      choice.kind = JoinMethodKind::IndexNestedLoop;
      choice.cost = index_cost;
    }
  }
  const size_t max_rows = std::max(left_rows, right_rows);
  if (max_rows < CostModel::HASH_JOIN_MIN_ROWS) {
    return choice;
  }
  const size_t build_rows = build_on_left ? left_rows : right_rows;
  const size_t probe_rows = build_on_left ? right_rows : left_rows;
  const double hash_cost =
      CostModel::estimateHashJoinCost(build_rows, probe_rows);
  if (hash_cost < choice.cost ||
      (choice.kind == JoinMethodKind::NestedLoop && selectivity < 1.0 &&
       hash_cost <= choice.cost)) {
    choice.kind = JoinMethodKind::HashJoin;
    choice.cost = hash_cost;
  }
  return choice;
}

const char *joinMethodKindName(JoinMethodKind kind) {
  switch (kind) {
    case JoinMethodKind::NestedLoop:
      return "NestedLoop";
    case JoinMethodKind::IndexNestedLoop:
      return "IndexNestedLoop";
    case JoinMethodKind::HashJoin:
      return "HashJoin";
  }
  return "NestedLoop";
}

}  // namespace db
