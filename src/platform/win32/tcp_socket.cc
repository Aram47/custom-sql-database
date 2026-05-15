#include "platform/tcp_socket.h"

#include <ws2tcpip.h>

#include <cstring>
#include <sstream>

#include "utils/exceptions.h"

namespace db {
namespace platform {

namespace {

void throw_network_error(const char *operation) {
  std::ostringstream oss;
  oss << operation << " failed (WSA error=" << WSAGetLastError() << ")";
  throw NetworkException(oss.str());
}

}  // namespace

TcpSocket::TcpSocket() = default;

TcpSocket::TcpSocket(socket_handle_t handle) : handle_(handle) {}

TcpSocket::TcpSocket(TcpSocket &&other) noexcept : handle_(other.handle_) {
  other.handle_ = kInvalidSocket;
}

TcpSocket &TcpSocket::operator=(TcpSocket &&other) noexcept {
  if (this != &other) {
    close();
    handle_ = other.handle_;
    other.handle_ = kInvalidSocket;
  }
  return *this;
}

TcpSocket::~TcpSocket() { close(); }

void TcpSocket::startup() {
  WSADATA wsa_data{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    throw NetworkException("WSAStartup failed");
  }
}

void TcpSocket::cleanup() { WSACleanup(); }

TcpSocket TcpSocket::create_tcp() {
  socket_handle_t fd =
      ::socket(AF_INET, static_cast<int>(SOCK_STREAM), IPPROTO_TCP);
  if (fd == INVALID_SOCKET) {
    throw_network_error("socket");
  }
  return TcpSocket(fd);
}

void TcpSocket::set_reuse_address(bool enable) {
  BOOL value = enable ? TRUE : FALSE;
  if (::setsockopt(handle_, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char *>(&value),
                   sizeof(value)) == SOCKET_ERROR) {
    throw_network_error("setsockopt");
  }
}

void TcpSocket::bind_any(uint16_t port) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  if (::bind(handle_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) ==
      SOCKET_ERROR) {
    throw_network_error("bind");
  }
}

void TcpSocket::bind_loopback(uint16_t port) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (InetPtonA(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
    throw NetworkException("InetPton failed");
  }
  if (::bind(handle_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) ==
      SOCKET_ERROR) {
    throw_network_error("bind");
  }
}

uint16_t TcpSocket::local_port() const {
  sockaddr_in addr{};
  int len = sizeof(addr);
  if (getsockname(handle_, reinterpret_cast<sockaddr *>(&addr), &len) ==
      SOCKET_ERROR) {
    throw_network_error("getsockname");
  }
  return ntohs(addr.sin_port);
}

void TcpSocket::listen(int backlog) {
  if (::listen(handle_, backlog) == SOCKET_ERROR) {
    throw_network_error("listen");
  }
}

TcpSocket TcpSocket::accept() {
  TcpSocket client;
  if (!try_accept(client)) {
    throw_network_error("accept");
  }
  return client;
}

bool TcpSocket::try_accept(TcpSocket &client) {
  sockaddr_in client_addr{};
  int client_len = sizeof(client_addr);
  socket_handle_t fd = ::accept(
      handle_, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
  if (fd == INVALID_SOCKET) {
    return false;
  }
  client = TcpSocket(fd);
  return true;
}

void TcpSocket::connect(const std::string &host, uint16_t port) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (InetPtonA(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
    throw NetworkException("Invalid host address");
  }
  if (::connect(handle_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) ==
      SOCKET_ERROR) {
    throw_network_error("connect");
  }
}

ssize_t TcpSocket::send(const void *data, size_t length) {
  int sent = ::send(handle_, static_cast<const char *>(data),
                    static_cast<int>(length), 0);
  if (sent == SOCKET_ERROR) {
    return -1;
  }
  return sent;
}

ssize_t TcpSocket::recv(void *buffer, size_t length) {
  int received = ::recv(handle_, static_cast<char *>(buffer),
                        static_cast<int>(length), 0);
  if (received == SOCKET_ERROR) {
    return -1;
  }
  return received;
}

void TcpSocket::shutdown_both() {
  if (is_valid()) {
    ::shutdown(handle_, SD_BOTH);
  }
}

void TcpSocket::close() {
  if (is_valid()) {
    ::closesocket(handle_);
    handle_ = kInvalidSocket;
  }
}

bool TcpSocket::is_valid() const { return handle_ != kInvalidSocket; }

socket_handle_t TcpSocket::native_handle() const { return handle_; }

}  // namespace platform
}  // namespace db
