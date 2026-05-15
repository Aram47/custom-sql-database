#include "network/server.h"

#include <algorithm>
#include <cstring>
#include <future>
#include <memory>

#include "platform/shutdown_handler.h"
#include "utils/logger.h"

namespace db {

// ==================== Connection ====================

Connection::Connection(
    platform::TcpSocket client_socket, Database *database,
    ThreadPool *thread_pool,
    std::function<void(std::shared_ptr<Connection>)> on_session_ended)
    : client_socket_(std::move(client_socket)),
      database_(database),
      thread_pool_(thread_pool),
      on_session_ended_(std::move(on_session_ended)) {}

Connection::~Connection() { stop(); }

void Connection::start() {
  connection_thread_ = std::thread([this] { run(); });
}

void Connection::stop() {
  active_ = false;
  if (client_socket_.is_valid()) {
    client_socket_.shutdown_both();
    client_socket_.close();
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

  if (client_socket_.is_valid()) {
    client_socket_.shutdown_both();
    client_socket_.close();
  }

  DB_LOG_INFO("Client disconnected");

  if (on_session_ended_) {
    on_session_ended_(shared_from_this());
  }
}

std::string Connection::read_message() {
  char buffer[4096];
  std::memset(buffer, 0, sizeof(buffer));

  ssize_t bytes_read =
      client_socket_.recv(buffer, sizeof(buffer) - 1);

  if (bytes_read <= 0) {
    return "";
  }

  buffer[bytes_read] = '\0';
  return std::string(buffer);
}

void Connection::send_message(const std::string &message) {
  std::lock_guard<std::mutex> lock(send_mutex_);
  if (!client_socket_.is_valid()) return;

  ssize_t bytes_sent =
      client_socket_.send(message.c_str(), message.length());
  if (bytes_sent < 0) {
    throw std::runtime_error("Failed to send message");
  }
}

// ==================== Server ====================

Server::Server(int port, size_t thread_pool_size, std::string data_directory)
    : port_(port),
      data_directory_(std::move(data_directory)),
      running_(false),
      thread_pool_(thread_pool_size),
      database_(data_directory_) {}

Server::~Server() { stop(); }

void Server::start() {
  if (running_) return;

  platform::TcpSocket::startup();
  database_.load_from_disk();

  server_socket_ = platform::TcpSocket::create_tcp();
  server_socket_.set_reuse_address(true);
  server_socket_.bind_any(static_cast<uint16_t>(port_));
  server_socket_.listen(5);

  running_ = true;

  platform::ShutdownHandler::install([this]() { stop(); });

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
  if (!running_.exchange(false)) {
    return;
  }

  platform::ShutdownHandler::remove();

  if (server_socket_.is_valid()) {
    server_socket_.shutdown_both();
    server_socket_.close();
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
  platform::TcpSocket::cleanup();
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
    platform::TcpSocket client_socket;
    if (!server_socket_.try_accept(client_socket)) {
      if (running_) {
        DB_LOG_ERROR("Failed to accept connection");
      }
      continue;
    }

    auto connection = std::make_shared<Connection>(
        std::move(client_socket), &database_, &thread_pool_,
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
