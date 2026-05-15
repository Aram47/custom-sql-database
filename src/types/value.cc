#include "types/value.h"

#include <cmath>
#include <sstream>

#include "types/type_converter.h"
#include "utils/exceptions.h"

namespace db {

// Constructors
Value::Value() : data_(std::monostate{}) {}

Value::Value(int64_t val) : data_(val) {}

Value::Value(int val) : data_(static_cast<int64_t>(val)) {}

Value::Value(double val) : data_(val) {}

Value::Value(const std::string &val) : data_(val) {}

Value::Value(const char *val) : data_(std::string(val)) {}

Value::Value(bool val) : data_(val) {}

// Accessors
bool Value::is_null() const {
  return std::holds_alternative<std::monostate>(data_);
}

bool Value::is_int() const { return std::holds_alternative<int64_t>(data_); }

bool Value::is_float() const { return std::holds_alternative<double>(data_); }

bool Value::is_string() const {
  return std::holds_alternative<std::string>(data_);
}

bool Value::is_bool() const { return std::holds_alternative<bool>(data_); }

int64_t Value::as_int() const {
  if (is_int()) return std::get<int64_t>(data_);
  if (is_float()) return static_cast<int64_t>(std::get<double>(data_));
  if (is_string()) {
    try {
      return std::stoll(std::get<std::string>(data_));
    } catch (...) {
      throw TypeException("Cannot convert string to int: " +
                          std::get<std::string>(data_));
    }
  }
  if (is_bool()) return std::get<bool>(data_) ? 1 : 0;
  throw TypeException("Cannot convert NULL to int");
}

double Value::as_float() const {
  if (is_float()) return std::get<double>(data_);
  if (is_int()) return static_cast<double>(std::get<int64_t>(data_));
  if (is_string()) {
    try {
      return std::stod(std::get<std::string>(data_));
    } catch (...) {
      throw TypeException("Cannot convert string to float: " +
                          std::get<std::string>(data_));
    }
  }
  if (is_bool()) return std::get<bool>(data_) ? 1.0 : 0.0;
  throw TypeException("Cannot convert NULL to float");
}

std::string Value::as_string() const {
  if (is_string()) return std::get<std::string>(data_);
  return to_string();
}

bool Value::as_bool() const {
  if (is_bool()) return std::get<bool>(data_);
  if (is_int()) return std::get<int64_t>(data_) != 0;
  if (is_float()) return std::get<double>(data_) != 0.0;
  if (is_string()) {
    auto str = std::get<std::string>(data_);
    return str != "" && str != "0" && str != "false";
  }
  throw TypeException("Cannot convert NULL to bool");
}

DataType Value::get_type() const {
  if (is_int()) return DataType::INT;
  if (is_float()) return DataType::FLOAT;
  if (is_string()) return DataType::STRING;
  if (is_bool()) return DataType::BOOLEAN;
  return DataType::STRING;
}

std::string Value::get_type_string() const {
  return data_type_to_string(get_type());
}

// Comparison operators
bool Value::operator==(const Value &other) const {
  if (is_null() && other.is_null()) return true;
  if (is_null() || other.is_null()) return false;

  if (is_int() && other.is_int()) return as_int() == other.as_int();
  if (is_float() && other.is_float())
    return std::abs(as_float() - other.as_float()) < 1e-9;
  if (is_string() && other.is_string()) return as_string() == other.as_string();
  if (is_bool() && other.is_bool()) return as_bool() == other.as_bool();

  // Cross-type numeric comparison
  if ((is_int() || is_float()) && (other.is_int() || other.is_float())) {
    return std::abs(as_float() - other.as_float()) < 1e-9;
  }

  return false;
}

bool Value::operator!=(const Value &other) const { return !(*this == other); }

bool Value::operator<(const Value &other) const {
  if (is_null() && other.is_null()) return false;
  if (is_null()) return true;
  if (other.is_null()) return false;

  if (is_int() && other.is_int()) return as_int() < other.as_int();
  if (is_float() && other.is_float()) return as_float() < other.as_float();
  if (is_string() && other.is_string()) return as_string() < other.as_string();

  if ((is_int() || is_float()) && (other.is_int() || other.is_float())) {
    return as_float() < other.as_float();
  }

  return to_string() < other.to_string();
}

bool Value::operator<=(const Value &other) const {
  return *this < other || *this == other;
}

bool Value::operator>(const Value &other) const { return !((*this) <= other); }

bool Value::operator>=(const Value &other) const { return !((*this) < other); }

// Arithmetic operations
Value Value::operator+(const Value &other) const {
  if (is_int() && other.is_int()) {
    return Value(as_int() + other.as_int());
  }
  if ((is_int() || is_float()) && (other.is_int() || other.is_float())) {
    return Value(as_float() + other.as_float());
  }
  if (is_string() || other.is_string()) {
    return Value(as_string() + other.as_string());
  }
  throw TypeException("Cannot add these types");
}

Value Value::operator-(const Value &other) const {
  if (is_int() && other.is_int()) {
    return Value(as_int() - other.as_int());
  }
  if ((is_int() || is_float()) && (other.is_int() || other.is_float())) {
    return Value(as_float() - other.as_float());
  }
  throw TypeException("Cannot subtract these types");
}

Value Value::operator*(const Value &other) const {
  if (is_int() && other.is_int()) {
    return Value(as_int() * other.as_int());
  }
  if ((is_int() || is_float()) && (other.is_int() || other.is_float())) {
    return Value(as_float() * other.as_float());
  }
  throw TypeException("Cannot multiply these types");
}

Value Value::operator/(const Value &other) const {
  if ((is_int() || is_float()) && (other.is_int() || other.is_float())) {
    double divisor = other.as_float();
    if (std::abs(divisor) < 1e-9) {
      throw TypeException("Division by zero");
    }
    return Value(as_float() / divisor);
  }
  throw TypeException("Cannot divide these types");
}

std::string Value::to_string() const {
  if (is_null()) return "NULL";
  if (is_int()) return std::to_string(std::get<int64_t>(data_));
  if (is_float()) {
    std::ostringstream oss;
    oss << std::get<double>(data_);
    return oss.str();
  }
  if (is_string()) return std::get<std::string>(data_);
  if (is_bool()) return std::get<bool>(data_) ? "true" : "false";
  return "UNKNOWN";
}

Value Value::from_string(const std::string &str, DataType type) {
  return TypeConverter::string_to_value(str, type);
}

}  // namespace db
