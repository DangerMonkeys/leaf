#include <Arduino.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#undef INADDR_NONE
#include "comms/udp_message_server.h"
#include "dispatch/message_bus.h"
#include "logging/log.h"
#include "power.h"
#include "runtime.h"
#include "sim/clock.h"
#include "system/shutdown_guard.h"
#include "ui/settings/settings.h"

namespace {
  struct Receiver : etl::message_router<Receiver, ButtonEventMessage, AmbientUpdate> {
    Receiver(bool stall)
        : etl::message_router<Receiver, ButtonEventMessage, AmbientUpdate>(1), stall(stall) {}
    bool stall;
    int sender;
    sockaddr_in destination{};
    std::atomic<unsigned> received{0};
    std::atomic<bool> entered{false}, release{false};
    void send() {
      constexpr char line[] = "A0,21.5,55";
      assert(sendto(sender, line, sizeof(line) - 1, 0, reinterpret_cast<sockaddr*>(&destination),
                    sizeof(destination)) == sizeof(line) - 1);
    }
    void on_receive(const ButtonEventMessage&) {
      // This is a real bus subscriber holding the same recursive bus lock as UI input.
      power.shutdown(false);
      power.shutdown(true);  // Low-battery reason wins before execution.
      power.shutdown(false);
      assert(power.info().onState == PowerState::On && flightTimer_isRunning());
      assert(!power.switchToOnState());
      send();
      for (unsigned i = 0; i < 200 && !udpMessageServer.callbackInFlightForTest(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      assert(udpMessageServer.callbackInFlightForTest());
      assert(received == 0);  // Admitted callback is waiting for this subscriber's bus lock.
    }
    void on_receive(const AmbientUpdate&) {
      entered = true;
      while (stall && !release) std::this_thread::yield();
      ++received;
    }
    void on_receive_unknown(const etl::imessage&) { assert(false); }
  };
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 2);
  const bool stall = std::string(argv[1]) == "timeout";
  char fixture[] = "/tmp/leaf-shutdown-bus-XXXXXX";
  assert(mkdtemp(fixture));
  sim::Runtime::Options options;
  options.httpPort = 0;
  options.speed = 0;
  options.acceptWarning = true;
  options.exitOnFatalError = true;
  options.cardRoot = std::string(fixture) + "/card";
  options.statePath = std::string(fixture) + "/state";
  sim::runtime().configure(options);
  sim::runtime().boot();
  settings.dev_mode = true;
  settings.setUdpInjectionEnabled(true);
  settings.system_autoOff = settings.log_autoStart = settings.log_autoStop = false;
  settings.log_saveTrack = false;
  flightTimer_start();

  MessageBus<1> bus;
  Receiver receiver(stall);
  bus.subscribe(receiver);
  udpMessageServer.publishTo(&bus);
  receiver.sender = socket(AF_INET, SOCK_DGRAM, 0);
  assert(receiver.sender >= 0);
  receiver.destination.sin_family = AF_INET;
  receiver.destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  receiver.destination.sin_port = htons(7431);
  udpMessageServer.update();
  bus.receive(ButtonEventMessage(Button::CENTER, ButtonEvent::HELD));
  sim::clock().setSpeed(1);  // Let the real UDP thread run while virtual delays are advanced.
  const auto start = sim::clock().nowUs();
  try {
    assert(power.serviceShutdown());
    assert(!stall);
    assert(receiver.received == 1 && !udpMessageServer.callbackInFlightForTest());
    assert(!power.serviceShutdown());
    assert(power.info().onState == PowerState::OffUSB && !flightTimer_isRunning());
    assert(digitalRead(shutdown_guard::LATCH_PIN) == LOW);
    assert(sim::clock().nowUs() - start < shutdown_guard::DEADLINE_US);
    assert(settings.udpInjectionEnabled());  // Nonpersistent suspension, no user setting change.
    udpMessageServer.update();
    receiver.send();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(receiver.received == 1);  // Charging cannot reopen injection.
    assert(power.switchToOnState());
    udpMessageServer.update();
    receiver.send();
    for (unsigned i = 0; i < 200 && receiver.received != 2; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(receiver.received == 2);  // A later intentional power-on can reopen normally.
    assert(udpMessageServer.quiesce());
    sim::clock().setSpeed(0);
    sim::clock().advanceUs(shutdown_guard::BACKSTOP_US + 20000);
    puts(
        "Deferred shutdown released the bus, drained an admitted UDP publisher, saved, charged and "
        "reopened on wake");
  } catch (const shutdown_guard::EmergencyReset&) {
    assert(stall);
    assert(shutdown_guard::lastReason() == shutdown_guard::Reason::WorkerTimeout);
    assert(digitalRead(shutdown_guard::LATCH_PIN) == LOW);
    assert(sim::clock().nowUs() - start <= 510000);
    receiver.release = true;
    for (unsigned i = 0; i < 200 && udpMessageServer.callbackInFlightForTest(); ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(!udpMessageServer.callbackInFlightForTest());
    puts("A stuck admitted publisher forced guarded recovery without deleting its resources");
  }
  udpMessageServer.stopPublishing();
  close(receiver.sender);
}
