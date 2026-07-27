#pragma once

#include <optional>
#include <string>

namespace db {

/** Session privilege level for v1 auth. */
enum class Role { Admin, Reader };

/**
 * Parses a role name from the users file.
 * @return nullopt if the name is not a known role.
 */
std::optional<Role> parse_role(const std::string &name);

/** Returns the canonical lowercase role name. */
std::string role_to_string(Role role);

}  // namespace db
