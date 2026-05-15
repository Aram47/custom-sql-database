#include "platform/shutdown_handler.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace db {
namespace platform {

namespace {

std::function<void()> g_callback;

BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
  if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT) {
    if (g_callback) {
      g_callback();
    }
    return TRUE;
  }
  return FALSE;
}

}  // namespace

void ShutdownHandler::install(std::function<void()> callback) {
  g_callback = std::move(callback);
  SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
}

void ShutdownHandler::remove() {
  SetConsoleCtrlHandler(console_ctrl_handler, FALSE);
  g_callback = nullptr;
}

}  // namespace platform
}  // namespace db
