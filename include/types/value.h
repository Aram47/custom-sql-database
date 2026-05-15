#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>

#include "types/data_type.h"

namespace db {

// Variant that holds different types
using ValueVariant = std::variant<std::monostate,  // NULL value
                                  int64_t,         // INT
                                  double,          // FLOAT
                                  std::string,     // STRING
                                  bool             // BOOLEAN
                                  >;

class Value {
 public:
  // Constructors
  Value();  // NULL value
  explicit Value(int64_t val);
  explicit Value(int val);
  explicit Value(double val);
  explicit Value(const std::string &val);
  explicit Value(const char *val);
  explicit Value(bool val);

  // Copy and move semantics
  Value(const Value &) = default;
  Value &operator=(const Value &) = default;
  Value(Value &&) = default;
  Value &operator=(Value &&) = default;

  // Accessors
  bool is_null() const;
  bool is_int() const;
  bool is_float() const;
  bool is_string() const;
  bool is_bool() const;

  int64_t as_int() const;
  double as_float() const;
  std::string as_string() const;
  bool as_bool() const;

  // Type checking and conversion
  DataType get_type() const;
  std::string get_type_string() const;

  // Comparison operators
  bool operator==(const Value &other) const;
  bool operator!=(const Value &other) const;
  bool operator<(const Value &other) const;
  bool operator<=(const Value &other) const;
  bool operator>(const Value &other) const;
  bool operator>=(const Value &other) const;

  // Arithmetic operations
  Value operator+(const Value &other) const;
  Value operator-(const Value &other) const;
  Value operator*(const Value &other) const;
  Value operator/(const Value &other) const;

  // String representation
  std::string to_string() const;

  // Create value from string with type
  static Value from_string(const std::string &str, DataType type);

 private:
  ValueVariant data_;

  friend class TypeConverter;
};

}  // namespace db
