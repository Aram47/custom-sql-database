#include "network/server.h"

#include <arpa/inet.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <future>
#include <memory>

#include "utils/logger.h"

namespace db {

// ==================== Connection ====================

Connection::Connection(
    int client_socket, Database *database, ThreadPool *thread_pool,
    std::function<void(std::shared_ptr<Connection>)> on_session_ended)
    : client_socket_(client_socket),
      database_(database),
      thread_pool_(thread_pool),
      on_session_ended_(std::move(on_session_ended)) {}

Connection::~Connection() { stop(); }

void Connection::start() {
  connection_thread_ = std::thread([this] { run(); });
}

void Connection::stop() {
  active_ = false;
  if (client_socket_ >= 0) {
    shutdown(client_socket_, SHUT_RDWR);
    close(client_socket_);
    client_socket_ = -1;
  }
  if (connection_thread_.joinable()) {
    connection_thread_.join();
  }
}

void Connection::run() {
  DB_LOG_INFO("Client connected");

  try {
    while (active_.load()) {
      std::string request = read_message();
      if (request.empty()) break;

      DB_LOG_DEBUG("Received request: ", request);

      auto completion = std::make_shared<std::promise<void>>();
      std::future<void> done = completion->get_future();

      thread_pool_->submit([this, request, completion]() {
        try {
          auto req = Protocol::parse_request(request);

          if (req.type == "QUERY") {
            auto result = database_->execute_query(req.data);
            std::string response = Protocol::format_query_result(result);
            send_message(response);
          } else if (req.type == "PING") {
            send_message("PONG\n");
          } else if (req.type == "QUIT") {
            send_message("OK|Goodbye\n");
            active_ = false;
          } else {
            send_message("ERROR|Unknown command\n");
          }
        } catch (const std::exception &e) {
          DB_LOG_ERROR("Error handling request: ", e.what());
          try {
            send_message(std::string("ERROR|") + e.what() + "\n");
          } catch (...) {
          }
        }
        completion->set_value();
      });

      done.wait();
    }
  } catch (const std::exception &e) {
    DB_LOG_ERROR("Connection error: ", e.what());
  }

  if (client_socket_ >= 0) {
    shutdown(client_socket_, SHUT_RDWR);
    close(client_socket_);
    client_socket_ = -1;
  }

  DB_LOG_INFO("Client disconnected");

  if (on_session_ended_) {
    on_session_ended_(shared_from_this());
  }
}

std::string Connection::read_message() {
  char buffer[4096];
  std::memset(buffer, 0, sizeof(buffer));

  ssize_t bytes_read = recv(client_socket_, buffer, sizeof(buffer) - 1, 0);

  if (bytes_read <= 0) {
    return "";
  }

  buffer[bytes_read] = '\0';
  return std::string(buffer);
}

void Connection::send_message(const std::string &message) {
  std::lock_guard<std::mutex> lock(send_mutex_);
  if (client_socket_ < 0) return;

  ssize_t bytes_sent =
      send(client_socket_, message.c_str(), message.length(), 0);
  if (bytes_sent < 0) {
    throw std::runtime_error("Failed to send message");
  }
}

// ==================== Server ====================

Server *globalServerPtr = nullptr;

void serverSignalHandler(int signal) {
  (void)signal;
  if (globalServerPtr) {
    globalServerPtr->stop();
  }
}

Server::Server(int port, size_t thread_pool_size, std::string data_directory)
    : port_(port),
      data_directory_(std::move(data_directory)),
      server_socket_(-1),
      running_(false),
      thread_pool_(thread_pool_size),
      database_(data_directory_) {}

Server::~Server() { stop(); }

void Server::start() {
  if (running_) return;

  database_.load_from_disk();

  server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket_ < 0) {
    throw NetworkException("Failed to create socket");
  }

  int reuse = 1;
  if (setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &reuse,
                 sizeof(reuse)) < 0) {
    close(server_socket_);
    throw NetworkException("Failed to set socket option");
  }

  sockaddr_in server_addr;
  std::memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(port_);

  if (bind(server_socket_, (struct sockaddr *)&server_addr,
           sizeof(server_addr)) < 0) {
    close(server_socket_);
    throw NetworkException("Failed to bind socket");
  }

  if (listen(server_socket_, 5) < 0) {
    close(server_socket_);
    throw NetworkException("Failed to listen");
  }

  running_ = true;
  globalServerPtr = this;

  signal(SIGINT, serverSignalHandler);

  accept_thread_ = std::thread([this] { accept_connections(); });

  DB_LOG_INFO("Server started on port ", port_);
}

void Server::schedule_unregister_connection(std::shared_ptr<Connection> conn) {
  thread_pool_.submit([this, conn]() {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    connections_.erase(
        std::remove(connections_.begin(), connections_.end(), conn),
        connections_.end());
  });
}

void Server::stop() {
  running_ = false;

  if (server_socket_ >= 0) {
    shutdown(server_socket_, SHUT_RDWR);
    close(server_socket_);
    server_socket_ = -1;
  }

  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }

  std::vector<std::shared_ptr<Connection>> snapshot;
  {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    snapshot = connections_;
  }
  for (const auto &c : snapshot) {
    c->stop();
  }

  thread_pool_.shutdown();
  DB_LOG_INFO("Server stopped");
}

void Server::wait() {
  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }
}

bool Server::is_running() const { return running_; }

int Server::get_port() const { return port_; }

void Server::accept_connections() {
  while (running_) {
    sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    int client_socket = accept(server_socket_, (struct sockaddr *)&client_addr,
                               &client_addr_len);
    if (client_socket < 0) {
      if (running_) {
        DB_LOG_ERROR("Failed to accept connection");
      }
      continue;
    }

    auto connection = std::make_shared<Connection>(
        client_socket, &database_, &thread_pool_,
        [this](std::shared_ptr<Connection> c) {
          schedule_unregister_connection(std::move(c));
        });

    {
      std::lock_guard<std::mutex> lock(connections_mutex_);
      connections_.push_back(connection);
    }

    connection->start();
  }
}

}  // namespace db
