#include "utils/cli_options.h"

#include <cstring>
#include <vector>

#include "gtest/gtest.h"

namespace db {
namespace {

std::vector<char *> make_argv(std::vector<std::string> &storage) {
  std::vector<char *> argv;
  for (auto &s : storage) {
    argv.push_back(s.data());
  }
  return argv;
}

TEST(CliOptionsTest, DefaultsAndHelp) {
  std::vector<std::string> storage{"nobugdb", "--help"};
  auto argv = make_argv(storage);
  std::string err;
  auto opts = parse_cli_options(static_cast<int>(argv.size()), argv.data(), err);
  ASSERT_TRUE(opts.has_value());
  EXPECT_TRUE(opts->show_help);
}

TEST(CliOptionsTest, ParsesAllFlags) {
  std::vector<std::string> storage{"nobugdb", "-p", "9100", "-w", "8", "-d",
                                   "/tmp/db", "--log-level", "DEBUG"};
  auto argv = make_argv(storage);
  std::string err;
  auto opts = parse_cli_options(static_cast<int>(argv.size()), argv.data(), err);
  ASSERT_TRUE(opts.has_value()) << err;
  EXPECT_EQ(opts->port, 9100);
  EXPECT_EQ(opts->workers, 8u);
  EXPECT_EQ(opts->data_directory, "/tmp/db");
  EXPECT_EQ(opts->log_level, LogLevel::DEBUG);
}

TEST(CliOptionsTest, ParsesAuthFlags) {
  std::vector<std::string> storage{"nobugdb", "--auth-file", "/tmp/users.conf",
                                   "--bootstrap-admin", "secret",
                                   "--no-require-auth"};
  auto argv = make_argv(storage);
  std::string err;
  auto opts = parse_cli_options(static_cast<int>(argv.size()), argv.data(), err);
  ASSERT_TRUE(opts.has_value()) << err;
  ASSERT_TRUE(opts->auth_file.has_value());
  EXPECT_EQ(*opts->auth_file, "/tmp/users.conf");
  ASSERT_TRUE(opts->bootstrap_admin_password.has_value());
  EXPECT_EQ(*opts->bootstrap_admin_password, "secret");
  EXPECT_FALSE(opts->effective_require_auth());
}

TEST(CliOptionsTest, AuthFileDefaultsRequireAuth) {
  std::vector<std::string> storage{"nobugdb", "--auth-file", "/tmp/users.conf"};
  auto argv = make_argv(storage);
  std::string err;
  auto opts = parse_cli_options(static_cast<int>(argv.size()), argv.data(), err);
  ASSERT_TRUE(opts.has_value()) << err;
  EXPECT_TRUE(opts->effective_require_auth());
}

TEST(CliOptionsTest, BootstrapWithoutAuthFileFails) {
  std::vector<std::string> storage{"nobugdb", "--bootstrap-admin", "x"};
  auto argv = make_argv(storage);
  std::string err;
  auto opts = parse_cli_options(static_cast<int>(argv.size()), argv.data(), err);
  EXPECT_FALSE(opts.has_value());
}

}  // namespace
}  // namespace db
