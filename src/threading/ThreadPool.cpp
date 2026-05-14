#include "include/threading/ThreadPool.h"
#include "include/utils/Logger.h"

namespace db
{

	ThreadPool::ThreadPool(size_t threadCount) : shutdownFlag(false)
	{
		for (size_t i = 0; i < threadCount; ++i)
		{
			workers.emplace_back([this]
													 { workerLoop(); });
		}
		DB_LOG_INFO("ThreadPool created with ", threadCount, " workers");
	}

	ThreadPool::~ThreadPool()
	{
		shutdown();
	}

	void ThreadPool::submit(Task task)
	{
		{
			std::lock_guard<std::mutex> lock(shutdownMutex);
			if (shutdownFlag)
			{
				DB_LOG_WARNING("Attempting to submit task to shutdown thread pool");
				return;
			}
		}
		taskQueue.enqueue(task);
	}

	void ThreadPool::shutdown()
	{
		{
			std::lock_guard<std::mutex> lock(shutdownMutex);
			if (shutdownFlag)
				return;
			shutdownFlag = true;
		}

		// Enqueue sentinel tasks to wake up all workers
		for (size_t i = 0; i < workers.size(); ++i)
		{
			taskQueue.enqueue(nullptr);
		}

		// Wait for all workers to finish
		for (auto &worker : workers)
		{
			if (worker.joinable())
			{
				worker.join();
			}
		}

		DB_LOG_INFO("ThreadPool shut down gracefully");
	}

	size_t ThreadPool::getThreadCount() const
	{
		return workers.size();
	}

	size_t ThreadPool::getPendingTaskCount() const
	{
		return taskQueue.size();
	}

	void ThreadPool::workerLoop()
	{
		Task task;
		while (true)
		{
			taskQueue.dequeue(task, true); // blocking

			{
				std::lock_guard<std::mutex> lock(shutdownMutex);
				if (shutdownFlag && !task)
					break;
			}

			if (!task)
				break;

			try
			{
				task();
			}
			catch (const std::exception &e)
			{
				DB_LOG_ERROR("Task threw exception: ", e.what());
			}
		}
	}

} // namespace db
