#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "types/value.h"

namespace db {

/** Partition strategy kind for a declarative parent table. */
enum class PartitionKind { Range, Hash };

/** Inclusive-lower / exclusive-upper bounds for RANGE partitions. */
struct RangePartitionBound {
  Value minInclusive;
  Value maxExclusive;
};

/** HASH partition membership: keyHash % modulus == remainder. */
struct HashPartitionBound {
  int64_t modulus{0};
  int64_t remainder{0};
};

/** Bounds for one child partition (exactly one of range/hash is set). */
struct PartitionBound {
  std::optional<RangePartitionBound> range;
  std::optional<HashPartitionBound> hash;
};

/** Named child table plus its routing bounds. */
struct PartitionDescriptor {
  std::string childTableName;
  PartitionBound bound;
};

/** Single sargable constraint on the partition key for pruning. */
struct PartitionPruneConstraint {
  enum class Op {
    Equal,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Between
  };
  Op op{Op::Equal};
  Value lower;
  Value upper;
  bool lowerInclusive{true};
  bool upperInclusive{true};
};

/**
 * Prune request. Empty constraints means scan all partitions.
 * Multiple constraints are AND-ed.
 */
struct PartitionPruneRequest {
  std::vector<PartitionPruneConstraint> constraints;
};

/**
 * Routes keys to child tables and prunes partitions for scans.
 * Strategy implementations: RangePartitionRouter, HashPartitionRouter.
 */
class IPartitionRouter {
 public:
  virtual ~IPartitionRouter() = default;

  /** Returns child table name for key, or nullopt if no matching partition. */
  virtual std::optional<std::string> resolveChild(const Value &key) const = 0;

  /** Returns child names that may contain rows matching the request. */
  virtual std::vector<std::string> prune(
      const PartitionPruneRequest &request) const = 0;
};

/**
 * Catalog metadata for a partitioned parent table.
 * Composed into Table; children hold the actual heaps.
 */
class PartitionedTableMetadata {
 public:
  PartitionedTableMetadata(PartitionKind kind, std::string keyColumn);

  PartitionKind getKind() const;
  const std::string &getKeyColumn() const;
  const std::vector<PartitionDescriptor> &getPartitions() const;

  /**
   * Validates and appends a partition. Returns false on overlap / bad hash.
   * @param error Optional human-readable reason.
   */
  bool addPartition(PartitionDescriptor descriptor, std::string *error);

  /** Removes a child by name; returns false if unknown. */
  bool removePartition(const std::string &childTableName);

  /** True when childTableName is registered under this parent. */
  bool hasChild(const std::string &childTableName) const;

  /** Builds a router matching the current partition list. */
  std::unique_ptr<IPartitionRouter> createRouter() const;

  /** Stable hash used by HASH partitioning (restart-safe). */
  static uint64_t hashValue(const Value &value);

 private:
  PartitionKind kind_;
  std::string key_column_;
  std::vector<PartitionDescriptor> partitions_;

  bool validateNewBound(const PartitionBound &bound, std::string *error) const;
};

/** Deterministic FNV-1a style mix for partition hashing. */
uint64_t computePartitionHash(const Value &value);

}  // namespace db
