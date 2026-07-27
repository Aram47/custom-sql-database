#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "utils/exceptions.h"

namespace db {

enum class LockMode { Shared, Exclusive };

/** Table/row lock manager with wait-for-graph deadlock detection.
 *  Lock keys are table names or "table:#rowIndex" for row locks.
 */
class LockManager {
 public:
  void acquire(uint64_t txn_id, const std::string &lock_key, LockMode mode);
  void release_all(uint64_t txn_id);

  static std::string make_row_lock_key(const std::string &table_name,
                                       size_t row_index);

 private:
  struct LockRequest {
    uint64_t txn_id;
    LockMode mode;
    bool granted{false};
  };

  struct TableLockState {
    std::vector<LockRequest> queue;
  };

  std::mutex mutex_;
  std::condition_variable cv_;
  std::unordered_map<std::string, TableLockState> tables_;
  std::unordered_map<uint64_t, std::unordered_set<std::string>> txn_tables_;
  std::unordered_map<uint64_t, std::unordered_set<uint64_t>> wait_for_;

  bool can_grant(const TableLockState &state, uint64_t txn_id,
                 LockMode mode) const;
  bool has_cycle(uint64_t start) const;
  void rebuild_wait_edges_locked();
};

}  // namespace db
