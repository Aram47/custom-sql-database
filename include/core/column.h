#pragma once

#include <memory>
#include <optional>
#include <string>

#include "types/data_type.h"
#include "types/value.h"
#include "utils/exceptions.h"

namespace db {

/** Schema column with type and constraint flags. */
class Column {
 public:
  Column(const std::string &name, DataType type, bool nullable = true,
         bool is_primary_key = false, bool is_unique = false);

  const std::string &get_name() const;
  DataType get_type() const;
  bool is_nullable() const;
  bool is_primary_key() const;
  bool is_unique() const;
  bool has_default() const;
  const Value &get_default_value() const;

  void set_name(const std::string &name);
  void set_nullable(bool nullable);
  void set_primary_key(bool is_primary_key);
  void set_unique(bool is_unique);
  void set_default_value(const Value &value);
  void clear_default_value();

  std::string to_string() const;
  bool operator==(const Column &other) const;
  bool operator!=(const Column &other) const;

 private:
  std::string name_;
  DataType type_;
  bool nullable_;
  bool primary_key_;
  bool unique_;
  std::optional<Value> default_value_;
};

}  // namespace db
