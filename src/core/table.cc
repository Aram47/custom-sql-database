#include "core/table.h"

#include "executor/select_column_binding.h"
#include "executor/select_expression_evaluator.h"

namespace db {
namespace {

SelectExpressionEvaluator make_table_check_evaluator(const Table &table) {
  std::vector<SelectColumnBinding> bindings;
  const std::string &table_name = table.get_name();
  for (const Column &col : table.get_columns()) {
    bindings.push_back({table_name, table_name, col.get_name()});
  }
  return SelectExpressionEvaluator(std::move(bindings));
}

std::atomic<FileId> g_next_file_id{1};

}  // namespace

FileId Table::allocate_file_id() { return g_next_file_id.fetch_add(1); }

void Table::initialize_heap() {
  heap_ = std::make_unique<HeapFile>(file_id_, *buffer_pool_, *owned_store_,
                                     collect_column_types());
}

Table::Table(const std::string &name)
    : table_name_(name),
      owned_store_(std::make_unique<MemoryPageStore>()),
      owned_pool_(std::make_unique<BufferPool>(8)),
      buffer_pool_(owned_pool_.get()),
      file_id_(allocate_file_id()) {
  initialize_heap();
}

Table::Table(const std::string &name, FileId file_id, IBufferPool &buffer_pool,
             std::unique_ptr<IPageStore> page_store)
    : table_name_(name),
      owned_store_(std::move(page_store)),
      buffer_pool_(&buffer_pool),
      file_id_(file_id) {
  if (!owned_store_) {
    throw StorageException("Table requires a page store");
  }
  initialize_heap();
}

std::vector<DataType> Table::collect_column_types() const {
  std::vector<DataType> types;
  types.reserve(columns_.size());
  for (const Column &col : columns_) {
    types.push_back(col.get_type());
  }
  return types;
}

void Table::sync_heap_column_types() {
  heap_->set_column_types(collect_column_types());
}

std::unique_ptr<Table> Table::clone() const {
  auto copy = std::make_unique<Table>(table_name_);
  copy->columns_ = columns_;
  copy->sync_heap_column_types();
  for (const ItemPointer &pointer : row_directory_) {
    const Row row = heap_->get_row(pointer);
    copy->row_directory_.push_back(copy->heap_->insert_row(row));
  }
  copy->secondary_indexes_ = secondary_indexes_;
  copy->foreign_keys_ = foreign_keys_;
  copy->checks_ = checks_;
  if (partition_meta_) {
    auto metaCopy = std::make_unique<PartitionedTableMetadata>(
        partition_meta_->getKind(), partition_meta_->getKeyColumn());
    for (const PartitionDescriptor &part : partition_meta_->getPartitions()) {
      std::string error;
      metaCopy->addPartition(part, &error);
    }
    copy->partition_meta_ = std::move(metaCopy);
  }
  copy->rebuild_indexes();
  copy->dirty_ = dirty_;
  return copy;
}

bool Table::isPartitioned() const { return partition_meta_ != nullptr; }

const PartitionedTableMetadata *Table::getPartitionMetadata() const {
  return partition_meta_.get();
}

PartitionedTableMetadata *Table::getMutablePartitionMetadata() {
  return partition_meta_.get();
}

void Table::setPartitionMetadata(
    std::unique_ptr<PartitionedTableMetadata> metadata) {
  partition_meta_ = std::move(metadata);
}

void Table::clearPartitionMetadata() { partition_meta_.reset(); }

const std::string &Table::get_name() const { return table_name_; }

void Table::set_name(const std::string &name) { table_name_ = name; }

size_t Table::get_column_count() const { return columns_.size(); }

size_t Table::get_row_count() const { return row_directory_.size(); }

void Table::add_column(const Column &column) {
  for (const auto &col : columns_) {
    if (col.get_name() == column.get_name()) {
      throw ConstraintException("Column '" + column.get_name() +
                                "' already exists");
    }
  }
  if (!column.is_nullable() && !row_directory_.empty()) {
    throw ConstraintException(
        "Cannot add NOT NULL column '" + column.get_name() +
        "' to a non-empty table without a default value");
  }
  std::vector<Row> existing = get_all_rows();
  columns_.push_back(column);
  heap_->clear();
  row_directory_.clear();
  sync_heap_column_types();
  for (Row &row : existing) {
    row.add_value(Value());
    row_directory_.push_back(heap_->insert_row(row));
  }
  if (column.is_primary_key() || column.is_unique()) {
    build_index(column.get_name());
  }
  mark_dirty();
}

void Table::drop_column(const std::string &column_name) {
  const int col_idx = get_column_index(column_name);
  if (col_idx < 0) {
    throw NotFoundException("Column '" + column_name + "' not found");
  }
  if (columns_.size() <= 1) {
    throw InvalidOperationException("Cannot drop the last column of a table");
  }
  column_indices_.erase(column_name);
  std::vector<std::string> indexes_to_drop;
  for (const auto &[index_name, cols] : secondary_indexes_) {
    for (const std::string &col : cols) {
      if (col == column_name) {
        indexes_to_drop.push_back(index_name);
        break;
      }
    }
  }
  for (const std::string &index_name : indexes_to_drop) {
    secondary_indexes_.erase(index_name);
    named_indices_.erase(index_name);
  }
  std::vector<Row> existing = get_all_rows();
  columns_.erase(columns_.begin() + col_idx);
  heap_->clear();
  row_directory_.clear();
  sync_heap_column_types();
  for (Row &row : existing) {
    row.remove_value(static_cast<size_t>(col_idx));
    row_directory_.push_back(heap_->insert_row(row));
  }
  rebuild_indexes();
  mark_dirty();
}

void Table::rename_column(const std::string &old_name,
                          const std::string &new_name) {
  if (old_name == new_name) {
    return;
  }
  const int col_idx = get_column_index(old_name);
  if (col_idx < 0) {
    throw NotFoundException("Column '" + old_name + "' not found");
  }
  if (get_column_index(new_name) >= 0) {
    throw ConstraintException("Column '" + new_name + "' already exists");
  }
  columns_[static_cast<size_t>(col_idx)].set_name(new_name);
  auto it = column_indices_.find(old_name);
  if (it != column_indices_.end()) {
    auto index = std::move(it->second);
    column_indices_.erase(it);
    column_indices_[new_name] = std::move(index);
  }
  for (auto &[index_name, cols] : secondary_indexes_) {
    (void)index_name;
    for (std::string &col : cols) {
      if (col == old_name) {
        col = new_name;
      }
    }
  }
  mark_dirty();
}

const Column &Table::get_column(size_t index) const {
  if (index >= columns_.size()) {
    throw NotFoundException("Column index " + std::to_string(index) +
                            " not found");
  }
  return columns_[index];
}

Column &Table::get_mutable_column(size_t index) {
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
      return static_cast<int>(i);
    }
  }
  return -1;
}

const std::vector<Column> &Table::get_columns() const { return columns_; }

IndexKey Table::build_index_key(
    const Row &row, const std::vector<std::string> &column_names) const {
  std::vector<Value> components;
  components.reserve(column_names.size());
  for (const std::string &column_name : column_names) {
    const int col_idx = get_column_index(column_name);
    if (col_idx < 0) {
      throw NotFoundException("Column '" + column_name + "' not found");
    }
    components.push_back(row.get_value(static_cast<size_t>(col_idx)));
  }
  return IndexKey(std::move(components));
}

void Table::insert_into_indexes(const Row &row, size_t row_index) {
  for (size_t i = 0; i < columns_.size(); ++i) {
    auto it = column_indices_.find(columns_[i].get_name());
    if (it != column_indices_.end()) {
      it->second->insert(row.get_value(i), row_index);
    }
  }
  for (const auto &[index_name, column_names] : secondary_indexes_) {
    auto it = named_indices_.find(index_name);
    if (it == named_indices_.end()) {
      continue;
    }
    it->second->insert(build_index_key(row, column_names), row_index);
  }
}

void Table::remove_from_indexes(const Row &row, size_t row_index) {
  for (size_t i = 0; i < columns_.size(); ++i) {
    auto it = column_indices_.find(columns_[i].get_name());
    if (it != column_indices_.end()) {
      it->second->remove(row.get_value(i), row_index);
    }
  }
  for (const auto &[index_name, column_names] : secondary_indexes_) {
    auto it = named_indices_.find(index_name);
    if (it == named_indices_.end()) {
      continue;
    }
    it->second->remove(build_index_key(row, column_names), row_index);
  }
}

void Table::insert_row(const Row &row) {
  validate_schema(row);
  if (auto violated = find_violated_check(row)) {
    throw ConstraintException("CHECK constraint '" + *violated + "' violated");
  }
  if (!validate_row(row)) {
    throw ConstraintException("Row does not satisfy table constraints");
  }
  row_directory_.push_back(heap_->insert_row(row));
  insert_into_indexes(row, row_directory_.size() - 1);
  mark_dirty();
}

void Table::insert_row_versioned(Row row, uint64_t xmin) {
  row.set_xmin(xmin);
  row.set_xmax(0);
  insert_row(row);
}

std::vector<Row> Table::get_all_rows() const {
  std::vector<Row> rows;
  rows.reserve(row_directory_.size());
  for (const ItemPointer &pointer : row_directory_) {
    rows.push_back(heap_->get_row(pointer));
  }
  return rows;
}

std::vector<size_t> Table::get_visible_row_indices(
    const TransactionManager &txn_manager, uint64_t reader_xid,
    const TransactionSnapshot *snapshot) const {
  std::vector<size_t> result;
  for (size_t i = 0; i < row_directory_.size(); ++i) {
    const Row row = heap_->get_row(row_directory_[i]);
    if (txn_manager.isVisible(row.get_xmin(), row.get_xmax(), reader_xid,
                              snapshot)) {
      result.push_back(i);
    }
  }
  return result;
}

std::vector<Row> Table::get_visible_rows(
    const TransactionManager &txn_manager, uint64_t reader_xid,
    const TransactionSnapshot *snapshot) const {
  std::vector<Row> result;
  for (size_t i : get_visible_row_indices(txn_manager, reader_xid, snapshot)) {
    result.push_back(get_row(i));
  }
  return result;
}

Row Table::get_row(size_t index) const {
  if (index >= row_directory_.size()) {
    throw std::out_of_range("Row index " + std::to_string(index) +
                            " out of range");
  }
  return heap_->get_row(row_directory_[index]);
}

void Table::replace_row(size_t index, const Row &row) {
  row_directory_[index] = heap_->update_row(row_directory_[index], row);
}

void Table::update_row(size_t index, const Row &row) {
  if (index >= row_directory_.size()) {
    throw std::out_of_range("Row index out of range");
  }
  validate_schema(row);
  if (auto violated = find_violated_check(row)) {
    throw ConstraintException("CHECK constraint '" + *violated + "' violated");
  }
  if (!validate_row(row, index)) {
    throw ConstraintException("Updated row does not satisfy table constraints");
  }
  const Row old_row = get_row(index);
  remove_from_indexes(old_row, index);
  Row stored = row;
  stored.set_xmin(old_row.get_xmin());
  stored.set_xmax(old_row.get_xmax());
  replace_row(index, stored);
  insert_into_indexes(stored, index);
  mark_dirty();
}

void Table::update_row_versioned(size_t index, Row new_row, uint64_t xid) {
  if (index >= row_directory_.size()) {
    throw std::out_of_range("Row index out of range");
  }
  validate_schema(new_row);
  if (auto violated = find_violated_check(new_row)) {
    throw ConstraintException("CHECK constraint '" + *violated + "' violated");
  }
  if (!validate_row(new_row, index)) {
    throw ConstraintException("Updated row does not satisfy table constraints");
  }
  Row old_row = get_row(index);
  old_row.set_xmax(xid);
  replace_row(index, old_row);
  new_row.set_xmin(xid);
  new_row.set_xmax(0);
  row_directory_.push_back(heap_->insert_row(new_row));
  insert_into_indexes(new_row, row_directory_.size() - 1);
  mark_dirty();
}

void Table::delete_row(size_t index) {
  if (index >= row_directory_.size()) {
    throw std::out_of_range("Row index out of range");
  }
  heap_->delete_slot(row_directory_[index]);
  row_directory_.erase(row_directory_.begin() + static_cast<long>(index));
  reindex_after_delete();
  mark_dirty();
}

void Table::delete_row_versioned(size_t index, uint64_t xid) {
  if (index >= row_directory_.size()) {
    throw std::out_of_range("Row index out of range");
  }
  Row row = get_row(index);
  row.set_xmax(xid);
  replace_row(index, row);
  mark_dirty();
}

void Table::vacuum_versions(const TransactionManager &txn_manager) {
  vacuum_versions(txn_manager, txn_manager.getVacuumHorizon());
}

void Table::vacuum_versions(const TransactionManager &txn_manager,
                            uint64_t vacuum_horizon) {
  std::vector<Row> kept;
  kept.reserve(row_directory_.size());
  for (const ItemPointer &pointer : row_directory_) {
    Row row = heap_->get_row(pointer);
    if (txn_manager.isAborted(row.get_xmin())) {
      continue;
    }
    if (row.get_xmax() != 0 && txn_manager.isAborted(row.get_xmax())) {
      row.set_xmax(0);
    }
    if (row.get_xmax() != 0 && txn_manager.isCommitted(row.get_xmax()) &&
        row.get_xmax() < vacuum_horizon) {
      continue;
    }
    kept.push_back(std::move(row));
  }
  heap_->clear();
  row_directory_.clear();
  sync_heap_column_types();
  for (const Row &row : kept) {
    row_directory_.push_back(heap_->insert_row(row));
  }
  rebuild_indexes();
  mark_dirty();
}

void Table::delete_all() {
  heap_->clear();
  row_directory_.clear();
  for (auto &[col_name, idx] : column_indices_) {
    (void)col_name;
    idx->clear();
  }
  for (auto &[index_name, idx] : named_indices_) {
    (void)index_name;
    idx->clear();
  }
  mark_dirty();
}

std::vector<size_t> Table::find_rows_by_value(const std::string &column_name,
                                              const Value &value) const {
  const int col_idx = get_column_index(column_name);
  if (col_idx < 0) {
    throw NotFoundException("Column '" + column_name + "' not found");
  }
  auto it = column_indices_.find(column_name);
  if (it != column_indices_.end()) {
    return it->second->find_equal(value);
  }
  for (const auto &[index_name, columns] : secondary_indexes_) {
    if (columns.empty() || columns[0] != column_name) {
      continue;
    }
    auto named = named_indices_.find(index_name);
    if (named == named_indices_.end()) {
      continue;
    }
    if (columns.size() == 1) {
      return named->second->find_equal(value);
    }
    return named->second->find_prefix(IndexKey(value));
  }
  std::vector<size_t> result;
  for (size_t i = 0; i < row_directory_.size(); ++i) {
    if (get_row(i).get_value(static_cast<size_t>(col_idx)) == value) {
      result.push_back(i);
    }
  }
  return result;
}

std::vector<size_t> Table::find_rows_by_range(
    const std::string &column_name, const std::optional<Value> &lower,
    bool lower_inclusive, const std::optional<Value> &upper,
    bool upper_inclusive) const {
  const int col_idx = get_column_index(column_name);
  if (col_idx < 0) {
    throw NotFoundException("Column '" + column_name + "' not found");
  }
  auto it = column_indices_.find(column_name);
  if (it != column_indices_.end()) {
    return it->second->find_range(lower, lower_inclusive, upper,
                                  upper_inclusive);
  }
  for (const auto &[index_name, columns] : secondary_indexes_) {
    if (columns.size() != 1 || columns[0] != column_name) {
      continue;
    }
    auto named = named_indices_.find(index_name);
    if (named != named_indices_.end()) {
      return named->second->find_range(lower, lower_inclusive, upper,
                                       upper_inclusive);
    }
  }
  std::vector<size_t> result;
  for (size_t i = 0; i < row_directory_.size(); ++i) {
    const Value &v = get_row(i).get_value(static_cast<size_t>(col_idx));
    if (v.is_null()) {
      continue;
    }
    bool ok_lower = true;
    bool ok_upper = true;
    if (lower.has_value()) {
      ok_lower = lower_inclusive ? !(v < *lower) : (*lower < v);
    }
    if (upper.has_value()) {
      ok_upper = upper_inclusive ? !(*upper < v) : (v < *upper);
    }
    if (ok_lower && ok_upper) {
      result.push_back(i);
    }
  }
  return result;
}

std::vector<size_t> Table::find_rows_by_predicate(
    std::function<bool(const Row &)> predicate) const {
  std::vector<size_t> result;
  for (size_t i = 0; i < row_directory_.size(); ++i) {
    if (predicate(get_row(i))) {
      result.push_back(i);
    }
  }
  return result;
}

std::vector<size_t> Table::find_rows_by_index_key(
    const std::string &index_name, const IndexKey &key) const {
  auto it = named_indices_.find(index_name);
  if (it == named_indices_.end()) {
    return {};
  }
  auto cols_it = secondary_indexes_.find(index_name);
  if (cols_it == secondary_indexes_.end()) {
    return {};
  }
  if (key.size() == cols_it->second.size()) {
    return it->second->find_equal(key);
  }
  if (key.size() < cols_it->second.size()) {
    return it->second->find_prefix(key);
  }
  return {};
}

std::optional<std::string> Table::find_matching_secondary_index(
    const std::vector<std::string> &prefix_columns) const {
  if (prefix_columns.empty()) {
    return std::nullopt;
  }
  std::optional<std::string> best;
  size_t best_len = 0;
  for (const auto &[index_name, columns] : secondary_indexes_) {
    if (columns.size() < prefix_columns.size()) {
      continue;
    }
    bool matches = true;
    for (size_t i = 0; i < prefix_columns.size(); ++i) {
      if (columns[i] != prefix_columns[i]) {
        matches = false;
        break;
      }
    }
    if (!matches) {
      continue;
    }
    if (columns.size() > best_len) {
      best_len = columns.size();
      best = index_name;
    }
  }
  return best;
}

bool Table::has_index(const std::string &column_name) const {
  if (column_indices_.count(column_name) > 0) {
    return true;
  }
  for (const auto &[index_name, columns] : secondary_indexes_) {
    (void)index_name;
    if (!columns.empty() && columns[0] == column_name) {
      return true;
    }
  }
  return false;
}

const BTreeIndex *Table::get_index(const std::string &column_name) const {
  auto it = column_indices_.find(column_name);
  if (it != column_indices_.end()) {
    return it->second.get();
  }
  for (const auto &[index_name, columns] : secondary_indexes_) {
    if (columns.size() == 1 && columns[0] == column_name) {
      return get_named_index(index_name);
    }
  }
  return nullptr;
}

const BTreeIndex *Table::get_named_index(const std::string &index_name) const {
  auto it = named_indices_.find(index_name);
  if (it == named_indices_.end()) {
    return nullptr;
  }
  return it->second.get();
}

bool Table::validate_row(const Row &row, size_t exclude_row_index) const {
  if (row.get_column_count() != columns_.size()) {
    return false;
  }
  for (size_t i = 0; i < columns_.size(); ++i) {
    if (!columns_[i].is_nullable() && row.get_value(i).is_null()) {
      return false;
    }
  }
  if (!validate_unique_constraint(row, exclude_row_index)) {
    return false;
  }
  if (!validate_primary_key_uniqueness(row, exclude_row_index)) {
    return false;
  }
  if (!validate_check_constraints(row)) {
    return false;
  }
  return true;
}

bool Table::validate_primary_key_uniqueness(const Row &row,
                                            size_t exclude_row_index) const {
  const int pk_idx = get_primary_key_index();
  if (pk_idx < 0) {
    return true;
  }
  const Value &pk_value = row.get_value(static_cast<size_t>(pk_idx));
  if (pk_value.is_null()) {
    return false;
  }
  if (has_index(columns_[static_cast<size_t>(pk_idx)].get_name())) {
    auto matches = find_rows_by_value(
        columns_[static_cast<size_t>(pk_idx)].get_name(), pk_value);
    for (size_t idx : matches) {
      if (idx != exclude_row_index && get_row(idx).get_xmax() == 0) {
        return false;
      }
    }
    return true;
  }
  for (size_t i = 0; i < row_directory_.size(); ++i) {
    if (i == exclude_row_index || get_row(i).get_xmax() != 0) {
      continue;
    }
    if (get_row(i).get_value(static_cast<size_t>(pk_idx)) == pk_value) {
      return false;
    }
  }
  return true;
}

bool Table::validate_unique_constraint(const Row &row,
                                       size_t exclude_row_index) const {
  for (size_t i = 0; i < columns_.size(); ++i) {
    if (!columns_[i].is_unique()) {
      continue;
    }
    const Value &value = row.get_value(i);
    if (value.is_null()) {
      continue;
    }
    if (has_index(columns_[i].get_name())) {
      auto matches = find_rows_by_value(columns_[i].get_name(), value);
      for (size_t idx : matches) {
        if (idx != exclude_row_index && get_row(idx).get_xmax() == 0) {
          return false;
        }
      }
      continue;
    }
    for (size_t j = 0; j < row_directory_.size(); ++j) {
      if (j == exclude_row_index || get_row(j).get_xmax() != 0) {
        continue;
      }
      if (get_row(j).get_value(i) == value) {
        return false;
      }
    }
  }
  return true;
}

void Table::rebuild_indexes() {
  column_indices_.clear();
  named_indices_.clear();
  for (const auto &col : columns_) {
    if (col.is_primary_key() || col.is_unique()) {
      build_index(col.get_name());
    }
  }
  for (const auto &[index_name, columns] : secondary_indexes_) {
    (void)columns;
    build_named_index(index_name);
  }
}

void Table::create_secondary_index(
    const std::string &index_name,
    const std::vector<std::string> &column_names) {
  if (secondary_indexes_.count(index_name) > 0) {
    throw ConstraintException("Index '" + index_name + "' already exists");
  }
  if (column_names.empty()) {
    throw InvalidOperationException("Index must include at least one column");
  }
  for (const std::string &column_name : column_names) {
    if (get_column_index(column_name) < 0) {
      throw NotFoundException("Column '" + column_name + "' not found");
    }
  }
  secondary_indexes_[index_name] = column_names;
  build_named_index(index_name);
  mark_dirty();
}

void Table::create_secondary_index(const std::string &index_name,
                                   const std::string &column_name) {
  create_secondary_index(index_name, std::vector<std::string>{column_name});
}

bool Table::is_column_still_indexed(const std::string &column_name) const {
  for (const auto &col : columns_) {
    if (col.get_name() == column_name &&
        (col.is_primary_key() || col.is_unique())) {
      return true;
    }
  }
  for (const auto &[name, cols] : secondary_indexes_) {
    (void)name;
    if (cols.size() == 1 && cols[0] == column_name) {
      return true;
    }
  }
  return false;
}

bool Table::drop_secondary_index(const std::string &index_name) {
  auto it = secondary_indexes_.find(index_name);
  if (it == secondary_indexes_.end()) {
    return false;
  }
  const std::vector<std::string> columns = it->second;
  secondary_indexes_.erase(it);
  named_indices_.erase(index_name);
  if (columns.size() == 1 && !is_column_still_indexed(columns[0])) {
    drop_index(columns[0]);
  }
  mark_dirty();
  return true;
}

bool Table::has_secondary_index(const std::string &index_name) const {
  return secondary_indexes_.count(index_name) > 0;
}

const std::map<std::string, std::vector<std::string>> &
Table::get_secondary_indexes() const {
  return secondary_indexes_;
}

void Table::set_secondary_indexes(
    const std::map<std::string, std::vector<std::string>> &indexes) {
  secondary_indexes_ = indexes;
}

void Table::add_foreign_key(const ForeignKeyDefinition &fk) {
  foreign_keys_.push_back(fk);
  mark_dirty();
}

const std::vector<ForeignKeyDefinition> &Table::get_foreign_keys() const {
  return foreign_keys_;
}

void Table::set_foreign_keys(std::vector<ForeignKeyDefinition> fks) {
  foreign_keys_ = std::move(fks);
}

void Table::add_check(const CheckConstraintDefinition &check) {
  for (const auto &existing : checks_) {
    if (existing.name == check.name) {
      throw ConstraintException("CHECK constraint '" + check.name +
                                "' already exists");
    }
  }
  checks_.push_back(check);
  mark_dirty();
}

bool Table::drop_check(const std::string &name) {
  for (auto it = checks_.begin(); it != checks_.end(); ++it) {
    if (it->name == name) {
      checks_.erase(it);
      mark_dirty();
      return true;
    }
  }
  return false;
}

const std::vector<CheckConstraintDefinition> &Table::get_checks() const {
  return checks_;
}

void Table::set_checks(std::vector<CheckConstraintDefinition> checks) {
  checks_ = std::move(checks);
}

bool Table::validate_check_constraints(const Row &row) const {
  return !find_violated_check(row).has_value();
}

std::optional<std::string> Table::find_violated_check(const Row &row) const {
  if (checks_.empty()) {
    return std::nullopt;
  }
  SelectExpressionEvaluator evaluator = make_table_check_evaluator(*this);
  for (const CheckConstraintDefinition &check : checks_) {
    if (!check.predicate) {
      continue;
    }
    if (!evaluator.evaluate_check_condition(row, check.predicate)) {
      return check.name;
    }
  }
  return std::nullopt;
}

void Table::ensure_index_for_column(const std::string &column_name) {
  build_index(column_name);
}

void Table::drop_index(const std::string &column_name) {
  column_indices_.erase(column_name);
}

void Table::mark_dirty() { dirty_ = true; }

void Table::clear_dirty() { dirty_ = false; }

bool Table::is_dirty() const { return dirty_; }

std::string Table::to_string() const {
  std::string str = "Table: " + table_name_ + "\n";
  str += "Columns:\n";
  for (const auto &col : columns_) {
    str += "  " + col.to_string() + "\n";
  }
  str += "Rows: " + std::to_string(row_directory_.size()) + "\n";
  return str;
}

void Table::build_index(const std::string &column_name) {
  const int col_idx = get_column_index(column_name);
  if (col_idx < 0) {
    throw NotFoundException("Column '" + column_name + "' not found");
  }
  auto idx = std::make_unique<BTreeIndex>();
  for (size_t i = 0; i < row_directory_.size(); ++i) {
    idx->insert(get_row(i).get_value(static_cast<size_t>(col_idx)), i);
  }
  column_indices_[column_name] = std::move(idx);
}

void Table::build_named_index(const std::string &index_name) {
  auto it = secondary_indexes_.find(index_name);
  if (it == secondary_indexes_.end()) {
    return;
  }
  auto idx = std::make_unique<BTreeIndex>();
  for (size_t i = 0; i < row_directory_.size(); ++i) {
    idx->insert(build_index_key(get_row(i), it->second), i);
  }
  named_indices_[index_name] = std::move(idx);
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
      return static_cast<int>(i);
    }
  }
  return -1;
}

void Table::reindex_after_delete() {
  for (auto &[col_name, idx] : column_indices_) {
    idx->clear();
    const int col_idx = get_column_index(col_name);
    if (col_idx < 0) {
      continue;
    }
    for (size_t i = 0; i < row_directory_.size(); ++i) {
      idx->insert(get_row(i).get_value(static_cast<size_t>(col_idx)), i);
    }
  }
  for (const auto &[index_name, columns] : secondary_indexes_) {
    auto it = named_indices_.find(index_name);
    if (it == named_indices_.end()) {
      continue;
    }
    it->second->clear();
    for (size_t i = 0; i < row_directory_.size(); ++i) {
      it->second->insert(build_index_key(get_row(i), columns), i);
    }
  }
}

HeapFile &Table::get_heap() { return *heap_; }

const HeapFile &Table::get_heap() const { return *heap_; }

FileId Table::get_file_id() const { return file_id_; }

void Table::rebuild_row_directory() {
  row_directory_.clear();
  heap_->scan([&](const ItemPointer &pointer, const Row &) {
    row_directory_.push_back(pointer);
  });
}

void Table::replace_heap_pages(const std::vector<std::vector<uint8_t>> &pages) {
  heap_->replace_pages(pages);
  rebuild_row_directory();
}

void Table::flush_heap() { buffer_pool_->flush_file(file_id_); }

}  // namespace db
