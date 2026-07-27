#include "core/role.h"

namespace db {

std::optional<Role> parse_role(const std::string &name) {
  if (name == "admin") {
    return Role::Admin;
  }
  if (name == "reader") {
    return Role::Reader;
  }
  return std::nullopt;
}

std::string role_to_string(Role role) {
  switch (role) {
    case Role::Admin:
      return "admin";
    case Role::Reader:
      return "reader";
  }
  return "admin";
}

}  // namespace db
