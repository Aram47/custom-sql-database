#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace db {

/**
 * Append-only write-ahead journal for crash recovery of committed dirty tables.
 * Records: TABLE_BLOB, COMMIT, DONE.
 */
class WalManager {
 public:
  explicit WalManager(std::string directory_path);

  void append_table_blob(const std::string &table_name,
                         const std::vector<uint8_t> &blob);
  void append_commit(uint64_t txn_id);
  void append_done();
  void sync();
  void truncate();

  /** Replays committed-but-not-done blobs onto .db files; then truncates. */
  void recover();

  static std::string wal_path(const std::string &directory_path);

 private:
  enum class RecordType : uint8_t { TableBlob = 1, Commit = 2, Done = 3 };

  std::string directory_path_;
  std::string path_;

  void append_record(RecordType type, const std::vector<uint8_t> &payload);
};

}  // namespace db
