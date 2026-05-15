#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "platform/platform_config.h"

namespace db {
namespace platform {

class TcpSocket {
 public:
  TcpSocket();
  explicit TcpSocket(socket_handle_t handle);

  TcpSocket(const TcpSocket &) = delete;
  TcpSocket &operator=(const TcpSocket &) = delete;
  TcpSocket(TcpSocket &&other) noexcept;
  TcpSocket &operator=(TcpSocket &&other) noexcept;

  ~TcpSocket();

  static void startup();
  static void cleanup();

  static TcpSocket create_tcp();

  void set_reuse_address(bool enable);
  void bind_any(uint16_t port);
  void bind_loopback(uint16_t port);
  uint16_t local_port() const;
  void listen(int backlog);
  TcpSocket accept();
  bool try_accept(TcpSocket &client);
  void connect(const std::string &host, uint16_t port);

  ssize_t send(const void *data, size_t length);
  ssize_t recv(void *buffer, size_t length);

  void shutdown_both();
  void close();

  bool is_valid() const;
  socket_handle_t native_handle() const;

 private:
  socket_handle_t handle_{kInvalidSocket};
};

}  // namespace platform
}  // namespace db
