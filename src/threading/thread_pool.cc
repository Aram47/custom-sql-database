#include "threading/thread_pool.h"

#include "utils/logger.h"

namespace db {

ThreadPool::ThreadPool(size_t thread_count) : shutdown_flag_(false) {
  for (size_t i = 0; i < thread_count; ++i) {
    workers_.emplace_back([this] { worker_loop(); });
  }
  DB_LOG_INFO("ThreadPool created with ", thread_count, " workers");
}

ThreadPool::~ThreadPool() { shutdown(); }

void ThreadPool::submit(Task task) {
  {
    std::lock_guard<std::mutex> lock(shutdown_mutex_);
    if (shutdown_flag_) {
      DB_LOG_WARNING("Attempting to submit task to shutdown thread pool");
      return;
    }
  }
  task_queue_.enqueue(task);
}

void ThreadPool::shutdown() {
  {
    std::lock_guard<std::mutex> lock(shutdown_mutex_);
    if (shutdown_flag_) return;
    shutdown_flag_ = true;
  }

  // Enqueue sentinel tasks to wake up all workers
  for (size_t i = 0; i < workers_.size(); ++i) {
    task_queue_.enqueue(nullptr);
  }

  // Wait for all workers to finish
  for (auto &worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }

  DB_LOG_INFO("ThreadPool shut down gracefully");
}

size_t ThreadPool::get_thread_count() const { return workers_.size(); }

size_t ThreadPool::get_pending_task_count() const { return task_queue_.size(); }

void ThreadPool::worker_loop() {
  Task task;
  while (true) {
    task_queue_.dequeue(task, true);  // blocking

    {
      std::lock_guard<std::mutex> lock(shutdown_mutex_);
      if (shutdown_flag_ && !task) break;
    }

    if (!task) break;

    try {
      task();
    } catch (const std::exception &e) {
      DB_LOG_ERROR("Task threw exception: ", e.what());
    }
  }
}

}  // namespace db
