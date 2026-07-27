#pragma once

#include <memory>
#include <string>

#include "parser/ast.h"

namespace db {

/** Immutable view: name, defining SELECT text, and cached AST. */
class ViewDefinition {
 public:
  ViewDefinition(std::string name, std::string select_sql);

  const std::string &get_name() const;
  const std::string &get_select_sql() const;
  const std::shared_ptr<SelectStatement> &get_select() const;

  /** Returns a freshly parsed copy of the defining SELECT. */
  std::shared_ptr<SelectStatement> parse_select() const;

 private:
  std::string name_;
  std::string select_sql_;
  std::shared_ptr<SelectStatement> select_;
};

}  // namespace db
