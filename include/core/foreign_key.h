#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace db {

/** Action applied when a referenced parent key is deleted or updated. */
enum class ReferentialAction : uint8_t {
  Restrict = 0,
  Cascade = 1,
  SetNull = 2,
  SetDefault = 3
};

/** Declarative foreign key stored on the child table. */
struct ForeignKeyDefinition {
  std::vector<std::string> child_columns;
  std::string parent_table;
  std::vector<std::string> parent_columns;
  ReferentialAction on_delete{ReferentialAction::Restrict};
  ReferentialAction on_update{ReferentialAction::Restrict};
};

}  // namespace db
