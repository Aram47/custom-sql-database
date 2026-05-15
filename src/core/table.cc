#include "core/table.h"

namespace db {

Table::Table(const std::string &name) : table_name_(name) {}

const std::string &Table::get_name() const { return table_name_; }

size_t Table::get_column_count() const { return columns_.size(); }

size_t Table::get_row_count() const { return rows_.size(); }

void Table::add_column(const Column &column) {
  // Check for duplicate column names
  for (const auto &col : columns_) {
    if (col.get_name() == column.get_name()) {
      throw ConstraintException("Column '" + column.get_name() +
                                "' already exists");
    }
  }

  columns_.push_back(column);
}

const Column &Table::get_column(size_t index) const {
  if (index >= columns_.size()) {
    throw NotFoundException("Column index " + std::to_string(index) +
                            " not found");
  }
  return columns_[index];
}

const Column &Table::get_column(const std::string &name) const {
  for (const auto &col : columns_) {
    if (col.get_name() == name) {
      return col;
    }
  }
  throw NotFoundException("Column '" + name + "' not found");
}

int Table::get_column_index(const std::string &name) const {
  for (size_t i = 0; i < columns_.size(); ++i) {
    if (columns_[i].get_name() == name) {
      return i;
    }
  }
  return -1;
}

const std::vector<Column> &Table::get_columns() const { return columns_; }

void Table::insert_row(const Row &row) {
  validate_schema(row);

  if (!validate_row(row)) {
    throw ConstraintException("Row does not satisfy table constraints");
  }

  rows_.push_back(row);

  // Update indices
  for (size_t i = 0; i < columns_.size(); ++i) {
    if (column_indices_.count(columns_[i].get_name())) {
      column_indices_[columns_[i].get_name()]->insert(row.get_value(i),
                                                      rows_.size() - 1);
    }
  }
}

std::vector<Row> Table::get_all_rows() const { return rows_; }

Row Table::get_row(size_t index) const {
  if (index >= rows_.size()) {
    throw std::out_of_range("Row index " + std::to_string(index) +
                            " out of range");
  }
  return rows_[index];
}

void Table::update_row(size_t index, const Row &row) {
  if (index >= rows_.size()) {
    throw std::out_of_range("Row index out of range");
  }

  validate_schema(row);

  if (!validate_row(row, index)) {
    throw ConstraintException("Updated row does not satisfy table constraints");
  }

  // Update indices
  for (size_t i = 0; i < columns_.size(); ++i) {
    if (column_indices_.count(columns_[i].get_name())) {
      column_indices_[columns_[i].get_name()]->remove(rows_[index].get_value(i),
                                                      index);
      column_indices_[columns_[i].get_name()]->insert(row.get_value(i), index);
    }
  }

  rows_[index] = row;
}

void Table::delete_row(size_t index) {
  if (index >= rows_.size()) {
    throw std::out_of_range("Row index out of range");
  }

  // Update indices
  for (size_t i = 0; i < columns_.size(); ++i) {
    if (column_indices_.count(columns_[i].get_name())) {
      column_indices_[columns_[i].get_name()]->remove(rows_[index].get_value(i),
                                                      index);
    }
  }

  rows_.erase(rows_.begin() + index);

  // Rebuild indices after deletion
  for (auto &[col_name, idx] : column_indices_) {
    idx->clear();
    for (size_t i = 0; i < rows_.size(); ++i) {
      int colIdx = get_column_index(col_name);
      if (colIdx >= 0) {
        idx->insert(rows_[i].get_value(colIdx), i);
      }
    }
  }
}

void Table::delete_all() {
  rows_.clear();
  for (auto &[col_name, idx] : column_indices_) {
    idx->clear();
  }
}

std::vector<size_t> Table::find_rows_by_value(const std::string &column_name,
                                              const Value &value) const {
  int colIdx = get_column_index(column_name);
  if (colIdx < 0) {
    throw NotFoundException("Column '" + column_name + "' not found");
  }

  std::vector<size_t> result;
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (rows_[i].get_value(colIdx) == value) {
      result.push_back(i);
    }
  }
  return result;
}

std::vector<size_t> Table::find_rows_by_predicate(
    std::function<bool(const Row &)> predicate) const {
  std::vector<size_t> result;
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (predicate(rows_[i])) {
      result.push_back(i);
    }
  }
  return result;
}

bool Table::validate_row(const Row &row, size_t exclude_row_index) const {
  if (row.get_column_count() != columns_.size()) {
    return false;
  }

  // Check nullability constraints
  for (size_t i = 0; i < columns_.size(); ++i) {
    if (!columns_[i].is_nullable() && row.get_value(i).is_null()) {
      return false;
    }
  }

  // Check uniqueness constraints
  if (!validate_unique_constraint(row, exclude_row_index)) {
    return false;
  }

  // Check primary key constraint
  if (!validate_primary_key_uniqueness(row, exclude_row_index)) {
    return false;
  }

  return true;
}

bool Table::validate_primary_key_uniqueness(const Row &row,
                                            size_t exclude_row_index) const {
  int pkIdx = get_primary_key_index();
  if (pkIdx < 0) return true;

  const Value &pkValue = row.get_value(pkIdx);
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (i == exclude_row_index) continue;
    if (rows_[i].get_value(pkIdx) == pkValue && !pkValue.is_null()) {
      return false;
    }
  }
  return true;
}

bool Table::validate_unique_constraint(const Row &row,
                                       size_t exclude_row_index) const {
  for (size_t i = 0; i < columns_.size(); ++i) {
    if (columns_[i].is_unique()) {
      const Value &value = row.get_value(i);
      if (value.is_null()) continue;

      for (size_t j = 0; j < rows_.size(); ++j) {
        if (j == exclude_row_index) continue;
        if (rows_[j].get_value(i) == value) {
          return false;
        }
      }
    }
  }
  return true;
}

std::vector<Row> &Table::get_mutable_rows() { return rows_; }

std::string Table::to_string() const {
  std::string str = "Table: " + table_name_ + "\n";
  str += "Columns:\n";
  for (const auto &col : columns_) {
    str += "  " + col.to_string() + "\n";
  }
  str += "Rows: " + std::to_string(rows_.size()) + "\n";
  return str;
}

void Table::build_index(const std::string &column_name) {
  int colIdx = get_column_index(column_name);
  if (colIdx < 0) {
    throw NotFoundException("Column '" + column_name + "' not found");
  }

  auto idx = std::make_unique<ColumnIndex>(colIdx);
  for (size_t i = 0; i < rows_.size(); ++i) {
    idx->insert(rows_[i].get_value(colIdx), i);
  }
  column_indices_[column_name] = std::move(idx);
}

void Table::validate_schema(const Row &row) const {
  if (row.get_column_count() != columns_.size()) {
    throw InvalidOperationException(
        "Row column count does not match table schema");
  }
}

int Table::get_primary_key_index() const {
  for (size_t i = 0; i < columns_.size(); ++i) {
    if (columns_[i].is_primary_key()) {
      return i;
    }
  }
  return -1;
}

}  // namespace db
