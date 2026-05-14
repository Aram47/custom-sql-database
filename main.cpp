#include <iostream>
#include <csignal>
#include "include/network/Server.h"
#include "include/utils/Logger.h"

using namespace db;

int main()
{
	try
	{
		// Set log level
		Logger::getInstance().setLevel(LogLevel::INFO);

		DB_LOG_INFO("========================================");
		DB_LOG_INFO("SQL Database Engine with CRUD Operations");
		DB_LOG_INFO("========================================");
		DB_LOG_INFO("Starting server on port 9000...");

		// Create and start server
		Server server(9000, 4); // 4 worker threads
		server.start();

		// Wait for shutdown signal
		server.wait();

		DB_LOG_INFO("Server shutdown complete");
		return 0;
	}
	catch (const std::exception &e)
	{
		DB_LOG_ERROR("Fatal error: ", e.what());
		return 1;
	}
}