#include "include/core/Column.h"

namespace db
{

	Column::Column(const std::string &name, DataType type, bool nullable,
								 bool isPrimaryKey, bool isUnique)
			: name(name), type(type), nullable(nullable),
				primaryKey(isPrimaryKey), unique(isUnique)
	{

		// Primary key columns cannot be nullable
		if (isPrimaryKey && nullable)
		{
			throw ConstraintException("Primary key column cannot be nullable");
		}
	}

	const std::string &Column::getName() const
	{
		return name;
	}

	DataType Column::getType() const
	{
		return type;
	}

	bool Column::isNullable() const
	{
		return nullable;
	}

	bool Column::isPrimaryKey() const
	{
		return primaryKey;
	}

	bool Column::isUnique() const
	{
		return unique;
	}

	std::string Column::toString() const
	{
		std::string str = name + " " + dataTypeToString(type);
		if (!nullable)
			str += " NOT NULL";
		if (primaryKey)
			str += " PRIMARY KEY";
		if (unique)
			str += " UNIQUE";
		return str;
	}

	bool Column::operator==(const Column &other) const
	{
		return name == other.name && type == other.type;
	}

	bool Column::operator!=(const Column &other) const
	{
		return !(*this == other);
	}

} // namespace db
