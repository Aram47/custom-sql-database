#pragma once

#include <atomic>
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
#include "core/partition.h"
#include "core/row.h"
#include "core/transaction_manager.h"
#include "core/unique_constraint.h"
#include "storage/buffer_pool.h"
#include "storage/heap_file.h"
#include "storage/item_pointer.h"
#include "storage/page_store.h"
#include "types/value.h"
#include "utils/exceptions.h"

namespace db {

/** Table facade over a page-backed HeapFile with in-memory indexes. */
class Table {
 public:
  explicit Table(const std::string &name);
  /**
   * Constructs a table using a shared buffer pool and an owned page store.
   * Used by Database / PersistenceManager for durable heaps.
   */
  Table(const std::string &name, FileId file_id, IBufferPool &buffer_pool,
        std::unique_ptr<IPageStore> page_store);
  ~Table() = default;

  /** Deep-copies schema, heap rows, secondary indexes and foreign keys. */
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
  /**
   * After COMMIT of xid: make versions self-describing for disk durability.
   * Live rows with xmin==xid get xmin=0; versions with xmax==xid are dropped.
   */
  void freeze_committed_versions(uint64_t xid);
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

  /** Table-level PRIMARY KEY column list (empty = none / legacy flags only). */
  const std::vector<std::string> &get_primary_key_columns() const;
  void set_primary_key_columns(std::vector<std::string> columns);
  /** Applies PRIMARY KEY: updates column flags + constraint index. */
  void apply_primary_key(const std::vector<std::string> &columns);
  /** Clears PRIMARY KEY flags and drops the PK constraint index. */
  bool drop_primary_key();

  const std::vector<UniqueConstraintDefinition> &get_unique_constraints() const;
  void set_unique_constraints(
      std::vector<UniqueConstraintDefinition> constraints);
  /** Adds a table-level UNIQUE (and optional column unique_ flags for singles). */
  void apply_unique_constraint(const UniqueConstraintDefinition &constraint);
  /** Drops UNIQUE matching the given column list; returns false if missing. */
  bool drop_unique_constraint(const std::vector<std::string> &columns);

  /** Syncs primary_key_columns_ from Column::is_primary_key when empty. */
  void sync_key_metadata_from_column_flags();

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

  std::string to_string() const;

  IndexKey build_index_key(const Row &row,
                           const std::vector<std::string> &column_names) const;

  HeapFile &get_heap();
  const HeapFile &get_heap() const;
  FileId get_file_id() const;

  /** Rebuilds logical row ids by scanning all live heap slots. */
  void rebuild_row_directory();

  /** Replaces heap page images (used by PersistenceManager load). */
  void replace_heap_pages(const std::vector<std::vector<uint8_t>> &pages);

  /** Flushes this table's dirty pages through the buffer pool. */
  void flush_heap();

  /** Allocates a process-wide unique file id for heap registration. */
  static FileId allocate_file_id();

  /** True when this table is a partitioned parent (catalog-only rows). */
  bool isPartitioned() const;
  const PartitionedTableMetadata *getPartitionMetadata() const;
  PartitionedTableMetadata *getMutablePartitionMetadata();
  void setPartitionMetadata(std::unique_ptr<PartitionedTableMetadata> metadata);
  void clearPartitionMetadata();

 private:
  std::string table_name_;
  std::vector<Column> columns_;
  std::unique_ptr<IPageStore> owned_store_;
  std::unique_ptr<BufferPool> owned_pool_;
  IBufferPool *buffer_pool_{nullptr};
  FileId file_id_{0};
  std::unique_ptr<HeapFile> heap_;
  std::vector<ItemPointer> row_directory_;
  std::map<std::string, std::unique_ptr<BTreeIndex>> column_indices_;
  /** Named secondary indexes (single- and multi-column). */
  std::map<std::string, std::unique_ptr<BTreeIndex>> named_indices_;
  /** Secondary index name -> ordered column names. */
  std::map<std::string, std::vector<std::string>> secondary_indexes_;
  std::vector<ForeignKeyDefinition> foreign_keys_;
  std::vector<CheckConstraintDefinition> checks_;
  std::vector<std::string> primary_key_columns_;
  std::vector<UniqueConstraintDefinition> unique_constraints_;
  /** Internal unique indexes backing PK / table UNIQUE (name → columns). */
  std::map<std::string, std::vector<std::string>> constraint_indexes_;
  std::unique_ptr<PartitionedTableMetadata> partition_meta_;
  bool dirty_{false};

  void initialize_heap();
  void sync_heap_column_types();
  std::vector<DataType> collect_column_types() const;
  void build_index(const std::string &column_name);
  void build_named_index(const std::string &index_name);
  void build_constraint_index(const std::string &index_name);
  void ensure_constraint_index(const std::string &index_name,
                               const std::vector<std::string> &column_names);
  void drop_constraint_index(const std::string &index_name);
  static std::string primary_key_index_name();
  static std::string unique_constraint_index_name(
      const UniqueConstraintDefinition &constraint);
  void insert_into_indexes(const Row &row, size_t row_index);
  void remove_from_indexes(const Row &row, size_t row_index);
  bool is_column_still_indexed(const std::string &column_name) const;
  void validate_schema(const Row &row) const;
  int get_primary_key_index() const;
  bool rows_share_key(const IndexKey &key, size_t exclude_row_index,
                      const std::string &index_name,
                      const std::vector<std::string> &column_names) const;
  void reindex_after_delete();
  void replace_row(size_t index, const Row &row);
};

}  // namespace db
