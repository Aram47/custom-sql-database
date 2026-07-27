#include "core/authorization.h"

#include <memory>

namespace db {
namespace {

bool can_reader_execute(const ParsedStatement &stmt) {
  if (std::holds_alternative<std::shared_ptr<SelectStatement>>(stmt)) {
    return true;
  }
  if (auto explain =
          std::get_if<std::shared_ptr<ExplainStatement>>(&stmt)) {
    return can_reader_execute((*explain)->get_inner());
  }
  if (std::holds_alternative<std::shared_ptr<BeginStatement>>(stmt) ||
      std::holds_alternative<std::shared_ptr<CommitStatement>>(stmt) ||
      std::holds_alternative<std::shared_ptr<RollbackStatement>>(stmt) ||
      std::holds_alternative<std::shared_ptr<DeallocatePreparedStatement>>(
          stmt)) {
    return true;
  }
  if (auto prepare =
          std::get_if<std::shared_ptr<PrepareStatement>>(&stmt)) {
    try {
      Parser parser((*prepare)->get_sql());
      return can_reader_execute(parser.parse_statement());
    } catch (...) {
      return false;
    }
  }
  return false;
}

}  // namespace

bool can_execute(Role role, const ParsedStatement &stmt) {
  if (role == Role::Admin) {
    return true;
  }
  return can_reader_execute(stmt);
}

}  // namespace db
