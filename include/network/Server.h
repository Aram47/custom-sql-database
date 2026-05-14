#pragma once

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <functional>
#include "include/core/Database.h"
#include "include/network/Protocol.h"
#include "include/threading/ThreadPool.h"

namespace db
{

	class Connection : public std::enable_shared_from_this<Connection>
	{
	public:
		Connection(int clientSocket, Database *database, ThreadPool *threadPool,
							 std::function<void(std::shared_ptr<Connection>)> onSessionEnded);
		~Connection();

		void start();
		void stop();

	private:
		int clientSocket;
		Database *database;
		ThreadPool *threadPool;
		std::function<void(std::shared_ptr<Connection>)> onSessionEnded;
		std::atomic<bool> active;
		std::thread connectionThread;
		std::mutex sendMutex;

		void run();
		std::string readMessage();
		void sendMessage(const std::string &message);
	};

	class Server
	{
	public:
		Server(int port = 9000, size_t threadPoolSize = 4, std::string dataDirectory = "data");
		~Server();

		void start();
		void stop();
		void wait();

		bool isRunning() const;
		int getPort() const;

	private:
		int port;
		std::string dataDirectory;
		int serverSocket;
		std::atomic<bool> running;
		std::thread acceptThread;
		ThreadPool threadPool;
		Database database;
		std::mutex connectionsMutex;
		std::vector<std::shared_ptr<Connection>> connections;

		void acceptConnections();
		void scheduleUnregisterConnection(std::shared_ptr<Connection> conn);
	};

} // namespace db
