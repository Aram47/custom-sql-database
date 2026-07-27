#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include "core/role.h"
#include "core/transaction_manager.h"

namespace db {

/**
 * Per-connection session state: auth identity, transaction, prepared statements.
 */
class SessionContext {
 public:
  SessionContext();

  uint64_t get_session_id() const;
  bool is_in_transaction() const;
  void set_in_transaction(bool in_transaction);
  uint64_t get_transaction_id() const;
  void set_transaction_id(uint64_t txn_id);

  void set_snapshot(TransactionSnapshot snapshot);
  void clear_snapshot();
  const TransactionSnapshot *get_snapshot() const;

  void put_prepared(const std::string &name, const std::string &sql);
  bool get_prepared(const std::string &name, std::string *sql) const;
  bool remove_prepared(const std::string &name);
  void clear_prepared();

  bool is_authenticated() const;
  void set_authenticated(bool authenticated);
  const std::string &get_username() const;
  void set_username(std::string username);
  std::optional<Role> get_role() const;
  void set_role(Role role);
  void clear_auth();

 private:
  uint64_t session_id_;
  bool in_transaction_{false};
  uint64_t transaction_id_{0};
  std::optional<TransactionSnapshot> snapshot_;
  std::unordered_map<std::string, std::string> prepared_statements_;
  bool authenticated_{false};
  std::string username_;
  std::optional<Role> role_;
};

}  // namespace db
