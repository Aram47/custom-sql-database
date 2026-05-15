#pragma once

#include <memory>
#include <string>

#include "types/data_type.h"
#include "utils/exceptions.h"

namespace db {

class Column {
 public:
  Column(const std::string &name, DataType type, bool nullable = true,
         bool is_primary_key = false, bool is_unique = false);

  // Accessors
  const std::string &get_name() const;
  DataType get_type() const;
  bool is_nullable() const;
  bool is_primary_key() const;
  bool is_unique() const;

  // String representation for debugging
  std::string to_string() const;

  // Equality
  bool operator==(const Column &other) const;
  bool operator!=(const Column &other) const;

 private:
  std::string name_;
  DataType type_;
  bool nullable_;
  bool primary_key_;
  bool unique_;
};

}  // namespace db
