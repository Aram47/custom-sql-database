#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>

namespace db {

template <typename T>
class WorkQueue {
 public:
  WorkQueue() = default;
  ~WorkQueue() = default;

  // Delete copy operations
  WorkQueue(const WorkQueue &) = delete;
  WorkQueue &operator=(const WorkQueue &) = delete;

  void enqueue(T item) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      queue_.push(item);
    }
    cv_.notify_one();
  }

  bool dequeue(T &item, bool blocking = true) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (blocking) {
      cv_.wait(lock, [this] { return !queue_.empty(); });
    } else {
      if (queue_.empty()) return false;
    }

    if (queue_.empty()) return false;

    item = queue_.front();
    queue_.pop();
    return true;
  }

  size_t size() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return queue_.size();
  }

  bool empty() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return queue_.empty();
  }

 private:
  std::queue<T> queue_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
};

}  // namespace db
