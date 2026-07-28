#pragma once

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/bind_context.h"
#include "core/correlation_context.h"
#include "core/lock_manager.h"
#include "core/partition.h"
#include "core/plan_cache.h"
#include "core/routine_catalog.h"
#include "core/session_context.h"
#include "core/table.h"
#include "core/transaction_manager.h"
#include "core/trigger.h"
#include "core/view_catalog.h"
#include "executor/query_executor.h"
#include "parser/parser.h"
#include "planner/table_statistics.h"
#include "storage/buffer_pool.h"
#include "storage/wal_manager.h"
#include "types/value.h"
#include "utils/exceptions.h"

namespace db {

/** Catalog of tables with query dispatch, transactions, and persistence. */
class Database {
 public:
  explicit Database(std::string storage_directory = "data",
                    int vacuum_interval_ms = 5000);
  Database(std::string storage_directory, int vacuum_interval_ms,
           size_t buffer_pool_pages);
  ~Database();

  Database(const Database &) = delete;
  Database &operator=(const Database &) = delete;

  void load_from_disk();
  void set_vacuum_interval_ms(int vacuum_interval_ms);

  /**
   * Flushes all dirty tables via the WAL protocol, syncs the buffer pool,
   * and truncates the WAL when safe.
   * @throws StorageException on persistence failure.
   */
  void checkpoint();

  QueryResult execute_query(const std::string &sql,
                            SessionContext *session = nullptr);

  void create_table(const std::string &table_name);
  void drop_table(const std::string &table_name);
  Table *get_table(const std::string &table_name);

  std::vector<std::string> list_tables() const;
  bool has_table(const std::string &table_name) const;
  bool has_view(const std::string &view_name) const;
  const ViewDefinition *get_view(const std::string &view_name) const;

  /** Ephemeral tables used while expanding views for a SELECT. */
  bool has_ephemeral_table(const std::string &table_name) const;
  void register_ephemeral_table(const std::string &name,
                                std::unique_ptr<Table> table);
  std::string allocate_ephemeral_table_name(const std::string &view_name);
  void clear_ephemeral_tables();
  void enter_ephemeral_scope();
  void leave_ephemeral_scope();

  const std::string &get_storage_directory() const;

  CorrelationContext *get_correlation_context();
  const BindContext *get_active_bind() const;
  TransactionManager &get_transaction_manager();
  uint64_t get_reader_xid(SessionContext *session) const;
  const TransactionSnapshot *get_reader_snapshot(SessionContext *session) const;
  SessionContext *get_active_session() const;

  /** Table cardinality / NDV / histogram stats for the planner. */
  TableStatistics &get_table_statistics();
  const TableStatistics &get_table_statistics() const;

  /**
   * Ensures statistics for table_name match current row count
   * (lazy refresh when missing or stale).
   */
  void ensureTableStatistics(const std::string &table_name);

  RoutineCatalog &get_routine_catalog();
  const RoutineCatalog &get_routine_catalog() const;

  TriggerCatalog &get_trigger_catalog();
  const TriggerCatalog &get_trigger_catalog() const;

  int get_trigger_depth() const;
  void enter_trigger_depth();
  void leave_trigger_depth();

  /** Used by ProcedureExecutor to run one body statement. */
  QueryResult run_parsed_statement(const ParsedStatement &stmt,
                                   SessionContext *session,
                                   const std::string &sql);
  void set_active_local_variables(
      std::unordered_map<std::string, Value> locals);
  void clear_active_local_variables();
  const std::unordered_map<std::string, Value> *get_active_local_variables()
      const;

  /** Runs an uncorrelated SELECT for subquery evaluation. */
  QueryResult run_select(std::shared_ptr<SelectStatement> stmt);

  Value evaluate_scalar_subquery(const std::shared_ptr<SelectStatement> &stmt,
                                 std::string *error_msg);
  bool evaluate_in_subquery(const Value &left,
                            const std::shared_ptr<SelectStatement> &stmt,
                            bool is_not, std::string *error_msg);
  bool evaluate_exists_subquery(const std::shared_ptr<SelectStatement> &stmt,
                                std::string *error_msg);

  void validate_foreign_keys_on_insert(const Table &table, const Row &row);
  void validate_foreign_keys_on_update(Table &table, const Row &old_row,
                                       const Row &new_row);
  void validate_foreign_keys_on_delete(Table &table, const Row &row);

  /**
   * Resolves INSERT/UPDATE target child for a partitioned parent.
   * @return Child table or nullptr with error filled.
   */
  Table *resolvePartitionChild(Table *parent, const Row &row,
                               std::string *error);

  /** Child table names after pruning WHERE on the partition key. */
  std::vector<std::string> listPrunedPartitions(
      Table *parent, const ExpressionPtr &whereExpr) const;

  /** Finds partitioned parent that owns childName, or nullptr. */
  Table *findPartitionParent(const std::string &childName);

  /** Loads visible rows; expands partitioned parents into pruned children. */
  std::vector<Row> loadVisibleRowsForRelation(
      Table *table, const ExpressionPtr &pruneWhere);

 private:
  std::string storage_directory_;
  size_t buffer_pool_pages_{64};
  std::unique_ptr<BufferPool> buffer_pool_;
  mutable std::recursive_mutex db_mutex_;
  std::map<std::string, std::unique_ptr<Table>> tables_;
  ViewCatalog view_catalog_;
  RoutineCatalog routine_catalog_;
  TriggerCatalog trigger_catalog_;
  std::map<std::string, std::unique_ptr<Table>> ephemeral_tables_;
  int ephemeral_scope_depth_{0};
  uint64_t next_ephemeral_id_{0};
  PlanCache plan_cache_;
  LockManager lock_manager_;
  WalManager wal_manager_;
  TransactionManager transaction_manager_;
  CorrelationContext correlation_context_;
  TableStatistics table_statistics_;
  std::unordered_map<std::string, Value> active_local_variables_;
  bool has_active_local_variables_{false};
  const BindContext *active_bind_{nullptr};
  SessionContext *active_session_{nullptr};
  int trigger_depth_{0};
  std::atomic<uint64_t> next_lock_id_{1};
  std::atomic<int> vacuum_interval_ms_{5000};
  std::atomic<bool> vacuum_stop_{false};
  std::condition_variable vacuum_cv_;
  std::mutex vacuum_mutex_;
  std::thread vacuum_thread_;

  QueryResult persist_dirty_tables(QueryResult result, bool in_transaction);
  /** Caller must hold db_mutex_. Flushes dirty tables; no-op if none dirty. */
  void flush_dirty_tables_locked();
  void mark_table_dirty(const std::string &table_name);
  void clear_plan_cache();
  void vacuum_all_tables();
  void start_vacuum_worker();
  void stop_vacuum_worker();
  void run_vacuum_worker();

  QueryResult dispatch_statement(const ParsedStatement &stmt,
                                 SessionContext *session,
                                 const std::string &sql);

  QueryResult execute_select_statement(std::shared_ptr<SelectStatement> stmt);
  QueryResult execute_set_operation_statement(
      std::shared_ptr<SetOperationStatement> stmt);
  QueryResult execute_query_operand(
      const SetOperationStatement::Operand &operand);
  QueryResult execute_insert_statement(std::shared_ptr<InsertStatement> stmt);
  QueryResult execute_update_statement(std::shared_ptr<UpdateStatement> stmt);
  QueryResult execute_delete_statement(std::shared_ptr<DeleteStatement> stmt);
  QueryResult execute_create_table_statement(
      std::shared_ptr<CreateTableStatement> stmt);
  QueryResult execute_create_partition_of(
      std::shared_ptr<CreateTableStatement> stmt);
  void attachPartitionMetadata(Table *table,
                               const std::shared_ptr<CreateTableStatement> &stmt);
  void persistPartitionMetadata(const std::string &parentName);
  QueryResult execute_drop_table_statement(
      std::shared_ptr<DropTableStatement> stmt);
  QueryResult execute_partitioned_update(
      std::shared_ptr<UpdateStatement> stmt, Table *parent);
  QueryResult execute_partitioned_delete(
      std::shared_ptr<DeleteStatement> stmt, Table *parent);
  QueryResult execute_alter_table_statement(
      std::shared_ptr<AlterTableStatement> stmt);
  QueryResult execute_create_index_statement(
      std::shared_ptr<CreateIndexStatement> stmt);
  QueryResult execute_drop_index_statement(
      std::shared_ptr<DropIndexStatement> stmt);
  QueryResult execute_create_view_statement(
      std::shared_ptr<CreateViewStatement> stmt);
  QueryResult execute_drop_view_statement(
      std::shared_ptr<DropViewStatement> stmt);
  QueryResult execute_create_function_statement(
      std::shared_ptr<CreateFunctionStatement> stmt);
  QueryResult execute_drop_function_statement(
      std::shared_ptr<DropFunctionStatement> stmt);
  QueryResult execute_create_procedure_statement(
      std::shared_ptr<CreateProcedureStatement> stmt);
  QueryResult execute_drop_procedure_statement(
      std::shared_ptr<DropProcedureStatement> stmt);
  QueryResult execute_call_statement(std::shared_ptr<CallStatement> stmt,
                                     SessionContext *session);
  QueryResult execute_create_trigger_statement(
      std::shared_ptr<CreateTriggerStatement> stmt);
  QueryResult execute_drop_trigger_statement(
      std::shared_ptr<DropTriggerStatement> stmt);
  QueryResult execute_begin(SessionContext *session);
  QueryResult execute_commit(SessionContext *session);
  QueryResult execute_rollback(SessionContext *session);
  QueryResult execute_prepare(std::shared_ptr<PrepareStatement> stmt,
                              SessionContext *session);
  QueryResult execute_execute_prepared(
      std::shared_ptr<ExecutePreparedStatement> stmt, SessionContext *session);
  QueryResult execute_deallocate(
      std::shared_ptr<DeallocatePreparedStatement> stmt,
      SessionContext *session);
  QueryResult execute_vacuum(std::shared_ptr<VacuumStatement> stmt);
  QueryResult execute_explain_statement(std::shared_ptr<ExplainStatement> stmt,
                                        SessionContext *session,
                                        const std::string &sql);

  void acquire_table_lock(SessionContext *session, const std::string &table,
                          LockMode mode);
  void acquire_row_lock(SessionContext *session, const std::string &table,
                        size_t row_index);
  void release_session_locks(SessionContext *session);
  uint64_t ensure_lock_txn_id(SessionContext *session);

  void register_foreign_keys_for_create(
      const std::shared_ptr<CreateTableStatement> &stmt);
  void register_checks_for_create(
      const std::shared_ptr<CreateTableStatement> &stmt);
  bool parent_has_key(const std::string &parent_table,
                      const std::vector<std::string> &parent_columns,
                      const std::vector<Value> &values) const;
  std::vector<size_t> find_child_row_indices(
      const std::string &child_table,
      const std::vector<std::string> &child_columns,
      const std::vector<Value> &values) const;
  void apply_referential_delete(const std::string &parent_table_name,
                                const Row &parent_row,
                                std::set<std::string> &visiting);
  void apply_referential_update(const std::string &parent_table_name,
                                const Row &old_row, const Row &new_row);
  void apply_set_default_on_child(Table &child, const ForeignKeyDefinition &fk,
                                  size_t row_index);
  bool is_parent_key_supported(const Table &parent,
                               const std::vector<std::string> &columns) const;

  std::vector<uint8_t> read_file_bytes(const std::string &path) const;
};

}  // namespace db
