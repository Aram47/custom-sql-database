#pragma once

#include <cstddef>

#include "planner/cost_model.h"

namespace db {

enum class JoinMethodKind { NestedLoop, IndexNestedLoop, HashJoin };

/** Inputs for choosing nested-loop vs hash join on one equi edge. */
struct JoinMethodInput {
  size_t left_rows{0};
  size_t right_rows{0};
  size_t left_ndv{0};
  size_t right_ndv{0};
  bool has_inner_index{false};
  bool is_equi_inner{false};
};

struct JoinMethodChoice {
  JoinMethodKind kind{JoinMethodKind::NestedLoop};
  double cost{0.0};
  bool build_on_left{true};
};

/**
 * Chooses NestedLoop, IndexNestedLoop, or HashJoin by estimated cost.
 * HashJoin only for INNER equi when no cheaper index probe and sizes
 * meet CostModel::HASH_JOIN_MIN_ROWS.
 */
class JoinMethodChooser {
 public:
  static JoinMethodChoice choose(const JoinMethodInput &input);
};

/** Human-readable name for EXPLAIN. */
const char *joinMethodKindName(JoinMethodKind kind);

}  // namespace db
