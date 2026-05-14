#pragma once

#include <string>
#include <memory>
#include "include/core/Table.h"
#include "include/utils/Exceptions.h"

namespace db
{

	class PersistenceManager
	{
	public:
		// Save a single table to file
		static void saveTable(const Table &table, const std::string &filePath);

		// Load a table from file
		static std::unique_ptr<Table> loadTable(const std::string &filePath);

		// Save multiple tables to a directory
		static void saveDatabase(const std::map<std::string, std::unique_ptr<Table>> &tables,
														 const std::string &directoryPath);

		// Load multiple tables from directory
		static std::map<std::string, std::unique_ptr<Table>> loadDatabase(const std::string &directoryPath);

	private:
		static constexpr uint32_t MAGIC_NUMBER = 0x44425442; // "DBTB"
		static constexpr uint16_t VERSION = 1;

		// Helper methods
		static void writeHeader(std::ofstream &file, const std::string &tableName);
		static void readHeader(std::ifstream &file, std::string &tableName);
	};

} // namespace db
