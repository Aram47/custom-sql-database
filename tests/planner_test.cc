#include "planner/access_path_chooser.h"
#include "planner/join_order_planner.h"

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

}  // namespace
}  // namespace db
