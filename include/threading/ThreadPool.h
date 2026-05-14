#pragma once

#include <thread>
#include <vector>
#include <functional>
#include <memory>
#include "include/threading/WorkQueue.h"

namespace db
{

	using Task = std::function<void()>;

	class ThreadPool
	{
	public:
		explicit ThreadPool(size_t threadCount = 4);
		~ThreadPool();

		// Delete copy operations
		ThreadPool(const ThreadPool &) = delete;
		ThreadPool &operator=(const ThreadPool &) = delete;

		// Submit a task to be executed
		void submit(Task task);

		// Shutdown the thread pool
		void shutdown();

		// Get number of worker threads
		size_t getThreadCount() const;

		// Get pending task count
		size_t getPendingTaskCount() const;

	private:
		std::vector<std::thread> workers;
		WorkQueue<Task> taskQueue;
		bool shutdownFlag;
		std::mutex shutdownMutex;

		void workerLoop();
	};

} // namespace db
