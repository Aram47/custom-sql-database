#include "include/core/Table.h"

namespace db
{

	Table::Table(const std::string &name) : tableName(name) {}

	const std::string &Table::getName() const
	{
		return tableName;
	}

	size_t Table::getColumnCount() const
	{
		return columns.size();
	}

	size_t Table::getRowCount() const
	{
		return rows.size();
	}

	void Table::addColumn(const Column &column)
	{
		// Check for duplicate column names
		for (const auto &col : columns)
		{
			if (col.getName() == column.getName())
			{
				throw ConstraintException("Column '" + column.getName() + "' already exists");
			}
		}

		columns.push_back(column);
	}

	const Column &Table::getColumn(size_t index) const
	{
		if (index >= columns.size())
		{
			throw NotFoundException("Column index " + std::to_string(index) + " not found");
		}
		return columns[index];
	}

	const Column &Table::getColumn(const std::string &name) const
	{
		for (const auto &col : columns)
		{
			if (col.getName() == name)
			{
				return col;
			}
		}
		throw NotFoundException("Column '" + name + "' not found");
	}

	int Table::getColumnIndex(const std::string &name) const
	{
		for (size_t i = 0; i < columns.size(); ++i)
		{
			if (columns[i].getName() == name)
			{
				return i;
			}
		}
		return -1;
	}

	const std::vector<Column> &Table::getColumns() const
	{
		return columns;
	}

	void Table::insertRow(const Row &row)
	{
		validateSchema(row);

		if (!validateRow(row))
		{
			throw ConstraintException("Row does not satisfy table constraints");
		}

		rows.push_back(row);

		// Update indices
		for (size_t i = 0; i < columns.size(); ++i)
		{
			if (columnIndices.count(columns[i].getName()))
			{
				columnIndices[columns[i].getName()]->insert(row.getValue(i), rows.size() - 1);
			}
		}
	}

	std::vector<Row> Table::getAllRows() const
	{
		return rows;
	}

	Row Table::getRow(size_t index) const
	{
		if (index >= rows.size())
		{
			throw std::out_of_range("Row index " + std::to_string(index) + " out of range");
		}
		return rows[index];
	}

	void Table::updateRow(size_t index, const Row &row)
	{
		if (index >= rows.size())
		{
			throw std::out_of_range("Row index out of range");
		}

		validateSchema(row);

		if (!validateRow(row))
		{
			throw ConstraintException("Updated row does not satisfy table constraints");
		}

		// Update indices
		for (size_t i = 0; i < columns.size(); ++i)
		{
			if (columnIndices.count(columns[i].getName()))
			{
				columnIndices[columns[i].getName()]->remove(rows[index].getValue(i), index);
				columnIndices[columns[i].getName()]->insert(row.getValue(i), index);
			}
		}

		rows[index] = row;
	}

	void Table::deleteRow(size_t index)
	{
		if (index >= rows.size())
		{
			throw std::out_of_range("Row index out of range");
		}

		// Update indices
		for (size_t i = 0; i < columns.size(); ++i)
		{
			if (columnIndices.count(columns[i].getName()))
			{
				columnIndices[columns[i].getName()]->remove(rows[index].getValue(i), index);
			}
		}

		rows.erase(rows.begin() + index);

		// Rebuild indices after deletion
		for (auto &[colName, idx] : columnIndices)
		{
			idx->clear();
			for (size_t i = 0; i < rows.size(); ++i)
			{
				int colIdx = getColumnIndex(colName);
				if (colIdx >= 0)
				{
					idx->insert(rows[i].getValue(colIdx), i);
				}
			}
		}
	}

	void Table::deleteAll()
	{
		rows.clear();
		for (auto &[colName, idx] : columnIndices)
		{
			idx->clear();
		}
	}

	std::vector<size_t> Table::findRowsByValue(const std::string &columnName, const Value &value) const
	{
		int colIdx = getColumnIndex(columnName);
		if (colIdx < 0)
		{
			throw NotFoundException("Column '" + columnName + "' not found");
		}

		std::vector<size_t> result;
		for (size_t i = 0; i < rows.size(); ++i)
		{
			if (rows[i].getValue(colIdx) == value)
			{
				result.push_back(i);
			}
		}
		return result;
	}

	std::vector<size_t> Table::findRowsByPredicate(std::function<bool(const Row &)> predicate) const
	{
		std::vector<size_t> result;
		for (size_t i = 0; i < rows.size(); ++i)
		{
			if (predicate(rows[i]))
			{
				result.push_back(i);
			}
		}
		return result;
	}

	bool Table::validateRow(const Row &row) const
	{
		if (row.getColumnCount() != columns.size())
		{
			return false;
		}

		// Check nullability constraints
		for (size_t i = 0; i < columns.size(); ++i)
		{
			if (!columns[i].isNullable() && row.getValue(i).isNull())
			{
				return false;
			}
		}

		// Check uniqueness constraints
		if (!validateUniqueConstraint(row))
		{
			return false;
		}

		// Check primary key constraint
		if (!validatePrimaryKeyUniqueness(row))
		{
			return false;
		}

		return true;
	}

	bool Table::validatePrimaryKeyUniqueness(const Row &row, size_t excludeRowIndex) const
	{
		int pkIdx = getPrimaryKeyIndex();
		if (pkIdx < 0)
			return true;

		const Value &pkValue = row.getValue(pkIdx);
		for (size_t i = 0; i < rows.size(); ++i)
		{
			if (i == excludeRowIndex)
				continue;
			if (rows[i].getValue(pkIdx) == pkValue && !pkValue.isNull())
			{
				return false;
			}
		}
		return true;
	}

	bool Table::validateUniqueConstraint(const Row &row, size_t excludeRowIndex) const
	{
		for (size_t i = 0; i < columns.size(); ++i)
		{
			if (columns[i].isUnique())
			{
				const Value &value = row.getValue(i);
				if (value.isNull())
					continue;

				for (size_t j = 0; j < rows.size(); ++j)
				{
					if (j == excludeRowIndex)
						continue;
					if (rows[j].getValue(i) == value)
					{
						return false;
					}
				}
			}
		}
		return true;
	}

	std::vector<Row> &Table::getMutableRows()
	{
		return rows;
	}

	std::string Table::toString() const
	{
		std::string str = "Table: " + tableName + "\n";
		str += "Columns:\n";
		for (const auto &col : columns)
		{
			str += "  " + col.toString() + "\n";
		}
		str += "Rows: " + std::to_string(rows.size()) + "\n";
		return str;
	}

	void Table::buildIndex(const std::string &columnName)
	{
		int colIdx = getColumnIndex(columnName);
		if (colIdx < 0)
		{
			throw NotFoundException("Column '" + columnName + "' not found");
		}

		auto idx = std::make_unique<ColumnIndex>(colIdx);
		for (size_t i = 0; i < rows.size(); ++i)
		{
			idx->insert(rows[i].getValue(colIdx), i);
		}
		columnIndices[columnName] = std::move(idx);
	}

	void Table::validateSchema(const Row &row) const
	{
		if (row.getColumnCount() != columns.size())
		{
			throw InvalidOperationException("Row column count does not match table schema");
		}
	}

	int Table::getPrimaryKeyIndex() const
	{
		for (size_t i = 0; i < columns.size(); ++i)
		{
			if (columns[i].isPrimaryKey())
			{
				return i;
			}
		}
		return -1;
	}

} // namespace db
