#pragma once

#include <functional>

namespace db {
namespace platform {

class ShutdownHandler {
 public:
  static void install(std::function<void()> callback);
  static void remove();
};

}  // namespace platform
}  // namespace db
