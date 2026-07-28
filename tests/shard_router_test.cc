#include "core/shard_map.h"
#include "core/shard_router.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "core/partition.h"
#include "gtest/gtest.h"
#include "tests/test_util.hh"
#include "types/value.h"

namespace db {
namespace {

TEST(ShardRouterTest, LoadConfAndResolveKeyToShard) {
  test_util::TempDbDir dir;
  const std::filesystem::path conf = dir.path() / "shard_map.conf";
  {
    std::ofstream out(conf);
    out << "0 127.0.0.1 9001\n";
    out << "1 127.0.0.1 9002\n";
    out << "sales_2024 0\n";
    out << "sales_2025 1\n";
  }
  std::string error;
  auto map = ShardMap::loadFromFile(conf.string(), &error);
  ASSERT_TRUE(map.has_value()) << error;
  ShardRouter router(std::move(*map));
  PartitionedTableMetadata meta(PartitionKind::Range, "y");
  ASSERT_TRUE(meta.addPartition(
      {"sales_2024",
       PartitionBound{RangePartitionBound{Value(2024), Value(2025)},
                     std::nullopt}},
      &error))
      << error;
  ASSERT_TRUE(meta.addPartition(
      {"sales_2025",
       PartitionBound{RangePartitionBound{Value(2025), Value(2026)},
                     std::nullopt}},
      &error))
      << error;
  auto partitionRouter = meta.createRouter();
  auto endpoint = router.resolveKey(*partitionRouter, Value(2024), &error);
  ASSERT_TRUE(endpoint.has_value()) << error;
  EXPECT_EQ(endpoint->shardId, 0);
  endpoint = router.resolveKey(*partitionRouter, Value(2025), &error);
  ASSERT_TRUE(endpoint.has_value()) << error;
  EXPECT_EQ(endpoint->shardId, 1);
}

TEST(ShardRouterTest, ResolvePruneToMultipleEndpoints) {
  std::string error;
  auto map = ShardMap::build(
      {{0, "127.0.0.1", 9001}, {1, "127.0.0.1", 9002}},
      {{"sales_2024", 0}, {"sales_2025", 1}}, &error);
  ASSERT_TRUE(map.has_value()) << error;
  ShardRouter router(std::move(*map));
  PartitionedTableMetadata meta(PartitionKind::Range, "y");
  ASSERT_TRUE(meta.addPartition(
      {"sales_2024",
       PartitionBound{RangePartitionBound{Value(2024), Value(2025)},
                     std::nullopt}},
      &error));
  ASSERT_TRUE(meta.addPartition(
      {"sales_2025",
       PartitionBound{RangePartitionBound{Value(2025), Value(2026)},
                     std::nullopt}},
      &error));
  auto partitionRouter = meta.createRouter();
  PartitionPruneRequest emptyRequest;
  auto endpoints =
      router.resolvePrune(*partitionRouter, emptyRequest, &error);
  ASSERT_TRUE(endpoints.has_value()) << error;
  ASSERT_EQ(endpoints->size(), 2u);
  EXPECT_EQ((*endpoints)[0].shardId, 0);
  EXPECT_EQ((*endpoints)[1].shardId, 1);
}

TEST(ShardRouterTest, RejectsMissingPlacement) {
  std::string error;
  auto map = ShardMap::build({{0, "127.0.0.1", 9001}},
                             {{"only_child", 0}}, &error);
  ASSERT_TRUE(map.has_value()) << error;
  EXPECT_FALSE(map->hasPlacement("missing"));
}

}  // namespace
}  // namespace db
