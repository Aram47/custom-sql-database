#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/partition.h"
#include "core/table.h"

namespace db {

/**
 * Persists partition metadata under `_partitions/{parent}.part`.
 * Parent and child heaps remain ordinary `.db` files.
 */
class PartitionCatalog {
 public:
  static constexpr const char *PARTITIONS_SUBDIR = "_partitions";
  static constexpr const char *PARTITION_EXTENSION = ".part";

  /** Writes metadata for a partitioned parent table. */
  static void saveParent(const std::string &storageDirectory,
                         const std::string &parentName,
                         const PartitionedTableMetadata &metadata);

  /** Removes the sidecar file for parentName if present. */
  static void removeParentFile(const std::string &storageDirectory,
                               const std::string &parentName);

  /**
   * Loads all sidecars and attaches metadata onto matching parent tables.
   * Skips entries whose parent table is missing.
   */
  static void loadAll(
      const std::string &storageDirectory,
      std::map<std::string, std::unique_ptr<Table>> &tables);

 private:
  static std::string buildPath(const std::string &storageDirectory,
                               const std::string &parentName);
  static std::string serializeValue(const Value &value);
  static Value deserializeValue(const std::string &text);
};

}  // namespace db
