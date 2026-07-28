#include "network/server.h"

#include <algorithm>
#include <cstring>
#include <future>
#include <memory>

#include "core/shard_map.h"
#include "core/shard_router.h"
#include "network/rpc_client.h"
#include "platform/shutdown_handler.h"
#include "utils/logger.h"

namespace db {
namespace {

bool split_auth_payload(const std::string &data, std::string *username,
                        std::string *password) {
  const size_t pipe_pos = data.find('|');
  if (pipe_pos == std::string::npos) {
    return false;
  }
  *username = data.substr(0, pipe_pos);
  *password = data.substr(pipe_pos + 1);
  return !username->empty();
}

}  // namespace

Connection::Connection(
    platform::TcpSocket client_socket, Database *database,
    ThreadPool *thread_pool, AuthManager *auth_manager, bool require_auth,
    CoordinatorQueryRouter *coordinator,
    const std::optional<std::string> &rpc_secret,
    std::function<void(std::shared_ptr<Connection>)> on_session_ended)
    : client_socket_(std::move(client_socket)),
      database_(database),
      thread_pool_(thread_pool),
      auth_manager_(auth_manager),
      require_auth_(require_auth),
      coordinator_(coordinator),
      rpc_secret_(rpc_secret),
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

void Connection::handle_auth_request(const Protocol::Request &req) {
  if (!auth_manager_) {
    send_message("ERROR|authentication not configured\n");
    return;
  }
  std::string username;
  std::string password;
  if (!split_auth_payload(req.data, &username, &password)) {
    send_message("ERROR|invalid AUTH format\n");
    return;
  }
  const std::optional<Role> role =
      auth_manager_->authenticate(username, password);
  if (!role) {
    session_.clear_auth();
    send_message("ERROR|authentication failed\n");
    return;
  }
  session_.set_authenticated(true);
  session_.set_username(username);
  session_.set_role(*role);
  send_message("OK|authenticated\n");
}

void Connection::handle_query_request(const Protocol::Request &req) {
  if (require_auth_ && !session_.is_authenticated()) {
    send_message("ERROR|authentication required\n");
    return;
  }
  QueryResult result;
  if (coordinator_) {
    result = coordinator_->executeQuery(req.data, &session_);
  } else {
    result = database_->execute_query(req.data, &session_);
  }
  send_message(Protocol::format_query_result(result));
}

void Connection::handle_rpc_query_request(const Protocol::Request &req) {
  if (!rpc_secret_) {
    send_message("ERROR|RPC_QUERY not enabled\n");
    return;
  }
  std::string secret;
  std::string sql;
  if (!Protocol::split_rpc_query(req.data, &secret, &sql)) {
    send_message("ERROR|invalid RPC_QUERY format\n");
    return;
  }
  if (secret != *rpc_secret_) {
    send_message("ERROR|invalid RPC secret\n");
    return;
  }
  auto result = database_->execute_query(sql, &session_);
  send_message(Protocol::format_query_result(result));
}

void Connection::run() {
  DB_LOG_INFO("Client connected");
  try {
    while (active_.load()) {
      std::string request = read_message();
      if (request.empty()) {
        break;
      }
      const bool is_auth_request =
          request.size() >= 4 && request.compare(0, 4, "AUTH") == 0;
      if (is_auth_request) {
        DB_LOG_DEBUG("Received request: AUTH|<redacted>");
      } else {
        DB_LOG_DEBUG("Received request: ", request);
      }
      auto completion = std::make_shared<std::promise<void>>();
      std::future<void> done = completion->get_future();
      thread_pool_->submit([this, request, completion]() {
        try {
          auto req = Protocol::parse_request(request);
          if (req.type == "AUTH") {
            handle_auth_request(req);
          } else if (req.type == "QUERY") {
            handle_query_request(req);
          } else if (req.type == "RPC_QUERY") {
            handle_rpc_query_request(req);
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
  ssize_t bytes_read = client_socket_.recv(buffer, sizeof(buffer) - 1);
  if (bytes_read <= 0) {
    return "";
  }
  buffer[bytes_read] = '\0';
  return std::string(buffer);
}

void Connection::send_message(const std::string &message) {
  std::lock_guard<std::mutex> lock(send_mutex_);
  if (!client_socket_.is_valid()) {
    return;
  }
  ssize_t bytes_sent =
      client_socket_.send(message.c_str(), message.length());
  if (bytes_sent < 0) {
    throw std::runtime_error("Failed to send message");
  }
}

Server::Server(int port, size_t thread_pool_size, std::string data_directory)
    : Server(port, thread_pool_size, std::move(data_directory),
             ServerAuthConfig{}, 64, ServerClusterConfig{}) {}

Server::Server(int port, size_t thread_pool_size, std::string data_directory,
               ServerAuthConfig auth_config)
    : Server(port, thread_pool_size, std::move(data_directory),
             std::move(auth_config), 64, ServerClusterConfig{}) {}

Server::Server(int port, size_t thread_pool_size, std::string data_directory,
               ServerAuthConfig auth_config, size_t buffer_pool_pages)
    : Server(port, thread_pool_size, std::move(data_directory),
             std::move(auth_config), buffer_pool_pages, ServerClusterConfig{}) {
}

Server::Server(int port, size_t thread_pool_size, std::string data_directory,
               ServerAuthConfig auth_config, size_t buffer_pool_pages,
               ServerClusterConfig cluster_config)
    : port_(port),
      data_directory_(std::move(data_directory)),
      buffer_pool_pages_(buffer_pool_pages),
      auth_config_(std::move(auth_config)),
      cluster_config_(std::move(cluster_config)),
      running_(false),
      thread_pool_(thread_pool_size),
      database_(data_directory_, 5000, buffer_pool_pages_) {}

Server::~Server() { stop(); }

void Server::initialize_auth() {
  if (!auth_config_.auth_file && !auth_config_.bootstrap_admin_password) {
    return;
  }
  auth_manager_ = std::make_unique<AuthManager>();
  if (auth_config_.bootstrap_admin_password) {
    if (!auth_config_.auth_file) {
      throw std::runtime_error("bootstrap admin requires auth file path");
    }
    auth_manager_->bootstrap_admin(*auth_config_.auth_file,
                                   *auth_config_.bootstrap_admin_password);
    DB_LOG_INFO("Bootstrapped admin user in ", *auth_config_.auth_file);
  } else if (auth_config_.auth_file) {
    auth_manager_->load_from_file(*auth_config_.auth_file);
    DB_LOG_INFO("Loaded auth file ", *auth_config_.auth_file);
  }
}

void Server::initialize_coordinator() {
  if (cluster_config_.role != ServerRole::Coordinator) {
    return;
  }
  if (!cluster_config_.shard_map_path || !cluster_config_.rpc_secret) {
    throw std::runtime_error("coordinator requires shard map and rpc secret");
  }
  std::string error;
  auto shardMap =
      ShardMap::loadFromFile(*cluster_config_.shard_map_path, &error);
  if (!shardMap) {
    throw std::runtime_error(error);
  }
  coordinator_ = std::make_unique<CoordinatorQueryRouter>(
      &database_, ShardRouter(std::move(*shardMap)),
      std::make_unique<TcpRpcClient>(), *cluster_config_.rpc_secret);
  DB_LOG_INFO("Coordinator mode enabled (shard-map=",
              *cluster_config_.shard_map_path, ")");
}

void Server::start() {
  if (running_) {
    return;
  }
  platform::TcpSocket::startup();
  initialize_auth();
  database_.load_from_disk();
  initialize_coordinator();
  server_socket_ = platform::TcpSocket::create_tcp();
  server_socket_.set_reuse_address(true);
  server_socket_.bind_any(static_cast<uint16_t>(port_));
  server_socket_.listen(5);
  running_ = true;
  platform::ShutdownHandler::install([this]() { stop(); });
  accept_thread_ = std::thread([this] { accept_connections(); });
  if (cluster_config_.role == ServerRole::Worker && cluster_config_.shard_id) {
    DB_LOG_INFO("Worker started on port ", port_,
                " (shard-id=", *cluster_config_.shard_id, ")");
  } else if (cluster_config_.role == ServerRole::Coordinator) {
    DB_LOG_INFO("Coordinator started on port ", port_);
  } else {
    DB_LOG_INFO("Server started on port ", port_);
  }
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
        auth_manager_.get(), auth_config_.require_auth, coordinator_.get(),
        cluster_config_.rpc_secret,
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
