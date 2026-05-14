#include "include/network/Server.h"
#include "include/utils/Logger.h"
#include <algorithm>
#include <cstring>
#include <future>
#include <memory>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>

namespace db
{

	// ==================== Connection ====================

	Connection::Connection(int clientSocket, Database *database, ThreadPool *threadPool,
												 std::function<void(std::shared_ptr<Connection>)> onSessionEnded)
			: clientSocket(clientSocket), database(database), threadPool(threadPool),
				onSessionEnded(std::move(onSessionEnded)), active(true) {}

	Connection::~Connection()
	{
		stop();
	}

	void Connection::start()
	{
		connectionThread = std::thread([this]
																	 { run(); });
	}

	void Connection::stop()
	{
		active = false;
		if (clientSocket >= 0)
		{
			shutdown(clientSocket, SHUT_RDWR);
			close(clientSocket);
			clientSocket = -1;
		}
		if (connectionThread.joinable())
		{
			connectionThread.join();
		}
	}

	void Connection::run()
	{
		DB_LOG_INFO("Client connected");

		try
		{
			while (active.load())
			{
				std::string request = readMessage();
				if (request.empty())
					break;

				DB_LOG_DEBUG("Received request: ", request);

				auto completion = std::make_shared<std::promise<void>>();
				std::future<void> done = completion->get_future();

				threadPool->submit([this, request, completion]()
													 {
														 try
														 {
															 auto req = Protocol::parseRequest(request);

															 if (req.type == "QUERY")
															 {
																 auto result = database->executeQuery(req.data);
																 std::string response = Protocol::formatQueryResult(result);
																 sendMessage(response);
															 }
															 else if (req.type == "PING")
															 {
																 sendMessage("PONG\n");
															 }
															 else if (req.type == "QUIT")
															 {
																 sendMessage("OK|Goodbye\n");
																 active = false;
															 }
															 else
															 {
																 sendMessage("ERROR|Unknown command\n");
															 }
														 }
														 catch (const std::exception &e)
														 {
															 DB_LOG_ERROR("Error handling request: ", e.what());
															 try
															 {
																 sendMessage(std::string("ERROR|") + e.what() + "\n");
															 }
															 catch (...)
															 {
															 }
														 }
														 completion->set_value();
													 });

				done.wait();
			}
		}
		catch (const std::exception &e)
		{
			DB_LOG_ERROR("Connection error: ", e.what());
		}

		if (clientSocket >= 0)
		{
			shutdown(clientSocket, SHUT_RDWR);
			close(clientSocket);
			clientSocket = -1;
		}

		DB_LOG_INFO("Client disconnected");

		if (onSessionEnded)
		{
			onSessionEnded(shared_from_this());
		}
	}

	std::string Connection::readMessage()
	{
		char buffer[4096];
		std::memset(buffer, 0, sizeof(buffer));

		ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

		if (bytesRead <= 0)
		{
			return "";
		}

		buffer[bytesRead] = '\0';
		return std::string(buffer);
	}

	void Connection::sendMessage(const std::string &message)
	{
		std::lock_guard<std::mutex> lock(sendMutex);
		if (clientSocket < 0)
			return;

		ssize_t bytesSent = send(clientSocket, message.c_str(), message.length(), 0);
		if (bytesSent < 0)
		{
			throw std::runtime_error("Failed to send message");
		}
	}

	// ==================== Server ====================

	Server *globalServerPtr = nullptr;

	void serverSignalHandler(int signal)
	{
		(void)signal;
		if (globalServerPtr)
		{
			globalServerPtr->stop();
		}
	}

	Server::Server(int port, size_t threadPoolSize, std::string dataDirectory)
			: port(port), dataDirectory(std::move(dataDirectory)), serverSocket(-1), running(false),
				threadPool(threadPoolSize), database(this->dataDirectory) {}

	Server::~Server()
	{
		stop();
	}

	void Server::start()
	{
		if (running)
			return;

		database.loadFromDisk();

		serverSocket = socket(AF_INET, SOCK_STREAM, 0);
		if (serverSocket < 0)
		{
			throw NetworkException("Failed to create socket");
		}

		int reuse = 1;
		if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
		{
			close(serverSocket);
			throw NetworkException("Failed to set socket option");
		}

		sockaddr_in serverAddr;
		std::memset(&serverAddr, 0, sizeof(serverAddr));
		serverAddr.sin_family = AF_INET;
		serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
		serverAddr.sin_port = htons(port);

		if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
		{
			close(serverSocket);
			throw NetworkException("Failed to bind socket");
		}

		if (listen(serverSocket, 5) < 0)
		{
			close(serverSocket);
			throw NetworkException("Failed to listen");
		}

		running = true;
		globalServerPtr = this;

		signal(SIGINT, serverSignalHandler);

		acceptThread = std::thread([this]
															 { acceptConnections(); });

		DB_LOG_INFO("Server started on port ", port);
	}

	void Server::scheduleUnregisterConnection(std::shared_ptr<Connection> conn)
	{
		threadPool.submit([this, conn]()
											{
												std::lock_guard<std::mutex> lock(connectionsMutex);
												connections.erase(std::remove(connections.begin(), connections.end(), conn),
																					connections.end());
											});
	}

	void Server::stop()
	{
		running = false;

		if (serverSocket >= 0)
		{
			shutdown(serverSocket, SHUT_RDWR);
			close(serverSocket);
			serverSocket = -1;
		}

		if (acceptThread.joinable())
		{
			acceptThread.join();
		}

		std::vector<std::shared_ptr<Connection>> snapshot;
		{
			std::lock_guard<std::mutex> lock(connectionsMutex);
			snapshot = connections;
		}
		for (const auto &c : snapshot)
		{
			c->stop();
		}

		threadPool.shutdown();
		DB_LOG_INFO("Server stopped");
	}

	void Server::wait()
	{
		if (acceptThread.joinable())
		{
			acceptThread.join();
		}
	}

	bool Server::isRunning() const
	{
		return running;
	}

	int Server::getPort() const
	{
		return port;
	}

	void Server::acceptConnections()
	{
		while (running)
		{
			sockaddr_in clientAddr;
			socklen_t clientAddrLen = sizeof(clientAddr);

			int clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddr, &clientAddrLen);
			if (clientSocket < 0)
			{
				if (running)
				{
					DB_LOG_ERROR("Failed to accept connection");
				}
				continue;
			}

			auto connection = std::make_shared<Connection>(
					clientSocket, &database, &threadPool,
					[this](std::shared_ptr<Connection> c)
					{ scheduleUnregisterConnection(std::move(c)); });

			{
				std::lock_guard<std::mutex> lock(connectionsMutex);
				connections.push_back(connection);
			}

			connection->start();
		}
	}

} // namespace db
