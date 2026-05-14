#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "include/types/Value.h"
#include "include/types/DataType.h"
#include "include/utils/Exceptions.h"

namespace db
{

	class TypeConverter
	{
	public:
		// String to Value conversion
		static Value stringToValue(const std::string &str, DataType type);

		// Value to string serialization
		static std::string valueToString(const Value &value);

		// Binary serialization/deserialization
		static std::vector<uint8_t> serializeValue(const Value &value);
		static Value deserializeValue(const std::vector<uint8_t> &bytes, DataType type);

		// Type validation
		static bool isValidValue(const std::string &str, DataType type);
		static bool isValidDateFormat(const std::string &str);
		static bool isValidUuidFormat(const std::string &str);

		// Numeric conversions
		static int64_t stringToInt(const std::string &str);
		static double stringToFloat(const std::string &str);
		static bool stringToBool(const std::string &str);

		// String conversions
		static std::string intToString(int64_t value);
		static std::string floatToString(double value);
		static std::string boolToString(bool value);

	private:
		// Helper methods
		static std::string trimWhitespace(const std::string &str);
		static bool parseDate(const std::string &str);
		static bool parseUuid(const std::string &str);
	};

} // namespace db
