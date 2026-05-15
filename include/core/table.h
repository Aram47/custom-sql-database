#pragma once

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/column.h"
#include "core/row.h"
#include "types/value.h"
#include "utils/exceptions.h"

namespace db {

// Forward declarations
class Table;

// Simple index for column lookups
class ColumnIndex {
 public:
  explicit ColumnIndex(size_t column_idx) : column_index_(column_idx) {}

  bool contains(const Value &value) const {
    return index_map_.find(value.to_string()) != index_map_.end();
  }

  std::vector<size_t> find(const Value &value) const {
    auto it = index_map_.find(value.to_string());
    if (it != index_map_.end()) {
      return it->second;
    }
    return {};
  }

  void insert(const Value &value, size_t row_idx) {
    index_map_[value.to_string()].push_back(row_idx);
  }

  void remove(const Value &value, size_t row_idx) {
    auto it = index_map_.find(value.to_string());
    if (it != index_map_.end()) {
      auto &vec = it->second;
      vec.erase(std::remove(vec.begin(), vec.end(), row_idx), vec.end());
      if (vec.empty()) {
        index_map_.erase(it);
      }
    }
  }

  void clear() { index_map_.clear(); }

 private:
  [[maybe_unused]] size_t column_index_;
  std::map<std::string, std::vector<size_t>> index_map_;
};

class Table {
 public:
  // Constructor and destructor
  Table(const std::string &name);
  ~Table() = default;

  // Prevent copying and moving
  Table(const Table &) = delete;
  Table &operator=(const Table &) = delete;
  Table(Table &&) = delete;
  Table &operator=(Table &&) = delete;

  // Table metadata
  const std::string &get_name() const;
  size_t get_column_count() const;
  size_t get_row_count() const;

  // Column management
  void add_column(const Column &column);
  const Column &get_column(size_t index) const;
  const Column &get_column(const std::string &name) const;
  int get_column_index(const std::string &name) const;
  const std::vector<Column> &get_columns() const;

  // Row CRUD operations
  void insert_row(const Row &row);
  std::vector<Row> get_all_rows() const;
  Row get_row(size_t index) const;
  void update_row(size_t index, const Row &row);
  void delete_row(size_t index);
  void delete_all();

  // Query operations
  std::vector<size_t> find_rows_by_value(const std::string &column_name,
                                         const Value &value) const;
  std::vector<size_t> find_rows_by_predicate(
      std::function<bool(const Row &)> predicate) const;

  // Constraint validation (exclude_row_index: row being updated; none for insert)
  bool validate_row(const Row &row,
                    size_t exclude_row_index = static_cast<size_t>(-1)) const;
  bool validate_primary_key_uniqueness(const Row &row,
                                       size_t exclude_row_index = -1) const;
  bool validate_unique_constraint(const Row &row,
                                  size_t exclude_row_index = -1) const;

  // Serialization helpers
  std::vector<Row> &get_mutable_rows();

  // String representation
  std::string to_string() const;

 private:
  std::string table_name_;
  std::vector<Column> columns_;
  std::vector<Row> rows_;
  std::map<std::string, std::unique_ptr<ColumnIndex>> column_indices_;

  // Helper methods
  void build_index(const std::string &column_name);
  void validate_schema(const Row &row) const;
  int get_primary_key_index() const;
};

}  // namespace db
