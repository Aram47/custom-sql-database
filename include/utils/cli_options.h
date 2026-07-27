#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "utils/logger.h"

namespace db {

/** Parsed server command-line options. */
struct CliOptions {
  int port{9000};
  size_t workers{4};
  std::string data_directory{"data"};
  LogLevel log_level{LogLevel::INFO};
  bool show_help{false};
  std::optional<std::string> auth_file;
  /** Tri-state: nullopt = default (true when auth_file set). */
  std::optional<bool> require_auth;
  std::optional<std::string> bootstrap_admin_password;

  /** Effective require-auth after applying defaults. */
  bool effective_require_auth() const {
    if (require_auth.has_value()) {
      return *require_auth;
    }
    return auth_file.has_value();
  }
};

/**
 * Parses argv into CliOptions.
 * @return nullopt and writes an error message to err_message on failure.
 */
std::optional<CliOptions> parse_cli_options(int argc, char *argv[],
                                            std::string &err_message);

/** Prints usage text for the database server. */
void print_cli_usage(const char *program_name);

}  // namespace db
