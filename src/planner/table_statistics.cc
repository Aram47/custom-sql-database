#include "planner/table_statistics.h"

namespace db {

void TableStatistics::setRowCount(const std::string &table_name,
                                  size_t row_count) {
  row_counts_[table_name] = row_count;
}

size_t TableStatistics::getRowCount(const std::string &table_name) const {
  auto it = row_counts_.find(table_name);
  if (it == row_counts_.end()) {
    return 0;
  }
  return it->second;
}

void TableStatistics::clear() { row_counts_.clear(); }

}  // namespace db
