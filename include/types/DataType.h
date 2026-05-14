#pragma once

#include <string>
#include <stdexcept>

namespace db
{

	enum class DataType
	{
		INT,
		FLOAT,
		STRING,
		BOOLEAN,
		DATE, // YYYY-MM-DD format
		UUID	// 36 char format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
	};

	inline std::string dataTypeToString(DataType type)
	{
		switch (type)
		{
		case DataType::INT:
			return "INT";
		case DataType::FLOAT:
			return "FLOAT";
		case DataType::STRING:
			return "STRING";
		case DataType::BOOLEAN:
			return "BOOLEAN";
		case DataType::DATE:
			return "DATE";
		case DataType::UUID:
			return "UUID";
		default:
			return "UNKNOWN";
		}
	}

	inline DataType stringToDataType(const std::string &str)
	{
		std::string upper = str;
		for (auto &c : upper)
			c = std::toupper(c);

		if (upper == "INT" || upper == "INTEGER")
			return DataType::INT;
		if (upper == "FLOAT" || upper == "REAL" || upper == "DOUBLE")
			return DataType::FLOAT;
		if (upper == "STRING" || upper == "TEXT" || upper == "VARCHAR")
			return DataType::STRING;
		if (upper == "BOOLEAN" || upper == "BOOL")
			return DataType::BOOLEAN;
		if (upper == "DATE")
			return DataType::DATE;
		if (upper == "UUID")
			return DataType::UUID;

		throw std::invalid_argument("Unknown data type: " + str);
	}

	inline size_t getTypeSize(DataType type)
	{
		switch (type)
		{
		case DataType::INT:
			return sizeof(int64_t);
		case DataType::FLOAT:
			return sizeof(double);
		case DataType::STRING:
			return 0; // Variable length
		case DataType::BOOLEAN:
			return sizeof(bool);
		case DataType::DATE:
			return 10; // YYYY-MM-DD
		case DataType::UUID:
			return 36; // UUID string format
		default:
			return 0;
		}
	}

} // namespace db
