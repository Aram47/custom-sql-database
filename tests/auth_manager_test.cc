#include "core/auth_manager.h"
#include "tests/test_util.hh"
#include "utils/sha256.h"

#include <fstream>

#include "gtest/gtest.h"

namespace db {
namespace {

TEST(Sha256Test, EmptyInputKnownDigest) {
  const std::vector<uint8_t> digest = sha256(std::string(""));
  EXPECT_EQ(to_hex(digest),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Sha256Test, AbcKnownDigest) {
  const std::vector<uint8_t> digest = sha256(std::string("abc"));
  EXPECT_EQ(to_hex(digest),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(AuthManagerTest, AuthenticateSuccessAndRole) {
  test_util::TempDbDir tmp;
  const std::string path = tmp.path_string() + "/users.conf";
  AuthManager writer;
  writer.bootstrap_admin(path, "secret");
  AuthManager reader;
  reader.load_from_file(path);
  const auto role = reader.authenticate("admin", "secret");
  ASSERT_TRUE(role.has_value());
  EXPECT_EQ(*role, Role::Admin);
}

TEST(AuthManagerTest, UnknownUserReturnsNullopt) {
  test_util::TempDbDir tmp;
  const std::string path = tmp.path_string() + "/users.conf";
  AuthManager manager;
  manager.bootstrap_admin(path, "secret");
  EXPECT_FALSE(manager.authenticate("nobody", "secret").has_value());
}

TEST(AuthManagerTest, BadPasswordReturnsNullopt) {
  test_util::TempDbDir tmp;
  const std::string path = tmp.path_string() + "/users.conf";
  AuthManager manager;
  manager.bootstrap_admin(path, "secret");
  EXPECT_FALSE(manager.authenticate("admin", "wrong").has_value());
}

TEST(AuthManagerTest, LoadReaderRole) {
  test_util::TempDbDir tmp;
  const std::string path = tmp.path_string() + "/users.conf";
  const std::vector<uint8_t> salt = AuthManager::generate_salt();
  const std::vector<uint8_t> hash =
      AuthManager::hash_password(salt, "readpass");
  {
    std::ofstream out(path);
    out << "viewer:reader:" << to_hex(salt) << ':' << to_hex(hash) << '\n';
  }
  AuthManager manager;
  manager.load_from_file(path);
  const auto role = manager.authenticate("viewer", "readpass");
  ASSERT_TRUE(role.has_value());
  EXPECT_EQ(*role, Role::Reader);
}

TEST(AuthManagerTest, InvalidLineThrows) {
  test_util::TempDbDir tmp;
  const std::string path = tmp.path_string() + "/users.conf";
  {
    std::ofstream out(path);
    out << "badline\n";
  }
  AuthManager manager;
  EXPECT_THROW(manager.load_from_file(path), std::runtime_error);
}

}  // namespace
}  // namespace db
