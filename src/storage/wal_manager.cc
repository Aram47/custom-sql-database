#include "storage/wal_manager.h"

#include <cstring>
#include <filesystem>
#include <fstream>

#include "utils/exceptions.h"
#include "utils/logger.h"

namespace fs = std::filesystem;

namespace db {

WalManager::WalManager(std::string directory_path)
    : directory_path_(std::move(directory_path)),
      path_(wal_path(directory_path_)) {}

std::string WalManager::wal_path(const std::string &directory_path) {
  return directory_path + "/wal.log";
}

void WalManager::append_record(RecordType type,
                               const std::vector<uint8_t> &payload) {
  if (!fs::exists(directory_path_)) {
    fs::create_directories(directory_path_);
  }
  std::ofstream file(path_, std::ios::binary | std::ios::app);
  if (!file) {
    throw StorageException("Cannot open WAL for append: " + path_);
  }
  uint8_t type_val = static_cast<uint8_t>(type);
  uint32_t size = static_cast<uint32_t>(payload.size());
  file.write(reinterpret_cast<const char *>(&type_val), sizeof(type_val));
  file.write(reinterpret_cast<const char *>(&size), sizeof(size));
  if (!payload.empty()) {
    file.write(reinterpret_cast<const char *>(payload.data()), payload.size());
  }
  file.flush();
  if (!file) {
    throw StorageException("Failed writing WAL: " + path_);
  }
}

void WalManager::append_table_blob(const std::string &table_name,
                                   const std::vector<uint8_t> &blob) {
  std::vector<uint8_t> payload;
  uint16_t name_len = static_cast<uint16_t>(table_name.size());
  payload.push_back(static_cast<uint8_t>(name_len & 0xff));
  payload.push_back(static_cast<uint8_t>((name_len >> 8) & 0xff));
  payload.insert(payload.end(), table_name.begin(), table_name.end());
  payload.insert(payload.end(), blob.begin(), blob.end());
  append_record(RecordType::TableBlob, payload);
}

void WalManager::append_commit(uint64_t txn_id) {
  std::vector<uint8_t> payload(sizeof(txn_id));
  std::memcpy(payload.data(), &txn_id, sizeof(txn_id));
  append_record(RecordType::Commit, payload);
}

void WalManager::append_done() { append_record(RecordType::Done, {}); }

void WalManager::sync() {
  std::ofstream file(path_, std::ios::binary | std::ios::app);
  if (!file) {
    return;
  }
  file.flush();
}

void WalManager::truncate() {
  std::error_code err;
  fs::remove(path_, err);
}

void WalManager::recover() {
  if (!fs::exists(path_)) {
    return;
  }
  std::ifstream file(path_, std::ios::binary);
  if (!file) {
    throw StorageException("Cannot open WAL for recovery: " + path_);
  }
  struct PendingBlob {
    std::string table_name;
    std::vector<uint8_t> blob;
  };
  std::vector<PendingBlob> pending;
  bool saw_commit = false;
  bool saw_done = false;
  while (file) {
    uint8_t type_val = 0;
    file.read(reinterpret_cast<char *>(&type_val), sizeof(type_val));
    if (!file) {
      break;
    }
    uint32_t size = 0;
    file.read(reinterpret_cast<char *>(&size), sizeof(size));
    if (!file) {
      break;
    }
    std::vector<uint8_t> payload(size);
    if (size > 0) {
      file.read(reinterpret_cast<char *>(payload.data()), size);
      if (!file) {
        break;
      }
    }
    const auto type = static_cast<RecordType>(type_val);
    if (type == RecordType::TableBlob) {
      if (payload.size() < 2) {
        continue;
      }
      uint16_t name_len =
          static_cast<uint16_t>(payload[0] | (payload[1] << 8));
      if (payload.size() < static_cast<size_t>(2 + name_len)) {
        continue;
      }
      PendingBlob item;
      item.table_name.assign(reinterpret_cast<char *>(payload.data() + 2),
                             name_len);
      item.blob.assign(payload.begin() + 2 + name_len, payload.end());
      pending.push_back(std::move(item));
    } else if (type == RecordType::Commit) {
      saw_commit = true;
    } else if (type == RecordType::Done) {
      saw_done = true;
      pending.clear();
      saw_commit = false;
    }
  }
  file.close();
  if (saw_commit && !saw_done) {
    for (const auto &item : pending) {
      const std::string dest =
          directory_path_ + "/" + item.table_name + ".db";
      const std::string temp = dest + ".tmp";
      {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
          throw StorageException("WAL recovery write failed: " + temp);
        }
        out.write(reinterpret_cast<const char *>(item.blob.data()),
                  static_cast<std::streamsize>(item.blob.size()));
        out.flush();
      }
      fs::rename(temp, dest);
      DB_LOG_INFO("WAL recovery restored table '", item.table_name, "'");
    }
  }
  truncate();
}

}  // namespace db
