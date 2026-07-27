#include "core/session_context.h"

#include <atomic>

namespace db {

namespace {
std::atomic<uint64_t> next_session_id{1};
}

SessionContext::SessionContext() : session_id_(next_session_id++) {}

uint64_t SessionContext::get_session_id() const { return session_id_; }

bool SessionContext::is_in_transaction() const { return in_transaction_; }

void SessionContext::set_in_transaction(bool in_transaction) {
  in_transaction_ = in_transaction;
}

uint64_t SessionContext::get_transaction_id() const { return transaction_id_; }

void SessionContext::set_transaction_id(uint64_t txn_id) {
  transaction_id_ = txn_id;
}

void SessionContext::set_snapshot(TransactionSnapshot snapshot) {
  snapshot_ = std::move(snapshot);
}

void SessionContext::clear_snapshot() { snapshot_.reset(); }

const TransactionSnapshot *SessionContext::get_snapshot() const {
  if (!snapshot_) {
    return nullptr;
  }
  return &(*snapshot_);
}

void SessionContext::put_prepared(const std::string &name,
                                  const std::string &sql) {
  prepared_statements_[name] = sql;
}

bool SessionContext::get_prepared(const std::string &name,
                                  std::string *sql) const {
  auto it = prepared_statements_.find(name);
  if (it == prepared_statements_.end()) {
    return false;
  }
  if (sql) {
    *sql = it->second;
  }
  return true;
}

bool SessionContext::remove_prepared(const std::string &name) {
  return prepared_statements_.erase(name) > 0;
}

void SessionContext::clear_prepared() { prepared_statements_.clear(); }

bool SessionContext::is_authenticated() const { return authenticated_; }

void SessionContext::set_authenticated(bool authenticated) {
  authenticated_ = authenticated;
}

const std::string &SessionContext::get_username() const { return username_; }

void SessionContext::set_username(std::string username) {
  username_ = std::move(username);
}

std::optional<Role> SessionContext::get_role() const { return role_; }

void SessionContext::set_role(Role role) { role_ = role; }

void SessionContext::clear_auth() {
  authenticated_ = false;
  username_.clear();
  role_.reset();
}

}  // namespace db
