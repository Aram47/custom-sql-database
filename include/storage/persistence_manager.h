#pragma once

#include <fstream>
#include <map>
#include <memory>
#include <string>

#include "core/table.h"
#include "utils/exceptions.h"

namespace db {

class PersistenceManager {
 public:
  // Save a single table to file
  static void save_table(const Table &table, const std::string &file_path);

  // Load a table from file
  static std::unique_ptr<Table> load_table(const std::string &file_path);

  // Save multiple tables to a directory
  static void save_database(
      const std::map<std::string, std::unique_ptr<Table>> &tables,
      const std::string &directory_path);

  // Load multiple tables from directory
  static std::map<std::string, std::unique_ptr<Table>> load_database(
      const std::string &directory_path);

 private:
  static constexpr uint32_t kMagicNumber = 0x44425442;  // "DBTB"
  static constexpr uint16_t kVersion = 1;

  // Helper methods
  static void write_header(std::ofstream &file, const std::string &table_name);
  static void read_header(std::ifstream &file, std::string &table_name);
};

}  // namespace db
