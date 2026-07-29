#include "core/database.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>

#include "core/authorization.h"
#include "core/index_key.h"
#include "core/check_constraint.h"
#include "core/partition_catalog.h"
#include "core/unique_constraint.h"
#include "core/view_expander.h"
#include "executor/ddl_executor.h"
#include "executor/join_select_executor.h"
#include "executor/limit_offset_operator.h"
#include "executor/partition_prune.h"
#include "executor/procedure_executor.h"
#include "executor/query_executor.h"
#include "executor/scalar_function.h"
#include "executor/select_analysis.h"
#include "executor/select_column_binding.h"
#include "executor/select_expression_evaluator.h"
#include "executor/select_pipeline.h"
#include "executor/set_operation_operator.h"
#include "executor/sort_operator.h"
#include "executor/trigger_executor.h"
#include "parser/parser.h"
#include "planner/query_explainer.h"
#include "storage/persistence_manager.h"
#include "storage/page_store.h"
#include "types/data_type.h"
#include "utils/logger.h"

namespace fs = std::filesystem;

namespace db {
namespace {

bool is_statement_authorized(Role role, const ParsedStatement &stmt,
                             SessionContext *session) {
  if (auto exec_stmt =
          std::get_if<std::shared_ptr<ExecutePreparedStatement>>(&stmt)) {
    if (!session) {
      return role == Role::Admin;
    }
    std::string prepared_sql;
    if (!session->get_prepared((*exec_stmt)->get_name(), &prepared_sql)) {
      return true;
    }
    try {
      Parser parser(prepared_sql);
      return can_execute(role, parser.parse_statement());
    } catch (...) {
      return false;
    }
  }
  return can_execute(role, stmt);
}

}  // namespace

Database::Database(std::string storage_directory, int vacuum_interval_ms)
    : Database(std::move(storage_directory), vacuum_interval_ms, 64) {}

Database::Database(std::string storage_directory, int vacuum_interval_ms,
                   size_t buffer_pool_pages)
    : storage_directory_(std::move(storage_directory)),
      buffer_pool_pages_(buffer_pool_pages == 0 ? 64 : buffer_pool_pages),
      buffer_pool_(std::make_unique<BufferPool>(buffer_pool_pages_)),
      wal_manager_(storage_directory_),
      vacuum_interval_ms_(vacuum_interval_ms) {
  start_vacuum_worker();
}

Database::~Database() {
  stop_vacuum_worker();
  try {
    if (buffer_pool_) {
      buffer_pool_->flush_all();
    }
  } catch (...) {
  }
}

void Database::set_vacuum_interval_ms(int vacuum_interval_ms) {
  vacuum_interval_ms_.store(vacuum_interval_ms);
  vacuum_cv_.notify_all();
}

void Database::start_vacuum_worker() {
  if (vacuum_interval_ms_.load() <= 0) {
    return;
  }
  vacuum_stop_.store(false);
  vacuum_thread_ = std::thread([this]() { run_vacuum_worker(); });
}

void Database::stop_vacuum_worker() {
  vacuum_stop_.store(true);
  vacuum_cv_.notify_all();
  if (vacuum_thread_.joinable()) {
    vacuum_thread_.join();
  }
}

void Database::run_vacuum_worker() {
  while (!vacuum_stop_.load()) {
    const int interval_ms = vacuum_interval_ms_.load();
    if (interval_ms <= 0) {
      std::unique_lock<std::mutex> lock(vacuum_mutex_);
      vacuum_cv_.wait(lock, [this]() {
        return vacuum_stop_.load() || vacuum_interval_ms_.load() > 0;
      });
      continue;
    }
    {
      std::unique_lock<std::mutex> lock(vacuum_mutex_);
      vacuum_cv_.wait_for(lock, std::chrono::milliseconds(interval_ms),
                          [this]() { return vacuum_stop_.load(); });
    }
    if (vacuum_stop_.load()) {
      break;
    }
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    vacuum_all_tables();
  }
}

const std::string &Database::get_storage_directory() const {
  return storage_directory_;
}

CorrelationContext *Database::get_correlation_context() {
  return &correlation_context_;
}

const BindContext *Database::get_active_bind() const { return active_bind_; }

TransactionManager &Database::get_transaction_manager() {
  return transaction_manager_;
}

uint64_t Database::get_reader_xid(SessionContext *session) const {
  if (session && session->is_in_transaction()) {
    return session->get_transaction_id();
  }
  return 0;
}

const TransactionSnapshot *Database::get_reader_snapshot(
    SessionContext *session) const {
  if (session && session->is_in_transaction()) {
    return session->get_snapshot();
  }
  return nullptr;
}

SessionContext *Database::get_active_session() const { return active_session_; }

TableStatistics &Database::get_table_statistics() { return table_statistics_; }

const TableStatistics &Database::get_table_statistics() const {
  return table_statistics_;
}

void Database::ensureTableStatistics(const std::string &table_name) {
  Table *table = get_table(table_name);
  if (!table) {
    return;
  }
  const bool missing = !table_statistics_.hasTable(table_name);
  const bool stale =
      !missing &&
      table_statistics_.getRowCount(table_name) != table->get_row_count();
  if (missing || stale) {
    table_statistics_.refreshTable(*table);
  }
}

void Database::load_from_disk() {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  try {
    wal_manager_.recover();
    tables_ = PersistenceManager::load_database(storage_directory_);
    for (auto &[name, table] : tables_) {
      (void)name;
      table->clear_dirty();
    }
    view_catalog_.load_all(storage_directory_);
    routine_catalog_.loadAll(storage_directory_);
    trigger_catalog_.loadAll(storage_directory_);
    PartitionCatalog::loadAll(storage_directory_, tables_);
  } catch (const StorageException &e) {
    DB_LOG_ERROR("Failed to load database from disk: ", e.what());
    throw;
  }
}

void Database::clear_plan_cache() { plan_cache_.clear(); }

void Database::vacuum_all_tables() {
  for (auto &[name, table] : tables_) {
    (void)name;
    table->vacuum_versions(transaction_manager_);
    table_statistics_.refreshTable(*table);
  }
}

uint64_t Database::ensure_lock_txn_id(SessionContext *session) {
  if (!session) {
    return 0;
  }
  if (session->get_transaction_id() == 0) {
    session->set_transaction_id(next_lock_id_++);
  }
  return session->get_transaction_id();
}

void Database::acquire_table_lock(SessionContext *session,
                                  const std::string &table, LockMode mode) {
  if (!session) {
    return;
  }
  const uint64_t txn_id = ensure_lock_txn_id(session);
  lock_manager_.acquire(txn_id, table, mode);
}

void Database::acquire_row_lock(SessionContext *session,
                                const std::string &table, size_t row_index) {
  if (!session) {
    return;
  }
  const uint64_t txn_id = ensure_lock_txn_id(session);
  lock_manager_.acquire(txn_id, LockManager::make_row_lock_key(table, row_index),
                        LockMode::Exclusive);
}

void Database::release_session_locks(SessionContext *session) {
  if (!session || session->get_transaction_id() == 0) {
    return;
  }
  lock_manager_.release_all(session->get_transaction_id());
}

void Database::flush_dirty_tables_locked() {
  std::vector<std::pair<std::string, std::string>> staged;
  for (auto &[name, table] : tables_) {
    if (!table->is_dirty()) {
      continue;
    }
    const std::string temp_path =
        storage_directory_ + "/.wal_stage_" + name + ".db";
    PersistenceManager::save_table(*table, temp_path);
    staged.emplace_back(name, temp_path);
  }
  if (staged.empty()) {
    if (buffer_pool_) {
      buffer_pool_->flush_all();
    }
    return;
  }
  for (const auto &[name, temp_path] : staged) {
    wal_manager_.append_table_blob(name, read_file_bytes(temp_path));
  }
  wal_manager_.append_commit(0);
  wal_manager_.sync();
  for (const auto &[name, temp_path] : staged) {
    const std::string dest =
        PersistenceManager::table_file_path(storage_directory_, name);
    fs::rename(temp_path, dest);
    tables_[name]->clear_dirty();
    if (tables_[name]->isPartitioned()) {
      PartitionCatalog::saveParent(storage_directory_, name,
                                   *tables_[name]->getPartitionMetadata());
    }
  }
  if (buffer_pool_) {
    buffer_pool_->flush_all();
  }
  wal_manager_.truncate();
}

void Database::checkpoint() {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  flush_dirty_tables_locked();
}

QueryResult Database::persist_dirty_tables(QueryResult result,
                                           bool in_transaction) {
  if (!result.success) {
    return result;
  }
  if (in_transaction) {
    return result;
  }
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  try {
    flush_dirty_tables_locked();
    return result;
  } catch (const StorageException &e) {
    DB_LOG_ERROR("Persistence failed: ", e.what());
    return QueryResult::error_result(std::string("Persistence failed: ") +
                                     e.what());
  }
}

std::vector<uint8_t> Database::read_file_bytes(const std::string &path) const {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw StorageException("Cannot read file: " + path);
  }
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
}

void Database::mark_table_dirty(const std::string &table_name) {
  auto it = tables_.find(table_name);
  if (it != tables_.end()) {
    it->second->mark_dirty();
  }
}

QueryResult Database::execute_query(const std::string &sql,
                                    SessionContext *session) {
  active_session_ = session;
  try {
    DB_LOG_DEBUG("Executing query: ", sql);
    auto cached = plan_cache_.get(sql);
    ParsedStatement stmt;
    if (cached) {
      stmt = *cached;
    } else {
      Parser parser(sql);
      stmt = parser.parse_statement();
      if (!std::holds_alternative<std::shared_ptr<PrepareStatement>>(stmt) &&
          !std::holds_alternative<std::shared_ptr<BeginStatement>>(stmt) &&
          !std::holds_alternative<std::shared_ptr<CommitStatement>>(stmt) &&
          !std::holds_alternative<std::shared_ptr<RollbackStatement>>(stmt) &&
          !std::holds_alternative<std::shared_ptr<ExecutePreparedStatement>>(
              stmt) &&
          !std::holds_alternative<std::shared_ptr<DeallocatePreparedStatement>>(
              stmt) &&
          !std::holds_alternative<std::shared_ptr<VacuumStatement>>(stmt) &&
          !std::holds_alternative<std::shared_ptr<ExplainStatement>>(stmt)) {
        plan_cache_.put(sql, stmt);
      }
    }
    if (session && session->is_authenticated()) {
      const std::optional<Role> role = session->get_role();
      if (role && !is_statement_authorized(*role, stmt, session)) {
        active_session_ = nullptr;
        return QueryResult::error_result("permission denied");
      }
    }
    QueryResult result = dispatch_statement(stmt, session, sql);
    active_session_ = nullptr;
    return result;
  } catch (const ParseException &e) {
    active_session_ = nullptr;
    DB_LOG_ERROR("Parse error: ", e.what());
    return QueryResult::error_result(e.what());
  } catch (const DeadlockException &e) {
    active_session_ = nullptr;
    if (session && session->is_in_transaction()) {
      execute_rollback(session);
    } else if (session) {
      release_session_locks(session);
    }
    DB_LOG_ERROR("Deadlock: ", e.what());
    return QueryResult::error_result(e.what());
  } catch (const NotFoundException &e) {
    active_session_ = nullptr;
    DB_LOG_ERROR("Not found: ", e.what());
    return QueryResult::error_result(e.what());
  } catch (const ConstraintException &e) {
    active_session_ = nullptr;
    DB_LOG_ERROR("Constraint error: ", e.what());
    return QueryResult::error_result(e.what());
  } catch (const DatabaseException &e) {
    active_session_ = nullptr;
    DB_LOG_ERROR("Database error: ", e.what());
    return QueryResult::error_result(e.what());
  } catch (const std::exception &e) {
    active_session_ = nullptr;
    DB_LOG_ERROR("Unexpected error: ", e.what());
    return QueryResult::error_result(std::string("Unexpected error: ") +
                                     e.what());
  }
}

QueryResult Database::dispatch_statement(const ParsedStatement &stmt,
                                         SessionContext *session,
                                         const std::string &sql) {
  const bool in_tx = session && session->is_in_transaction();
  if (auto begin_stmt = std::get_if<std::shared_ptr<BeginStatement>>(&stmt)) {
    (void)begin_stmt;
    return execute_begin(session);
  }
  if (auto commit_stmt = std::get_if<std::shared_ptr<CommitStatement>>(&stmt)) {
    (void)commit_stmt;
    return execute_commit(session);
  }
  if (auto rollback_stmt =
          std::get_if<std::shared_ptr<RollbackStatement>>(&stmt)) {
    (void)rollback_stmt;
    return execute_rollback(session);
  }
  if (auto prepare_stmt = std::get_if<std::shared_ptr<PrepareStatement>>(&stmt)) {
    return execute_prepare(*prepare_stmt, session);
  }
  if (auto exec_stmt =
          std::get_if<std::shared_ptr<ExecutePreparedStatement>>(&stmt)) {
    return execute_execute_prepared(*exec_stmt, session);
  }
  if (auto dealloc_stmt =
          std::get_if<std::shared_ptr<DeallocatePreparedStatement>>(&stmt)) {
    return execute_deallocate(*dealloc_stmt, session);
  }
  auto run_locked = [&](auto &&fn) -> QueryResult {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    return fn();
  };
  if (auto vacuum_stmt = std::get_if<std::shared_ptr<VacuumStatement>>(&stmt)) {
    return run_locked([&]() { return execute_vacuum(*vacuum_stmt); });
  }
  if (auto explain_stmt = std::get_if<std::shared_ptr<ExplainStatement>>(&stmt)) {
    return execute_explain_statement(*explain_stmt, session, sql);
  }
  if (auto select_stmt = std::get_if<std::shared_ptr<SelectStatement>>(&stmt)) {
    QueryResult result = run_locked(
        [&]() { return execute_select_statement(*select_stmt); });
    return result;
  }
  if (auto set_op_stmt =
          std::get_if<std::shared_ptr<SetOperationStatement>>(&stmt)) {
    return run_locked(
        [&]() { return execute_set_operation_statement(*set_op_stmt); });
  }
  if (auto insert_stmt = std::get_if<std::shared_ptr<InsertStatement>>(&stmt)) {
    acquire_table_lock(session, (*insert_stmt)->get_table(),
                       LockMode::Exclusive);
    QueryResult result = run_locked(
        [&]() { return execute_insert_statement(*insert_stmt); });
    if (!in_tx) {
      release_session_locks(session);
    }
    return persist_dirty_tables(result, in_tx);
  }
  if (auto update_stmt = std::get_if<std::shared_ptr<UpdateStatement>>(&stmt)) {
    std::vector<size_t> row_indices;
    {
      std::lock_guard<std::recursive_mutex> lock(db_mutex_);
      Table *table = get_table((*update_stmt)->get_table());
      if (table) {
        UpdateExecutor preview(*update_stmt, table, this);
        row_indices = preview.collect_matching_indices();
      }
    }
    for (size_t row_index : row_indices) {
      acquire_row_lock(session, (*update_stmt)->get_table(), row_index);
    }
    QueryResult result = run_locked(
        [&]() { return execute_update_statement(*update_stmt); });
    if (!in_tx) {
      release_session_locks(session);
    }
    return persist_dirty_tables(result, in_tx);
  }
  if (auto delete_stmt = std::get_if<std::shared_ptr<DeleteStatement>>(&stmt)) {
    std::vector<size_t> row_indices;
    {
      std::lock_guard<std::recursive_mutex> lock(db_mutex_);
      Table *table = get_table((*delete_stmt)->get_table());
      if (table) {
        DeleteExecutor preview(*delete_stmt, table, this);
        row_indices = preview.collect_matching_indices();
      }
    }
    for (size_t row_index : row_indices) {
      acquire_row_lock(session, (*delete_stmt)->get_table(), row_index);
    }
    QueryResult result = run_locked(
        [&]() { return execute_delete_statement(*delete_stmt); });
    if (!in_tx) {
      release_session_locks(session);
    }
    return persist_dirty_tables(result, in_tx);
  }
  if (auto create_stmt =
          std::get_if<std::shared_ptr<CreateTableStatement>>(&stmt)) {
    clear_plan_cache();
    QueryResult result = run_locked(
        [&]() { return execute_create_table_statement(*create_stmt); });
    return persist_dirty_tables(result, in_tx);
  }
  if (auto drop_stmt = std::get_if<std::shared_ptr<DropTableStatement>>(&stmt)) {
    clear_plan_cache();
    acquire_table_lock(session, (*drop_stmt)->get_table_name(),
                       LockMode::Exclusive);
    QueryResult result = run_locked(
        [&]() { return execute_drop_table_statement(*drop_stmt); });
    if (!in_tx) {
      release_session_locks(session);
    }
    return result;
  }
  if (auto alter_stmt =
          std::get_if<std::shared_ptr<AlterTableStatement>>(&stmt)) {
    clear_plan_cache();
    acquire_table_lock(session, (*alter_stmt)->get_table_name(),
                       LockMode::Exclusive);
    QueryResult result = run_locked(
        [&]() { return execute_alter_table_statement(*alter_stmt); });
    if (!in_tx) {
      release_session_locks(session);
    }
    return persist_dirty_tables(result, in_tx);
  }
  if (auto create_index =
          std::get_if<std::shared_ptr<CreateIndexStatement>>(&stmt)) {
    clear_plan_cache();
    acquire_table_lock(session, (*create_index)->get_table_name(),
                       LockMode::Exclusive);
    QueryResult result = run_locked(
        [&]() { return execute_create_index_statement(*create_index); });
    if (!in_tx) {
      release_session_locks(session);
    }
    return persist_dirty_tables(result, in_tx);
  }
  if (auto drop_index =
          std::get_if<std::shared_ptr<DropIndexStatement>>(&stmt)) {
    clear_plan_cache();
    QueryResult result = run_locked(
        [&]() { return execute_drop_index_statement(*drop_index); });
    return persist_dirty_tables(result, in_tx);
  }
  if (auto create_view =
          std::get_if<std::shared_ptr<CreateViewStatement>>(&stmt)) {
    clear_plan_cache();
    QueryResult result = run_locked(
        [&]() { return execute_create_view_statement(*create_view); });
    return result;
  }
  if (auto drop_view = std::get_if<std::shared_ptr<DropViewStatement>>(&stmt)) {
    clear_plan_cache();
    QueryResult result =
        run_locked([&]() { return execute_drop_view_statement(*drop_view); });
    return result;
  }
  if (auto create_fn =
          std::get_if<std::shared_ptr<CreateFunctionStatement>>(&stmt)) {
    clear_plan_cache();
    return run_locked(
        [&]() { return execute_create_function_statement(*create_fn); });
  }
  if (auto drop_fn =
          std::get_if<std::shared_ptr<DropFunctionStatement>>(&stmt)) {
    clear_plan_cache();
    return run_locked(
        [&]() { return execute_drop_function_statement(*drop_fn); });
  }
  if (auto create_proc =
          std::get_if<std::shared_ptr<CreateProcedureStatement>>(&stmt)) {
    clear_plan_cache();
    return run_locked(
        [&]() { return execute_create_procedure_statement(*create_proc); });
  }
  if (auto drop_proc =
          std::get_if<std::shared_ptr<DropProcedureStatement>>(&stmt)) {
    clear_plan_cache();
    return run_locked(
        [&]() { return execute_drop_procedure_statement(*drop_proc); });
  }
  if (auto call_stmt = std::get_if<std::shared_ptr<CallStatement>>(&stmt)) {
    return execute_call_statement(*call_stmt, session);
  }
  if (auto create_trig =
          std::get_if<std::shared_ptr<CreateTriggerStatement>>(&stmt)) {
    clear_plan_cache();
    return run_locked(
        [&]() { return execute_create_trigger_statement(*create_trig); });
  }
  if (auto drop_trig =
          std::get_if<std::shared_ptr<DropTriggerStatement>>(&stmt)) {
    clear_plan_cache();
    return run_locked(
        [&]() { return execute_drop_trigger_statement(*drop_trig); });
  }
  if (std::holds_alternative<std::shared_ptr<SetNewStatement>>(stmt)) {
    return QueryResult::error_result(
        "SET NEW is only valid inside a BEFORE trigger body");
  }
  return QueryResult::error_result("Unknown statement type");
}

void Database::create_table(const std::string &table_name) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  if (tables_.count(table_name)) {
    throw ConstraintException("Table '" + table_name + "' already exists");
  }
  if (view_catalog_.has_view(table_name)) {
    throw ConstraintException("Relation '" + table_name +
                              "' already exists as a view");
  }
  tables_[table_name] = std::make_unique<Table>(
      table_name, Table::allocate_file_id(), *buffer_pool_,
      std::make_unique<MemoryPageStore>());
  tables_[table_name]->mark_dirty();
}

void Database::drop_table(const std::string &table_name) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  if (!tables_.count(table_name)) {
    throw NotFoundException("Table '" + table_name + "' not found");
  }
  tables_.erase(table_name);
  PersistenceManager::remove_table_file(storage_directory_, table_name);
}

Table *Database::get_table(const std::string &table_name) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  auto ephemeral = ephemeral_tables_.find(table_name);
  if (ephemeral != ephemeral_tables_.end()) {
    return ephemeral->second.get();
  }
  if (!tables_.count(table_name)) {
    return nullptr;
  }
  return tables_[table_name].get();
}

std::vector<std::string> Database::list_tables() const {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  std::vector<std::string> result;
  for (const auto &[name, table] : tables_) {
    (void)table;
    result.push_back(name);
  }
  return result;
}

bool Database::has_table(const std::string &table_name) const {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  return tables_.count(table_name) > 0;
}

bool Database::has_view(const std::string &view_name) const {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  return view_catalog_.has_view(view_name);
}

const ViewDefinition *Database::get_view(const std::string &view_name) const {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  return view_catalog_.get_view(view_name);
}

bool Database::has_ephemeral_table(const std::string &table_name) const {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  return ephemeral_tables_.count(table_name) > 0;
}

void Database::register_ephemeral_table(const std::string &name,
                                        std::unique_ptr<Table> table) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  ephemeral_tables_[name] = std::move(table);
}

std::string Database::allocate_ephemeral_table_name(
    const std::string &view_name) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  return "__view_" + view_name + "_" + std::to_string(++next_ephemeral_id_);
}

void Database::clear_ephemeral_tables() {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  ephemeral_tables_.clear();
}

void Database::enter_ephemeral_scope() {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  ++ephemeral_scope_depth_;
}

void Database::leave_ephemeral_scope() {
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  if (ephemeral_scope_depth_ > 0) {
    --ephemeral_scope_depth_;
  }
  if (ephemeral_scope_depth_ == 0) {
    ephemeral_tables_.clear();
  }
}

QueryResult Database::run_select(std::shared_ptr<SelectStatement> stmt) {
  return execute_select_statement(std::move(stmt));
}

Value Database::evaluate_scalar_subquery(
    const std::shared_ptr<SelectStatement> &stmt, std::string *error_msg) {
  QueryResult result = run_select(stmt);
  if (!result.success) {
    if (error_msg) *error_msg = result.message;
    return Value();
  }
  if (result.rows.empty()) {
    return Value();
  }
  if (result.rows.size() > 1 || result.rows[0].size() != 1) {
    if (error_msg) {
      *error_msg = "Scalar subquery must return at most one row and one column";
    }
    return Value();
  }
  return result.rows[0][0];
}

bool Database::evaluate_in_subquery(const Value &left,
                                    const std::shared_ptr<SelectStatement> &stmt,
                                    bool is_not, std::string *error_msg) {
  QueryResult result = run_select(stmt);
  if (!result.success) {
    if (error_msg) *error_msg = result.message;
    return false;
  }
  if (left.is_null()) {
    return false;
  }
  bool saw_null = false;
  bool found = false;
  for (const auto &row : result.rows) {
    if (row.empty()) {
      continue;
    }
    if (row[0].is_null()) {
      saw_null = true;
      continue;
    }
    if (row[0] == left) {
      found = true;
      break;
    }
  }
  if (found) {
    return !is_not;
  }
  if (saw_null) {
    return false;
  }
  return is_not;
}

bool Database::evaluate_exists_subquery(
    const std::shared_ptr<SelectStatement> &stmt, std::string *error_msg) {
  QueryResult result = run_select(stmt);
  if (!result.success) {
    if (error_msg) *error_msg = result.message;
    return false;
  }
  return !result.rows.empty();
}

QueryResult Database::execute_select_statement(
    std::shared_ptr<SelectStatement> stmt) {
  if (stmt->get_from_table().empty()) {
    return QueryResult::error_result("SELECT requires FROM clause");
  }
  enter_ephemeral_scope();
  struct EphemeralScopeGuard {
    Database *database;
    explicit EphemeralScopeGuard(Database *db) : database(db) {}
    ~EphemeralScopeGuard() { database->leave_ephemeral_scope(); }
  } guard(this);
  std::string expand_error;
  ViewExpander expander(this);
  std::shared_ptr<SelectStatement> expanded =
      expander.expand(stmt, &expand_error);
  if (!expanded) {
    return QueryResult::error_result(expand_error);
  }
  Table *base = get_table(expanded->get_from_table());
  if (!base) {
    return QueryResult::error_result("Table '" + expanded->get_from_table() +
                                     "' not found");
  }
  if (!expanded->get_joins().empty()) {
    JoinSelectExecutor executor(expanded, this);
    return executor.execute();
  }
  SelectExecutor executor(expanded, base, this);
  return executor.execute();
}

QueryResult Database::execute_query_operand(
    const SetOperationStatement::Operand &operand) {
  if (auto select = std::get_if<std::shared_ptr<SelectStatement>>(&operand)) {
    return execute_select_statement(*select);
  }
  return execute_set_operation_statement(
      std::get<std::shared_ptr<SetOperationStatement>>(operand));
}

QueryResult Database::execute_set_operation_statement(
    std::shared_ptr<SetOperationStatement> stmt) {
  if (!stmt) {
    return QueryResult::error_result("Empty set operation");
  }
  QueryResult leftResult = execute_query_operand(stmt->get_left());
  if (!leftResult.success) {
    return leftResult;
  }
  QueryResult rightResult = execute_query_operand(stmt->get_right());
  if (!rightResult.success) {
    return rightResult;
  }
  SetOperationOperator setOp;
  QueryResult combined = setOp.execute(leftResult, rightResult, stmt->get_kind(),
                                       stmt->is_all());
  if (!combined.success) {
    return combined;
  }
  // limit < 0 means unlimited; limit == 0 must still apply (empty result).
  if (stmt->get_order_by_columns().empty() && stmt->get_limit() < 0 &&
      stmt->get_offset() <= 0) {
    return combined;
  }
  auto orderStmt = std::make_shared<SelectStatement>();
  for (const auto &[expr, ascending] : stmt->get_order_by_columns()) {
    orderStmt->add_order_by_column(expr, ascending);
  }
  orderStmt->set_limit(stmt->get_limit());
  orderStmt->set_offset(stmt->get_offset());
  SelectPipelineContext ctx{orderStmt, evaluator_for_result_columns(combined)};
  SortOperator sort;
  combined = sort.apply(std::move(combined), ctx);
  if (!combined.success) {
    return combined;
  }
  LimitOffsetOperator limitOffset;
  return limitOffset.apply(std::move(combined), ctx);
}

QueryResult Database::execute_insert_statement(
    std::shared_ptr<InsertStatement> stmt) {
  Table *table = get_table(stmt->get_table());
  if (!table) {
    return QueryResult::error_result("Table '" + stmt->get_table() +
                                     "' not found");
  }
  InsertExecutor executor(stmt, table, this);
  return executor.execute();
}

QueryResult Database::execute_update_statement(
    std::shared_ptr<UpdateStatement> stmt) {
  Table *table = get_table(stmt->get_table());
  if (!table) {
    return QueryResult::error_result("Table '" + stmt->get_table() +
                                     "' not found");
  }
  if (table->isPartitioned()) {
    return execute_partitioned_update(stmt, table);
  }
  UpdateExecutor executor(stmt, table, this);
  return executor.execute();
}

QueryResult Database::execute_delete_statement(
    std::shared_ptr<DeleteStatement> stmt) {
  Table *table = get_table(stmt->get_table());
  if (!table) {
    return QueryResult::error_result("Table '" + stmt->get_table() +
                                     "' not found");
  }
  if (table->isPartitioned()) {
    return execute_partitioned_delete(stmt, table);
  }
  DeleteExecutor executor(stmt, table, this);
  return executor.execute();
}

bool Database::parent_has_key(const std::string &parent_table,
                              const std::vector<std::string> &parent_columns,
                              const std::vector<Value> &values) const {
  auto it = tables_.find(parent_table);
  if (it == tables_.end() || parent_columns.size() != values.size() ||
      parent_columns.empty()) {
    return false;
  }
  const Table *parent = it->second.get();
  if (parent_columns.size() == 1 && parent->has_index(parent_columns[0])) {
    return !parent->find_rows_by_value(parent_columns[0], values[0]).empty();
  }
  if (parent_columns.size() > 1) {
    for (const auto &[index_name, columns] : parent->get_secondary_indexes()) {
      if (columns == parent_columns) {
        IndexKey key(values);
        return !parent->find_rows_by_index_key(index_name, key).empty();
      }
    }
  }
  std::vector<int> indices;
  indices.reserve(parent_columns.size());
  for (const std::string &column_name : parent_columns) {
    const int col_idx = parent->get_column_index(column_name);
    if (col_idx < 0) {
      return false;
    }
    indices.push_back(col_idx);
  }
  for (size_t i = 0; i < parent->get_row_count(); ++i) {
    const Row &row = parent->get_row(i);
    bool matches = true;
    for (size_t c = 0; c < indices.size(); ++c) {
      if (row.get_value(static_cast<size_t>(indices[c])) != values[c]) {
        matches = false;
        break;
      }
    }
    if (matches) {
      return true;
    }
  }
  return false;
}

std::vector<size_t> Database::find_child_row_indices(
    const std::string &child_table,
    const std::vector<std::string> &child_columns,
    const std::vector<Value> &values) const {
  std::vector<size_t> result;
  auto it = tables_.find(child_table);
  if (it == tables_.end() || child_columns.size() != values.size() ||
      child_columns.empty()) {
    return result;
  }
  const Table *child = it->second.get();
  if (child_columns.size() == 1 && child->has_index(child_columns[0])) {
    return child->find_rows_by_value(child_columns[0], values[0]);
  }
  std::vector<int> indices;
  indices.reserve(child_columns.size());
  for (const std::string &column_name : child_columns) {
    const int col_idx = child->get_column_index(column_name);
    if (col_idx < 0) {
      return result;
    }
    indices.push_back(col_idx);
  }
  for (size_t i = 0; i < child->get_row_count(); ++i) {
    const Row &row = child->get_row(i);
    bool matches = true;
    for (size_t c = 0; c < indices.size(); ++c) {
      if (row.get_value(static_cast<size_t>(indices[c])) != values[c]) {
        matches = false;
        break;
      }
    }
    if (matches) {
      result.push_back(i);
    }
  }
  return result;
}

void Database::apply_set_default_on_child(Table &child,
                                          const ForeignKeyDefinition &fk,
                                          size_t row_index) {
  Row child_row = child.get_row(row_index);
  for (const std::string &column_name : fk.child_columns) {
    const int child_col = child.get_column_index(column_name);
    if (child_col < 0) {
      continue;
    }
    const Column &column = child.get_column(static_cast<size_t>(child_col));
    if (!column.has_default()) {
      throw ConstraintException(
          "FOREIGN KEY SET DEFAULT failed: column '" + column_name +
          "' has no DEFAULT");
    }
    child_row.set_value(static_cast<size_t>(child_col),
                        column.get_default_value());
  }
  child.update_row(row_index, child_row);
}

bool Database::is_parent_key_supported(
    const Table &parent, const std::vector<std::string> &columns) const {
  if (columns.empty()) {
    return false;
  }
  const auto &pk = parent.get_primary_key_columns();
  if (!pk.empty() && pk == columns) {
    return true;
  }
  for (const auto &uq : parent.get_unique_constraints()) {
    if (uq.columns == columns) {
      return true;
    }
  }
  if (columns.size() == 1) {
    const int col_idx = parent.get_column_index(columns[0]);
    if (col_idx < 0) {
      return false;
    }
    const Column &column = parent.get_column(static_cast<size_t>(col_idx));
    return column.is_primary_key() || column.is_unique();
  }
  for (const auto &[index_name, index_columns] : parent.get_secondary_indexes()) {
    (void)index_name;
    if (index_columns == columns) {
      return true;
    }
  }
  return false;
}

void Database::validate_foreign_keys_on_insert(const Table &table,
                                               const Row &row) {
  for (const auto &fk : table.get_foreign_keys()) {
    std::vector<Value> values;
    values.reserve(fk.child_columns.size());
    bool has_null = false;
    for (const std::string &column_name : fk.child_columns) {
      const int col_idx = table.get_column_index(column_name);
      if (col_idx < 0) {
        has_null = true;
        break;
      }
      const Value &value = row.get_value(static_cast<size_t>(col_idx));
      if (value.is_null()) {
        has_null = true;
        break;
      }
      values.push_back(value);
    }
    if (has_null) {
      continue;
    }
    if (!parent_has_key(fk.parent_table, fk.parent_columns, values)) {
      throw ConstraintException("FOREIGN KEY violation on columns of table '" +
                                table.get_name() + "'");
    }
  }
}

void Database::apply_referential_delete(const std::string &parent_table_name,
                                        const Row &parent_row,
                                        std::set<std::string> &visiting) {
  if (visiting.count(parent_table_name) > 0) {
    throw ConstraintException("FOREIGN KEY CASCADE cycle detected");
  }
  visiting.insert(parent_table_name);
  Table *parent = get_table(parent_table_name);
  if (!parent) {
    visiting.erase(parent_table_name);
    return;
  }
  for (auto &[child_name, child_ptr] : tables_) {
    if (child_name == parent_table_name) {
      continue;
    }
    for (const auto &fk : child_ptr->get_foreign_keys()) {
      if (fk.parent_table != parent_table_name) {
        continue;
      }
      std::vector<Value> parent_values;
      parent_values.reserve(fk.parent_columns.size());
      bool has_null = false;
      for (const std::string &column_name : fk.parent_columns) {
        const int parent_idx = parent->get_column_index(column_name);
        if (parent_idx < 0) {
          has_null = true;
          break;
        }
        const Value &val =
            parent_row.get_value(static_cast<size_t>(parent_idx));
        if (val.is_null()) {
          has_null = true;
          break;
        }
        parent_values.push_back(val);
      }
      if (has_null) {
        continue;
      }
      auto child_indices =
          find_child_row_indices(child_name, fk.child_columns, parent_values);
      if (child_indices.empty()) {
        continue;
      }
      if (fk.on_delete == ReferentialAction::Restrict) {
        throw ConstraintException(
            "FOREIGN KEY RESTRICT: cannot delete referenced key");
      }
      if (fk.on_delete == ReferentialAction::SetNull) {
        TriggerExecutor triggers(this, get_active_session());
        for (size_t idx : child_indices) {
          Row old_child = child_ptr->get_row(idx);
          Row child_row = old_child;
          for (const std::string &column_name : fk.child_columns) {
            const int child_col = child_ptr->get_column_index(column_name);
            if (child_col < 0) {
              continue;
            }
            if (!child_ptr->get_column(static_cast<size_t>(child_col))
                     .is_nullable()) {
              throw ConstraintException(
                  "FOREIGN KEY SET NULL failed: column '" + column_name +
                  "' is NOT NULL");
            }
            child_row.set_value(static_cast<size_t>(child_col), Value());
          }
          QueryResult before = triggers.executeBeforeUpdate(
              child_name, old_child, child_row);
          if (!before.success) {
            throw InvalidOperationException(before.message);
          }
          child_ptr->update_row(idx, child_row);
          QueryResult after =
              triggers.executeAfterUpdate(child_name, old_child, child_row);
          if (!after.success) {
            throw InvalidOperationException(after.message);
          }
        }
        continue;
      }
      if (fk.on_delete == ReferentialAction::SetDefault) {
        for (size_t idx : child_indices) {
          apply_set_default_on_child(*child_ptr, fk, idx);
        }
        continue;
      }
      std::sort(child_indices.begin(), child_indices.end(),
                std::greater<size_t>());
      TriggerExecutor triggers(this, get_active_session());
      for (size_t idx : child_indices) {
        if (idx >= child_ptr->get_row_count()) {
          continue;
        }
        Row child_row = child_ptr->get_row(idx);
        apply_referential_delete(child_name, child_row, visiting);
        QueryResult before =
            triggers.executeBeforeDelete(child_name, child_row);
        if (!before.success) {
          throw InvalidOperationException(before.message);
        }
        child_ptr->delete_row(idx);
        QueryResult after =
            triggers.executeAfterDelete(child_name, child_row);
        if (!after.success) {
          throw InvalidOperationException(after.message);
        }
      }
    }
  }
  visiting.erase(parent_table_name);
}

void Database::apply_referential_update(const std::string &parent_table_name,
                                        const Row &old_row,
                                        const Row &new_row) {
  Table *parent = get_table(parent_table_name);
  if (!parent) {
    return;
  }
  for (auto &[child_name, child_ptr] : tables_) {
    if (child_name == parent_table_name) {
      continue;
    }
    for (const auto &fk : child_ptr->get_foreign_keys()) {
      if (fk.parent_table != parent_table_name) {
        continue;
      }
      std::vector<Value> old_values;
      std::vector<Value> new_values;
      old_values.reserve(fk.parent_columns.size());
      new_values.reserve(fk.parent_columns.size());
      bool unchanged = true;
      bool has_null = false;
      for (const std::string &column_name : fk.parent_columns) {
        const int parent_idx = parent->get_column_index(column_name);
        if (parent_idx < 0) {
          has_null = true;
          break;
        }
        const Value &old_val =
            old_row.get_value(static_cast<size_t>(parent_idx));
        const Value &new_val =
            new_row.get_value(static_cast<size_t>(parent_idx));
        if (old_val.is_null()) {
          has_null = true;
          break;
        }
        if (old_val != new_val) {
          unchanged = false;
        }
        old_values.push_back(old_val);
        new_values.push_back(new_val);
      }
      if (has_null || unchanged) {
        continue;
      }
      auto child_indices =
          find_child_row_indices(child_name, fk.child_columns, old_values);
      if (child_indices.empty()) {
        continue;
      }
      if (fk.on_update == ReferentialAction::Restrict) {
        throw ConstraintException(
            "FOREIGN KEY RESTRICT: cannot update referenced key");
      }
      if (fk.on_update == ReferentialAction::SetNull) {
        TriggerExecutor triggers(this, get_active_session());
        for (size_t idx : child_indices) {
          Row old_child = child_ptr->get_row(idx);
          Row child_row = old_child;
          for (const std::string &column_name : fk.child_columns) {
            const int child_col = child_ptr->get_column_index(column_name);
            if (child_col < 0) {
              continue;
            }
            if (!child_ptr->get_column(static_cast<size_t>(child_col))
                     .is_nullable()) {
              throw ConstraintException(
                  "FOREIGN KEY SET NULL failed: column '" + column_name +
                  "' is NOT NULL");
            }
            child_row.set_value(static_cast<size_t>(child_col), Value());
          }
          QueryResult before = triggers.executeBeforeUpdate(
              child_name, old_child, child_row);
          if (!before.success) {
            throw InvalidOperationException(before.message);
          }
          child_ptr->update_row(idx, child_row);
          QueryResult after =
              triggers.executeAfterUpdate(child_name, old_child, child_row);
          if (!after.success) {
            throw InvalidOperationException(after.message);
          }
        }
        continue;
      }
      if (fk.on_update == ReferentialAction::SetDefault) {
        for (size_t idx : child_indices) {
          apply_set_default_on_child(*child_ptr, fk, idx);
        }
        continue;
      }
      TriggerExecutor triggers(this, get_active_session());
      for (size_t idx : child_indices) {
        Row old_child = child_ptr->get_row(idx);
        Row child_row = old_child;
        for (size_t c = 0; c < fk.child_columns.size(); ++c) {
          const int child_col =
              child_ptr->get_column_index(fk.child_columns[c]);
          if (child_col >= 0) {
            child_row.set_value(static_cast<size_t>(child_col), new_values[c]);
          }
        }
        QueryResult before =
            triggers.executeBeforeUpdate(child_name, old_child, child_row);
        if (!before.success) {
          throw InvalidOperationException(before.message);
        }
        child_ptr->update_row(idx, child_row);
        QueryResult after =
            triggers.executeAfterUpdate(child_name, old_child, child_row);
        if (!after.success) {
          throw InvalidOperationException(after.message);
        }
      }
    }
  }
}

void Database::validate_foreign_keys_on_update(Table &table, const Row &old_row,
                                               const Row &new_row) {
  validate_foreign_keys_on_insert(table, new_row);
  apply_referential_update(table.get_name(), old_row, new_row);
}

void Database::validate_foreign_keys_on_delete(Table &table, const Row &row) {
  std::set<std::string> visiting;
  apply_referential_delete(table.get_name(), row, visiting);
}

void Database::register_foreign_keys_for_create(
    const std::shared_ptr<CreateTableStatement> &stmt) {
  Table *table = get_table(stmt->get_table_name());
  if (!table) {
    return;
  }
  for (const auto &fk : stmt->get_foreign_keys()) {
    if (fk.child_columns.size() != fk.parent_columns.size() ||
        fk.child_columns.empty()) {
      throw ConstraintException("Invalid FOREIGN KEY column lists");
    }
    if (!has_table(fk.parent_table)) {
      throw ConstraintException("Referenced table '" + fk.parent_table +
                                "' does not exist");
    }
    Table *parent = get_table(fk.parent_table);
    for (const std::string &column_name : fk.parent_columns) {
      if (parent->get_column_index(column_name) < 0) {
        throw ConstraintException("Referenced column '" + column_name +
                                  "' not found");
      }
    }
    for (const std::string &column_name : fk.child_columns) {
      if (table->get_column_index(column_name) < 0) {
        throw ConstraintException("Foreign key column '" + column_name +
                                  "' not found");
      }
    }
    if (!is_parent_key_supported(*parent, fk.parent_columns)) {
      throw ConstraintException(
          "FOREIGN KEY parent columns must be PRIMARY KEY, UNIQUE, or covered "
          "by a secondary index");
    }
    table->add_foreign_key(fk);
  }
}

void Database::register_checks_for_create(
    const std::shared_ptr<CreateTableStatement> &stmt) {
  Table *table = get_table(stmt->get_table_name());
  if (!table) {
    return;
  }
  for (CheckConstraintDefinition check : stmt->get_checks()) {
    prepare_check_constraint(table, check);
    table->add_check(check);
  }
}

void Database::register_keys_for_create(
    const std::shared_ptr<CreateTableStatement> &stmt) {
  Table *table = get_table(stmt->get_table_name());
  if (!table) {
    return;
  }
  table->sync_key_metadata_from_column_flags();
  if (!stmt->get_primary_key_columns().empty()) {
    if (!table->get_primary_key_columns().empty()) {
      throw ConstraintException("Multiple PRIMARY KEY definitions");
    }
    table->apply_primary_key(stmt->get_primary_key_columns());
  }
  for (const auto &[name, columns] : stmt->get_unique_constraints()) {
    UniqueConstraintDefinition uq;
    uq.name = name;
    uq.columns = columns;
    table->apply_unique_constraint(uq);
  }
}

QueryResult Database::execute_create_table_statement(
    std::shared_ptr<CreateTableStatement> stmt) {
  if (stmt->isPartitionOf()) {
    return execute_create_partition_of(stmt);
  }
  if (view_catalog_.has_view(stmt->get_table_name())) {
    return QueryResult::error_result("Relation '" + stmt->get_table_name() +
                                     "' already exists as a view");
  }
  if (stmt->hasPartitionBy() && !stmt->get_foreign_keys().empty()) {
    return QueryResult::error_result(
        "FOREIGN KEY is not supported on partitioned parent tables");
  }
  CreateTableExecutor executor(stmt, &tables_);
  QueryResult result = executor.execute();
  if (result.success) {
    try {
      register_keys_for_create(stmt);
      register_foreign_keys_for_create(stmt);
      register_checks_for_create(stmt);
      if (stmt->hasPartitionBy()) {
        attachPartitionMetadata(get_table(stmt->get_table_name()), stmt);
      }
    } catch (const std::exception &e) {
      tables_.erase(stmt->get_table_name());
      return QueryResult::error_result(e.what());
    }
    mark_table_dirty(stmt->get_table_name());
    auto *table = get_table(stmt->get_table_name());
    if (table) {
      table->rebuild_indexes();
    }
    if (stmt->hasPartitionBy()) {
      try {
        persistPartitionMetadata(stmt->get_table_name());
      } catch (const std::exception &e) {
        return QueryResult::error_result(e.what());
      }
    }
  }
  return result;
}

void Database::attachPartitionMetadata(
    Table *table, const std::shared_ptr<CreateTableStatement> &stmt) {
  if (!table) {
    throw ConstraintException("Internal error: missing table for PARTITION BY");
  }
  if (table->get_column_index(stmt->getPartitionKeyColumn()) < 0) {
    throw ConstraintException("Partition key column '" +
                              stmt->getPartitionKeyColumn() + "' not found");
  }
  table->setPartitionMetadata(std::make_unique<PartitionedTableMetadata>(
      stmt->getPartitionKind(), stmt->getPartitionKeyColumn()));
}

void Database::persistPartitionMetadata(const std::string &parentName) {
  Table *table = get_table(parentName);
  if (!table || !table->isPartitioned()) {
    return;
  }
  PartitionCatalog::saveParent(storage_directory_, parentName,
                               *table->getPartitionMetadata());
}

QueryResult Database::execute_create_partition_of(
    std::shared_ptr<CreateTableStatement> stmt) {
  const std::string &childName = stmt->get_table_name();
  const std::string &parentName = stmt->getPartitionOfParent();
  if (tables_.count(childName) || view_catalog_.has_view(childName)) {
    return QueryResult::error_result("Relation '" + childName +
                                     "' already exists");
  }
  Table *parent = get_table(parentName);
  if (!parent) {
    return QueryResult::error_result("Parent table '" + parentName +
                                     "' not found");
  }
  if (!parent->isPartitioned()) {
    return QueryResult::error_result("Table '" + parentName +
                                     "' is not partitioned");
  }
  if (!parent->get_foreign_keys().empty()) {
    return QueryResult::error_result(
        "FOREIGN KEY is not supported on partitioned parent tables");
  }
  PartitionedTableMetadata *meta = parent->getMutablePartitionMetadata();
  const PartitionBound &bound = stmt->getPartitionBound();
  if (meta->getKind() == PartitionKind::Range && !bound.range) {
    return QueryResult::error_result(
        "RANGE parent requires FOR VALUES FROM (...) TO (...)");
  }
  if (meta->getKind() == PartitionKind::Hash && !bound.hash) {
    return QueryResult::error_result(
        "HASH parent requires FOR VALUES WITH (MODULUS ..., REMAINDER ...)");
  }
  try {
    auto child = std::make_unique<Table>(childName);
    for (const Column &col : parent->get_columns()) {
      child->add_column(col);
    }
    child->set_checks(parent->get_checks());
    child->set_secondary_indexes(parent->get_secondary_indexes());
    PartitionDescriptor descriptor;
    descriptor.childTableName = childName;
    descriptor.bound = bound;
    std::string error;
    if (!meta->addPartition(descriptor, &error)) {
      return QueryResult::error_result(error);
    }
    tables_[childName] = std::move(child);
    tables_[childName]->rebuild_indexes();
    mark_table_dirty(childName);
    mark_table_dirty(parentName);
    persistPartitionMetadata(parentName);
  } catch (const std::exception &e) {
    tables_.erase(childName);
    meta->removePartition(childName);
    return QueryResult::error_result(e.what());
  }
  return QueryResult::success_result("CREATE TABLE OK");
}

QueryResult Database::execute_drop_table_statement(
    std::shared_ptr<DropTableStatement> stmt) {
  const std::string &name = stmt->get_table_name();
  Table *table = get_table(name);
  if (!table) {
    return QueryResult::error_result("Table '" + name + "' not found");
  }
  if (table->isPartitioned()) {
    const auto partitions = table->getPartitionMetadata()->getPartitions();
    for (const PartitionDescriptor &part : partitions) {
      tables_.erase(part.childTableName);
      PersistenceManager::remove_table_file(storage_directory_,
                                            part.childTableName);
    }
    PartitionCatalog::removeParentFile(storage_directory_, name);
    DropTableExecutor executor(stmt, &tables_, storage_directory_);
    return executor.execute();
  }
  Table *parent = findPartitionParent(name);
  if (parent) {
    parent->getMutablePartitionMetadata()->removePartition(name);
    mark_table_dirty(parent->get_name());
    try {
      persistPartitionMetadata(parent->get_name());
    } catch (const std::exception &e) {
      return QueryResult::error_result(e.what());
    }
  }
  DropTableExecutor executor(stmt, &tables_, storage_directory_);
  return executor.execute();
}

Table *Database::resolvePartitionChild(Table *parent, const Row &row,
                                       std::string *error) {
  if (!parent || !parent->isPartitioned()) {
    if (error) {
      *error = "Table is not partitioned";
    }
    return nullptr;
  }
  const PartitionedTableMetadata *meta = parent->getPartitionMetadata();
  const int keyIndex = parent->get_column_index(meta->getKeyColumn());
  if (keyIndex < 0) {
    if (error) {
      *error = "Partition key column missing";
    }
    return nullptr;
  }
  const Value &key = row.get_value(static_cast<size_t>(keyIndex));
  auto router = meta->createRouter();
  std::optional<std::string> childName = router->resolveChild(key);
  if (!childName) {
    if (error) {
      *error = "No partition for partition key value " + key.to_string();
    }
    return nullptr;
  }
  Table *child = get_table(*childName);
  if (!child) {
    if (error) {
      *error = "Partition child '" + *childName + "' not found";
    }
    return nullptr;
  }
  return child;
}

std::vector<std::string> Database::listPrunedPartitions(
    Table *parent, const ExpressionPtr &whereExpr) const {
  if (!parent || !parent->isPartitioned()) {
    return {};
  }
  const PartitionedTableMetadata *meta = parent->getPartitionMetadata();
  PartitionPruneRequest request =
      buildPartitionPruneRequest(meta->getKeyColumn(), whereExpr);
  return meta->createRouter()->prune(request);
}

Table *Database::findPartitionParent(const std::string &childName) {
  for (auto &[name, table] : tables_) {
    (void)name;
    if (!table->isPartitioned()) {
      continue;
    }
    if (table->getPartitionMetadata()->hasChild(childName)) {
      return table.get();
    }
  }
  return nullptr;
}

std::vector<Row> Database::loadVisibleRowsForRelation(
    Table *table, const ExpressionPtr &pruneWhere) {
  if (!table) {
    return {};
  }
  if (!table->isPartitioned()) {
    SessionContext *session = get_active_session();
    return table->get_visible_rows(get_transaction_manager(),
                                   get_reader_xid(session),
                                   get_reader_snapshot(session));
  }
  std::vector<std::string> children = listPrunedPartitions(table, pruneWhere);
  std::vector<Row> rows;
  SessionContext *session = get_active_session();
  for (const std::string &childName : children) {
    Table *child = get_table(childName);
    if (!child) {
      continue;
    }
    std::vector<Row> childRows = child->get_visible_rows(
        get_transaction_manager(), get_reader_xid(session),
        get_reader_snapshot(session));
    rows.insert(rows.end(), childRows.begin(), childRows.end());
  }
  return rows;
}

QueryResult Database::execute_partitioned_update(
    std::shared_ptr<UpdateStatement> stmt, Table *parent) {
  const std::vector<std::string> children =
      listPrunedPartitions(parent, stmt->get_where_condition());
  int updatedCount = 0;
  for (const std::string &childName : children) {
    Table *child = get_table(childName);
    if (!child) {
      continue;
    }
    auto childStmt = std::make_shared<UpdateStatement>(childName);
    childStmt->set_where_condition(stmt->get_where_condition());
    for (const auto &clause : stmt->get_set_clauses()) {
      childStmt->add_set_clause(clause.first, clause.second);
    }
    UpdateExecutor probe(childStmt, child, this);
    std::vector<size_t> matching = probe.collect_matching_indices();
    std::sort(matching.begin(), matching.end(), std::greater<size_t>());
    std::vector<SelectColumnBinding> bindings;
    for (const Column &col : child->get_columns()) {
      bindings.push_back({childName, childName, col.get_name()});
    }
    SelectExpressionEvaluator eval(std::move(bindings));
    bind_subquery_evaluators(eval, this);
    TriggerExecutor triggers(this, get_active_session());
    for (size_t i : matching) {
      if (i >= child->get_row_count()) {
        continue;
      }
      Row current = child->get_row(i);
      Row newRow = current;
      for (const auto &[colName, expr] : stmt->get_set_clauses()) {
        int colIdx = child->get_column_index(colName);
        if (colIdx >= 0) {
          newRow.set_value(static_cast<size_t>(colIdx),
                           eval.evaluate_dml_assignment_rhs(current, expr));
        }
      }
      try {
        validate_foreign_keys_on_update(*child, current, newRow);
        QueryResult before = triggers.executeBeforeUpdate(
            parent->get_name(), current, newRow);
        if (!before.success) {
          return before;
        }
        std::string routeError;
        Table *target = resolvePartitionChild(parent, newRow, &routeError);
        if (!target) {
          return QueryResult::error_result(routeError);
        }
        if (target == child) {
          if (get_active_session() && get_active_session()->is_in_transaction()) {
            child->update_row_versioned(i, newRow,
                                        get_reader_xid(get_active_session()));
          } else {
            child->update_row(i, newRow);
          }
        } else {
          if (get_active_session() && get_active_session()->is_in_transaction()) {
            child->delete_row_versioned(i, get_reader_xid(get_active_session()));
            target->insert_row_versioned(newRow,
                                         get_reader_xid(get_active_session()));
          } else {
            child->delete_row(i);
            target->insert_row(newRow);
          }
        }
        QueryResult after =
            triggers.executeAfterUpdate(parent->get_name(), current, newRow);
        if (!after.success) {
          return after;
        }
        ++updatedCount;
      } catch (const std::exception &e) {
        return QueryResult::error_result(std::string("Update failed: ") +
                                         e.what());
      }
    }
  }
  QueryResult result = QueryResult::success_result("UPDATE OK");
  result.affected_rows = updatedCount;
  return result;
}

QueryResult Database::execute_partitioned_delete(
    std::shared_ptr<DeleteStatement> stmt, Table *parent) {
  const std::vector<std::string> children =
      listPrunedPartitions(parent, stmt->get_where_condition());
  int deletedCount = 0;
  TriggerExecutor triggers(this, get_active_session());
  const bool in_tx =
      get_active_session() && get_active_session()->is_in_transaction();
  for (const std::string &childName : children) {
    Table *child = get_table(childName);
    if (!child) {
      continue;
    }
    auto childStmt = std::make_shared<DeleteStatement>(childName);
    childStmt->set_where_condition(stmt->get_where_condition());
    DeleteExecutor probe(childStmt, child, this);
    std::vector<size_t> matching = probe.collect_matching_indices();
    std::sort(matching.begin(), matching.end(), std::greater<size_t>());
    for (size_t i : matching) {
      if (i >= child->get_row_count()) {
        continue;
      }
      try {
        Row row = child->get_row(i);
        validate_foreign_keys_on_delete(*child, row);
        QueryResult before =
            triggers.executeBeforeDelete(parent->get_name(), row);
        if (!before.success) {
          return before;
        }
        if (in_tx) {
          child->delete_row_versioned(i, get_reader_xid(get_active_session()));
        } else {
          child->delete_row(i);
        }
        QueryResult after =
            triggers.executeAfterDelete(parent->get_name(), row);
        if (!after.success) {
          return after;
        }
        ++deletedCount;
        mark_table_dirty(childName);
      } catch (const std::exception &e) {
        return QueryResult::error_result(std::string("Delete failed: ") +
                                         e.what());
      }
    }
  }
  QueryResult result = QueryResult::success_result("DELETE OK");
  result.affected_rows = deletedCount;
  return result;
}

QueryResult Database::execute_alter_table_statement(
    std::shared_ptr<AlterTableStatement> stmt) {
  AlterTableExecutor executor(stmt, &tables_, storage_directory_);
  return executor.execute();
}

QueryResult Database::execute_create_index_statement(
    std::shared_ptr<CreateIndexStatement> stmt) {
  Table *table = get_table(stmt->get_table_name());
  if (!table) {
    return QueryResult::error_result("Table '" + stmt->get_table_name() +
                                     "' not found");
  }
  for (const auto &[name, t] : tables_) {
    (void)name;
    if (t->has_secondary_index(stmt->get_index_name())) {
      return QueryResult::error_result("Index '" + stmt->get_index_name() +
                                       "' already exists");
    }
  }
  try {
    table->create_secondary_index(stmt->get_index_name(),
                                  stmt->get_column_names());
  } catch (const std::exception &e) {
    return QueryResult::error_result(e.what());
  }
  return QueryResult::success_result("CREATE INDEX OK");
}

QueryResult Database::execute_drop_index_statement(
    std::shared_ptr<DropIndexStatement> stmt) {
  for (auto &[name, table] : tables_) {
    (void)name;
    if (table->drop_secondary_index(stmt->get_index_name())) {
      return QueryResult::success_result("DROP INDEX OK");
    }
  }
  return QueryResult::error_result("Index '" + stmt->get_index_name() +
                                   "' not found");
}

QueryResult Database::execute_create_view_statement(
    std::shared_ptr<CreateViewStatement> stmt) {
  const std::string &view_name = stmt->get_view_name();
  if (has_table(view_name)) {
    return QueryResult::error_result("Relation '" + view_name +
                                     "' already exists as a table");
  }
  if (view_catalog_.has_view(view_name)) {
    return QueryResult::error_result("View '" + view_name + "' already exists");
  }
  try {
    view_catalog_.register_view(view_name, stmt->get_select_sql());
    view_catalog_.save_view(storage_directory_, view_name);
  } catch (const std::exception &e) {
    if (view_catalog_.has_view(view_name)) {
      view_catalog_.unregister_view(view_name);
    }
    return QueryResult::error_result(e.what());
  }
  return QueryResult::success_result("CREATE VIEW OK");
}

QueryResult Database::execute_drop_view_statement(
    std::shared_ptr<DropViewStatement> stmt) {
  const std::string &view_name = stmt->get_view_name();
  if (!view_catalog_.has_view(view_name)) {
    if (stmt->is_if_exists()) {
      return QueryResult::success_result("DROP VIEW OK");
    }
    return QueryResult::error_result("View '" + view_name + "' not found");
  }
  try {
    view_catalog_.unregister_view(view_name);
    view_catalog_.remove_view_file(storage_directory_, view_name);
  } catch (const std::exception &e) {
    return QueryResult::error_result(e.what());
  }
  return QueryResult::success_result("DROP VIEW OK");
}

RoutineCatalog &Database::get_routine_catalog() { return routine_catalog_; }

const RoutineCatalog &Database::get_routine_catalog() const {
  return routine_catalog_;
}

TriggerCatalog &Database::get_trigger_catalog() { return trigger_catalog_; }

const TriggerCatalog &Database::get_trigger_catalog() const {
  return trigger_catalog_;
}

int Database::get_trigger_depth() const {
  if (active_session_) {
    return active_session_->get_trigger_depth();
  }
  return trigger_depth_;
}

void Database::enter_trigger_depth() {
  if (active_session_) {
    active_session_->enter_trigger();
    return;
  }
  ++trigger_depth_;
}

void Database::leave_trigger_depth() {
  if (active_session_) {
    active_session_->leave_trigger();
    return;
  }
  if (trigger_depth_ > 0) {
    --trigger_depth_;
  }
}

QueryResult Database::run_parsed_statement(const ParsedStatement &stmt,
                                           SessionContext *session,
                                           const std::string &sql) {
  return dispatch_statement(stmt, session, sql);
}

void Database::set_active_local_variables(
    std::unordered_map<std::string, Value> locals) {
  active_local_variables_ = std::move(locals);
  has_active_local_variables_ = true;
}

void Database::clear_active_local_variables() {
  active_local_variables_.clear();
  has_active_local_variables_ = false;
}

const std::unordered_map<std::string, Value> *
Database::get_active_local_variables() const {
  if (!has_active_local_variables_) {
    return nullptr;
  }
  return &active_local_variables_;
}

QueryResult Database::execute_create_function_statement(
    std::shared_ptr<CreateFunctionStatement> stmt) {
  const std::string &name = stmt->get_name();
  if (ScalarFunctionRegistry::instance().hasFunction(name)) {
    return QueryResult::error_result("Cannot override builtin function '" +
                                     name + "'");
  }
  if (expression_has_aggregate(stmt->get_body())) {
    return QueryResult::error_result(
        "Aggregate functions are not allowed in scalar function body");
  }
  std::vector<SelectColumnBinding> bindings;
  for (const RoutineParamAst &param : stmt->get_params()) {
    bindings.push_back({"", "", param.name});
  }
  SelectExpressionEvaluator validator(bindings);
  if (auto err = validator.validate_expression_tree(stmt->get_body())) {
    return QueryResult::error_result(*err);
  }
  try {
    std::vector<RoutineParameter> params;
    for (const RoutineParamAst &param : stmt->get_params()) {
      params.push_back({param.name, string_to_data_type(param.type_name)});
    }
    routine_catalog_.registerFunction(std::make_unique<FunctionDefinition>(
        name, std::move(params), string_to_data_type(stmt->get_return_type()),
        stmt->get_body(), stmt->get_source_sql()));
    routine_catalog_.saveFunction(storage_directory_, name);
  } catch (const std::exception &e) {
    if (routine_catalog_.hasFunction(name)) {
      routine_catalog_.unregisterFunction(name);
    }
    return QueryResult::error_result(e.what());
  }
  return QueryResult::success_result("CREATE FUNCTION OK");
}

QueryResult Database::execute_drop_function_statement(
    std::shared_ptr<DropFunctionStatement> stmt) {
  const std::string &name = stmt->get_name();
  if (!routine_catalog_.hasFunction(name)) {
    if (stmt->is_if_exists()) {
      return QueryResult::success_result("DROP FUNCTION OK");
    }
    return QueryResult::error_result("Function '" + name + "' not found");
  }
  try {
    routine_catalog_.unregisterFunction(name);
    routine_catalog_.removeFunctionFile(storage_directory_, name);
  } catch (const std::exception &e) {
    return QueryResult::error_result(e.what());
  }
  return QueryResult::success_result("DROP FUNCTION OK");
}

QueryResult Database::execute_create_procedure_statement(
    std::shared_ptr<CreateProcedureStatement> stmt) {
  const std::string &name = stmt->get_name();
  try {
    std::vector<RoutineParameter> params;
    for (const RoutineParamAst &param : stmt->get_params()) {
      params.push_back({param.name, string_to_data_type(param.type_name)});
    }
    routine_catalog_.registerProcedure(std::make_unique<ProcedureDefinition>(
        name, std::move(params), stmt->get_statement_sqls(),
        stmt->get_source_sql()));
    routine_catalog_.saveProcedure(storage_directory_, name);
  } catch (const std::exception &e) {
    if (routine_catalog_.hasProcedure(name)) {
      routine_catalog_.unregisterProcedure(name);
    }
    return QueryResult::error_result(e.what());
  }
  return QueryResult::success_result("CREATE PROCEDURE OK");
}

QueryResult Database::execute_drop_procedure_statement(
    std::shared_ptr<DropProcedureStatement> stmt) {
  const std::string &name = stmt->get_name();
  if (!routine_catalog_.hasProcedure(name)) {
    if (stmt->is_if_exists()) {
      return QueryResult::success_result("DROP PROCEDURE OK");
    }
    return QueryResult::error_result("Procedure '" + name + "' not found");
  }
  try {
    routine_catalog_.unregisterProcedure(name);
    routine_catalog_.removeProcedureFile(storage_directory_, name);
  } catch (const std::exception &e) {
    return QueryResult::error_result(e.what());
  }
  return QueryResult::success_result("DROP PROCEDURE OK");
}

QueryResult Database::execute_call_statement(
    std::shared_ptr<CallStatement> stmt, SessionContext *session) {
  const ProcedureDefinition *proc =
      routine_catalog_.getProcedure(stmt->get_name());
  if (!proc) {
    return QueryResult::error_result("Procedure '" + stmt->get_name() +
                                     "' not found");
  }
  SelectExpressionEvaluator arg_eval({});
  arg_eval.set_routine_catalog(&routine_catalog_);
  arg_eval.set_correlation_context(&correlation_context_);
  if (const auto *locals = get_active_local_variables()) {
    arg_eval.set_local_variables(*locals);
  }
  std::vector<Value> args;
  args.reserve(stmt->get_arguments().size());
  for (const ExpressionPtr &arg_expr : stmt->get_arguments()) {
    std::string err;
    args.push_back(arg_eval.evaluate_expression(Row(), arg_expr, &err));
    if (!err.empty()) {
      return QueryResult::error_result(err);
    }
  }
  ProcedureExecutor executor(this, session);
  return executor.executeCall(*proc, args);
}

QueryResult Database::execute_create_trigger_statement(
    std::shared_ptr<CreateTriggerStatement> stmt) {
  const std::string &name = stmt->get_name();
  if (!has_table(stmt->get_table_name())) {
    return QueryResult::error_result("Table '" + stmt->get_table_name() +
                                     "' not found");
  }
  try {
    trigger_catalog_.registerTrigger(std::make_unique<TriggerDefinition>(
        name, stmt->get_table_name(), stmt->get_timing(), stmt->get_event(),
        stmt->get_statement_sqls(), stmt->get_source_sql()));
    trigger_catalog_.saveTrigger(storage_directory_, name);
  } catch (const std::exception &e) {
    if (trigger_catalog_.hasTrigger(name)) {
      trigger_catalog_.unregisterTrigger(name);
    }
    return QueryResult::error_result(e.what());
  }
  return QueryResult::success_result("CREATE TRIGGER OK");
}

QueryResult Database::execute_drop_trigger_statement(
    std::shared_ptr<DropTriggerStatement> stmt) {
  const std::string &name = stmt->get_name();
  if (!trigger_catalog_.hasTrigger(name)) {
    if (stmt->is_if_exists()) {
      return QueryResult::success_result("DROP TRIGGER OK");
    }
    return QueryResult::error_result("Trigger '" + name + "' not found");
  }
  try {
    const std::string stored_name =
        trigger_catalog_.getTrigger(name)->getName();
    trigger_catalog_.unregisterTrigger(name);
    trigger_catalog_.removeTriggerFile(storage_directory_, stored_name);
  } catch (const std::exception &e) {
    return QueryResult::error_result(e.what());
  }
  return QueryResult::success_result("DROP TRIGGER OK");
}

QueryResult Database::execute_begin(SessionContext *session) {
  if (!session) {
    return QueryResult::error_result("BEGIN requires a session");
  }
  if (session->is_in_transaction()) {
    return QueryResult::error_result("Transaction already active");
  }
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  const uint64_t txn_id = transaction_manager_.beginTransaction();
  session->set_transaction_id(txn_id);
  TransactionSnapshot snapshot = transaction_manager_.captureSnapshot();
  transaction_manager_.registerSnapshot(txn_id, snapshot);
  session->set_snapshot(std::move(snapshot));
  session->set_in_transaction(true);
  return QueryResult::success_result("BEGIN OK");
}

QueryResult Database::execute_commit(SessionContext *session) {
  if (!session || !session->is_in_transaction()) {
    return QueryResult::error_result("No active transaction");
  }
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  const uint64_t txn_id = session->get_transaction_id();
  transaction_manager_.commitTransaction(txn_id);
  vacuum_all_tables();
  // Freeze versioned rows so COMMIT survives restart without RAM statuses_.
  // Skip while older snapshots still need xid visibility (SI safety).
  if (transaction_manager_.canFreezeCommitted(txn_id)) {
    for (auto &[name, table] : tables_) {
      (void)name;
      table->freeze_committed_versions(txn_id);
    }
  }
  QueryResult result = persist_dirty_tables(QueryResult::success_result("COMMIT OK"),
                                            false);
  if (!result.success) {
    return result;
  }
  release_session_locks(session);
  session->set_in_transaction(false);
  session->set_transaction_id(0);
  session->clear_snapshot();
  return result;
}

QueryResult Database::execute_rollback(SessionContext *session) {
  if (!session || !session->is_in_transaction()) {
    return QueryResult::error_result("No active transaction");
  }
  std::lock_guard<std::recursive_mutex> lock(db_mutex_);
  const uint64_t txn_id = session->get_transaction_id();
  transaction_manager_.abortTransaction(txn_id);
  vacuum_all_tables();
  release_session_locks(session);
  session->set_in_transaction(false);
  session->set_transaction_id(0);
  session->clear_snapshot();
  return QueryResult::success_result("ROLLBACK OK");
}

QueryResult Database::execute_prepare(std::shared_ptr<PrepareStatement> stmt,
                                      SessionContext *session) {
  if (!session) {
    return QueryResult::error_result("PREPARE requires a session");
  }
  Parser check(stmt->get_sql());
  check.parse_statement();
  session->put_prepared(stmt->get_name(), stmt->get_sql());
  plan_cache_.put(stmt->get_sql(), Parser(stmt->get_sql()).parse_statement());
  return QueryResult::success_result("PREPARE OK");
}

QueryResult Database::execute_execute_prepared(
    std::shared_ptr<ExecutePreparedStatement> stmt, SessionContext *session) {
  if (!session) {
    return QueryResult::error_result("EXECUTE requires a session");
  }
  std::string sql;
  if (!session->get_prepared(stmt->get_name(), &sql)) {
    return QueryResult::error_result("Prepared statement '" + stmt->get_name() +
                                     "' not found");
  }
  Parser parser(sql);
  ParsedStatement parsed = parser.parse_statement();
  if (parser.get_parameter_count() != stmt->get_arguments().size()) {
    return QueryResult::error_result(
        "EXECUTE argument count does not match PREPARE parameters");
  }
  BindContext bind(stmt->get_arguments());
  active_bind_ = &bind;
  QueryResult result = dispatch_statement(parsed, session, sql);
  active_bind_ = nullptr;
  return result;
}

QueryResult Database::execute_deallocate(
    std::shared_ptr<DeallocatePreparedStatement> stmt,
    SessionContext *session) {
  if (!session) {
    return QueryResult::error_result("DEALLOCATE requires a session");
  }
  if (!session->remove_prepared(stmt->get_name())) {
    return QueryResult::error_result("Prepared statement '" + stmt->get_name() +
                                     "' not found");
  }
  return QueryResult::success_result("DEALLOCATE OK");
}

QueryResult Database::execute_vacuum(std::shared_ptr<VacuumStatement> stmt) {
  if (stmt->get_table_name().empty()) {
    vacuum_all_tables();
    return QueryResult::success_result("VACUUM OK");
  }
  Table *table = get_table(stmt->get_table_name());
  if (!table) {
    return QueryResult::error_result("Table '" + stmt->get_table_name() +
                                     "' not found");
  }
  table->vacuum_versions(transaction_manager_);
  table_statistics_.refreshTable(*table);
  return QueryResult::success_result("VACUUM OK");
}

QueryResult Database::execute_explain_statement(
    std::shared_ptr<ExplainStatement> stmt, SessionContext *session,
    const std::string &sql) {
  (void)sql;
  if (!stmt) {
    return QueryResult::error_result("Internal error: empty EXPLAIN");
  }
  QueryExplainer explainer(this);
  std::vector<std::string> planLines =
      explainer.buildPlanLines(stmt->get_inner());
  QueryResult innerResult =
      dispatch_statement(stmt->get_inner(), session, sql);
  if (!innerResult.success) {
    return innerResult;
  }
  if (!innerResult.column_names.empty()) {
    planLines.push_back("Rows returned: " +
                        std::to_string(innerResult.rows.size()));
  } else {
    planLines.push_back("Affected rows: " +
                        std::to_string(innerResult.affected_rows));
  }
  QueryResult result;
  result.success = true;
  result.message = "EXPLAIN OK";
  result.column_names = {"QUERY PLAN"};
  result.rows.reserve(planLines.size());
  for (const std::string &line : planLines) {
    result.rows.push_back({Value(line)});
  }
  return result;
}

}  // namespace db
