#pragma once

#include "core/role.h"
#include "parser/parser.h"

namespace db {

/**
 * Returns whether the role may execute the statement.
 * PREPARE is authorized against the prepared SQL text.
 * EXECUTE PREPARED must be authorized by the caller against the stored SQL
 * (this function returns true for Admin and false for Reader on Execute).
 */
bool can_execute(Role role, const ParsedStatement &stmt);

}  // namespace db
