#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "threading/work_queue.h"

namespace db {

using Task = std::function<void()>;

class ThreadPool {
 public:
  explicit ThreadPool(size_t thread_count = 4);
  ~ThreadPool();

  // Delete copy operations
  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;

  // Submit a task to be executed
  void submit(Task task);

  // Shutdown the thread pool
  void shutdown();

  // Get number of worker threads
  size_t get_thread_count() const;

  // Get pending task count
  size_t get_pending_task_count() const;

 private:
  std::vector<std::thread> workers_;
  WorkQueue<Task> task_queue_;
  bool shutdown_flag_{};
  std::mutex shutdown_mutex_{};

  void worker_loop();
};

}  // namespace db
