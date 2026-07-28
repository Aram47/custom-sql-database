#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "types/value.h"

namespace db {

class Table;

/** Per-column cardinality and optional equal-width histogram. */
struct ColumnStatistics {
  size_t ndv{0};
  size_t null_count{0};
  std::optional<Value> min_value;
  std::optional<Value> max_value;
  std::vector<size_t> equal_width_buckets;
};

/**
 * Lightweight per-table statistics for cost-based planning.
 * Call refreshTable after VACUUM or lazily before join method choice.
 */
class TableStatistics {
 public:
  static constexpr size_t DEFAULT_HISTOGRAM_BUCKETS = 16;

  /**
   * Recomputes row count, NDV, null counts, min/max, and equal-width
   * histograms for every column of the table.
   */
  void refreshTable(const Table &table);

  bool hasTable(const std::string &table_name) const;
  size_t getRowCount(const std::string &table_name) const;
  std::optional<ColumnStatistics> getColumnStatistics(
      const std::string &table_name, const std::string &column_name) const;
  void clear();

 private:
  struct TableStatsEntry {
    size_t row_count{0};
    std::unordered_map<std::string, ColumnStatistics> columns;
  };

  std::unordered_map<std::string, TableStatsEntry> tables_;
};

}  // namespace db
