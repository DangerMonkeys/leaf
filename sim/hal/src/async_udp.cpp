// AsyncUDP over a real host socket.
//
// This backs the firmware's own UDP injection server, so the emulator accepts the same packets a
// device on WiFi accepts.  sim/play_buslog.py, pointed at 127.0.0.1, drives the emulator with a
// recording exactly as it drives hardware.

#include <AsyncUDP.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <thread>
#include <vector>

AsyncUDP::~AsyncUDP() { close(); }

bool AsyncUDP::listen(uint16_t port) {
  socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_ < 0) return false;

  int reuse = 1;
  setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(port);
  if (bind(socket_, (sockaddr*)&address, sizeof(address)) < 0) {
    ::close(socket_);
    socket_ = -1;
    return false;
  }

  running_ = true;
  // Detached rather than joined: close() shuts the socket down, which drops the thread out of
  // recvfrom, and the emulator only ever closes on the way out.
  std::thread([this] {
    // Packets are handed to the firmware's handler on this thread.  The message bus takes its own
    // recursive mutex, which is what makes that safe against the device loop.
    std::vector<uint8_t> buffer(2048);
    while (running_) {
      sockaddr_in from{};
      socklen_t fromLength = sizeof(from);
      const ssize_t received =
          recvfrom(socket_, buffer.data(), buffer.size(), 0, (sockaddr*)&from, &fromLength);
      if (received <= 0) continue;
      if (!callback_) continue;
      AsyncUDPPacket packet(buffer.data(), (size_t)received, IPAddress(from.sin_addr.s_addr));
      callback_(packet);
    }
  }).detach();
  return true;
}

void AsyncUDP::close() {
  running_ = false;
  if (socket_ >= 0) {
    ::shutdown(socket_, SHUT_RDWR);
    ::close(socket_);
    socket_ = -1;
  }
}

void AsyncUDP::onPacket(std::function<void(AsyncUDPPacket&)> callback) {
  callback_ = std::move(callback);
}
