#include "utils/cli_options.h"

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>

namespace db {
namespace {

bool parse_positive_int(const std::string &text, int &out_value) {
  if (text.empty()) {
    return false;
  }
  char *end = nullptr;
  const long parsed = std::strtol(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '\0' || parsed <= 0 ||
      parsed > 65535) {
    return false;
  }
  out_value = static_cast<int>(parsed);
  return true;
}

bool parse_non_negative_int(const std::string &text, int &out_value) {
  if (text.empty()) {
    return false;
  }
  char *end = nullptr;
  const long parsed = std::strtol(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '\0' || parsed < 0 ||
      parsed > 2147483647L) {
    return false;
  }
  out_value = static_cast<int>(parsed);
  return true;
}

bool parse_positive_size(const std::string &text, size_t &out_value) {
  if (text.empty()) {
    return false;
  }
  char *end = nullptr;
  const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '\0' || parsed == 0) {
    return false;
  }
  out_value = static_cast<size_t>(parsed);
  return true;
}

bool parse_log_level(const std::string &text, LogLevel &out_level) {
  std::string upper = text;
  for (char &c : upper) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  if (upper == "DEBUG") {
    out_level = LogLevel::Debug;
    return true;
  }
  if (upper == "INFO") {
    out_level = LogLevel::Info;
    return true;
  }
  if (upper == "WARNING" || upper == "WARN") {
    out_level = LogLevel::Warning;
    return true;
  }
  if (upper == "ERROR") {
    out_level = LogLevel::Error;
    return true;
  }
  return false;
}

bool parse_server_role(const std::string &text, ServerRole &out_role) {
  std::string lower = text;
  for (char &c : lower) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  if (lower == "standalone") {
    out_role = ServerRole::Standalone;
    return true;
  }
  if (lower == "worker") {
    out_role = ServerRole::Worker;
    return true;
  }
  if (lower == "coordinator") {
    out_role = ServerRole::Coordinator;
    return true;
  }
  return false;
}

}  // namespace

void print_cli_usage(const char *program_name) {
  const char *name = program_name != nullptr ? program_name : "nobugdb";
  std::cout << "Usage: " << name << " [options]\n"
            << "Options:\n"
            << "  -p, --port <n>              TCP listen port (default: 9000)\n"
            << "  -w, --workers <n>           Worker thread pool size (default: 4)\n"
            << "  -d, --data-dir <path>       Table storage directory (default: data)\n"
            << "      --buffer-pool-pages <n>  Buffer pool frame count (default: 64)\n"
            << "      --auth-file <path>      Users file (username:role:salt:hash)\n"
            << "      --require-auth          Require AUTH before QUERY (default if --auth-file)\n"
            << "      --no-require-auth       Allow QUERY without AUTH\n"
            << "      --bootstrap-admin <pw>  Create/update admin in --auth-file\n"
            << "      --role <r>              standalone|worker|coordinator (default: standalone)\n"
            << "      --shard-id <n>          Worker shard id (documentation / ops)\n"
            << "      --shard-map <path>      Shard map conf (required for coordinator)\n"
            << "      --rpc-secret <s>        Internode RPC shared secret\n"
            << "      --log-level <lvl>       DEBUG|INFO|WARNING|ERROR (default: INFO)\n"
            << "  -h, --help                  Show this help and exit\n";
}

std::optional<CliOptions> parse_cli_options(int argc, char *argv[],
                                            std::string &err_message) {
  CliOptions options;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    std::string inline_value;
    const size_t eq = arg.find('=');
    if (eq != std::string::npos && arg.rfind("--", 0) == 0) {
      inline_value = arg.substr(eq + 1);
      arg = arg.substr(0, eq);
    }
    if (arg == "-h" || arg == "--help") {
      options.show_help = true;
      return options;
    }
    auto require_value = [&](const std::string &flag) -> std::optional<std::string> {
      if (!inline_value.empty()) {
        std::string value = inline_value;
        inline_value.clear();
        return value;
      }
      if (i + 1 >= argc) {
        err_message = "Missing value for " + flag;
        return std::nullopt;
      }
      ++i;
      return std::string(argv[i]);
    };
    if (arg == "-p" || arg == "--port") {
      auto value = require_value(arg);
      if (!value) {
        return std::nullopt;
      }
      if (!parse_positive_int(*value, options.port)) {
        err_message = "Invalid port: " + *value;
        return std::nullopt;
      }
      continue;
    }
    if (arg == "-w" || arg == "--workers") {
      auto value = require_value(arg);
      if (!value) {
        return std::nullopt;
      }
      if (!parse_positive_size(*value, options.workers)) {
        err_message = "Invalid workers: " + *value;
        return std::nullopt;
      }
      continue;
    }
    if (arg == "-d" || arg == "--data-dir") {
      auto value = require_value(arg);
      if (!value) {
        return std::nullopt;
      }
      options.data_directory = *value;
      continue;
    }
    if (arg == "--buffer-pool-pages") {
      auto value = require_value(arg);
      if (!value) {
        return std::nullopt;
      }
      if (!parse_positive_size(*value, options.buffer_pool_pages)) {
        err_message = "Invalid buffer-pool-pages: " + *value;
        return std::nullopt;
      }
      continue;
    }
    if (arg == "--auth-file") {
      auto value = require_value(arg);
      if (!value) {
        return std::nullopt;
      }
      options.auth_file = *value;
      continue;
    }
    if (arg == "--require-auth") {
      if (!inline_value.empty()) {
        err_message = "Unexpected value for --require-auth";
        return std::nullopt;
      }
      options.require_auth = true;
      continue;
    }
    if (arg == "--no-require-auth") {
      if (!inline_value.empty()) {
        err_message = "Unexpected value for --no-require-auth";
        return std::nullopt;
      }
      options.require_auth = false;
      continue;
    }
    if (arg == "--bootstrap-admin") {
      auto value = require_value(arg);
      if (!value) {
        return std::nullopt;
      }
      options.bootstrap_admin_password = *value;
      continue;
    }
    if (arg == "--role") {
      auto value = require_value(arg);
      if (!value) {
        return std::nullopt;
      }
      if (!parse_server_role(*value, options.role)) {
        err_message = "Invalid role: " + *value;
        return std::nullopt;
      }
      continue;
    }
    if (arg == "--shard-id") {
      auto value = require_value(arg);
      if (!value) {
        return std::nullopt;
      }
      int shardId = 0;
      if (!parse_non_negative_int(*value, shardId)) {
        err_message = "Invalid shard-id: " + *value;
        return std::nullopt;
      }
      options.shard_id = shardId;
      continue;
    }
    if (arg == "--shard-map") {
      auto value = require_value(arg);
      if (!value) {
        return std::nullopt;
      }
      options.shard_map_path = *value;
      continue;
    }
    if (arg == "--rpc-secret") {
      auto value = require_value(arg);
      if (!value) {
        return std::nullopt;
      }
      options.rpc_secret = *value;
      continue;
    }
    if (arg == "--log-level") {
      auto value = require_value(arg);
      if (!value) {
        return std::nullopt;
      }
      if (!parse_log_level(*value, options.log_level)) {
        err_message = "Invalid log level: " + *value;
        return std::nullopt;
      }
      continue;
    }
    err_message = "Unknown argument: " + arg;
    return std::nullopt;
  }
  if (options.bootstrap_admin_password && !options.auth_file) {
    err_message = "--bootstrap-admin requires --auth-file";
    return std::nullopt;
  }
  if (options.role == ServerRole::Coordinator) {
    if (!options.shard_map_path) {
      err_message = "coordinator requires --shard-map";
      return std::nullopt;
    }
    if (!options.rpc_secret) {
      err_message = "coordinator requires --rpc-secret";
      return std::nullopt;
    }
  }
  if (options.role == ServerRole::Worker && !options.rpc_secret) {
    err_message = "worker requires --rpc-secret";
    return std::nullopt;
  }
  return options;
}

}  // namespace db
