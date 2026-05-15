#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "platform/tcp_socket.h"

class DatabaseClient {
 private:
  db::platform::TcpSocket socket_{};
  std::string host_;
  int port_{};

 public:
  DatabaseClient(const std::string &host = "127.0.0.1", int port = 9000)
      : host_(host), port_(port) {}

  ~DatabaseClient() { disconnect(); }

  bool connect() {
    try {
      db::platform::TcpSocket::startup();
      socket_ = db::platform::TcpSocket::create_tcp();
      socket_.connect(host_, static_cast<uint16_t>(port_));
      std::cout << "Connected to database server at " << host_ << ":" << port_
                << std::endl;
      return true;
    } catch (const std::exception &e) {
      std::cerr << "Connection failed: " << e.what() << std::endl;
      disconnect();
      return false;
    }
  }

  void disconnect() {
    if (socket_.is_valid()) {
      socket_.close();
    }
    db::platform::TcpSocket::cleanup();
  }

  bool execute_query(const std::string &query) {
    if (!socket_.is_valid()) {
      std::cerr << "Not connected" << std::endl;
      return false;
    }

    std::string request = "QUERY|" + query + "\n";

    if (socket_.send(request.c_str(), request.length()) < 0) {
      std::cerr << "Failed to send query" << std::endl;
      return false;
    }

    return read_response();
  }

  void interactive_mode() {
    std::string query;
    std::cout << "\nInteractive Mode (type 'quit' to exit, 'help' for help)"
              << std::endl;
    std::cout << "======================================================"
              << std::endl;

    while (true) {
      std::cout << "SQL> ";
      std::getline(std::cin, query);

      if (query == "quit" || query == "exit") {
        break;
      }

      if (query == "help") {
        print_help();
        continue;
      }

      if (query.empty()) {
        continue;
      }

      execute_query(query);
    }
  }

  void run_batch_file(const std::string &filename) {
    std::ifstream file(filename);
    if (!file) {
      std::cerr << "Cannot open file: " << filename << std::endl;
      return;
    }

    std::string line;
    std::string query;
    while (std::getline(file, line)) {
      if (line.empty() || line[0] == '#') continue;
      query += line + " ";
      if (line.back() == ';') {
        query.pop_back();
        execute_query(query);
        query.clear();
      }
    }

    if (!query.empty()) {
      execute_query(query);
    }
  }

 private:
  bool read_response() {
    char buffer[8192];
    std::memset(buffer, 0, sizeof(buffer));

    ssize_t bytes_read = socket_.recv(buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
      std::cerr << "Failed to read response" << std::endl;
      return false;
    }

    buffer[bytes_read] = '\0';
    std::string response(buffer);

    if (response.find("ERROR") == 0) {
      std::cerr << "Error: " << response.substr(6) << std::endl;
      return false;
    }

    if (response.find("OK|") == 0) {
      std::string data = response.substr(3);
      if (data == "Goodbye\n") {
        std::cout << "Disconnected" << std::endl;
        return true;
      }

      std::istringstream iss(data);
      std::string line;
      bool first_line = true;
      while (std::getline(iss, line)) {
        if (line.empty()) continue;

        for (char &c : line) {
          if (c == '\t') c = ' ';
        }
        std::cout << line << std::endl;
        first_line = false;
      }

      if (first_line) {
        std::cout << "Query executed successfully" << std::endl;
      }
      return true;
    }

    if (response == "PONG\n") {
      std::cout << "Server is alive" << std::endl;
      return true;
    }

    std::cout << response;
    return true;
  }

  void print_help() {
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
    std::cout << "============================\n" << std::endl;
  }
};

int main(int argc, char *argv[]) {
  DatabaseClient client;

  if (!client.connect()) {
    return 1;
  }

  if (argc > 1) {
    std::cout << "Executing batch file: " << argv[1] << std::endl;
    client.run_batch_file(argv[1]);
  } else {
    client.interactive_mode();
  }

  return 0;
}
