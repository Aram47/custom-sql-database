#pragma once

#include <vector>
#include <string>
#include <map>
#include <memory>
#include <algorithm>
#include "include/core/Column.h"
#include "include/core/Row.h"
#include "include/types/Value.h"
#include "include/utils/Exceptions.h"

namespace db
{

	// Forward declarations
	class Table;

	// Simple index for column lookups
	class ColumnIndex
	{
	public:
		explicit ColumnIndex(size_t columnIdx) : columnIndex(columnIdx) {}

		bool contains(const Value &value) const
		{
			return indexMap.find(value.toString()) != indexMap.end();
		}

		std::vector<size_t> find(const Value &value) const
		{
			auto it = indexMap.find(value.toString());
			if (it != indexMap.end())
			{
				return it->second;
			}
			return {};
		}

		void insert(const Value &value, size_t rowIdx)
		{
			indexMap[value.toString()].push_back(rowIdx);
		}

		void remove(const Value &value, size_t rowIdx)
		{
			auto it = indexMap.find(value.toString());
			if (it != indexMap.end())
			{
				auto &vec = it->second;
				vec.erase(std::remove(vec.begin(), vec.end(), rowIdx), vec.end());
				if (vec.empty())
				{
					indexMap.erase(it);
				}
			}
		}

		void clear()
		{
			indexMap.clear();
		}

	private:
		size_t columnIndex;
		std::map<std::string, std::vector<size_t>> indexMap;
	};

	class Table
	{
	public:
		// Constructor and destructor
		Table(const std::string &name);
		~Table() = default;

		// Prevent copying and moving
		Table(const Table &) = delete;
		Table &operator=(const Table &) = delete;
		Table(Table &&) = delete;
		Table &operator=(Table &&) = delete;

		// Table metadata
		const std::string &getName() const;
		size_t getColumnCount() const;
		size_t getRowCount() const;

		// Column management
		void addColumn(const Column &column);
		const Column &getColumn(size_t index) const;
		const Column &getColumn(const std::string &name) const;
		int getColumnIndex(const std::string &name) const;
		const std::vector<Column> &getColumns() const;

		// Row CRUD operations
		void insertRow(const Row &row);
		std::vector<Row> getAllRows() const;
		Row getRow(size_t index) const;
		void updateRow(size_t index, const Row &row);
		void deleteRow(size_t index);
		void deleteAll();

		// Query operations
		std::vector<size_t> findRowsByValue(const std::string &columnName, const Value &value) const;
		std::vector<size_t> findRowsByPredicate(std::function<bool(const Row &)> predicate) const;

		// Constraint validation
		bool validateRow(const Row &row) const;
		bool validatePrimaryKeyUniqueness(const Row &row, size_t excludeRowIndex = -1) const;
		bool validateUniqueConstraint(const Row &row, size_t excludeRowIndex = -1) const;

		// Serialization helpers
		std::vector<Row> &getMutableRows();

		// String representation
		std::string toString() const;

	private:
		std::string tableName;
		std::vector<Column> columns;
		std::vector<Row> rows;
		std::map<std::string, std::unique_ptr<ColumnIndex>> columnIndices;

		// Helper methods
		void buildIndex(const std::string &columnName);
		void validateSchema(const Row &row) const;
		int getPrimaryKeyIndex() const;
	};

} // namespace db
