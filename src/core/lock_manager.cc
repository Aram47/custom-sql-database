#include "core/lock_manager.h"

#include <algorithm>
#include <functional>
#include <queue>
#include <string>

namespace db {

std::string LockManager::make_row_lock_key(const std::string &table_name,
                                           size_t row_index) {
  return table_name + ":#" + std::to_string(row_index);
}

bool LockManager::can_grant(const TableLockState &state, uint64_t txn_id,
                            LockMode mode) const {
  for (const auto &req : state.queue) {
    if (!req.granted) {
      continue;
    }
    if (req.txn_id == txn_id) {
      if (mode == LockMode::Shared || req.mode == LockMode::Exclusive) {
        return true;
      }
      continue;
    }
    if (mode == LockMode::Exclusive || req.mode == LockMode::Exclusive) {
      return false;
    }
  }
  return true;
}

bool LockManager::has_cycle(uint64_t start) const {
  std::unordered_set<uint64_t> visiting;
  std::unordered_set<uint64_t> visited;
  std::function<bool(uint64_t)> dfs = [&](uint64_t node) -> bool {
    if (visiting.count(node)) {
      return true;
    }
    if (visited.count(node)) {
      return false;
    }
    visiting.insert(node);
    auto it = wait_for_.find(node);
    if (it != wait_for_.end()) {
      for (uint64_t next : it->second) {
        if (dfs(next)) {
          return true;
        }
      }
    }
    visiting.erase(node);
    visited.insert(node);
    return false;
  };
  return dfs(start);
}

void LockManager::rebuild_wait_edges_locked() {
  wait_for_.clear();
  for (const auto &[table_name, state] : tables_) {
    (void)table_name;
    std::unordered_set<uint64_t> holders;
    for (const auto &req : state.queue) {
      if (req.granted) {
        holders.insert(req.txn_id);
      }
    }
    for (const auto &req : state.queue) {
      if (req.granted) {
        continue;
      }
      for (uint64_t holder : holders) {
        if (holder != req.txn_id) {
          wait_for_[req.txn_id].insert(holder);
        }
      }
    }
  }
}

void LockManager::acquire(uint64_t txn_id, const std::string &table_name,
                          LockMode mode) {
  std::unique_lock<std::mutex> lock(mutex_);
  TableLockState &state = tables_[table_name];
  for (const auto &req : state.queue) {
    if (req.granted && req.txn_id == txn_id) {
      if (req.mode == LockMode::Exclusive || mode == LockMode::Shared) {
        txn_tables_[txn_id].insert(table_name);
        return;
      }
    }
  }
  state.queue.push_back(LockRequest{txn_id, mode, false});
  while (true) {
    if (can_grant(state, txn_id, mode)) {
      for (auto &req : state.queue) {
        if (req.txn_id == txn_id && !req.granted) {
          req.granted = true;
          if (mode == LockMode::Exclusive) {
            req.mode = LockMode::Exclusive;
          }
          break;
        }
      }
      txn_tables_[txn_id].insert(table_name);
      rebuild_wait_edges_locked();
      cv_.notify_all();
      return;
    }
    rebuild_wait_edges_locked();
    if (has_cycle(txn_id)) {
      state.queue.erase(
          std::remove_if(state.queue.begin(), state.queue.end(),
                         [txn_id](const LockRequest &r) {
                           return r.txn_id == txn_id && !r.granted;
                         }),
          state.queue.end());
      rebuild_wait_edges_locked();
      throw DeadlockException("deadlock detected");
    }
    cv_.wait(lock);
  }
}

void LockManager::release_all(uint64_t txn_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto held = txn_tables_.find(txn_id);
  if (held != txn_tables_.end()) {
    for (const std::string &table_name : held->second) {
      auto it = tables_.find(table_name);
      if (it == tables_.end()) {
        continue;
      }
      auto &queue = it->second.queue;
      queue.erase(std::remove_if(queue.begin(), queue.end(),
                                 [txn_id](const LockRequest &r) {
                                   return r.txn_id == txn_id;
                                 }),
                  queue.end());
      if (queue.empty()) {
        tables_.erase(it);
      }
    }
    txn_tables_.erase(held);
  }
  rebuild_wait_edges_locked();
  cv_.notify_all();
}

}  // namespace db
