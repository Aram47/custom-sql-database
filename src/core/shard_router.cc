#include "core/shard_router.h"

namespace db {

ShardRouter::ShardRouter(ShardMap shardMap)
    : shard_map_(std::move(shardMap)) {}

const ShardMap &ShardRouter::getShardMap() const { return shard_map_; }

std::optional<ShardEndpoint> ShardRouter::resolveKey(
    const IPartitionRouter &router, const Value &key,
    std::string *error) const {
  const std::optional<std::string> child = router.resolveChild(key);
  if (!child) {
    if (error) {
      *error = "no partition for key " + key.to_string();
    }
    return std::nullopt;
  }
  return resolveChildName(*child, error);
}

std::optional<std::vector<ShardEndpoint>> ShardRouter::resolvePrune(
    const IPartitionRouter &router, const PartitionPruneRequest &request,
    std::string *error) const {
  const std::vector<std::string> children = router.prune(request);
  if (children.empty()) {
    if (error) {
      *error = "partition prune selected no children";
    }
    return std::nullopt;
  }
  return shard_map_.resolveChildren(children, error);
}

std::optional<ShardEndpoint> ShardRouter::resolveChildName(
    const std::string &childName, std::string *error) const {
  const std::optional<int> shardId = shard_map_.findShardId(childName);
  if (!shardId) {
    if (error) {
      *error = "no shard placement for partition " + childName;
    }
    return std::nullopt;
  }
  const std::optional<ShardEndpoint> endpoint =
      shard_map_.findEndpoint(*shardId);
  if (!endpoint) {
    if (error) {
      *error = "unknown shard id " + std::to_string(*shardId);
    }
    return std::nullopt;
  }
  return endpoint;
}

}  // namespace db
