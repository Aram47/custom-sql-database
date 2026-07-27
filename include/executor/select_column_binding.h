#pragma once

#include <string>

namespace db {

struct SelectColumnBinding {
  std::string alias;
  std::string physical_table;
  std::string column_name;
};

}  // namespace db
