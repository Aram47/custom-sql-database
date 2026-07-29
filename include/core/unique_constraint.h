#pragma once

#include <string>
#include <vector>

namespace db {

/** Declarative table-level UNIQUE constraint (one or more columns). */
struct UniqueConstraintDefinition {
  std::string name;
  std::vector<std::string> columns;
};

}  // namespace db
