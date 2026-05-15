#include "storage/persistence_manager.h"

#include <filesystem>
#include <fstream>

#include "types/type_converter.h"
#include "utils/logger.h"

namespace fs = std::filesystem;

namespace db {

void PersistenceManager::save_table(const Table &table,
                                    const std::string &file_path) {
  std::ofstream file(file_path, std::ios::binary);
  if (!file) {
    throw StorageException("Cannot open file for writing: " + file_path);
  }

  try {
    // Write header
    write_header(file, table.get_name());

    // Write number of columns
    uint32_t colCount = table.get_column_count();
    file.write(reinterpret_cast<const char *>(&colCount), sizeof(colCount));

    // Write columns
    for (size_t i = 0; i < colCount; ++i) {
      const auto &col = table.get_column(i);

      // Write column name
      std::string name = col.get_name();
      uint16_t nameLen = name.length();
      file.write(reinterpret_cast<const char *>(&nameLen), sizeof(nameLen));
      file.write(name.c_str(), nameLen);

      // Write column type
      uint8_t typeVal = static_cast<uint8_t>(col.get_type());
      file.write(reinterpret_cast<const char *>(&typeVal), sizeof(typeVal));

      // Write flags
      uint8_t flags = 0;
      if (!col.is_nullable()) flags |= 0x01;
      if (col.is_primary_key()) flags |= 0x02;
      if (col.is_unique()) flags |= 0x04;
      file.write(reinterpret_cast<const char *>(&flags), sizeof(flags));
    }

    // Write number of rows
    uint32_t rowCount = table.get_row_count();
    file.write(reinterpret_cast<const char *>(&rowCount), sizeof(rowCount));

    // Write rows
    const auto &rows = table.get_all_rows();
    for (const auto &row : rows) {
      for (size_t i = 0; i < colCount; ++i) {
        auto serialized = TypeConverter::serialize_value(row.get_value(i));
        uint32_t valueLen = serialized.size();
        file.write(reinterpret_cast<const char *>(&valueLen), sizeof(valueLen));
        file.write(reinterpret_cast<const char *>(serialized.data()), valueLen);
      }
    }

    file.close();
    DB_LOG_INFO("Saved table '", table.get_name(), "' to ", file_path);
  } catch (const std::exception &e) {
    throw StorageException("Error saving table: " + std::string(e.what()));
  }
}

std::unique_ptr<Table> PersistenceManager::load_table(
    const std::string &file_path) {
  std::ifstream file(file_path, std::ios::binary);
  if (!file) {
    throw StorageException("Cannot open file for reading: " + file_path);
  }

  try {
    std::string table_name;
    read_header(file, table_name);

    auto table = std::make_unique<Table>(table_name);

    // Read columns
    uint32_t colCount;
    file.read(reinterpret_cast<char *>(&colCount), sizeof(colCount));

    for (uint32_t i = 0; i < colCount; ++i) {
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
      table->add_column(col);
    }

    // Read rows
    uint32_t rowCount;
    file.read(reinterpret_cast<char *>(&rowCount), sizeof(rowCount));

    for (uint32_t i = 0; i < rowCount; ++i) {
      Row row;
      for (uint32_t j = 0; j < colCount; ++j) {
        uint32_t valueLen;
        file.read(reinterpret_cast<char *>(&valueLen), sizeof(valueLen));
        std::vector<uint8_t> serialized(valueLen);
        file.read(reinterpret_cast<char *>(serialized.data()), valueLen);

        const auto &col = table->get_column(j);
        Value value =
            TypeConverter::deserialize_value(serialized, col.get_type());
        row.add_value(value);
      }
      table->insert_row(row);
    }

    file.close();
    DB_LOG_INFO("Loaded table '", table_name, "' from ", file_path);
    return table;
  } catch (const std::exception &e) {
    throw StorageException("Error loading table: " + std::string(e.what()));
  }
}

void PersistenceManager::save_database(
    const std::map<std::string, std::unique_ptr<Table>> &tables,
    const std::string &directory_path) {
  try {
    fs::create_directories(directory_path);

    for (const auto &[name, table] : tables) {
      std::string file_path = directory_path + "/" + name + ".db";
      save_table(*table, file_path);
    }

    DB_LOG_INFO("Saved database to ", directory_path);
  } catch (const std::exception &e) {
    throw StorageException("Error saving database: " + std::string(e.what()));
  }
}

std::map<std::string, std::unique_ptr<Table>> PersistenceManager::load_database(
    const std::string &directory_path) {
  std::map<std::string, std::unique_ptr<Table>> tables;

  try {
    if (!fs::exists(directory_path)) {
      DB_LOG_INFO("Database directory does not exist: ", directory_path);
      return tables;
    }

    for (const auto &entry : fs::directory_iterator(directory_path)) {
      if (entry.path().extension() == ".db") {
        try {
          auto table = load_table(entry.path().string());
          tables[table->get_name()] = std::move(table);
        } catch (const std::exception &e) {
          DB_LOG_WARNING("Failed to load table from ", entry.path().string(),
                         ": ", e.what());
        }
      }
    }

    DB_LOG_INFO("Loaded database from ", directory_path, " with ",
                tables.size(), " tables");
    return tables;
  } catch (const std::exception &e) {
    throw StorageException("Error loading database: " + std::string(e.what()));
  }
}

void PersistenceManager::write_header(std::ofstream &file,
                                      const std::string &table_name) {
  uint32_t magic = kMagicNumber;
  uint16_t version = kVersion;

  file.write(reinterpret_cast<const char *>(&magic), sizeof(magic));
  file.write(reinterpret_cast<const char *>(&version), sizeof(version));

  uint16_t nameLen = table_name.length();
  file.write(reinterpret_cast<const char *>(&nameLen), sizeof(nameLen));
  file.write(table_name.c_str(), nameLen);
}

void PersistenceManager::read_header(std::ifstream &file,
                                     std::string &table_name) {
  uint32_t magic;
  uint16_t version;

  file.read(reinterpret_cast<char *>(&magic), sizeof(magic));
  if (magic != kMagicNumber) {
    throw StorageException("Invalid file format");
  }

  file.read(reinterpret_cast<char *>(&version), sizeof(version));
  if (version != kVersion) {
    throw StorageException("Unsupported file version");
  }

  uint16_t nameLen;
  file.read(reinterpret_cast<char *>(&nameLen), sizeof(nameLen));
  table_name.resize(nameLen);
  file.read(&table_name[0], nameLen);
}

}  // namespace db
