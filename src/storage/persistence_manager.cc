#include "storage/persistence_manager.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <vector>

#include "core/check_constraint.h"
#include "core/foreign_key.h"
#include "core/unique_constraint.h"
#include "storage/page_format.h"
#include "types/type_converter.h"
#include "utils/logger.h"

namespace fs = std::filesystem;

namespace db {

std::string PersistenceManager::table_file_path(
    const std::string &directory_path, const std::string &table_name) {
  return directory_path + "/" + table_name + ".db";
}

void PersistenceManager::save_table(const Table &table,
                                    const std::string &file_path) {
  const std::string temp_path = file_path + ".tmp";
  try {
    fs::path parent = fs::path(file_path).parent_path();
    if (!parent.empty()) {
      fs::create_directories(parent);
    }
    {
      std::ofstream file(temp_path, std::ios::binary | std::ios::trunc);
      if (!file) {
        throw StorageException("Cannot open file for writing: " + temp_path);
      }
      write_header(file, table.get_name());
      uint32_t col_count = static_cast<uint32_t>(table.get_column_count());
      file.write(reinterpret_cast<const char *>(&col_count), sizeof(col_count));
      for (size_t i = 0; i < col_count; ++i) {
        const auto &col = table.get_column(i);
        write_string(file, col.get_name());
        uint8_t type_val = static_cast<uint8_t>(col.get_type());
        file.write(reinterpret_cast<const char *>(&type_val), sizeof(type_val));
        uint8_t flags = 0;
        if (!col.is_nullable()) {
          flags |= 0x01;
        }
        if (col.is_primary_key()) {
          flags |= 0x02;
        }
        if (col.is_unique()) {
          flags |= 0x04;
        }
        file.write(reinterpret_cast<const char *>(&flags), sizeof(flags));
        uint8_t has_default = col.has_default() ? 1 : 0;
        file.write(reinterpret_cast<const char *>(&has_default),
                   sizeof(has_default));
        if (has_default) {
          auto serialized =
              TypeConverter::serialize_value(col.get_default_value());
          uint32_t value_len = static_cast<uint32_t>(serialized.size());
          file.write(reinterpret_cast<const char *>(&value_len),
                     sizeof(value_len));
          file.write(reinterpret_cast<const char *>(serialized.data()),
                     value_len);
        }
      }
      const auto &secondary = table.get_secondary_indexes();
      uint32_t index_count = static_cast<uint32_t>(secondary.size());
      file.write(reinterpret_cast<const char *>(&index_count),
                 sizeof(index_count));
      for (const auto &[index_name, column_names] : secondary) {
        write_string(file, index_name);
        uint32_t col_count = static_cast<uint32_t>(column_names.size());
        file.write(reinterpret_cast<const char *>(&col_count),
                   sizeof(col_count));
        for (const std::string &column_name : column_names) {
          write_string(file, column_name);
        }
      }
      const auto &foreign_keys = table.get_foreign_keys();
      uint32_t fk_count = static_cast<uint32_t>(foreign_keys.size());
      file.write(reinterpret_cast<const char *>(&fk_count), sizeof(fk_count));
      for (const auto &fk : foreign_keys) {
        uint32_t fk_col_count = static_cast<uint32_t>(fk.child_columns.size());
        file.write(reinterpret_cast<const char *>(&fk_col_count),
                   sizeof(fk_col_count));
        for (const std::string &column_name : fk.child_columns) {
          write_string(file, column_name);
        }
        write_string(file, fk.parent_table);
        for (const std::string &column_name : fk.parent_columns) {
          write_string(file, column_name);
        }
        uint8_t on_delete = static_cast<uint8_t>(fk.on_delete);
        uint8_t on_update = static_cast<uint8_t>(fk.on_update);
        file.write(reinterpret_cast<const char *>(&on_delete),
                   sizeof(on_delete));
        file.write(reinterpret_cast<const char *>(&on_update),
                   sizeof(on_update));
      }
      const auto &checks = table.get_checks();
      uint32_t check_count = static_cast<uint32_t>(checks.size());
      file.write(reinterpret_cast<const char *>(&check_count),
                 sizeof(check_count));
      for (const auto &check : checks) {
        write_string(file, check.name);
        write_string(file, check.expression_text);
      }
      const auto &pk_columns = table.get_primary_key_columns();
      uint32_t pk_count = static_cast<uint32_t>(pk_columns.size());
      file.write(reinterpret_cast<const char *>(&pk_count), sizeof(pk_count));
      for (const std::string &column_name : pk_columns) {
        write_string(file, column_name);
      }
      const auto &unique_constraints = table.get_unique_constraints();
      uint32_t uq_count = static_cast<uint32_t>(unique_constraints.size());
      file.write(reinterpret_cast<const char *>(&uq_count), sizeof(uq_count));
      for (const auto &uq : unique_constraints) {
        write_string(file, uq.name);
        uint32_t uq_col_count = static_cast<uint32_t>(uq.columns.size());
        file.write(reinterpret_cast<const char *>(&uq_col_count),
                   sizeof(uq_col_count));
        for (const std::string &column_name : uq.columns) {
          write_string(file, column_name);
        }
      }
      const_cast<Table &>(table).flush_heap();
      const std::vector<std::vector<uint8_t>> pages =
          table.get_heap().get_page_store().export_pages();
      uint32_t page_count = static_cast<uint32_t>(pages.size());
      file.write(reinterpret_cast<const char *>(&page_count),
                 sizeof(page_count));
      for (const auto &page : pages) {
        if (page.size() != kPageSize) {
          throw StorageException("Invalid page size while saving table");
        }
        file.write(reinterpret_cast<const char *>(page.data()),
                   static_cast<std::streamsize>(kPageSize));
      }
      file.flush();
      if (!file) {
        throw StorageException("Failed while writing: " + temp_path);
      }
    }
    fs::rename(temp_path, file_path);
    DB_LOG_INFO("Saved table '", table.get_name(), "' to ", file_path);
  } catch (const StorageException &) {
    fs::remove(temp_path);
    throw;
  } catch (const std::exception &e) {
    fs::remove(temp_path);
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
    const uint16_t version = read_header(file, table_name);
    auto table = std::make_unique<Table>(table_name);
    uint32_t col_count = 0;
    file.read(reinterpret_cast<char *>(&col_count), sizeof(col_count));
    for (uint32_t i = 0; i < col_count; ++i) {
      std::string name = read_string(file);
      uint8_t type_val = 0;
      file.read(reinterpret_cast<char *>(&type_val), sizeof(type_val));
      DataType type = static_cast<DataType>(type_val);
      uint8_t flags = 0;
      file.read(reinterpret_cast<char *>(&flags), sizeof(flags));
      bool nullable = !(flags & 0x01);
      bool primary_key = (flags & 0x02) != 0;
      bool unique = (flags & 0x04) != 0;
      Column col(name, type, nullable, primary_key, unique);
      if (version >= 4) {
        uint8_t has_default = 0;
        file.read(reinterpret_cast<char *>(&has_default), sizeof(has_default));
        if (has_default) {
          uint32_t value_len = 0;
          file.read(reinterpret_cast<char *>(&value_len), sizeof(value_len));
          std::vector<uint8_t> serialized(value_len);
          file.read(reinterpret_cast<char *>(serialized.data()), value_len);
          col.set_default_value(
              TypeConverter::deserialize_value(serialized, type));
        }
      }
      table->add_column(col);
    }
    if (version >= 2) {
      uint32_t index_count = 0;
      file.read(reinterpret_cast<char *>(&index_count), sizeof(index_count));
      std::map<std::string, std::vector<std::string>> secondary;
      for (uint32_t i = 0; i < index_count; ++i) {
        std::string index_name = read_string(file);
        std::vector<std::string> column_names;
        if (version >= 3) {
          uint32_t index_col_count = 0;
          file.read(reinterpret_cast<char *>(&index_col_count),
                    sizeof(index_col_count));
          column_names.reserve(index_col_count);
          for (uint32_t c = 0; c < index_col_count; ++c) {
            column_names.push_back(read_string(file));
          }
        } else {
          column_names.push_back(read_string(file));
        }
        secondary[index_name] = std::move(column_names);
      }
      table->set_secondary_indexes(secondary);
      uint32_t fk_count = 0;
      file.read(reinterpret_cast<char *>(&fk_count), sizeof(fk_count));
      std::vector<ForeignKeyDefinition> foreign_keys;
      foreign_keys.reserve(fk_count);
      for (uint32_t i = 0; i < fk_count; ++i) {
        ForeignKeyDefinition fk;
        if (version >= 4) {
          uint32_t fk_col_count = 0;
          file.read(reinterpret_cast<char *>(&fk_col_count),
                    sizeof(fk_col_count));
          fk.child_columns.reserve(fk_col_count);
          for (uint32_t c = 0; c < fk_col_count; ++c) {
            fk.child_columns.push_back(read_string(file));
          }
          fk.parent_table = read_string(file);
          fk.parent_columns.reserve(fk_col_count);
          for (uint32_t c = 0; c < fk_col_count; ++c) {
            fk.parent_columns.push_back(read_string(file));
          }
        } else {
          fk.child_columns.push_back(read_string(file));
          fk.parent_table = read_string(file);
          fk.parent_columns.push_back(read_string(file));
        }
        if (version >= 3) {
          uint8_t on_delete = 0;
          uint8_t on_update = 0;
          file.read(reinterpret_cast<char *>(&on_delete), sizeof(on_delete));
          file.read(reinterpret_cast<char *>(&on_update), sizeof(on_update));
          fk.on_delete = static_cast<ReferentialAction>(on_delete);
          fk.on_update = static_cast<ReferentialAction>(on_update);
        }
        foreign_keys.push_back(std::move(fk));
      }
      table->set_foreign_keys(std::move(foreign_keys));
      if (version >= 5) {
        uint32_t check_count = 0;
        file.read(reinterpret_cast<char *>(&check_count), sizeof(check_count));
        std::vector<CheckConstraintDefinition> checks;
        checks.reserve(check_count);
        for (uint32_t i = 0; i < check_count; ++i) {
          CheckConstraintDefinition check;
          check.name = read_string(file);
          check.expression_text = read_string(file);
          check.predicate = parse_check_expression(check.expression_text);
          checks.push_back(std::move(check));
        }
        table->set_checks(std::move(checks));
      }
      if (version >= 7) {
        uint32_t pk_count = 0;
        file.read(reinterpret_cast<char *>(&pk_count), sizeof(pk_count));
        std::vector<std::string> pk_columns;
        pk_columns.reserve(pk_count);
        for (uint32_t i = 0; i < pk_count; ++i) {
          pk_columns.push_back(read_string(file));
        }
        table->set_primary_key_columns(std::move(pk_columns));
        uint32_t uq_count = 0;
        file.read(reinterpret_cast<char *>(&uq_count), sizeof(uq_count));
        std::vector<UniqueConstraintDefinition> unique_constraints;
        unique_constraints.reserve(uq_count);
        for (uint32_t i = 0; i < uq_count; ++i) {
          UniqueConstraintDefinition uq;
          uq.name = read_string(file);
          uint32_t uq_col_count = 0;
          file.read(reinterpret_cast<char *>(&uq_col_count),
                    sizeof(uq_col_count));
          uq.columns.reserve(uq_col_count);
          for (uint32_t c = 0; c < uq_col_count; ++c) {
            uq.columns.push_back(read_string(file));
          }
          unique_constraints.push_back(std::move(uq));
        }
        table->set_unique_constraints(std::move(unique_constraints));
      } else {
        table->sync_key_metadata_from_column_flags();
      }
    }
    if (version >= 6) {
      uint32_t page_count = 0;
      file.read(reinterpret_cast<char *>(&page_count), sizeof(page_count));
      std::vector<std::vector<uint8_t>> pages;
      pages.reserve(page_count);
      for (uint32_t i = 0; i < page_count; ++i) {
        std::vector<uint8_t> page(kPageSize);
        file.read(reinterpret_cast<char *>(page.data()),
                  static_cast<std::streamsize>(kPageSize));
        if (!file) {
          throw StorageException("Truncated page data in " + file_path);
        }
        pages.push_back(std::move(page));
      }
      table->replace_heap_pages(pages);
    } else {
      uint32_t row_count = 0;
      file.read(reinterpret_cast<char *>(&row_count), sizeof(row_count));
      for (uint32_t i = 0; i < row_count; ++i) {
        Row row;
        for (uint32_t j = 0; j < col_count; ++j) {
          uint32_t value_len = 0;
          file.read(reinterpret_cast<char *>(&value_len), sizeof(value_len));
          std::vector<uint8_t> serialized(value_len);
          file.read(reinterpret_cast<char *>(serialized.data()), value_len);
          const auto &col = table->get_column(j);
          Value value =
              TypeConverter::deserialize_value(serialized, col.get_type());
          row.add_value(value);
        }
        table->insert_row(row);
      }
    }
    file.close();
    table->rebuild_indexes();
    table->clear_dirty();
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
      save_table(*table, table_file_path(directory_path, name));
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

void PersistenceManager::remove_table_file(const std::string &directory_path,
                                           const std::string &table_name) {
  const std::string path = table_file_path(directory_path, table_name);
  const std::string temp_path = path + ".tmp";
  std::error_code err;
  fs::remove(path, err);
  fs::remove(temp_path, err);
  DB_LOG_INFO("Removed table file for '", table_name, "' from ",
              directory_path);
}

void PersistenceManager::rename_table_file(const std::string &directory_path,
                                           const std::string &old_name,
                                           const std::string &new_name) {
  const std::string old_path = table_file_path(directory_path, old_name);
  const std::string new_path = table_file_path(directory_path, new_name);
  if (!fs::exists(old_path)) {
    return;
  }
  std::error_code err;
  fs::rename(old_path, new_path, err);
  if (err) {
    throw StorageException("Failed to rename table file from '" + old_name +
                           "' to '" + new_name + "': " + err.message());
  }
  fs::remove(old_path + ".tmp", err);
  DB_LOG_INFO("Renamed table file '", old_name, "' to '", new_name, "'");
}

void PersistenceManager::write_header(std::ofstream &file,
                                      const std::string &table_name) {
  uint32_t magic = kMagicNumber;
  uint16_t version = kVersion;
  file.write(reinterpret_cast<const char *>(&magic), sizeof(magic));
  file.write(reinterpret_cast<const char *>(&version), sizeof(version));
  write_string(file, table_name);
}

uint16_t PersistenceManager::read_header(std::ifstream &file,
                                         std::string &table_name) {
  uint32_t magic = 0;
  uint16_t version = 0;
  file.read(reinterpret_cast<char *>(&magic), sizeof(magic));
  if (magic != kMagicNumber) {
    throw StorageException("Invalid file format");
  }
  file.read(reinterpret_cast<char *>(&version), sizeof(version));
  if (version < kMinSupportedVersion || version > kVersion) {
    throw StorageException("Unsupported file version");
  }
  table_name = read_string(file);
  return version;
}

void PersistenceManager::write_string(std::ofstream &file,
                                      const std::string &value) {
  uint16_t name_len = static_cast<uint16_t>(value.length());
  file.write(reinterpret_cast<const char *>(&name_len), sizeof(name_len));
  file.write(value.c_str(), name_len);
}

std::string PersistenceManager::read_string(std::ifstream &file) {
  uint16_t name_len = 0;
  file.read(reinterpret_cast<char *>(&name_len), sizeof(name_len));
  std::string value(name_len, '\0');
  if (name_len > 0) {
    file.read(&value[0], name_len);
  }
  return value;
}

}  // namespace db
