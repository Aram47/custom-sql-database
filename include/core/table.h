#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/btree_index.h"
#include "core/check_constraint.h"
#include "core/column.h"
#include "core/foreign_key.h"
#include "core/index_key.h"
#include "core/row.h"
#include "core/transaction_manager.h"
#include "types/value.h"
#include "utils/exceptions.h"

namespace db {

/** In-memory table with schema, rows, and B-tree indexes on PK/UNIQUE. */
class Table {
 public:
  explicit Table(const std::string &name);
  ~Table() = default;

  /** Deep-copies schema, rows, secondary indexes and foreign keys. */
  std::unique_ptr<Table> clone() const;

  Table(const Table &) = delete;
  Table &operator=(const Table &) = delete;
  Table(Table &&) = delete;
  Table &operator=(Table &&) = delete;

  const std::string &get_name() const;
  void set_name(const std::string &name);
  size_t get_column_count() const;
  size_t get_row_count() const;

  void add_column(const Column &column);
  void drop_column(const std::string &column_name);
  void rename_column(const std::string &old_name, const std::string &new_name);
  const Column &get_column(size_t index) const;
  Column &get_mutable_column(size_t index);
  const Column &get_column(const std::string &name) const;
  int get_column_index(const std::string &name) const;
  const std::vector<Column> &get_columns() const;

  void insert_row(const Row &row);
  void insert_row_versioned(Row row, uint64_t xmin);
  void update_row(size_t index, const Row &row);
  void update_row_versioned(size_t index, Row new_row, uint64_t xid);
  void delete_row(size_t index);
  void delete_row_versioned(size_t index, uint64_t xid);
  void delete_all();
  /** Removes aborted inserts and clears aborted deletes; drops committed deletes. */
  void vacuum_versions(const TransactionManager &txn_manager);
  void vacuum_versions(const TransactionManager &txn_manager,
                       uint64_t vacuum_horizon);
  std::vector<Row> get_all_rows() const;
  /** Rows visible to reader_xid under optional snapshot (nullptr = autocommit). */
  std::vector<Row> get_visible_rows(const TransactionManager &txn_manager,
                                    uint64_t reader_xid,
                                    const TransactionSnapshot *snapshot) const;
  std::vector<size_t> get_visible_row_indices(
      const TransactionManager &txn_manager, uint64_t reader_xid,
      const TransactionSnapshot *snapshot) const;
  Row get_row(size_t index) const;
  std::vector<size_t> find_rows_by_value(const std::string &column_name,
                                         const Value &value) const;
  std::vector<size_t> find_rows_by_range(
      const std::string &column_name, const std::optional<Value> &lower,
      bool lower_inclusive, const std::optional<Value> &upper,
      bool upper_inclusive) const;
  std::vector<size_t> find_rows_by_predicate(
      std::function<bool(const Row &)> predicate) const;
  /** Lookup via named secondary index using a full or prefix IndexKey. */
  std::vector<size_t> find_rows_by_index_key(const std::string &index_name,
                                             const IndexKey &key) const;
  /** Best secondary index whose leftmost columns match the given names. */
  std::optional<std::string> find_matching_secondary_index(
      const std::vector<std::string> &prefix_columns) const;

  bool has_index(const std::string &column_name) const;
  const BTreeIndex *get_index(const std::string &column_name) const;
  const BTreeIndex *get_named_index(const std::string &index_name) const;

  /** Registers a named secondary index on one or more columns. */
  void create_secondary_index(const std::string &index_name,
                              const std::vector<std::string> &column_names);
  /** @deprecated Prefer vector overload; single-column convenience. */
  void create_secondary_index(const std::string &index_name,
                              const std::string &column_name);
  /** Drops a named secondary index; returns false if unknown. */
  bool drop_secondary_index(const std::string &index_name);
  bool has_secondary_index(const std::string &index_name) const;
  const std::map<std::string, std::vector<std::string>> &get_secondary_indexes()
      const;
  void set_secondary_indexes(
      const std::map<std::string, std::vector<std::string>> &indexes);

  void add_foreign_key(const ForeignKeyDefinition &fk);
  const std::vector<ForeignKeyDefinition> &get_foreign_keys() const;
  void set_foreign_keys(std::vector<ForeignKeyDefinition> fks);

  void add_check(const CheckConstraintDefinition &check);
  bool drop_check(const std::string &name);
  const std::vector<CheckConstraintDefinition> &get_checks() const;
  void set_checks(std::vector<CheckConstraintDefinition> checks);

  bool validate_row(const Row &row,
                    size_t exclude_row_index = static_cast<size_t>(-1)) const;
  bool validate_primary_key_uniqueness(
      const Row &row,
      size_t exclude_row_index = static_cast<size_t>(-1)) const;
  bool validate_unique_constraint(
      const Row &row,
      size_t exclude_row_index = static_cast<size_t>(-1)) const;
  bool validate_check_constraints(const Row &row) const;
  /** Returns the name of the first violated CHECK, if any. */
  std::optional<std::string> find_violated_check(const Row &row) const;

  /** Builds or rebuilds B-tree indexes for all PK/UNIQUE columns. */
  void rebuild_indexes();
  void ensure_index_for_column(const std::string &column_name);
  void drop_index(const std::string &column_name);

  void mark_dirty();
  void clear_dirty();
  bool is_dirty() const;

  std::vector<Row> &get_mutable_rows();
  std::string to_string() const;

  IndexKey build_index_key(const Row &row,
                           const std::vector<std::string> &column_names) const;

 private:
  std::string table_name_;
  std::vector<Column> columns_;
  std::vector<Row> rows_;
  std::map<std::string, std::unique_ptr<BTreeIndex>> column_indices_;
  /** Named secondary indexes (single- and multi-column). */
  std::map<std::string, std::unique_ptr<BTreeIndex>> named_indices_;
  /** Secondary index name -> ordered column names. */
  std::map<std::string, std::vector<std::string>> secondary_indexes_;
  std::vector<ForeignKeyDefinition> foreign_keys_;
  std::vector<CheckConstraintDefinition> checks_;
  bool dirty_{false};

  void build_index(const std::string &column_name);
  void build_named_index(const std::string &index_name);
  void insert_into_indexes(const Row &row, size_t row_index);
  void remove_from_indexes(const Row &row, size_t row_index);
  bool is_column_still_indexed(const std::string &column_name) const;
  void validate_schema(const Row &row) const;
  int get_primary_key_index() const;
  void reindex_after_delete();
};

}  // namespace db
