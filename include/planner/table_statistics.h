#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

namespace db {

/** Lightweight per-table cardinality stats for cost-based planning. */
class TableStatistics {
 public:
  void setRowCount(const std::string &table_name, size_t row_count);
  size_t getRowCount(const std::string &table_name) const;
  void clear();

 private:
  std::unordered_map<std::string, size_t> row_counts_;
};

}  // namespace db
