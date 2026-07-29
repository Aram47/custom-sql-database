#include "core/transaction_manager.h"

#include <limits>

namespace db {

uint64_t TransactionManager::beginTransaction() {
  std::lock_guard<std::mutex> lock(mutex_);
  const uint64_t xid = next_xid_++;
  statuses_[xid] = TransactionStatus::InProgress;
  active_xids_.insert(xid);
  return xid;
}

TransactionSnapshot TransactionManager::captureSnapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  TransactionSnapshot snapshot;
  snapshot.xmax = next_xid_;
  snapshot.active_xids = active_xids_;
  return snapshot;
}

void TransactionManager::registerSnapshot(
    uint64_t xid, const TransactionSnapshot &snapshot) {
  std::lock_guard<std::mutex> lock(mutex_);
  active_snapshots_[xid] = snapshot;
}

void TransactionManager::commitTransaction(uint64_t xid) {
  std::lock_guard<std::mutex> lock(mutex_);
  statuses_[xid] = TransactionStatus::Committed;
  active_xids_.erase(xid);
  active_snapshots_.erase(xid);
}

void TransactionManager::abortTransaction(uint64_t xid) {
  std::lock_guard<std::mutex> lock(mutex_);
  statuses_[xid] = TransactionStatus::Aborted;
  active_xids_.erase(xid);
  active_snapshots_.erase(xid);
}

bool TransactionManager::isCommitted(uint64_t xid) const {
  if (xid == 0) {
    return true;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = statuses_.find(xid);
  if (it == statuses_.end()) {
    return false;
  }
  return it->second == TransactionStatus::Committed;
}

bool TransactionManager::isAborted(uint64_t xid) const {
  if (xid == 0) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = statuses_.find(xid);
  if (it == statuses_.end()) {
    return false;
  }
  return it->second == TransactionStatus::Aborted;
}

uint64_t TransactionManager::getVacuumHorizon() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (active_snapshots_.empty()) {
    return next_xid_;
  }
  uint64_t horizon = std::numeric_limits<uint64_t>::max();
  for (const auto &[xid, snapshot] : active_snapshots_) {
    (void)xid;
    if (snapshot.xmax < horizon) {
      horizon = snapshot.xmax;
    }
  }
  return horizon;
}

bool TransactionManager::canFreezeCommitted(uint64_t xid) const {
  if (xid == 0) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &[owner, snapshot] : active_snapshots_) {
    (void)owner;
    // Same predicates as isVisible: these snapshots still treat xid as unseen.
    if (xid >= snapshot.xmax || snapshot.active_xids.count(xid) > 0) {
      return false;
    }
  }
  return true;
}

bool TransactionManager::isVisible(uint64_t xmin, uint64_t xmax,
                                   uint64_t reader_xid,
                                   const TransactionSnapshot *snapshot) const {
  if (xmin == 0) {
    if (xmax == 0) {
      return true;
    }
    if (xmax == reader_xid) {
      return false;
    }
    if (snapshot == nullptr) {
      return !isCommitted(xmax);
    }
    if (xmax >= snapshot->xmax || snapshot->active_xids.count(xmax) > 0) {
      return true;
    }
    return !isCommitted(xmax);
  }
  if (xmin == reader_xid) {
    return xmax != reader_xid;
  }
  if (snapshot == nullptr) {
    if (!isCommitted(xmin)) {
      return false;
    }
    if (xmax == 0) {
      return true;
    }
    if (xmax == reader_xid) {
      return false;
    }
    return !isCommitted(xmax);
  }
  if (xmin >= snapshot->xmax || snapshot->active_xids.count(xmin) > 0) {
    return false;
  }
  if (!isCommitted(xmin)) {
    return false;
  }
  if (xmax == 0) {
    return true;
  }
  if (xmax == reader_xid) {
    return false;
  }
  if (xmax >= snapshot->xmax || snapshot->active_xids.count(xmax) > 0) {
    return true;
  }
  return !isCommitted(xmax);
}

}  // namespace db
