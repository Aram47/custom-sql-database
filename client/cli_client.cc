#include <cstdlib>
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
  std::string username_;
  std::string password_;

 public:
  DatabaseClient(const std::string &host = "127.0.0.1", int port = 9000)
      : host_(host), port_(port) {}

  ~DatabaseClient() { disconnect(); }

  void set_credentials(const std::string &username,
                       const std::string &password) {
    username_ = username;
    password_ = password;
  }

  bool connect() {
    try {
      db::platform::TcpSocket::startup();
      socket_ = db::platform::TcpSocket::create_tcp();
      socket_.connect(host_, static_cast<uint16_t>(port_));
      std::cout << "Connected to NoBugDB at " << host_ << ":" << port_
                << std::endl;
      if (!username_.empty()) {
        if (!authenticate()) {
          disconnect();
          return false;
        }
      }
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

  bool authenticate() {
    if (!socket_.is_valid()) {
      std::cerr << "Not connected" << std::endl;
      return false;
    }
    const std::string request = "AUTH|" + username_ + "|" + password_ + "\n";
    if (socket_.send(request.c_str(), request.length()) < 0) {
      std::cerr << "Failed to send AUTH" << std::endl;
      return false;
    }
    return read_response();
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
      if (data == "authenticated\n" || data.rfind("authenticated", 0) == 0) {
        std::cout << "Authenticated as " << username_ << std::endl;
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
    std::cout << "\n=== NoBugDB Client Help ===" << std::endl;
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

struct ClientOptions {
  std::string host{"127.0.0.1"};
  int port{9000};
  std::string username;
  std::string password;
  std::string batch_file;
  bool show_help{false};
};

void print_client_usage(const char *program_name) {
  const char *name = program_name != nullptr ? program_name : "nobugdb-cli";
  std::cout << "Usage: " << name << " [options] [batch.sql]\n"
            << "Options:\n"
            << "  -H, --host <host>       Server host (default: 127.0.0.1)\n"
            << "  -p, --port <n>          Server port (default: 9000)\n"
            << "  -u, --user <name>       Authenticate as user\n"
            << "      --password <pw>     Password for AUTH\n"
            << "  -h, --help              Show this help and exit\n";
}

bool parse_client_options(int argc, char *argv[], ClientOptions &options,
                          std::string &err_message) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      options.show_help = true;
      return true;
    }
    auto require_value = [&](const std::string &flag) -> bool {
      if (i + 1 >= argc) {
        err_message = "Missing value for " + flag;
        return false;
      }
      ++i;
      return true;
    };
    if (arg == "-H" || arg == "--host") {
      if (!require_value(arg)) {
        return false;
      }
      options.host = argv[i];
      continue;
    }
    if (arg == "-p" || arg == "--port") {
      if (!require_value(arg)) {
        return false;
      }
      options.port = std::atoi(argv[i]);
      if (options.port <= 0 || options.port > 65535) {
        err_message = "Invalid port";
        return false;
      }
      continue;
    }
    if (arg == "-u" || arg == "--user") {
      if (!require_value(arg)) {
        return false;
      }
      options.username = argv[i];
      continue;
    }
    if (arg == "--password") {
      if (!require_value(arg)) {
        return false;
      }
      options.password = argv[i];
      continue;
    }
    if (!arg.empty() && arg[0] == '-') {
      err_message = "Unknown argument: " + arg;
      return false;
    }
    if (!options.batch_file.empty()) {
      err_message = "Multiple batch files specified";
      return false;
    }
    options.batch_file = arg;
  }
  if (!options.username.empty() && options.password.empty()) {
    const char *env_password = std::getenv("NOBUGDB_PASSWORD");
    if (env_password != nullptr) {
      options.password = env_password;
    }
  }
  if (!options.username.empty() && options.password.empty()) {
    err_message = "--user requires --password or NOBUGDB_PASSWORD";
    return false;
  }
  return true;
}

int main(int argc, char *argv[]) {
  ClientOptions options;
  std::string err_message;
  if (!parse_client_options(argc, argv, options, err_message)) {
    std::cerr << "Error: " << err_message << std::endl;
    print_client_usage(argv[0]);
    return 1;
  }
  if (options.show_help) {
    print_client_usage(argv[0]);
    return 0;
  }
  DatabaseClient client(options.host, options.port);
  if (!options.username.empty()) {
    client.set_credentials(options.username, options.password);
  }
  if (!client.connect()) {
    return 1;
  }
  if (!options.batch_file.empty()) {
    std::cout << "Executing batch file: " << options.batch_file << std::endl;
    client.run_batch_file(options.batch_file);
  } else {
    client.interactive_mode();
  }
  return 0;
}
