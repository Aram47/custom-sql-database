#pragma once

#include <string>
#include <memory>
#include "include/types/DataType.h"
#include "include/utils/Exceptions.h"

namespace db
{

	class Column
	{
	public:
		Column(const std::string &name, DataType type, bool nullable = true,
					 bool isPrimaryKey = false, bool isUnique = false);

		// Accessors
		const std::string &getName() const;
		DataType getType() const;
		bool isNullable() const;
		bool isPrimaryKey() const;
		bool isUnique() const;

		// String representation for debugging
		std::string toString() const;

		// Equality
		bool operator==(const Column &other) const;
		bool operator!=(const Column &other) const;

	private:
		std::string name;
		DataType type;
		bool nullable;
		bool primaryKey;
		bool unique;
	};

} // namespace db
