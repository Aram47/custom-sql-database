#include "core/plan_cache.h"

namespace db {

PlanCache::PlanCache(size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {}

std::optional<ParsedStatement> PlanCache::get(const std::string &sql) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = entries_.find(sql);
  if (it == entries_.end()) {
    return std::nullopt;
  }
  lru_order_.erase(it->second.second);
  lru_order_.push_front(sql);
  it->second.second = lru_order_.begin();
  return it->second.first;
}

void PlanCache::put(const std::string &sql, ParsedStatement stmt) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = entries_.find(sql);
  if (it != entries_.end()) {
    lru_order_.erase(it->second.second);
    entries_.erase(it);
  }
  if (entries_.size() >= capacity_) {
    const std::string &old = lru_order_.back();
    entries_.erase(old);
    lru_order_.pop_back();
  }
  lru_order_.push_front(sql);
  entries_[sql] = {std::move(stmt), lru_order_.begin()};
}

void PlanCache::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  entries_.clear();
  lru_order_.clear();
}

}  // namespace db
