#pragma once

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "core/bind_context.h"
#include "core/correlation_context.h"
#include "core/lock_manager.h"
#include "core/plan_cache.h"
#include "core/session_context.h"
#include "core/table.h"
#include "core/transaction_manager.h"
#include "core/view_catalog.h"
#include "executor/query_executor.h"
#include "parser/parser.h"
#include "storage/wal_manager.h"
#include "utils/exceptions.h"

namespace db {

/** Catalog of tables with query dispatch, transactions, and persistence. */
class Database {
 public:
  explicit Database(std::string storage_directory = "data",
                    int vacuum_interval_ms = 5000);
  ~Database();

  Database(const Database &) = delete;
  Database &operator=(const Database &) = delete;

  void load_from_disk();
  void set_vacuum_interval_ms(int vacuum_interval_ms);

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

 private:
  std::string storage_directory_;
  mutable std::recursive_mutex db_mutex_;
  std::map<std::string, std::unique_ptr<Table>> tables_;
  ViewCatalog view_catalog_;
  std::map<std::string, std::unique_ptr<Table>> ephemeral_tables_;
  int ephemeral_scope_depth_{0};
  uint64_t next_ephemeral_id_{0};
  PlanCache plan_cache_;
  LockManager lock_manager_;
  WalManager wal_manager_;
  TransactionManager transaction_manager_;
  CorrelationContext correlation_context_;
  const BindContext *active_bind_{nullptr};
  SessionContext *active_session_{nullptr};
  std::atomic<uint64_t> next_lock_id_{1};
  std::atomic<int> vacuum_interval_ms_{5000};
  std::atomic<bool> vacuum_stop_{false};
  std::condition_variable vacuum_cv_;
  std::mutex vacuum_mutex_;
  std::thread vacuum_thread_;

  QueryResult persist_dirty_tables(QueryResult result, bool in_transaction);
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
  QueryResult execute_insert_statement(std::shared_ptr<InsertStatement> stmt);
  QueryResult execute_update_statement(std::shared_ptr<UpdateStatement> stmt);
  QueryResult execute_delete_statement(std::shared_ptr<DeleteStatement> stmt);
  QueryResult execute_create_table_statement(
      std::shared_ptr<CreateTableStatement> stmt);
  QueryResult execute_drop_table_statement(
      std::shared_ptr<DropTableStatement> stmt);
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
