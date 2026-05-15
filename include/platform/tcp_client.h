#pragma once

#include <cstdint>
#include <string>

namespace db {
namespace platform {

std::string tcp_exchange(const std::string &host, uint16_t port,
                         const std::string &message);

}  // namespace platform
}  // namespace db
