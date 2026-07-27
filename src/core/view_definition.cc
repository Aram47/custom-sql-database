#include "core/view_definition.h"

#include "parser/parser.h"
#include "utils/exceptions.h"

namespace db {

ViewDefinition::ViewDefinition(std::string name, std::string select_sql)
    : name_(std::move(name)), select_sql_(std::move(select_sql)) {
  try {
    select_ = parse_select();
  } catch (const ParseException &e) {
    throw ConstraintException("Invalid view SELECT for '" + name_ + "': " +
                              e.what());
  }
  if (!select_) {
    throw ConstraintException("View '" + name_ + "' requires a SELECT body");
  }
}

const std::string &ViewDefinition::get_name() const { return name_; }

const std::string &ViewDefinition::get_select_sql() const {
  return select_sql_;
}

const std::shared_ptr<SelectStatement> &ViewDefinition::get_select() const {
  return select_;
}

std::shared_ptr<SelectStatement> ViewDefinition::parse_select() const {
  Parser parser(select_sql_);
  return parser.parse_select_statement();
}

}  // namespace db
