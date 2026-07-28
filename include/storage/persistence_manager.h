#pragma once

#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <string>

#include "core/table.h"
#include "utils/exceptions.h"

namespace db {

/**
 * Binary on-disk persistence for tables.
 * Saves use a temporary file then rename for atomic replace.
 */
class PersistenceManager {
 public:
  /** Saves a single table atomically (write .tmp then rename to .db). */
  static void save_table(const Table &table, const std::string &file_path);
  static std::unique_ptr<Table> load_table(const std::string &file_path);
  static void save_database(
      const std::map<std::string, std::unique_ptr<Table>> &tables,
      const std::string &directory_path);
  static std::map<std::string, std::unique_ptr<Table>> load_database(
      const std::string &directory_path);
  /** Removes {directory}/{table_name}.db and any stray .tmp. */
  static void remove_table_file(const std::string &directory_path,
                                const std::string &table_name);
  /** Renames on-disk table file from old_name.db to new_name.db. */
  static void rename_table_file(const std::string &directory_path,
                                const std::string &old_name,
                                const std::string &new_name);
  static std::string table_file_path(const std::string &directory_path,
                                     const std::string &table_name);

 private:
  static constexpr uint32_t kMagicNumber = 0x44425442;  // "DBTB"
  /**
   * Format v6: schema header (as v5) then page_count + fixed-size heap pages.
   * WAL still stores the full .db file as a logical table blob (no page-diff WAL).
   */
  static constexpr uint16_t kVersion = 6;
  static constexpr uint16_t kMinSupportedVersion = 1;
  static void write_header(std::ofstream &file, const std::string &table_name);
  /** Reads magic/version/name; returns file format version. */
  static uint16_t read_header(std::ifstream &file, std::string &table_name);
  static void write_string(std::ofstream &file, const std::string &value);
  static std::string read_string(std::ifstream &file);
};

}  // namespace db
