#include <ArduinoJson.h>
#include <assert.h>

#include <atomic>
#include <string>
#include <vector>

#include "diagnostics/cpu_utilization.h"
#include "dispatch/message_bus.h"
#include "hardware/lc86g.h"
#include "sim/clock.h"
#include "ui/settings/settings.h"

namespace {
  const std::string sentence = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";

  // Expire a slot while the real driver is reading buffered bytes, without a host-thread race.
  class TimedSerial : public HardwareSerial {
   public:
    explicit TimedSerial(std::atomic<uint32_t>& ticks) : ticks_(ticks) {}
    size_t write(uint8_t) override { return 1; }
    int available() override { return bytes.size() - consumed; }
    int peek() override { return available() ? bytes[consumed] : -1; }
    void flush() override {}
    int read() override {
      assert(available());
      int c = bytes[consumed++];
      if (consumed == expireAfter) ticks_.fetch_add(1, std::memory_order_release);
      return c;
    }
    std::string bytes;
    size_t consumed = 0;
    size_t expireAfter = 0;

   private:
    std::atomic<uint32_t>& ticks_;
  };

  struct Receiver : etl::message_router<Receiver, GpsMessage> {
    explicit Receiver(std::atomic<uint32_t>& ticks)
        : etl::message_router<Receiver, GpsMessage>(1), ticks_(ticks) {}
    void on_receive(const GpsMessage& message) {
      received.emplace_back(message.nmea.c_str());
      cpu_utilization::gpsValidity(true);
      if (expireInCallback) ticks_.fetch_add(1, std::memory_order_release);
    }
    void on_receive_unknown(const etl::imessage&) { assert(false); }
    bool expireInCallback = false;
    std::vector<std::string> received;
    std::atomic<uint32_t>& ticks_;
  };

  class StringPrint : public Print {
   public:
    size_t write(uint8_t c) override {
      text += static_cast<char>(c);
      return 1;
    }
    std::string text;
  };

  void checkYield(size_t cut, bool capture) {
    std::atomic<uint32_t> ticks{17};
    TimedSerial serial(ticks);
    LC86G reader(serial);
    reader.init();
    MessageBus<1> bus;
    Receiver receiver(ticks);
    bus.subscribe(receiver);
    reader.publishTo(&bus);
    serial.bytes = sentence + "\r\n" + sentence + "\r\n";
    serial.expireAfter = cut;
    if (capture) {
      assert(cpu_utilization::start());
      cpu_utilization::beginSlot(1, 17, sim::clock().nowUs(), sim::clock().nowUs());
    }

    // An already-expired slot must not consume even one byte.
    assert(!reader.readLine(ticks, 16));
    assert(serial.consumed == 0 && receiver.received.empty());
    const bool complete = cut > sentence.size();
    assert(reader.readLine(ticks, 17) == complete);
    assert(serial.consumed == cut);
    assert(receiver.received.size() == (complete ? 1 : 0));
    assert(serial.available() > 0);
    sim::clock().advanceUs(10000);

    // Even buffered complete sentences must wait until the new slot is serviced.
    assert(!reader.readLine(ticks, 17));
    assert(serial.consumed == cut);
    if (capture) cpu_utilization::beginSlot(2, 18, sim::clock().nowUs(), sim::clock().nowUs());
    if (!complete) {
      assert(reader.readLine(ticks, 18));
      assert(serial.consumed == sentence.size() + 1);
    }
    assert(receiver.received.size() == 1 && receiver.received[0] == sentence);

    // Keep completion callbacks inline, including a tick firing within the callback.
    receiver.expireInCallback = true;
    assert(reader.readLine(ticks, 18));
    assert(ticks.load() == 19);
    assert(receiver.received.size() == 2 && receiver.received[1] == sentence);
    assert(!reader.readLine(ticks, 18));
    assert(serial.available() == 1);  // trailing LF is still pending
    assert(!reader.readLine(ticks, 19));
    assert(!serial.available() && receiver.received.size() == 2);

    if (capture) {
      cpu_utilization::freeze();
      StringPrint output;
      assert(cpu_utilization::writeJson(output));
      JsonDocument json;
      assert(!deserializeJson(json, output.text));
      assert(json["gps_profile"]["total"].as<unsigned>() == 2);
      assert(json["gps_profile"]["partial_uart"].as<unsigned>() == 0);
      assert(json["gps_profile"]["uart_resets"].as<unsigned>() == 0);
      assert(json["gps_profile"]["recent"][0]["slot"].as<unsigned>() == (complete ? 1 : 2));
    }
  }
}  // namespace

int main() {
  sim::clock().setSpeed(0);
  settings.dev_mode = false;
  for (size_t cut : {size_t(1), size_t(25), sentence.size(), sentence.size() + 1})
    checkYield(cut, false);
  settings.dev_mode = true;
  for (size_t cut : {size_t(1), size_t(25), sentence.size(), sentence.size() + 1})
    checkYield(cut, true);
  puts(
      "UART yields between characters, preserves partial sentences and keeps callbacks inline; "
      "capture on/off passed");
}
