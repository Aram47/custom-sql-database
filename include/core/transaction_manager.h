#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace db {

enum class TransactionStatus { InProgress, Committed, Aborted };

/** Snapshot taken at BEGIN for snapshot-isolation visibility checks. */
struct TransactionSnapshot {
  uint64_t xmax{0};
  std::unordered_set<uint64_t> active_xids;
};

/**
 * Allocates transaction IDs, tracks commit/abort, and builds read snapshots.
 */
class TransactionManager {
 public:
  uint64_t beginTransaction();
  TransactionSnapshot captureSnapshot() const;
  void registerSnapshot(uint64_t xid, const TransactionSnapshot &snapshot);
  void commitTransaction(uint64_t xid);
  void abortTransaction(uint64_t xid);
  bool isCommitted(uint64_t xid) const;
  bool isAborted(uint64_t xid) const;
  bool isVisible(uint64_t xmin, uint64_t xmax, uint64_t reader_xid,
                 const TransactionSnapshot *snapshot) const;
  /** Lowest snapshot xmax among active readers; safe GC cutoff. */
  uint64_t getVacuumHorizon() const;

 private:
  mutable std::mutex mutex_;
  uint64_t next_xid_{1};
  std::unordered_map<uint64_t, TransactionStatus> statuses_;
  std::unordered_set<uint64_t> active_xids_;
  std::unordered_map<uint64_t, TransactionSnapshot> active_snapshots_;
};

}  // namespace db
