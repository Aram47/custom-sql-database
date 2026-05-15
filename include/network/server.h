#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/database.h"
#include "network/protocol.h"
#include "threading/thread_pool.h"

namespace db {

class Connection : public std::enable_shared_from_this<Connection> {
 public:
  Connection(int client_socket, Database *database, ThreadPool *thread_pool,
             std::function<void(std::shared_ptr<Connection>)> on_session_ended);
  ~Connection();

  void start();
  void stop();

 private:
  int client_socket_{-1};
  Database *database_{};
  ThreadPool *thread_pool_{};
  std::function<void(std::shared_ptr<Connection>)> on_session_ended_{};
  std::atomic<bool> active_{true};
  std::thread connection_thread_{};
  std::mutex send_mutex_{};

  void run();
  std::string read_message();
  void send_message(const std::string &message);
};

class Server {
 public:
  Server(int port = 9000, size_t thread_pool_size = 4,
         std::string data_directory = "data");
  ~Server();

  void start();
  void stop();
  void wait();

  bool is_running() const;
  int get_port() const;

 private:
  int port_{};
  std::string data_directory_;
  int server_socket_{-1};
  std::atomic<bool> running_{false};
  std::thread accept_thread_{};
  ThreadPool thread_pool_;
  Database database_;
  std::mutex connections_mutex_{};
  std::vector<std::shared_ptr<Connection>> connections_;

  void accept_connections();
  void schedule_unregister_connection(std::shared_ptr<Connection> conn);
};

}  // namespace db
