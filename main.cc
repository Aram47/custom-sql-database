#include <iostream>

#include "network/server.h"
#include "utils/cli_options.h"
#include "utils/logger.h"

using namespace db;

int main(int argc, char *argv[]) {
  try {
    std::string err_message;
    auto options = parse_cli_options(argc, argv, err_message);
    if (!options) {
      std::cerr << "Error: " << err_message << "\n";
      print_cli_usage(argv[0]);
      return 1;
    }
    if (options->show_help) {
      print_cli_usage(argv[0]);
      return 0;
    }
    Logger::get_instance().set_level(options->log_level);
    DB_LOG_INFO("========================================");
    DB_LOG_INFO("NoBugDB");
    DB_LOG_INFO("The world's first database with absolutely no bugs.");
    DB_LOG_INFO("========================================");
    DB_LOG_INFO("Starting NoBugDB...");
    DB_LOG_INFO("Checking for bugs...");
    DB_LOG_INFO("0 bugs found.");
    DB_LOG_INFO("Welcome.");
    DB_LOG_INFO("Starting server on port ", options->port, " (workers=",
                options->workers, ", data-dir=", options->data_directory,
                ", buffer-pool-pages=", options->buffer_pool_pages, ")");
    ServerAuthConfig auth_config;
    auth_config.auth_file = options->auth_file;
    auth_config.require_auth = options->effective_require_auth();
    auth_config.bootstrap_admin_password = options->bootstrap_admin_password;
    if (auth_config.require_auth) {
      DB_LOG_INFO("Authentication required");
    }
    ServerClusterConfig cluster_config;
    cluster_config.role = options->role;
    cluster_config.shard_id = options->shard_id;
    cluster_config.shard_map_path = options->shard_map_path;
    cluster_config.rpc_secret = options->rpc_secret;
    Server server(options->port, options->workers, options->data_directory,
                  auth_config, options->buffer_pool_pages, cluster_config);
    server.start();
    server.wait();
    DB_LOG_INFO("Server shutdown complete");
    return 0;
  } catch (const std::exception &e) {
    DB_LOG_ERROR("Fatal error: ", e.what());
    return 1;
  }
}
