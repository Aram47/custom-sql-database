#include "platform/tcp_client.h"

#include <vector>

#include "platform/tcp_socket.h"

namespace db {
namespace platform {

std::string tcp_exchange(const std::string &host, uint16_t port,
                         const std::string &message) {
  try {
    TcpSocket client = TcpSocket::create_tcp();
    client.connect(host, port);
    if (client.send(message.data(), message.size()) < 0) {
      return "";
    }

    std::vector<char> buffer(8192);
    ssize_t received = client.recv(buffer.data(), buffer.size() - 1);
    client.close();

    if (received <= 0) {
      return "";
    }
    buffer[static_cast<size_t>(received)] = '\0';
    return std::string(buffer.data(), static_cast<size_t>(received));
  } catch (...) {
    return "";
  }
}

}  // namespace platform
}  // namespace db
