#include "include/types/Value.h"
#include "include/types/TypeConverter.h"
#include "include/utils/Exceptions.h"
#include <sstream>
#include <cmath>

namespace db
{

	// Constructors
	Value::Value() : data(std::monostate{}) {}

	Value::Value(int64_t val) : data(val) {}

	Value::Value(int val) : data(static_cast<int64_t>(val)) {}

	Value::Value(double val) : data(val) {}

	Value::Value(const std::string &val) : data(val) {}

	Value::Value(const char *val) : data(std::string(val)) {}

	Value::Value(bool val) : data(val) {}

	// Accessors
	bool Value::isNull() const
	{
		return std::holds_alternative<std::monostate>(data);
	}

	bool Value::isInt() const
	{
		return std::holds_alternative<int64_t>(data);
	}

	bool Value::isFloat() const
	{
		return std::holds_alternative<double>(data);
	}

	bool Value::isString() const
	{
		return std::holds_alternative<std::string>(data);
	}

	bool Value::isBool() const
	{
		return std::holds_alternative<bool>(data);
	}

	int64_t Value::asInt() const
	{
		if (isInt())
			return std::get<int64_t>(data);
		if (isFloat())
			return static_cast<int64_t>(std::get<double>(data));
		if (isString())
		{
			try
			{
				return std::stoll(std::get<std::string>(data));
			}
			catch (...)
			{
				throw TypeException("Cannot convert string to int: " + std::get<std::string>(data));
			}
		}
		if (isBool())
			return std::get<bool>(data) ? 1 : 0;
		throw TypeException("Cannot convert NULL to int");
	}

	double Value::asFloat() const
	{
		if (isFloat())
			return std::get<double>(data);
		if (isInt())
			return static_cast<double>(std::get<int64_t>(data));
		if (isString())
		{
			try
			{
				return std::stod(std::get<std::string>(data));
			}
			catch (...)
			{
				throw TypeException("Cannot convert string to float: " + std::get<std::string>(data));
			}
		}
		if (isBool())
			return std::get<bool>(data) ? 1.0 : 0.0;
		throw TypeException("Cannot convert NULL to float");
	}

	std::string Value::asString() const
	{
		if (isString())
			return std::get<std::string>(data);
		return toString();
	}

	bool Value::asBool() const
	{
		if (isBool())
			return std::get<bool>(data);
		if (isInt())
			return std::get<int64_t>(data) != 0;
		if (isFloat())
			return std::get<double>(data) != 0.0;
		if (isString())
		{
			auto str = std::get<std::string>(data);
			return str != "" && str != "0" && str != "false";
		}
		throw TypeException("Cannot convert NULL to bool");
	}

	DataType Value::getType() const
	{
		if (isInt())
			return DataType::INT;
		if (isFloat())
			return DataType::FLOAT;
		if (isString())
			return DataType::STRING;
		if (isBool())
			return DataType::BOOLEAN;
		return DataType::STRING;
	}

	std::string Value::getTypeString() const
	{
		return dataTypeToString(getType());
	}

	// Comparison operators
	bool Value::operator==(const Value &other) const
	{
		if (isNull() && other.isNull())
			return true;
		if (isNull() || other.isNull())
			return false;

		if (isInt() && other.isInt())
			return asInt() == other.asInt();
		if (isFloat() && other.isFloat())
			return std::abs(asFloat() - other.asFloat()) < 1e-9;
		if (isString() && other.isString())
			return asString() == other.asString();
		if (isBool() && other.isBool())
			return asBool() == other.asBool();

		// Cross-type numeric comparison
		if ((isInt() || isFloat()) && (other.isInt() || other.isFloat()))
		{
			return std::abs(asFloat() - other.asFloat()) < 1e-9;
		}

		return false;
	}

	bool Value::operator!=(const Value &other) const
	{
		return !(*this == other);
	}

	bool Value::operator<(const Value &other) const
	{
		if (isNull() && other.isNull())
			return false;
		if (isNull())
			return true;
		if (other.isNull())
			return false;

		if (isInt() && other.isInt())
			return asInt() < other.asInt();
		if (isFloat() && other.isFloat())
			return asFloat() < other.asFloat();
		if (isString() && other.isString())
			return asString() < other.asString();

		if ((isInt() || isFloat()) && (other.isInt() || other.isFloat()))
		{
			return asFloat() < other.asFloat();
		}

		return toString() < other.toString();
	}

	bool Value::operator<=(const Value &other) const
	{
		return *this < other || *this == other;
	}

	bool Value::operator>(const Value &other) const
	{
		return !((*this) <= other);
	}

	bool Value::operator>=(const Value &other) const
	{
		return !((*this) < other);
	}

	// Arithmetic operations
	Value Value::operator+(const Value &other) const
	{
		if (isInt() && other.isInt())
		{
			return Value(asInt() + other.asInt());
		}
		if ((isInt() || isFloat()) && (other.isInt() || other.isFloat()))
		{
			return Value(asFloat() + other.asFloat());
		}
		if (isString() || other.isString())
		{
			return Value(asString() + other.asString());
		}
		throw TypeException("Cannot add these types");
	}

	Value Value::operator-(const Value &other) const
	{
		if (isInt() && other.isInt())
		{
			return Value(asInt() - other.asInt());
		}
		if ((isInt() || isFloat()) && (other.isInt() || other.isFloat()))
		{
			return Value(asFloat() - other.asFloat());
		}
		throw TypeException("Cannot subtract these types");
	}

	Value Value::operator*(const Value &other) const
	{
		if (isInt() && other.isInt())
		{
			return Value(asInt() * other.asInt());
		}
		if ((isInt() || isFloat()) && (other.isInt() || other.isFloat()))
		{
			return Value(asFloat() * other.asFloat());
		}
		throw TypeException("Cannot multiply these types");
	}

	Value Value::operator/(const Value &other) const
	{
		if ((isInt() || isFloat()) && (other.isInt() || other.isFloat()))
		{
			double divisor = other.asFloat();
			if (std::abs(divisor) < 1e-9)
			{
				throw TypeException("Division by zero");
			}
			return Value(asFloat() / divisor);
		}
		throw TypeException("Cannot divide these types");
	}

	std::string Value::toString() const
	{
		if (isNull())
			return "NULL";
		if (isInt())
			return std::to_string(std::get<int64_t>(data));
		if (isFloat())
		{
			std::ostringstream oss;
			oss << std::get<double>(data);
			return oss.str();
		}
		if (isString())
			return std::get<std::string>(data);
		if (isBool())
			return std::get<bool>(data) ? "true" : "false";
		return "UNKNOWN";
	}

	Value Value::fromString(const std::string &str, DataType type)
	{
		return TypeConverter::stringToValue(str, type);
	}

} // namespace db
