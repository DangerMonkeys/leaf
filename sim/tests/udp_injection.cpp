#include <Arduino.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <thread>

// The Arduino IPAddress shim supplies its own definition.
#undef INADDR_NONE

#include "comms/udp_message_server.h"
#include "dispatch/message_bus.h"
#include "dispatch/message_types.h"
#include "runtime.h"
#include "ui/settings/settings.h"

namespace {
  struct Receiver : etl::message_router<Receiver, AmbientUpdate> {
    Receiver() : etl::message_router<Receiver, AmbientUpdate>(1) {}
    void on_receive(const AmbientUpdate& message) {
      assert(message.temperature == 21.5f && message.relativeHumidity == 55.0f);
      ++received;
    }
    void on_receive_unknown(const etl::imessage&) { assert(false); }
    std::atomic<unsigned> received{0};
  };

  void waitForCount(const Receiver& receiver, unsigned expected) {
    for (int i = 0; i < 200 && receiver.received < expected; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(receiver.received == expected);
  }

  void checkPortReleased() {
    const int socket = ::socket(AF_INET, SOCK_DGRAM, 0);
    assert(socket >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(7431);
    assert(bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    close(socket);
  }
}  // namespace

int main() {
  auto& device = sim::runtime();
  sim::Runtime::Options options;
  options.httpPort = 0;
  options.speed = 0;
  options.acceptWarning = true;
  options.exitOnFatalError = true;
  options.statePath = "sim/build-tests/udp-injection/state";
  options.cardRoot = "sim/build-tests/udp-injection/card";
  device.configure(options);
  device.boot();

  settings.loadDefaults();
  assert(!settings.dev_udpInjection && !settings.udpInjectionEnabled());
  settings.setUdpInjectionEnabled(true);
  assert(!settings.dev_udpInjection);  // developer mode is required to opt in
  settings.setDeveloperMode(true);
  assert(!settings.udpInjectionEnabled());
  settings.setUdpInjectionEnabled(true);
  settings.save();
  settings.loadDefaults();
  settings.retrieve();
  assert(settings.dev_mode && settings.dev_udpInjection && settings.udpInjectionEnabled());
  settings.toggleBoolOnOff(&settings.dev_mode);  // the device's developer-mode control
  assert(!settings.dev_mode && !settings.dev_udpInjection);
  settings.save();
  settings.retrieve();
  settings.setDeveloperMode(true);
  assert(!settings.dev_udpInjection && !settings.udpInjectionEnabled());

  MessageBus<1> bus;
  Receiver receiver;
  bus.subscribe(receiver);
  udpMessageServer.publishTo(&bus);

  const int sender = socket(AF_INET, SOCK_DGRAM, 0);
  assert(sender >= 0);
  sockaddr_in destination{};
  destination.sin_family = AF_INET;
  destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  destination.sin_port = htons(7431);
  const auto send = [&](const char* line) {
    assert(sendto(sender, line, strlen(line), 0, reinterpret_cast<sockaddr*>(&destination),
                  sizeof(destination)) == static_cast<ssize_t>(strlen(line)));
  };

  // Developer mode alone must never start the listener.
  udpMessageServer.update();
  checkPortReleased();

  unsigned received = 0;
  const auto checkRejected = [&]() {
    send("A0,21.5,55");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(receiver.received == received);
  };
  for (unsigned cycle = 0; cycle < 3; ++cycle) {
    settings.setUdpInjectionEnabled(true);
    udpMessageServer.update();
    udpMessageServer.update();  // repeated loop calls must not reopen an active socket
    send("A0,21.5,55");
    waitForCount(receiver, ++received);

    settings.setUdpInjectionEnabled(false);
    udpMessageServer.update();
    assert(settings.dev_mode);
    checkRejected();

    settings.setUdpInjectionEnabled(true);
    udpMessageServer.update();
    send("A0,21.5,55");
    waitForCount(receiver, ++received);

    // The device loop publishes mode changes to the asynchronous receiver.
    settings.toggleBoolOnOff(&settings.dev_mode);
    udpMessageServer.update();
    assert(!settings.dev_udpInjection);
    checkRejected();

    settings.setDeveloperMode(true);
    udpMessageServer.update();
    checkRejected();  // enabling developer mode must not resume injection
  }

  udpMessageServer.stopPublishing();
  close(sender);
  puts("UDP injection defaults, persistence, both gates, packet parsing and restart checks passed");
}
