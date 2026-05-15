#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

class DatabaseClient {
 private:
  int sock_fd_{-1};
  std::string host_;
  int port_{};

 public:
  DatabaseClient(const std::string &host = "127.0.0.1", int port = 9000)
      : sock_fd_(-1), host_(host), port_(port) {}

  ~DatabaseClient() { disconnect(); }

  bool connect() {
    sock_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd_ < 0) {
      std::cerr << "Failed to create socket" << std::endl;
      return false;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);

    if (inet_pton(AF_INET, host_.c_str(), &server_addr.sin_addr) <= 0) {
      std::cerr << "Invalid address" << std::endl;
      close(sock_fd_);
      return false;
    }

    if (::connect(sock_fd_, (struct sockaddr *)&server_addr,
                  sizeof(server_addr)) < 0) {
      std::cerr << "Connection failed" << std::endl;
      close(sock_fd_);
      return false;
    }

    std::cout << "Connected to database server at " << host_ << ":" << port_
              << std::endl;
    return true;
  }

  void disconnect() {
    if (sock_fd_ >= 0) {
      close(sock_fd_);
      sock_fd_ = -1;
    }
  }

  bool execute_query(const std::string &query) {
    if (sock_fd_ < 0) {
      std::cerr << "Not connected" << std::endl;
      return false;
    }

    std::string request = "QUERY|" + query + "\n";

    if (send(sock_fd_, request.c_str(), request.length(), 0) < 0) {
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
        query.pop_back();  // Remove semicolon
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

    ssize_t bytes_read = recv(sock_fd_, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0) {
      std::cerr << "Failed to read response" << std::endl;
      return false;
    }

    buffer[bytes_read] = '\0';
    std::string response(buffer);

    // Parse and display response
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

      // Parse tab-separated values
      std::istringstream iss(data);
      std::string line;
      bool first_line = true;
      while (std::getline(iss, line)) {
        if (line.empty()) continue;

        // Replace tabs with pipes for display
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
    // Batch mode: execute file
    std::cout << "Executing batch file: " << argv[1] << std::endl;
    client.run_batch_file(argv[1]);
  } else {
    // Interactive mode
    client.interactive_mode();
  }

  return 0;
}
