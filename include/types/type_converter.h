#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "types/data_type.h"
#include "types/value.h"
#include "utils/exceptions.h"

namespace db {

class TypeConverter {
 public:
  // String to Value conversion
  static Value string_to_value(const std::string &str, DataType type);

  // Value to string serialization
  static std::string value_to_string(const Value &value);

  // Binary serialization/deserialization
  static std::vector<uint8_t> serialize_value(const Value &value);
  static Value deserialize_value(const std::vector<uint8_t> &bytes,
                                 DataType type);

  // Type validation
  static bool is_valid_value(const std::string &str, DataType type);
  static bool is_valid_date_format(const std::string &str);
  static bool is_valid_uuid_format(const std::string &str);

  // Numeric conversions
  static int64_t string_to_int(const std::string &str);
  static double string_to_float(const std::string &str);
  static bool string_to_bool(const std::string &str);

  // String conversions
  static std::string int_to_string(int64_t value);
  static std::string float_to_string(double value);
  static std::string bool_to_string(bool value);

 private:
  // Helper methods
  static std::string trim_whitespace(const std::string &str);
  static bool parseDate(const std::string &str);
  static bool parseUuid(const std::string &str);
};

}  // namespace db
