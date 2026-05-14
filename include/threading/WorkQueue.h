#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <functional>

namespace db
{

	template <typename T>
	class WorkQueue
	{
	public:
		WorkQueue() = default;
		~WorkQueue() = default;

		// Delete copy operations
		WorkQueue(const WorkQueue &) = delete;
		WorkQueue &operator=(const WorkQueue &) = delete;

		void enqueue(T item)
		{
			{
				std::unique_lock<std::mutex> lock(mutex);
				queue.push(item);
			}
			cv.notify_one();
		}

		bool dequeue(T &item, bool blocking = true)
		{
			std::unique_lock<std::mutex> lock(mutex);

			if (blocking)
			{
				cv.wait(lock, [this]
								{ return !queue.empty(); });
			}
			else
			{
				if (queue.empty())
					return false;
			}

			if (queue.empty())
				return false;

			item = queue.front();
			queue.pop();
			return true;
		}

		size_t size() const
		{
			std::unique_lock<std::mutex> lock(mutex);
			return queue.size();
		}

		bool empty() const
		{
			std::unique_lock<std::mutex> lock(mutex);
			return queue.empty();
		}

	private:
		std::queue<T> queue;
		mutable std::mutex mutex;
		std::condition_variable cv;
	};

} // namespace db
