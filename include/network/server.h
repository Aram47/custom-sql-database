#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "core/auth_manager.h"
#include "core/database.h"
#include "core/session_context.h"
#include "network/coordinator_query_router.h"
#include "network/protocol.h"
#include "platform/tcp_socket.h"
#include "threading/thread_pool.h"
#include "utils/cli_options.h"

namespace db {

/** Optional authentication configuration for Server. */
struct ServerAuthConfig {
  std::optional<std::string> auth_file;
  bool require_auth{false};
  std::optional<std::string> bootstrap_admin_password;
};

/** Cluster / sharding configuration for Server. */
struct ServerClusterConfig {
  ServerRole role{ServerRole::Standalone};
  std::optional<int> shard_id;
  std::optional<std::string> shard_map_path;
  std::optional<std::string> rpc_secret;
};

class Connection : public std::enable_shared_from_this<Connection> {
 public:
  Connection(platform::TcpSocket client_socket, Database *database,
             ThreadPool *thread_pool, AuthManager *auth_manager,
             bool require_auth, CoordinatorQueryRouter *coordinator,
             const std::optional<std::string> &rpc_secret,
             std::function<void(std::shared_ptr<Connection>)> on_session_ended);
  ~Connection();

  void start();
  void stop();

 private:
  platform::TcpSocket client_socket_{};
  Database *database_{};
  ThreadPool *thread_pool_{};
  AuthManager *auth_manager_{};
  bool require_auth_{false};
  CoordinatorQueryRouter *coordinator_{};
  std::optional<std::string> rpc_secret_;
  std::function<void(std::shared_ptr<Connection>)> on_session_ended_{};
  std::atomic<bool> active_{true};
  std::thread connection_thread_{};
  std::mutex send_mutex_{};
  SessionContext session_;

  void run();
  std::string read_message();
  void send_message(const std::string &message);
  void handle_auth_request(const Protocol::Request &req);
  void handle_query_request(const Protocol::Request &req);
  void handle_rpc_query_request(const Protocol::Request &req);
};

class Server {
 public:
  Server(int port, size_t thread_pool_size, std::string data_directory);
  Server(int port, size_t thread_pool_size, std::string data_directory,
         ServerAuthConfig auth_config);
  Server(int port, size_t thread_pool_size, std::string data_directory,
         ServerAuthConfig auth_config, size_t buffer_pool_pages);
  Server(int port, size_t thread_pool_size, std::string data_directory,
         ServerAuthConfig auth_config, size_t buffer_pool_pages,
         ServerClusterConfig cluster_config);
  ~Server();

  void start();
  void stop();
  void wait();

  bool is_running() const;
  int get_port() const;

 private:
  int port_{};
  std::string data_directory_;
  size_t buffer_pool_pages_{64};
  ServerAuthConfig auth_config_;
  ServerClusterConfig cluster_config_;
  std::unique_ptr<AuthManager> auth_manager_;
  platform::TcpSocket server_socket_{};
  std::atomic<bool> running_{false};
  std::thread accept_thread_{};
  ThreadPool thread_pool_;
  Database database_;
  std::unique_ptr<CoordinatorQueryRouter> coordinator_;
  std::mutex connections_mutex_{};
  std::vector<std::shared_ptr<Connection>> connections_;

  void accept_connections();
  void schedule_unregister_connection(std::shared_ptr<Connection> conn);
  void initialize_auth();
  void initialize_coordinator();
};

}  // namespace db
