#include "include/types/TypeConverter.h"
#include "include/utils/Logger.h"
#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace db
{

	Value TypeConverter::stringToValue(const std::string &str, DataType type)
	{
		if (str == "NULL" || str.empty())
		{
			return Value();
		}

		std::string trimmed = trimWhitespace(str);

		switch (type)
		{
		case DataType::INT:
			return Value(stringToInt(trimmed));
		case DataType::FLOAT:
			return Value(stringToFloat(trimmed));
		case DataType::STRING:
			return Value(trimmed);
		case DataType::BOOLEAN:
			return Value(stringToBool(trimmed));
		case DataType::DATE:
			if (!isValidDateFormat(trimmed))
			{
				throw TypeException("Invalid date format: " + trimmed + ". Expected YYYY-MM-DD");
			}
			return Value(trimmed);
		case DataType::UUID:
			if (!isValidUuidFormat(trimmed))
			{
				throw TypeException("Invalid UUID format: " + trimmed);
			}
			return Value(trimmed);
		default:
			throw TypeException("Unknown data type");
		}
	}

	std::string TypeConverter::valueToString(const Value &value)
	{
		return value.toString();
	}

	std::vector<uint8_t> TypeConverter::serializeValue(const Value &value)
	{
		std::vector<uint8_t> bytes;

		if (value.isNull())
		{
			bytes.push_back(0xFF); // NULL marker
			return bytes;
		}

		if (value.isInt())
		{
			bytes.push_back(0x00); // INT marker
			int64_t val = value.asInt();
			for (int i = 0; i < 8; i++)
			{
				bytes.push_back((val >> (8 * i)) & 0xFF);
			}
		}
		else if (value.isFloat())
		{
			bytes.push_back(0x01); // FLOAT marker
			double val = value.asFloat();
			uint8_t *ptr = reinterpret_cast<uint8_t *>(&val);
			for (size_t i = 0; i < sizeof(double); i++)
			{
				bytes.push_back(ptr[i]);
			}
		}
		else if (value.isString())
		{
			bytes.push_back(0x02); // STRING marker
			std::string str = value.asString();
			uint32_t len = str.length();
			for (int i = 0; i < 4; i++)
			{
				bytes.push_back((len >> (8 * i)) & 0xFF);
			}
			for (char c : str)
			{
				bytes.push_back(static_cast<uint8_t>(c));
			}
		}
		else if (value.isBool())
		{
			bytes.push_back(0x03); // BOOL marker
			bytes.push_back(value.asBool() ? 0x01 : 0x00);
		}

		return bytes;
	}

	Value TypeConverter::deserializeValue(const std::vector<uint8_t> &bytes, DataType type)
	{
		if (bytes.empty())
		{
			return Value();
		}

		uint8_t marker = bytes[0];

		if (marker == 0xFF)
		{
			return Value();
		}

		if (marker == 0x00 && bytes.size() >= 9)
		{
			int64_t val = 0;
			for (int i = 0; i < 8; i++)
			{
				val |= (static_cast<int64_t>(bytes[i + 1]) << (8 * i));
			}
			return Value(val);
		}

		if (marker == 0x01 && bytes.size() >= 9)
		{
			double val;
			uint8_t *ptr = reinterpret_cast<uint8_t *>(&val);
			for (size_t i = 0; i < sizeof(double); i++)
			{
				ptr[i] = bytes[i + 1];
			}
			return Value(val);
		}

		if (marker == 0x02 && bytes.size() >= 5)
		{
			uint32_t len = 0;
			for (int i = 0; i < 4; i++)
			{
				len |= (static_cast<uint32_t>(bytes[i + 1]) << (8 * i));
			}
			if (bytes.size() >= 5 + len)
			{
				std::string str(bytes.begin() + 5, bytes.begin() + 5 + len);
				return Value(str);
			}
		}

		if (marker == 0x03 && bytes.size() >= 2)
		{
			return Value(bytes[1] != 0);
		}

		throw TypeException("Invalid serialized value");
	}

	bool TypeConverter::isValidValue(const std::string &str, DataType type)
	{
		if (str == "NULL")
			return true;

		std::string trimmed = trimWhitespace(str);
		if (trimmed.empty())
			return false;

		switch (type)
		{
		case DataType::INT:
			try
			{
				stringToInt(trimmed);
				return true;
			}
			catch (...)
			{
				return false;
			}
		case DataType::FLOAT:
			try
			{
				stringToFloat(trimmed);
				return true;
			}
			catch (...)
			{
				return false;
			}
		case DataType::STRING:
			return true;
		case DataType::BOOLEAN:
			return trimmed == "true" || trimmed == "false" ||
						 trimmed == "TRUE" || trimmed == "FALSE" ||
						 trimmed == "1" || trimmed == "0";
		case DataType::DATE:
			return isValidDateFormat(trimmed);
		case DataType::UUID:
			return isValidUuidFormat(trimmed);
		default:
			return false;
		}
	}

	bool TypeConverter::isValidDateFormat(const std::string &str)
	{
		std::regex dateRegex("^\\d{4}-\\d{2}-\\d{2}$");
		if (!std::regex_match(str, dateRegex))
		{
			return false;
		}

		try
		{
			int year = std::stoi(str.substr(0, 4));
			int month = std::stoi(str.substr(5, 2));
			int day = std::stoi(str.substr(8, 2));

			if (month < 1 || month > 12)
				return false;
			if (day < 1 || day > 31)
				return false;

			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool TypeConverter::isValidUuidFormat(const std::string &str)
	{
		std::regex uuidRegex("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
		return std::regex_match(str, uuidRegex);
	}

	int64_t TypeConverter::stringToInt(const std::string &str)
	{
		try
		{
			return std::stoll(str);
		}
		catch (...)
		{
			throw TypeException("Cannot convert to integer: " + str);
		}
	}

	double TypeConverter::stringToFloat(const std::string &str)
	{
		try
		{
			return std::stod(str);
		}
		catch (...)
		{
			throw TypeException("Cannot convert to float: " + str);
		}
	}

	bool TypeConverter::stringToBool(const std::string &str)
	{
		std::string lower = str;
		std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

		if (lower == "true" || lower == "1" || lower == "yes")
			return true;
		if (lower == "false" || lower == "0" || lower == "no")
			return false;

		throw TypeException("Cannot convert to boolean: " + str);
	}

	std::string TypeConverter::intToString(int64_t value)
	{
		return std::to_string(value);
	}

	std::string TypeConverter::floatToString(double value)
	{
		std::ostringstream oss;
		oss << value;
		return oss.str();
	}

	std::string TypeConverter::boolToString(bool value)
	{
		return value ? "true" : "false";
	}

	std::string TypeConverter::trimWhitespace(const std::string &str)
	{
		size_t start = str.find_first_not_of(" \t\n\r");
		if (start == std::string::npos)
			return "";
		size_t end = str.find_last_not_of(" \t\n\r");
		return str.substr(start, end - start + 1);
	}

} // namespace db
