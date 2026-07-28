#include "planner/join_method_plan.h"

#include "core/database.h"
#include "core/table.h"
#include "executor/index_predicate.h"
#include "planner/table_statistics.h"

#include <optional>

namespace db {
namespace {

int findBindingIndex(const std::vector<SelectColumnBinding> &bindings,
                     const std::string &column, const std::string &table_or_alias,
                     const std::string &preferred_alias) {
  for (size_t bi = 0; bi < bindings.size(); ++bi) {
    if (bindings[bi].column_name != column) {
      continue;
    }
    if (table_or_alias.empty() || bindings[bi].alias == table_or_alias ||
        bindings[bi].physical_table == table_or_alias ||
        bindings[bi].alias == preferred_alias) {
      return static_cast<int>(bi);
    }
  }
  for (size_t bi = 0; bi < bindings.size(); ++bi) {
    if (bindings[bi].column_name == column) {
      return static_cast<int>(bi);
    }
  }
  return -1;
}

size_t lookupNdv(Database *database, const std::string &table_name,
                 const std::string &column_name, size_t fallback_rows) {
  if (!database) {
    return fallback_rows == 0 ? 1 : fallback_rows;
  }
  database->ensureTableStatistics(table_name);
  const std::optional<ColumnStatistics> stats =
      database->get_table_statistics().getColumnStatistics(table_name,
                                                           column_name);
  if (!stats || stats->ndv == 0) {
    return fallback_rows == 0 ? 1 : fallback_rows;
  }
  return stats->ndv;
}

}  // namespace

EquiJoinPlan planEquiJoinMethod(
    Database *database, Table *left_table, Table *right_table,
    const std::string &left_alias, const std::string &right_alias,
    const ExpressionPtr &on_expr, size_t left_rows, size_t right_rows,
    const std::vector<SelectColumnBinding> &edge_bindings) {
  EquiJoinPlan plan;
  plan.method.kind = JoinMethodKind::NestedLoop;
  plan.method.cost =
      CostModel::estimateNestedLoopCost(left_rows, right_rows, false);
  plan.method.build_on_left = left_rows <= right_rows;
  if (!left_table || !right_table || !on_expr) {
    return plan;
  }
  std::string left_table_name;
  std::string left_column;
  std::string right_table_name;
  std::string right_column;
  if (!try_extract_equi_join_columns(on_expr, left_table_name, left_column,
                                     right_table_name, right_column)) {
    return plan;
  }
  plan.is_equi = true;
  const bool right_is_rhs =
      (right_table_name.empty() ||
       right_table_name == right_table->get_name() ||
       right_table_name == right_alias) &&
      right_table->get_column_index(right_column) >= 0;
  const bool left_is_rhs =
      (left_table_name.empty() || left_table_name == right_table->get_name() ||
       left_table_name == right_alias) &&
      right_table->get_column_index(left_column) >= 0;
  if (right_is_rhs) {
    plan.right_key_index = right_table->get_column_index(right_column);
    plan.left_key_index =
        findBindingIndex(edge_bindings, left_column, left_table_name,
                         left_alias);
    plan.left_key_column = left_column;
    plan.right_key_column = right_column;
  } else if (left_is_rhs) {
    plan.right_key_index = right_table->get_column_index(left_column);
    plan.left_key_index =
        findBindingIndex(edge_bindings, right_column, right_table_name,
                         left_alias);
    plan.left_key_column = right_column;
    plan.right_key_column = left_column;
  } else {
    plan.is_equi = false;
    return plan;
  }
  if (plan.left_key_index < 0 || plan.right_key_index < 0) {
    plan.is_equi = false;
    return plan;
  }
  const bool has_inner_index =
      right_table->has_index(plan.right_key_column);
  const size_t left_ndv =
      lookupNdv(database, left_table->get_name(), plan.left_key_column,
                left_rows);
  const size_t right_ndv =
      lookupNdv(database, right_table->get_name(), plan.right_key_column,
                right_rows);
  JoinMethodInput input;
  input.left_rows = left_rows;
  input.right_rows = right_rows;
  input.left_ndv = left_ndv;
  input.right_ndv = right_ndv;
  input.has_inner_index = has_inner_index;
  input.is_equi_inner = true;
  plan.method = JoinMethodChooser::choose(input);
  return plan;
}

}  // namespace db
