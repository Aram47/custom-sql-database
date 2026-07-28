#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "executor/select_column_binding.h"
#include "parser/ast.h"
#include "planner/join_method_chooser.h"

namespace db {

class Database;
class Table;

/** Resolved equi-join keys and chosen physical method for one join edge. */
struct EquiJoinPlan {
  bool is_equi{false};
  int left_key_index{-1};
  int right_key_index{-1};
  std::string left_key_column;
  std::string right_key_column;
  JoinMethodChoice method;
};

/**
 * Ensures stats, resolves equi columns, and chooses NestedLoop /
 * IndexNestedLoop / HashJoin. left_rows / right_rows are cardinality estimates
 * (table row counts or intermediate sizes).
 */
EquiJoinPlan planEquiJoinMethod(
    Database *database, Table *left_table, Table *right_table,
    const std::string &left_alias, const std::string &right_alias,
    const ExpressionPtr &on_expr, size_t left_rows, size_t right_rows,
    const std::vector<SelectColumnBinding> &edge_bindings);

}  // namespace db
