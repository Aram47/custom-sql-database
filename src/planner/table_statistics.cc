#include "planner/table_statistics.h"

#include <unordered_set>

#include "core/table.h"
#include "executor/row_key.h"

namespace db {
namespace {

struct ValueHash {
  std::size_t operator()(const Value &value) const { return hashValue(value); }
};

bool isNumericValue(const Value &value) {
  return value.is_int() || value.is_float();
}

double toNumericDouble(const Value &value) {
  if (value.is_int()) {
    return static_cast<double>(value.as_int());
  }
  return value.as_float();
}

void buildEqualWidthBuckets(ColumnStatistics &stats, size_t bucket_count) {
  stats.equal_width_buckets.clear();
  if (!stats.min_value || !stats.max_value || bucket_count == 0) {
    return;
  }
  if (!isNumericValue(*stats.min_value) || !isNumericValue(*stats.max_value)) {
    return;
  }
  const double min_v = toNumericDouble(*stats.min_value);
  const double max_v = toNumericDouble(*stats.max_value);
  if (!(max_v > min_v)) {
    return;
  }
  stats.equal_width_buckets.assign(bucket_count, 0);
}

size_t bucketIndexForValue(const Value &value, const Value &min_value,
                           const Value &max_value, size_t bucket_count) {
  const double min_v = toNumericDouble(min_value);
  const double max_v = toNumericDouble(max_value);
  const double width = (max_v - min_v) / static_cast<double>(bucket_count);
  if (width <= 0.0) {
    return 0;
  }
  const double v = toNumericDouble(value);
  size_t index =
      static_cast<size_t>((v - min_v) / width);
  if (index >= bucket_count) {
    index = bucket_count - 1;
  }
  return index;
}

}  // namespace

void TableStatistics::refreshTable(const Table &table) {
  TableStatsEntry entry;
  entry.row_count = table.get_row_count();
  const std::vector<Column> &columns = table.get_columns();
  std::vector<std::unordered_set<Value, ValueHash>> unique_values(
      columns.size());
  std::vector<ColumnStatistics> column_stats(columns.size());
  for (size_t row_index = 0; row_index < entry.row_count; ++row_index) {
    const Row row = table.get_row(row_index);
    for (size_t col = 0; col < columns.size(); ++col) {
      const Value &value = row.get_value(col);
      if (value.is_null()) {
        column_stats[col].null_count += 1;
        continue;
      }
      unique_values[col].insert(value);
      if (!column_stats[col].min_value || value < *column_stats[col].min_value) {
        column_stats[col].min_value = value;
      }
      if (!column_stats[col].max_value || value > *column_stats[col].max_value) {
        column_stats[col].max_value = value;
      }
    }
  }
  for (size_t col = 0; col < columns.size(); ++col) {
    ColumnStatistics &stats = column_stats[col];
    stats.ndv = unique_values[col].size();
    buildEqualWidthBuckets(stats, DEFAULT_HISTOGRAM_BUCKETS);
    if (!stats.equal_width_buckets.empty()) {
      for (size_t row_index = 0; row_index < entry.row_count; ++row_index) {
        const Value &value = table.get_row(row_index).get_value(col);
        if (value.is_null() || !isNumericValue(value)) {
          continue;
        }
        const size_t bucket = bucketIndexForValue(
            value, *stats.min_value, *stats.max_value,
            stats.equal_width_buckets.size());
        stats.equal_width_buckets[bucket] += 1;
      }
    }
    entry.columns[columns[col].get_name()] = std::move(stats);
  }
  tables_[table.get_name()] = std::move(entry);
}

bool TableStatistics::hasTable(const std::string &table_name) const {
  return tables_.find(table_name) != tables_.end();
}

size_t TableStatistics::getRowCount(const std::string &table_name) const {
  auto it = tables_.find(table_name);
  if (it == tables_.end()) {
    return 0;
  }
  return it->second.row_count;
}

std::optional<ColumnStatistics> TableStatistics::getColumnStatistics(
    const std::string &table_name, const std::string &column_name) const {
  auto table_it = tables_.find(table_name);
  if (table_it == tables_.end()) {
    return std::nullopt;
  }
  auto col_it = table_it->second.columns.find(column_name);
  if (col_it == table_it->second.columns.end()) {
    return std::nullopt;
  }
  return col_it->second;
}

void TableStatistics::clear() { tables_.clear(); }

}  // namespace db
