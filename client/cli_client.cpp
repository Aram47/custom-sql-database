#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sstream>

class DatabaseClient
{
private:
	int socket;
	std::string host;
	int port;

public:
	DatabaseClient(const std::string &host = "127.0.0.1", int port = 9000)
			: socket(-1), host(host), port(port) {}

	~DatabaseClient()
	{
		disconnect();
	}

	bool connect()
	{
		socket = ::socket(AF_INET, SOCK_STREAM, 0);
		if (socket < 0)
		{
			std::cerr << "Failed to create socket" << std::endl;
			return false;
		}

		sockaddr_in serverAddr;
		std::memset(&serverAddr, 0, sizeof(serverAddr));
		serverAddr.sin_family = AF_INET;
		serverAddr.sin_port = htons(port);

		if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) <= 0)
		{
			std::cerr << "Invalid address" << std::endl;
			close(socket);
			return false;
		}

		if (::connect(socket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
		{
			std::cerr << "Connection failed" << std::endl;
			close(socket);
			return false;
		}

		std::cout << "Connected to database server at " << host << ":" << port << std::endl;
		return true;
	}

	void disconnect()
	{
		if (socket >= 0)
		{
			close(socket);
			socket = -1;
		}
	}

	bool executeQuery(const std::string &query)
	{
		if (socket < 0)
		{
			std::cerr << "Not connected" << std::endl;
			return false;
		}

		std::string request = "QUERY|" + query + "\n";

		if (send(socket, request.c_str(), request.length(), 0) < 0)
		{
			std::cerr << "Failed to send query" << std::endl;
			return false;
		}

		return readResponse();
	}

	void interactiveMode()
	{
		std::string query;
		std::cout << "\nInteractive Mode (type 'quit' to exit, 'help' for help)" << std::endl;
		std::cout << "======================================================" << std::endl;

		while (true)
		{
			std::cout << "SQL> ";
			std::getline(std::cin, query);

			if (query == "quit" || query == "exit")
			{
				break;
			}

			if (query == "help")
			{
				printHelp();
				continue;
			}

			if (query.empty())
			{
				continue;
			}

			executeQuery(query);
		}
	}

	void runBatchFile(const std::string &filename)
	{
		std::ifstream file(filename);
		if (!file)
		{
			std::cerr << "Cannot open file: " << filename << std::endl;
			return;
		}

		std::string line;
		std::string query;
		while (std::getline(file, line))
		{
			if (line.empty() || line[0] == '#')
				continue;
			query += line + " ";
			if (line.back() == ';')
			{
				query.pop_back(); // Remove semicolon
				executeQuery(query);
				query.clear();
			}
		}

		if (!query.empty())
		{
			executeQuery(query);
		}
	}

private:
	bool readResponse()
	{
		char buffer[8192];
		std::memset(buffer, 0, sizeof(buffer));

		ssize_t bytesRead = recv(socket, buffer, sizeof(buffer) - 1, 0);
		if (bytesRead <= 0)
		{
			std::cerr << "Failed to read response" << std::endl;
			return false;
		}

		buffer[bytesRead] = '\0';
		std::string response(buffer);

		// Parse and display response
		if (response.find("ERROR") == 0)
		{
			std::cerr << "Error: " << response.substr(6) << std::endl;
			return false;
		}

		if (response.find("OK|") == 0)
		{
			std::string data = response.substr(3);
			if (data == "Goodbye\n")
			{
				std::cout << "Disconnected" << std::endl;
				return true;
			}

			// Parse tab-separated values
			std::istringstream iss(data);
			std::string line;
			bool firstLine = true;
			while (std::getline(iss, line))
			{
				if (line.empty())
					continue;

				// Replace tabs with pipes for display
				for (char &c : line)
				{
					if (c == '\t')
						c = ' ';
				}
				std::cout << line << std::endl;
				firstLine = false;
			}

			if (firstLine)
			{
				std::cout << "Query executed successfully" << std::endl;
			}
			return true;
		}

		if (response == "PONG\n")
		{
			std::cout << "Server is alive" << std::endl;
			return true;
		}

		std::cout << response;
		return true;
	}

	void printHelp()
	{
		std::cout << "\n=== Database Client Help ===" << std::endl;
		std::cout << "Commands:" << std::endl;
		std::cout << "  CREATE TABLE <name> (...)" << std::endl;
		std::cout << "  INSERT INTO <table> VALUES (...)" << std::endl;
		std::cout << "  SELECT * FROM <table>" << std::endl;
		std::cout << "  UPDATE <table> SET ... WHERE ..." << std::endl;
		std::cout << "  DELETE FROM <table> WHERE ..." << std::endl;
		std::cout << "\nClient Commands:" << std::endl;
		std::cout << "  help  - Show this help" << std::endl;
		std::cout << "  quit  - Exit the client" << std::endl;
		std::cout << "============================\n"
							<< std::endl;
	}
};

int main(int argc, char *argv[])
{
	DatabaseClient client;

	if (!client.connect())
	{
		return 1;
	}

	if (argc > 1)
	{
		// Batch mode: execute file
		std::cout << "Executing batch file: " << argv[1] << std::endl;
		client.runBatchFile(argv[1]);
	}
	else
	{
		// Interactive mode
		client.interactiveMode();
	}

	return 0;
}
