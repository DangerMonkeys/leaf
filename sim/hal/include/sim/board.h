// The virtual Leaf board: everything the firmware pokes at through pins, ADCs, LEDC and UARTs.
//
// The emulator UI reads and writes this state; the firmware reaches it through the ordinary
// Arduino calls.  Pressing a button in the browser sets a pin level here, and hardware/buttons.cpp
// -- the real one, with its real debounce state machine -- sees exactly what it sees on a device.
#pragma once

#include <stdint.h>

#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace sim {

  // Pin numbering: ESP32 GPIOs are 0..63.  IO-expander pins are offset so both live in one map.
  constexpr uint16_t IOEX_PIN_BASE = 100;

  struct ToneEvent {
    uint32_t atMs = 0;
    uint32_t frequencyHz = 0;  // 0 means silence
    uint8_t volume = 0;        // 0..3, from the speaker's two volume pins
  };

  class Board {
   public:
    // ------------------------------------------------------------------ GPIO
    void pinMode(uint16_t pin, uint8_t mode);
    void digitalWrite(uint16_t pin, uint8_t value);
    int digitalRead(uint16_t pin) const;

    // Input pins driven by the emulator (buttons, card detect, charge status).
    void setInputLevel(uint16_t pin, bool high);

    // ------------------------------------------------------------------ ADC
    void setAdcMilliVolts(uint16_t pin, uint32_t mv);
    uint32_t adcMilliVolts(uint16_t pin) const;

    // ------------------------------------------------------------------ speaker (LEDC)
    void writeTone(uint32_t frequencyHz);
    uint32_t currentTone() const;
    void setVolumePins(bool a, bool b);
    uint8_t volume() const;
    // Tone changes, kept as an append-only stream so several listening browsers each hear the
    // whole beep rather than dividing the events between them.  Appends everything after
    // `cursor` to `into` and returns the cursor to pass next time; a listener that falls behind
    // resumes at the oldest event still held.
    uint64_t toneEventsSince(uint64_t cursor, std::vector<ToneEvent>& into) const;

    // Sequence number one past the newest tone event.  A browser starts here rather than at zero:
    // tones are live events, and replaying the whole flight's beeps on connect is not useful.
    uint64_t toneEventCount() const;

    // ------------------------------------------------------------------ GPS UART
    // Bytes the emulator pushes here are read back by the firmware's Serial0, so recorded NMEA
    // reaches the GPS driver the same way the real receiver's bytes do.
    void gpsFeed(const std::string& bytes);
    int gpsAvailable() const;
    int gpsRead();
    int gpsPeek() const;
    // What the firmware sends to the GPS module (configuration commands); kept for the UI.
    void gpsTransmit(uint8_t byte);
    std::string drainGpsTransmit();

    // ------------------------------------------------------------------ misc
    // Both drive the pins power.cpp reads, so the firmware's own battery arithmetic and charge
    // detection produce what was asked for rather than the emulator reporting it separately.
    void setBatteryMilliVolts(uint32_t mv);
    void setCharging(bool charging);

   private:
    mutable std::mutex mutex_;

    struct Pin {
      uint8_t mode = 0;
      bool outputLevel = false;
      bool inputLevel = false;
      bool isOutput = false;
      uint32_t adcMv = 0;
    };
    mutable Pin pins_[256];

    uint32_t tone_ = 0;
    bool volA_ = false;
    bool volB_ = false;
    std::vector<ToneEvent> toneEvents_;
    uint64_t toneFirstSeq_ = 0;  // sequence number of toneEvents_.front()

    std::deque<uint8_t> gpsRx_;
    std::string gpsTx_;
  };

  Board& board();

  // Convenience for the emulator: the five D-pad buttons by name.  Safe to call from any thread --
  // it is a pin write, not a firmware call -- which is what lets the UI wake a device that has
  // halted in the firmware's fatal-error handler (that handler waits for a key press).
  bool pressButton(const std::string& name, bool down);

  // A fatal error halts the device inside the firmware's handler, which waits for a button press.
  // The console watches for it so the emulator can say the device has stopped rather than looking
  // like it hung.
  void noteFatalError(const std::string& message);
  bool fatalErrorSeen();
  std::string fatalErrorMessage();

}  // namespace sim
