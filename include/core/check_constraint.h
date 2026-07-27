#pragma once

#include <memory>
#include <string>

namespace db {

class Expression;
class Table;

/** Declarative CHECK constraint stored on a table. */
struct CheckConstraintDefinition {
  std::string name;
  std::string expression_text;
  std::shared_ptr<Expression> predicate;
};

/**
 * Returns true when the predicate is allowed for CHECK in v1
 * (no subqueries, no aggregate functions).
 */
bool is_check_expression_allowed(const std::shared_ptr<Expression> &predicate);

/** Parses a standalone expression from persisted or DDL text. */
std::shared_ptr<Expression> parse_check_expression(
    const std::string &expression_text);

/**
 * Validates the predicate, fills expression_text / auto-name, and checks
 * column references against the table schema.
 */
void prepare_check_constraint(Table *table, CheckConstraintDefinition &check);

}  // namespace db
