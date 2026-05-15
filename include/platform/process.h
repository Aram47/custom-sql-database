#pragma once

#include "platform/platform_config.h"

#if DB_PLATFORM_WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace db {
namespace platform {

inline unsigned long current_process_id() {
#if DB_PLATFORM_WIN32
  return static_cast<unsigned long>(GetCurrentProcessId());
#else
  return static_cast<unsigned long>(getpid());
#endif
}

}  // namespace platform
}  // namespace db
