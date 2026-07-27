#pragma once

#include <memory>
#include <set>
#include <string>

#include "parser/ast.h"

namespace db {

class Database;

/** Maximum nesting depth for views referencing other views. */
constexpr int MAX_VIEW_NESTING_DEPTH = 8;

/**
 * Expands view references in FROM/JOIN by materializing each view into an
 * ephemeral table and rewriting the table name (alias preserved).
 */
class ViewExpander {
 public:
  explicit ViewExpander(Database *database);

  /**
   * Returns a shallow-cloned SELECT with views replaced by ephemeral tables.
   * On failure, error_message is set and nullptr is returned.
   */
  std::shared_ptr<SelectStatement> expand(
      const std::shared_ptr<SelectStatement> &stmt, std::string *error_message);

 private:
  Database *database_;

  std::shared_ptr<SelectStatement> clone_select(
      const std::shared_ptr<SelectStatement> &stmt) const;
  std::shared_ptr<SelectStatement> expand_internal(
      const std::shared_ptr<SelectStatement> &stmt,
      std::set<std::string> *visiting, int depth, std::string *error_message);
  bool expand_table_ref(std::string *table_name, std::set<std::string> *visiting,
                        int depth, std::string *error_message);
  bool materialize_view(const std::string &view_name, std::string *table_name,
                        std::set<std::string> *visiting, int depth,
                        std::string *error_message);
};

}  // namespace db
