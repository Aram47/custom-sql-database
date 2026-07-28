#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/role.h"

namespace db {

/** Credential store loaded from a users.conf-style file. */
class AuthManager {
 public:
  static constexpr size_t SALT_SIZE = 16;

  struct UserRecord {
    Role role{Role::Reader};
    std::vector<uint8_t> salt;
    std::vector<uint8_t> password_hash;
  };

  AuthManager() = default;

  /**
   * Loads credentials from path.
   * Line format: username:role:salt_hex:hash_hex
   * @throws std::runtime_error on I/O or format errors.
   */
  void load_from_file(const std::string &path);

  /**
   * Verifies username and password.
   * @return role on success, nullopt otherwise.
   */
  std::optional<Role> authenticate(const std::string &username,
                                   const std::string &password) const;

  /**
   * Creates or updates the admin user and writes the file.
   * Creates parent directories if needed.
   */
  void bootstrap_admin(const std::string &path, const std::string &password);

  /** Returns true if at least one user is loaded. */
  bool has_users() const;

  /** Computes SHA-256(salt || password). */
  static std::vector<uint8_t> hash_password(const std::vector<uint8_t> &salt,
                                            const std::string &password);

  /** Generates a random salt of SALT_SIZE bytes. */
  static std::vector<uint8_t> generate_salt();

 private:
  std::unordered_map<std::string, UserRecord> users_;

  void save_to_file(const std::string &path) const;
  static UserRecord parse_line(const std::string &line, size_t line_number);
};

}  // namespace db
