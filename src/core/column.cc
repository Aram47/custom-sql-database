#include "core/column.h"

namespace db {

Column::Column(const std::string &name, DataType type, bool nullable,
               bool is_primary_key, bool is_unique)
    : name_(name),
      type_(type),
      nullable_(nullable),
      primary_key_(is_primary_key),
      unique_(is_unique) {
  // Primary key columns cannot be nullable
  if (is_primary_key && nullable) {
    throw ConstraintException("Primary key column cannot be nullable");
  }
}

const std::string &Column::get_name() const { return name_; }

DataType Column::get_type() const { return type_; }

bool Column::is_nullable() const { return nullable_; }

bool Column::is_primary_key() const { return primary_key_; }

bool Column::is_unique() const { return unique_; }

std::string Column::to_string() const {
  std::string str = name_ + " " + data_type_to_string(type_);
  if (!nullable_) str += " NOT NULL";
  if (primary_key_) str += " PRIMARY KEY";
  if (unique_) str += " UNIQUE";
  return str;
}

bool Column::operator==(const Column &other) const {
  return name_ == other.name_ && type_ == other.type_;
}

bool Column::operator!=(const Column &other) const { return !(*this == other); }

}  // namespace db
