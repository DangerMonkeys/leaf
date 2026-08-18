#include "sim/board.h"

#include <string.h>

#include "hardware/configuration.h"
#include "sim/clock.h"

namespace sim {

  namespace {
    // power.cpp reads the battery through a divider on this ADC pin (its BATT_SENSE), scaling the
    // reading by 69/41.  The emulator drives the pin so that the firmware's own arithmetic
    // produces the battery voltage the user asked for.
    constexpr uint16_t BATTERY_SENSE_PIN = 1;
  }  // namespace

  Board& board() {
    static Board instance;
    return instance;
  }

  void Board::setBatteryMilliVolts(uint32_t mv) {
    setAdcMilliVolts(BATTERY_SENSE_PIN, mv * 41 / 69);
  }

  void Board::setCharging(bool charging) {
    // power.cpp reads charge status off the IO expander, active low.  Driving the pin rather than
    // holding a flag of our own is what makes the firmware's own charge handling run.
#if defined(POWER_CHARGE_GOOD) && defined(POWER_CHARGE_GOOD_IOEX)
    const uint16_t pin =
        POWER_CHARGE_GOOD_IOEX ? (uint16_t)(IOEX_PIN_BASE + POWER_CHARGE_GOOD) : POWER_CHARGE_GOOD;
    setInputLevel(pin, !charging);
#else
    (void)charging;
#endif
  }

  void Board::pinMode(uint16_t pin, uint8_t mode) {
    if (pin >= 256) return;
    std::lock_guard<std::mutex> lock(mutex_);
    pins_[pin].mode = mode;
    // 0x03 is Arduino's OUTPUT; anything with the input bit only is driven by the emulator.
    pins_[pin].isOutput = (mode & 0x02) != 0;
  }

  void Board::digitalWrite(uint16_t pin, uint8_t value) {
    if (pin >= 256) return;
    std::lock_guard<std::mutex> lock(mutex_);
    pins_[pin].outputLevel = value != 0;
  }

  int Board::digitalRead(uint16_t pin) const {
    if (pin >= 256) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    const Pin& p = pins_[pin];
    return (p.isOutput ? p.outputLevel : p.inputLevel) ? 1 : 0;
  }

  void Board::setInputLevel(uint16_t pin, bool high) {
    if (pin >= 256) return;
    std::lock_guard<std::mutex> lock(mutex_);
    pins_[pin].inputLevel = high;
  }

  void Board::setAdcMilliVolts(uint16_t pin, uint32_t mv) {
    if (pin >= 256) return;
    std::lock_guard<std::mutex> lock(mutex_);
    pins_[pin].adcMv = mv;
  }

  uint32_t Board::adcMilliVolts(uint16_t pin) const {
    if (pin >= 256) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    return pins_[pin].adcMv;
  }

  void Board::writeTone(uint32_t frequencyHz) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (frequencyHz == tone_) return;
    tone_ = frequencyHz;
    ToneEvent event;
    event.atMs = clock().millis();
    event.frequencyHz = frequencyHz;
    event.volume = (uint8_t)((volA_ ? 1 : 0) + (volB_ ? 2 : 0));
    toneEvents_.push_back(event);
    // The UI drains these; if nobody is listening, keep the buffer bounded.
    if (toneEvents_.size() > 512) toneEvents_.erase(toneEvents_.begin(), toneEvents_.begin() + 256);
  }

  uint32_t Board::currentTone() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tone_;
  }

  void Board::setVolumePins(bool a, bool b) {
    std::lock_guard<std::mutex> lock(mutex_);
    volA_ = a;
    volB_ = b;
  }

  uint8_t Board::volume() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return (uint8_t)((volA_ ? 1 : 0) + (volB_ ? 2 : 0));
  }

  std::vector<ToneEvent> Board::drainToneEvents() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ToneEvent> out;
    out.swap(toneEvents_);
    return out;
  }

  void Board::gpsFeed(const std::string& bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (unsigned char c : bytes) gpsRx_.push_back(c);
  }

  int Board::gpsAvailable() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return (int)gpsRx_.size();
  }

  int Board::gpsRead() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (gpsRx_.empty()) return -1;
    const int c = gpsRx_.front();
    gpsRx_.pop_front();
    return c;
  }

  int Board::gpsPeek() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return gpsRx_.empty() ? -1 : gpsRx_.front();
  }

  void Board::gpsTransmit(uint8_t byte) {
    std::lock_guard<std::mutex> lock(mutex_);
    gpsTx_.push_back((char)byte);
    if (gpsTx_.size() > 4096) gpsTx_.erase(0, gpsTx_.size() - 4096);
  }

  std::string Board::drainGpsTransmit() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string out;
    out.swap(gpsTx_);
    return out;
  }

  namespace {
    std::mutex g_fatalMutex;
    bool g_fatalSeen = false;
    std::string g_fatalMessage;
  }  // namespace

  void noteFatalError(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_fatalMutex);
    if (g_fatalSeen) return;  // keep the first one: later ones are usually consequences
    g_fatalSeen = true;
    g_fatalMessage = message;
  }

  bool fatalErrorSeen() {
    std::lock_guard<std::mutex> lock(g_fatalMutex);
    return g_fatalSeen;
  }

  std::string fatalErrorMessage() {
    std::lock_guard<std::mutex> lock(g_fatalMutex);
    return g_fatalMessage;
  }

  bool pressButton(const std::string& name, bool down) {
    uint16_t pin = 0;
    if (name == "CENTER")
      pin = BUTTON_PIN_CENTER;
    else if (name == "LEFT")
      pin = BUTTON_PIN_LEFT;
    else if (name == "RIGHT")
      pin = BUTTON_PIN_RIGHT;
    else if (name == "UP")
      pin = BUTTON_PIN_UP;
    else if (name == "DOWN")
      pin = BUTTON_PIN_DOWN;
    else
      return false;

    // Buttons pull their pin high when pressed, the same polarity hardware/buttons.cpp expects.
    board().setInputLevel(pin, down);
    return true;
  }

}  // namespace sim
