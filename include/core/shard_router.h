#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/partition.h"
#include "core/shard_map.h"
#include "types/value.h"

namespace db {

/**
 * Maps partition routing results to shard endpoints.
 * Composes IPartitionRouter with ShardMap placements.
 */
class ShardRouter {
 public:
  explicit ShardRouter(ShardMap shardMap);

  const ShardMap &getShardMap() const;

  /**
   * Resolves a single partition key to one worker endpoint.
   * @return nullopt when no child matches or placement is missing.
   */
  std::optional<ShardEndpoint> resolveKey(const IPartitionRouter &router,
                                          const Value &key,
                                          std::string *error) const;

  /**
   * Resolves prune results to unique worker endpoints.
   * Empty prune (scan all) uses all children known to the router via request.
   */
  std::optional<std::vector<ShardEndpoint>> resolvePrune(
      const IPartitionRouter &router, const PartitionPruneRequest &request,
      std::string *error) const;

  /** Resolves a child table name via the placement map. */
  std::optional<ShardEndpoint> resolveChildName(const std::string &childName,
                                                std::string *error) const;

 private:
  ShardMap shard_map_;
};

}  // namespace db
