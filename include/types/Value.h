#pragma once

#include <variant>
#include <string>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include "include/types/DataType.h"

namespace db
{

	// Variant that holds different types
	using ValueVariant = std::variant<
			std::monostate, // NULL value
			int64_t,				// INT
			double,					// FLOAT
			std::string,		// STRING
			bool						// BOOLEAN
			>;

	class Value
	{
	public:
		// Constructors
		Value(); // NULL value
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
		bool isNull() const;
		bool isInt() const;
		bool isFloat() const;
		bool isString() const;
		bool isBool() const;

		int64_t asInt() const;
		double asFloat() const;
		std::string asString() const;
		bool asBool() const;

		// Type checking and conversion
		DataType getType() const;
		std::string getTypeString() const;

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
		std::string toString() const;

		// Create value from string with type
		static Value fromString(const std::string &str, DataType type);

	private:
		ValueVariant data;

		friend class TypeConverter;
	};

} // namespace db
