#include "include/storage/PersistenceManager.h"
#include "include/types/TypeConverter.h"
#include "include/utils/Logger.h"
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace db
{

	void PersistenceManager::saveTable(const Table &table, const std::string &filePath)
	{
		std::ofstream file(filePath, std::ios::binary);
		if (!file)
		{
			throw StorageException("Cannot open file for writing: " + filePath);
		}

		try
		{
			// Write header
			writeHeader(file, table.getName());

			// Write number of columns
			uint32_t colCount = table.getColumnCount();
			file.write(reinterpret_cast<const char *>(&colCount), sizeof(colCount));

			// Write columns
			for (size_t i = 0; i < colCount; ++i)
			{
				const auto &col = table.getColumn(i);

				// Write column name
				std::string name = col.getName();
				uint16_t nameLen = name.length();
				file.write(reinterpret_cast<const char *>(&nameLen), sizeof(nameLen));
				file.write(name.c_str(), nameLen);

				// Write column type
				uint8_t typeVal = static_cast<uint8_t>(col.getType());
				file.write(reinterpret_cast<const char *>(&typeVal), sizeof(typeVal));

				// Write flags
				uint8_t flags = 0;
				if (!col.isNullable())
					flags |= 0x01;
				if (col.isPrimaryKey())
					flags |= 0x02;
				if (col.isUnique())
					flags |= 0x04;
				file.write(reinterpret_cast<const char *>(&flags), sizeof(flags));
			}

			// Write number of rows
			uint32_t rowCount = table.getRowCount();
			file.write(reinterpret_cast<const char *>(&rowCount), sizeof(rowCount));

			// Write rows
			const auto &rows = table.getAllRows();
			for (const auto &row : rows)
			{
				for (size_t i = 0; i < colCount; ++i)
				{
					auto serialized = TypeConverter::serializeValue(row.getValue(i));
					uint32_t valueLen = serialized.size();
					file.write(reinterpret_cast<const char *>(&valueLen), sizeof(valueLen));
					file.write(reinterpret_cast<const char *>(serialized.data()), valueLen);
				}
			}

			file.close();
			DB_LOG_INFO("Saved table '", table.getName(), "' to ", filePath);
		}
		catch (const std::exception &e)
		{
			throw StorageException("Error saving table: " + std::string(e.what()));
		}
	}

	std::unique_ptr<Table> PersistenceManager::loadTable(const std::string &filePath)
	{
		std::ifstream file(filePath, std::ios::binary);
		if (!file)
		{
			throw StorageException("Cannot open file for reading: " + filePath);
		}

		try
		{
			std::string tableName;
			readHeader(file, tableName);

			auto table = std::make_unique<Table>(tableName);

			// Read columns
			uint32_t colCount;
			file.read(reinterpret_cast<char *>(&colCount), sizeof(colCount));

			for (uint32_t i = 0; i < colCount; ++i)
			{
				// Read column name
				uint16_t nameLen;
				file.read(reinterpret_cast<char *>(&nameLen), sizeof(nameLen));
				std::string name(nameLen, '\0');
				file.read(&name[0], nameLen);

				// Read column type
				uint8_t typeVal;
				file.read(reinterpret_cast<char *>(&typeVal), sizeof(typeVal));
				DataType type = static_cast<DataType>(typeVal);

				// Read flags
				uint8_t flags;
				file.read(reinterpret_cast<char *>(&flags), sizeof(flags));

				bool nullable = !(flags & 0x01);
				bool primaryKey = (flags & 0x02) != 0;
				bool unique = (flags & 0x04) != 0;

				Column col(name, type, nullable, primaryKey, unique);
				table->addColumn(col);
			}

			// Read rows
			uint32_t rowCount;
			file.read(reinterpret_cast<char *>(&rowCount), sizeof(rowCount));

			for (uint32_t i = 0; i < rowCount; ++i)
			{
				Row row;
				for (uint32_t j = 0; j < colCount; ++j)
				{
					uint32_t valueLen;
					file.read(reinterpret_cast<char *>(&valueLen), sizeof(valueLen));
					std::vector<uint8_t> serialized(valueLen);
					file.read(reinterpret_cast<char *>(serialized.data()), valueLen);

					const auto &col = table->getColumn(j);
					Value value = TypeConverter::deserializeValue(serialized, col.getType());
					row.addValue(value);
				}
				table->insertRow(row);
			}

			file.close();
			DB_LOG_INFO("Loaded table '", tableName, "' from ", filePath);
			return table;
		}
		catch (const std::exception &e)
		{
			throw StorageException("Error loading table: " + std::string(e.what()));
		}
	}

	void PersistenceManager::saveDatabase(const std::map<std::string, std::unique_ptr<Table>> &tables,
																				const std::string &directoryPath)
	{
		try
		{
			fs::create_directories(directoryPath);

			for (const auto &[name, table] : tables)
			{
				std::string filePath = directoryPath + "/" + name + ".db";
				saveTable(*table, filePath);
			}

			DB_LOG_INFO("Saved database to ", directoryPath);
		}
		catch (const std::exception &e)
		{
			throw StorageException("Error saving database: " + std::string(e.what()));
		}
	}

	std::map<std::string, std::unique_ptr<Table>> PersistenceManager::loadDatabase(const std::string &directoryPath)
	{
		std::map<std::string, std::unique_ptr<Table>> tables;

		try
		{
			if (!fs::exists(directoryPath))
			{
				DB_LOG_INFO("Database directory does not exist: ", directoryPath);
				return tables;
			}

			for (const auto &entry : fs::directory_iterator(directoryPath))
			{
				if (entry.path().extension() == ".db")
				{
					try
					{
						auto table = loadTable(entry.path().string());
						tables[table->getName()] = std::move(table);
					}
					catch (const std::exception &e)
					{
						DB_LOG_WARNING("Failed to load table from ", entry.path().string(), ": ", e.what());
					}
				}
			}

			DB_LOG_INFO("Loaded database from ", directoryPath, " with ", tables.size(), " tables");
			return tables;
		}
		catch (const std::exception &e)
		{
			throw StorageException("Error loading database: " + std::string(e.what()));
		}
	}

	void PersistenceManager::writeHeader(std::ofstream &file, const std::string &tableName)
	{
		uint32_t magic = MAGIC_NUMBER;
		uint16_t version = VERSION;

		file.write(reinterpret_cast<const char *>(&magic), sizeof(magic));
		file.write(reinterpret_cast<const char *>(&version), sizeof(version));

		uint16_t nameLen = tableName.length();
		file.write(reinterpret_cast<const char *>(&nameLen), sizeof(nameLen));
		file.write(tableName.c_str(), nameLen);
	}

	void PersistenceManager::readHeader(std::ifstream &file, std::string &tableName)
	{
		uint32_t magic;
		uint16_t version;

		file.read(reinterpret_cast<char *>(&magic), sizeof(magic));
		if (magic != MAGIC_NUMBER)
		{
			throw StorageException("Invalid file format");
		}

		file.read(reinterpret_cast<char *>(&version), sizeof(version));
		if (version != VERSION)
		{
			throw StorageException("Unsupported file version");
		}

		uint16_t nameLen;
		file.read(reinterpret_cast<char *>(&nameLen), sizeof(nameLen));
		tableName.resize(nameLen);
		file.read(&tableName[0], nameLen);
	}

} // namespace db
