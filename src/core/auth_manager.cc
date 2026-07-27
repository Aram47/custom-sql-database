#include "core/auth_manager.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>

#include "utils/sha256.h"

namespace db {
namespace {

std::string trim_whitespace(const std::string &text) {
  size_t start = 0;
  while (start < text.size() &&
         (text[start] == ' ' || text[start] == '\t' || text[start] == '\r')) {
    ++start;
  }
  size_t end = text.size();
  while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                         text[end - 1] == '\r')) {
    --end;
  }
  return text.substr(start, end - start);
}

std::vector<std::string> split_colon(const std::string &line) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= line.size()) {
    const size_t pos = line.find(':', start);
    if (pos == std::string::npos) {
      parts.push_back(line.substr(start));
      break;
    }
    parts.push_back(line.substr(start, pos - start));
    start = pos + 1;
  }
  return parts;
}

}  // namespace

void AuthManager::load_from_file(const std::string &path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Failed to open auth file: " + path);
  }
  users_.clear();
  std::string line;
  size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    const std::string trimmed = trim_whitespace(line);
    if (trimmed.empty() || trimmed[0] == '#') {
      continue;
    }
    UserRecord record = parse_line(trimmed, line_number);
    const size_t first_colon = trimmed.find(':');
    const std::string username = trimmed.substr(0, first_colon);
    users_[username] = std::move(record);
  }
}

std::optional<Role> AuthManager::authenticate(
    const std::string &username, const std::string &password) const {
  const auto it = users_.find(username);
  if (it == users_.end()) {
    return std::nullopt;
  }
  const std::vector<uint8_t> actual =
      hash_password(it->second.salt, password);
  if (actual != it->second.password_hash) {
    return std::nullopt;
  }
  return it->second.role;
}

void AuthManager::bootstrap_admin(const std::string &path,
                                  const std::string &password) {
  UserRecord admin;
  admin.role = Role::Admin;
  admin.salt = generate_salt();
  admin.password_hash = hash_password(admin.salt, password);
  users_["admin"] = std::move(admin);
  save_to_file(path);
}

bool AuthManager::has_users() const { return !users_.empty(); }

std::vector<uint8_t> AuthManager::hash_password(
    const std::vector<uint8_t> &salt, const std::string &password) {
  std::vector<uint8_t> material = salt;
  material.insert(material.end(),
                  reinterpret_cast<const uint8_t *>(password.data()),
                  reinterpret_cast<const uint8_t *>(password.data()) +
                      password.size());
  return sha256(material);
}

std::vector<uint8_t> AuthManager::generate_salt() {
  std::vector<uint8_t> salt(SALT_SIZE);
  std::random_device device;
  for (size_t i = 0; i < salt.size(); ++i) {
    salt[i] = static_cast<uint8_t>(device());
  }
  return salt;
}

void AuthManager::save_to_file(const std::string &path) const {
  const size_t slash = path.find_last_of("/\\");
  if (slash != std::string::npos) {
    const std::string directory = path.substr(0, slash);
    if (!directory.empty()) {
      std::error_code ec;
      std::filesystem::create_directories(directory, ec);
      if (ec) {
        throw std::runtime_error("Failed to create auth directory: " +
                                 directory);
      }
    }
  }
  const std::string temp_path = path + ".tmp";
  {
    std::ofstream output(temp_path, std::ios::trunc);
    if (!output) {
      throw std::runtime_error("Failed to write auth file: " + temp_path);
    }
    output << "# NoBugDB users.conf — username:role:salt_hex:hash_hex\n";
    for (const auto &entry : users_) {
      output << entry.first << ':' << role_to_string(entry.second.role) << ':'
             << to_hex(entry.second.salt) << ':'
             << to_hex(entry.second.password_hash) << '\n';
    }
  }
  std::error_code ec;
  std::filesystem::rename(temp_path, path, ec);
  if (ec) {
    throw std::runtime_error("Failed to replace auth file: " + path);
  }
}

AuthManager::UserRecord AuthManager::parse_line(const std::string &line,
                                                size_t line_number) {
  const std::vector<std::string> parts = split_colon(line);
  if (parts.size() != 4) {
    throw std::runtime_error("Invalid auth line " +
                             std::to_string(line_number) +
                             ": expected username:role:salt_hex:hash_hex");
  }
  if (parts[0].empty()) {
    throw std::runtime_error("Invalid auth line " +
                             std::to_string(line_number) +
                             ": empty username");
  }
  const std::optional<Role> role = parse_role(parts[1]);
  if (!role) {
    throw std::runtime_error("Invalid auth line " +
                             std::to_string(line_number) + ": unknown role");
  }
  UserRecord record;
  record.role = *role;
  record.salt = from_hex(parts[2]);
  record.password_hash = from_hex(parts[3]);
  if (record.salt.size() != SALT_SIZE) {
    throw std::runtime_error("Invalid auth line " +
                             std::to_string(line_number) +
                             ": salt must be 16 bytes");
  }
  if (record.password_hash.size() != 32) {
    throw std::runtime_error("Invalid auth line " +
                             std::to_string(line_number) +
                             ": hash must be 32 bytes");
  }
  return record;
}

}  // namespace db
