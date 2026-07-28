#include "planner/access_path_chooser.h"
#include "planner/cost_model.h"
#include "planner/join_method_chooser.h"
#include "planner/join_order_planner.h"
#include "planner/table_statistics.h"

#include "core/column.h"
#include "core/row.h"
#include "core/table.h"
#include "types/data_type.h"
#include "types/value.h"

#include "gtest/gtest.h"

namespace db {
namespace {

TEST(PlannerTest, PrefersIndexWhenSelective) {
  AccessPathChoice choice = AccessPathChooser::choose(10000, true, 0.001);
  EXPECT_EQ(choice.kind, AccessPathKind::IndexScan);
}

TEST(PlannerTest, PrefersSeqScanWithoutIndex) {
  AccessPathChoice choice = AccessPathChooser::choose(100, false, 0.1);
  EXPECT_EQ(choice.kind, AccessPathKind::SeqScan);
}

TEST(PlannerTest, JoinOrderStartsWithSmallerRelation) {
  std::vector<JoinRelation> relations = {
      {"big", 1000, false},
      {"small", 10, true},
      {"mid", 100, false},
  };
  std::vector<size_t> order = JoinOrderPlanner::planLeftDeepOrder(relations);
  ASSERT_EQ(order.size(), 3u);
  EXPECT_EQ(order[0], 1u);
}

TEST(PlannerTest, HashJoinCostCheaperThanPlainNestedLoop) {
  const double hash_cost = CostModel::estimateHashJoinCost(40, 40);
  const double nested_cost =
      CostModel::estimateNestedLoopCost(40, 40, false);
  EXPECT_LT(hash_cost, nested_cost);
}

TEST(PlannerTest, JoinMethodChooserPrefersHashWithoutIndex) {
  JoinMethodInput input;
  input.left_rows = 50;
  input.right_rows = 50;
  input.left_ndv = 50;
  input.right_ndv = 50;
  input.has_inner_index = false;
  input.is_equi_inner = true;
  JoinMethodChoice choice = JoinMethodChooser::choose(input);
  EXPECT_EQ(choice.kind, JoinMethodKind::HashJoin);
}

TEST(PlannerTest, JoinMethodChooserPrefersIndexNestedLoop) {
  JoinMethodInput input;
  input.left_rows = 50;
  input.right_rows = 50;
  input.left_ndv = 50;
  input.right_ndv = 50;
  input.has_inner_index = true;
  input.is_equi_inner = true;
  JoinMethodChoice choice = JoinMethodChooser::choose(input);
  EXPECT_EQ(choice.kind, JoinMethodKind::IndexNestedLoop);
}

TEST(PlannerTest, JoinMethodChooserKeepsNestedLoopForSmallTables) {
  JoinMethodInput input;
  input.left_rows = 5;
  input.right_rows = 5;
  input.left_ndv = 5;
  input.right_ndv = 5;
  input.has_inner_index = false;
  input.is_equi_inner = true;
  JoinMethodChoice choice = JoinMethodChooser::choose(input);
  EXPECT_EQ(choice.kind, JoinMethodKind::NestedLoop);
}

TEST(PlannerTest, TableStatisticsComputesNdvAndHistogram) {
  Table table("stats_t");
  table.add_column(Column("id", DataType::INT));
  table.add_column(Column("v", DataType::INT));
  for (int i = 0; i < 8; ++i) {
    Row row;
    row.add_value(Value(i));
    row.add_value(Value(i % 4));
    table.insert_row(row);
  }
  TableStatistics stats;
  stats.refreshTable(table);
  EXPECT_TRUE(stats.hasTable("stats_t"));
  EXPECT_EQ(stats.getRowCount("stats_t"), 8u);
  auto id_stats = stats.getColumnStatistics("stats_t", "id");
  ASSERT_TRUE(id_stats.has_value());
  EXPECT_EQ(id_stats->ndv, 8u);
  EXPECT_FALSE(id_stats->equal_width_buckets.empty());
  auto v_stats = stats.getColumnStatistics("stats_t", "v");
  ASSERT_TRUE(v_stats.has_value());
  EXPECT_EQ(v_stats->ndv, 4u);
}

}  // namespace
}  // namespace db
