#include "platform/shutdown_handler.h"

#include <csignal>

namespace db {
namespace platform {

namespace {

std::function<void()> g_callback;
struct sigaction g_old_sigint {};
struct sigaction g_old_sigterm {};
bool g_installed = false;

void handle_signal(int) {
  if (g_callback) {
    g_callback();
  }
}

void install_handler(int signum, struct sigaction *old_action) {
  struct sigaction action {};
  action.sa_handler = handle_signal;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  sigaction(signum, &action, old_action);
}

}  // namespace

void ShutdownHandler::install(std::function<void()> callback) {
  g_callback = std::move(callback);
  if (!g_installed) {
    install_handler(SIGINT, &g_old_sigint);
    install_handler(SIGTERM, &g_old_sigterm);
    g_installed = true;
  }
}

void ShutdownHandler::remove() {
  if (g_installed) {
    sigaction(SIGINT, &g_old_sigint, nullptr);
    sigaction(SIGTERM, &g_old_sigterm, nullptr);
    g_installed = false;
  }
  g_callback = nullptr;
}

}  // namespace platform
}  // namespace db
