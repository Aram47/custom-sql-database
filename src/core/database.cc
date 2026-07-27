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
#include "core/view_expander.h"
#include "executor/ddl_executor.h"
#include "executor/join_select_executor.h"
#include "executor/query_executor.h"
#include "parser/parser.h"
#include "planner/query_explainer.h"
#include "storage/persistence_manager.h"
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
    : storage_directory_(std::move(storage_directory)),
      wal_manager_(storage_directory_),
      vacuum_interval_ms_(vacuum_interval_ms) {
  start_vacuum_worker();
}

Database::~Database() { stop_vacuum_worker(); }

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
      return result;
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
    }
    wal_manager_.truncate();
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
  tables_[table_name] = std::make_unique<Table>(table_name);
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
        for (size_t idx : child_indices) {
          Row child_row = child_ptr->get_row(idx);
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
          child_ptr->update_row(idx, child_row);
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
      for (size_t idx : child_indices) {
        if (idx >= child_ptr->get_row_count()) {
          continue;
        }
        Row child_row = child_ptr->get_row(idx);
        apply_referential_delete(child_name, child_row, visiting);
        child_ptr->delete_row(idx);
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
        for (size_t idx : child_indices) {
          Row child_row = child_ptr->get_row(idx);
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
          child_ptr->update_row(idx, child_row);
        }
        continue;
      }
      if (fk.on_update == ReferentialAction::SetDefault) {
        for (size_t idx : child_indices) {
          apply_set_default_on_child(*child_ptr, fk, idx);
        }
        continue;
      }
      for (size_t idx : child_indices) {
        Row child_row = child_ptr->get_row(idx);
        for (size_t c = 0; c < fk.child_columns.size(); ++c) {
          const int child_col =
              child_ptr->get_column_index(fk.child_columns[c]);
          if (child_col >= 0) {
            child_row.set_value(static_cast<size_t>(child_col), new_values[c]);
          }
        }
        child_ptr->update_row(idx, child_row);
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

QueryResult Database::execute_create_table_statement(
    std::shared_ptr<CreateTableStatement> stmt) {
  if (view_catalog_.has_view(stmt->get_table_name())) {
    return QueryResult::error_result("Relation '" + stmt->get_table_name() +
                                     "' already exists as a view");
  }
  CreateTableExecutor executor(stmt, &tables_);
  QueryResult result = executor.execute();
  if (result.success) {
    try {
      register_foreign_keys_for_create(stmt);
      register_checks_for_create(stmt);
    } catch (const std::exception &e) {
      tables_.erase(stmt->get_table_name());
      return QueryResult::error_result(e.what());
    }
    mark_table_dirty(stmt->get_table_name());
    auto *table = get_table(stmt->get_table_name());
    if (table) {
      table->rebuild_indexes();
    }
  }
  return result;
}

QueryResult Database::execute_drop_table_statement(
    std::shared_ptr<DropTableStatement> stmt) {
  DropTableExecutor executor(stmt, &tables_, storage_directory_);
  return executor.execute();
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
