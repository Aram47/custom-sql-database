#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace db {

/** Network address of one shard worker. */
struct ShardEndpoint {
  int shardId{0};
  std::string host;
  uint16_t port{0};
};

/**
 * Static shard topology: workers plus child-partition placements.
 * Loaded from shard_map.conf (3-field worker lines, 2-field placement lines).
 */
class ShardMap {
 public:
  /**
   * Parses conf file. Requires at least one worker and one placement.
   * @return nullopt and writes reason to error on failure.
   */
  static std::optional<ShardMap> loadFromFile(const std::string &path,
                                              std::string *error);

  /** Builds a map from in-memory workers and placements (tests). */
  static std::optional<ShardMap> build(
      std::vector<ShardEndpoint> workers,
      std::map<std::string, int> placements, std::string *error);

  /** All configured workers in ascending shard id order. */
  std::vector<ShardEndpoint> listWorkers() const;

  /** Looks up worker by shard id. */
  std::optional<ShardEndpoint> findEndpoint(int shardId) const;

  /** Looks up shard id for a child partition table name. */
  std::optional<int> findShardId(const std::string &childTableName) const;

  /** True when childTableName has an explicit placement. */
  bool hasPlacement(const std::string &childTableName) const;

  /**
   * Maps child table names to unique endpoints (stable shard-id order).
   * Missing placement yields nullopt and an error message.
   */
  std::optional<std::vector<ShardEndpoint>> resolveChildren(
      const std::vector<std::string> &childNames, std::string *error) const;

  /** First worker in the map (fallback for non-partitioned tables). */
  std::optional<ShardEndpoint> firstWorker() const;

 private:
  std::map<int, ShardEndpoint> workers_;
  std::map<std::string, int> placements_;

  static bool validate(const ShardMap &map, std::string *error);
};

}  // namespace db
