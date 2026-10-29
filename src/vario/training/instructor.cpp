#ifdef LEAF_TRAINING_INSTRUCTOR
// Instructor startup uses the board IO expander, but no local flight sensors or estimator.
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <SPI.h>
#include <TCA9555.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <etl/message.h>
#include <etl/message_bus.h>
#include <etl/message_router.h>
#include <algorithm>
#include <atomic>
#include <initializer_list>
#include "climb_audio.h"
#include "console.h"
#include "esp_random.h"
#include "hardware/configuration.h"
#include "hardware/io_pins.h"
#include "instructor_controls.h"
#include "instructor_link.h"
#include "instructor_pairing_screen.h"
#include "instructor_screen.h"
#include "protocol.h"
#include "radio.h"
#include "ui/audio/notes.h"
// v3.2.5 uses the expander's all-high reset output latches for non-volume pins.
#ifndef IOEX_REG_OUTPUT_PORT0
#define IOEX_REG_OUTPUT_PORT0 0xff
#endif
#ifndef IOEX_REG_OUTPUT_PORT1
#define IOEX_REG_OUTPUT_PORT1 0xff
#endif
namespace {
  training::Config config;
  training::Console console;
  training::Radio radio;
  training::Receiver receiver;
  training::ClimbAudio climbAudio;
  training::InstructorLink enrollment;
  U8G2_ST75256_JLX19296_F_4W_HW_SPI lcd(U8G2_R1, 10, 17, 18);
  uint32_t session = 0, sequence = 0, lastFocusAt = 0;
  bool muted = false, powered = true;
  training::InstructorButtons controls;
  training::InstructorNavigation navigation;
  training::InstructorUnits units;
  training::InstructorBattery battery;
  char notice[20]{};
  uint32_t noticeAt = 0;
  void inform(const char* text) {
    snprintf(notice, sizeof(notice), "%s", text);
    noticeAt = millis();
    char line[48];
    const int n = snprintf(line, sizeof(line), "PAIR %s\n", text);
    if (n > 0 && Serial.availableForWrite() >= n)
      Serial.write(reinterpret_cast<const uint8_t*>(line), n);
  }
  training::InstructorRoster roster() {
    training::InstructorRoster r;
    r.group = config.group;
    for (uint8_t i = 0; i < training::STUDENTS; ++i) r.ids[i] = receiver.students[i].id;
    return r;
  }
  bool saveRoster(const training::InstructorRoster& r) {
    if (!r.valid()) return false;
    const uint32_t words[] = {r.group, r.ids[0], r.ids[1], r.ids[2], r.ids[3]};
    if (config.store.putBytes("roster", words, sizeof(words)) == sizeof(words)) return true;
    inform("Save failed");
    return false;
  }
  struct NameCache {
    uint32_t ids[training::STUDENTS]{};
    char names[training::STUDENTS][training::PILOT_NAME_BYTES + 1]{};
  };
  void saveNames() {
    NameCache cache;
    for (uint8_t i = 0; i < training::STUDENTS; ++i) {
      cache.ids[i] = receiver.students[i].id;
      training::copyPilotName(cache.names[i], receiver.students[i].pilotName);
    }
    if (config.store.putBytes("pilot_names", &cache, sizeof(cache)) != sizeof(cache))
      inform("Name save failed");
  }
  void loadNames() {
    NameCache cache;
    if (config.store.getBytesLength("pilot_names") != sizeof(cache) ||
        config.store.getBytes("pilot_names", &cache, sizeof(cache)) != sizeof(cache))
      return;
    for (uint8_t i = 0; i < training::STUDENTS; ++i)
      if (cache.ids[i] && cache.ids[i] == receiver.students[i].id &&
          training::validPilotName(cache.names[i]))
        training::copyPilotName(receiver.students[i].pilotName, cache.names[i]);
  }
  void loadRoster() {
    auto r = roster();
    for (uint8_t i = 0; i < training::STUDENTS; ++i) {
      char key[8];
      snprintf(key, sizeof(key), "id%u", i);
      r.ids[i] = config.store.getUInt(key, 0);
    }
    uint32_t words[5]{};
    if (config.store.getBytesLength("roster") == sizeof(words) &&
        config.store.getBytes("roster", words, sizeof(words)) == sizeof(words)) {
      r.group = words[0];
      for (uint8_t i = 0; i < training::STUDENTS; ++i) r.ids[i] = words[i + 1];
    }
    if (!r.valid()) {
      r = {};
      inform("Roster invalid");
    }
    config.group = receiver.group = r.group;
    for (uint8_t i = 0; i < training::STUDENTS; ++i) receiver.students[i].id = r.ids[i];
  }
  void receiveEnrollment(const training::Packet&, uint32_t);
  void focus(uint8_t);

  TCA9535 ioExpander(IOEX_ADDR);
  bool speakerControlReady = false;
  uint8_t volume = 2, appliedVolume = 0;
  static_assert(SPEAKER_VOLA_IOEX && SPEAKER_VOLB_IOEX && SPEAKER_VOLA < 8 && SPEAKER_VOLB < 8,
                "Instructor speaker gain must be on expander port 0");
  constexpr uint8_t VOLUME_MASK = (1U << SPEAKER_VOLA) | (1U << SPEAKER_VOLB);
  uint8_t volumeOutputs(uint8_t level) {
    // Change both gain bits in one port write; preserve every unrelated output latch.
    return (IOEX_REG_OUTPUT_PORT0 & ~VOLUME_MASK) | ((level & 1) ? (1U << SPEAKER_VOLA) : 0) |
           ((level & 2) ? (1U << SPEAKER_VOLB) : 0);
  }
  void initBoardControls() {
    // No sensor address scan or sensor readiness checks. Failed IO is bounded and nonfatal.
    Wire.setTimeOut(10);
    if (!Wire.begin(8, 9, 400000)) {
      Serial.println("INSTRUCTOR: board IO bus unavailable");
      return;
    }
    Wire.setTimeOut(10);
    speakerControlReady = ioExpander.isConnected() && ioExpander.write8(0, volumeOutputs(0)) &&
                          ioExpander.write8(1, IOEX_REG_OUTPUT_PORT1) &&
                          ioExpander.pinMode8(0, IOEX_REG_CONFIG_PORT0) &&
                          ioExpander.pinMode8(1, IOEX_REG_CONFIG_PORT1);
    if (!speakerControlReady) Serial.println("INSTRUCTOR: board IO controls unavailable");
  }
  void setVolume(uint8_t level) {
    if (level > 3) return;
    muted = level == 0;
    if (level && level != volume) {
      volume = level;
      config.store.putUChar("volume", volume);
    }
  }
  uint32_t accepted = 0, playingTone = 0, outputHz = 0;
  uint32_t soundTestAt = 0, soundDuration = 750, soundFrequency = 1000;
  bool soundTest = false;
  void testSound(uint32_t frequency = 1000, uint32_t duration = 750) {
    soundTest = true;
    soundTestAt = millis();
    soundFrequency = frequency;
    soundDuration = duration;
  }
  void applyBoardVolume() {
    const uint8_t desired = muted ? 0 : volume;
    if (!speakerControlReady || desired == appliedVolume) return;
    ledcWriteTone(14, 0);
    playingTone = 0;
    outputHz = 0;
    if (!ioExpander.write8(0, volumeOutputs(desired))) {
      speakerControlReady = false;  // Do not retry a failing bus on the radio/audio loop.
      Serial.println("INSTRUCTOR: speaker volume control unavailable");
      return;
    }
    appliedVolume = desired;
  }
  struct Received : etl::message<240> {
    training::Packet packet;
    uint32_t at;
    Received(const training::Packet& p, uint32_t t) : packet(p), at(t) {}
  };
  class Sink : public etl::message_router<Sink, Received> {
   public:
    Sink() : message_router(0) {}
    void on_receive(const Received& msg) {
      receiveEnrollment(msg.packet, msg.at);
      // Enrollment is explicit. Telemetry can never claim an unknown roster slot.
      if (receiver.accept(msg.packet, msg.at, false)) {
        ++accepted;
        if (enrollment.linked(msg.packet, msg.at)) inform("Student linked");
      }
    }
    void on_receive_unknown(const etl::imessage&) {}
  } sink;
  etl::message_bus<1> bus;
  char bleName[12]{};
  NimBLEServer* server = nullptr;
  NimBLECharacteristic* tx = nullptr;
  std::atomic<bool> restartAdvertising{false};
  class BleCallbacks : public NimBLEServerCallbacks {
    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
      restartAdvertising.store(true);
    }
  } callbacks;
  char output[96]{};
  size_t outputLength = 0, outputAt = 0;
  uint32_t outputSince = 0, lastBle = 0, lastGps = 0, lastVario = 0, notifyErrors = 0;
  uint8_t gpsSentence = 0;
  void discardOutput() {
    // Terminate a partly sent NMEA line when changing source or losing freshness.
    output[0] = '\r';
    output[1] = '\n';
    outputLength = 2;
    outputAt = 0;
    outputSince = millis();
  }
  void focus(uint8_t value) {
    if (value >= training::STUDENTS) return;
    if (receiver.focus != value) config.store.putUChar("focus", value);
    receiver.focus = value;
    navigation.row = value;
    receiver.cancel();
    climbAudio.reset();
    lastFocusAt = millis() - training::FOCUS_PERIOD_MS;
    ledcWriteTone(14, 0);
    playingTone = 0;
    outputHz = 0;
    discardOutput();
    lastGps = millis() - 1000;
    gpsSentence = 0;
  }
  void pair(uint8_t preferredSlot = training::STUDENTS) {
    if (!config.group) {
      auto r = roster();
      do {
        r.group = esp_random();
      } while (!r.group || r.group == training::DISCOVERY_GROUP);
      if (!saveRoster(r)) return;
      config.group = receiver.group = r.group;
    }
    if (!radio.ready() && !radio.begin()) {
      inform("Radio unavailable");
      return;
    }
    navigation.page = training::InstructorPage::Roster;
    enrollment.open(millis(), preferredSlot);
    inform("Searching students");
  }
  void chooseStudent() {
    if (enrollment.phase(millis()) != training::InstructorLink::Phase::Discovering) return;
    const auto r = roster();
    if (!enrollment.choose(r, millis())) {
      const auto& c = enrollment.candidates[enrollment.selection];
      inform(c.fresh(millis()) && r.availableSlot(c.id) < 0 ? "Roster full" : "No fresh student");
      return;
    }
    inform("Offer selected");
  }
  void receiveEnrollment(const training::Packet& packet, uint32_t now) {
    enrollment.hello(packet, now);
    if (!enrollment.needsCommit(packet, now, training::deviceId(), session)) return;
    auto r = roster();
    const uint8_t slot = enrollment.slot();
    // Never replace another student, even if runtime state changes during discovery.
    if (r.ids[slot] && r.ids[slot] != enrollment.peer()) {
      enrollment.cancel();
      inform("Slot occupied");
      return;
    }
    r.ids[slot] = enrollment.peer();
    if (!saveRoster(r)) {
      enrollment.cancel();
      return;
    }
    receiver.cancel();
    receiver.students[slot] = {};
    receiver.students[slot].id = enrollment.peer();
    training::copyPilotName(receiver.students[slot].pilotName, enrollment.peerName());
    saveNames();
    focus(slot);
    enrollment.committed(now);
    inform("Student saved");
  }
  void forget() {
    auto r = roster();
    r.ids[receiver.focus] = 0;
    if (!saveRoster(r)) return;
    enrollment.cancel();
    receiver.students[receiver.focus] = {};
    saveNames();
    focus(receiver.focus);
    inform("Student removed");
  }
  void bluetooth() {
    if (restartAdvertising.exchange(false)) NimBLEDevice::getAdvertising()->start();
    if (!server->getConnectedCount()) {
      outputLength = outputAt = 0;
      return;
    }
    const uint32_t now = millis();
    if (outputLength && uint32_t(now - outputSince) > 500) {
      ++notifyErrors;
      discardOutput();
    }
    if (!outputLength) {
      unsigned sentence = 2;
      if (gpsSentence) {
        sentence = 1;
        gpsSentence = 0;
      } else if (uint32_t(now - lastGps) >= 1000) {
        sentence = 0;
        gpsSentence = 1;
        lastGps = now;
      } else if (uint32_t(now - lastVario) < 250)
        return;
      else
        lastVario = now;
      outputLength = training::nmea(receiver, now, sentence, output, sizeof(output));
      outputAt = 0;
      outputSince = now;
    }
    if (outputLength && uint32_t(now - lastBle) >= 5) {
      lastBle = now;
      const size_t n =
          std::min<size_t>(20, outputLength - outputAt);  // Valid even at default ATT MTU=23.
      tx->setValue(reinterpret_cast<uint8_t*>(output) + outputAt, n);
      if (tx->notify()) {
        outputAt += n;
        if (outputAt == outputLength) outputLength = outputAt = 0;
      }
    }
  }
  void initBattery() {
    pinMode(1, INPUT);
    if (!POWER_CHARGE_GOOD_IOEX) pinMode(POWER_CHARGE_GOOD, INPUT_PULLUP);
#ifdef ISET
    pinMode(ISET, INPUT);
#endif
    // Match normal Leaf's 500-mA input limit on boards with direct charge-control pins.
    if (!POWER_CHARGE_I2_IOEX) {
      digitalWrite(POWER_CHARGE_I2, LOW);
      pinMode(POWER_CHARGE_I2, OUTPUT);
    }
    if (!POWER_CHARGE_I1_IOEX) {
      digitalWrite(POWER_CHARGE_I1, HIGH);
      pinMode(POWER_CHARGE_I1, OUTPUT);
    }
  }
  void updateBattery(uint32_t now) {
    if (!battery.due(now)) return;
    bool chargingKnown = true, charging = false, usbKnown = false, usb = false;
    if (POWER_CHARGE_GOOD_IOEX) {
      chargingKnown = speakerControlReady;
      if (chargingKnown) {
        const int port = ioExpander.read8(POWER_CHARGE_GOOD / 8);
        if (ioExpander.lastError() == 0) {
          charging = !(port & (1U << (POWER_CHARGE_GOOD % 8)));
#if POWER_GOOD_IOEX
          if (POWER_GOOD / 8 == POWER_CHARGE_GOOD / 8) {
            usbKnown = true;
            usb = !(port & (1U << (POWER_GOOD % 8)));
          }
#endif
        } else {
          chargingKnown = speakerControlReady = false;
          Serial.println("INSTRUCTOR: board battery status unavailable");
        }
      }
    } else {
      charging = !digitalRead(POWER_CHARGE_GOOD);
    }
    uint16_t currentMA = 0;
#ifdef ISET
    currentMA = analogReadMilliVolts(ISET) * 400 / 1100;
#endif
    battery.sample(now, analogReadMilliVolts(1), chargingKnown, charging,
#ifdef ISET
                   true,
#else
                   false,
#endif
                   currentMA, usbKnown, usb);
#ifdef LED_PIN
    digitalWrite(LED_PIN, powered && battery.usbKnown && battery.usb ? LOW : HIGH);
#endif
  }
  void commands() {
    if (const char* line = console.read()) {
      uint32_t value = 0;
      if (training::argument(line, "group", value)) {
        training::InstructorRoster r;
        r.group = value;
        if (!saveRoster(r)) return;
        enrollment.cancel();
        receiver = {};
        config.group = receiver.group = value;
        if (value)
          radio.begin();
        else
          radio.end();
        focus(0);
      } else if (training::argument(line, "focus", value) && value < training::STUDENTS)
        focus(value);
      else if (!strcmp(line, "pair"))
        pair();
      else if (!strcmp(line, "offer"))
        chooseStudent();
      else if (!strcmp(line, "cancel"))
        enrollment.cancel();
      else if (!strcmp(line, "forget"))
        forget();
      else if (training::argument(line, "volume", value) && value <= 3)
        setVolume(value);
      else if (!strcmp(line, "beep"))
        testSound();
      else if (!strcmp(line, "mute"))
        muted = true;
      else if (!strcmp(line, "unmute"))
        muted = false;
      else if (!strcmp(line, "status")) {
        char line[96];
        const int n =
            snprintf(line, sizeof(line), "PAIR phase=%u peer=%08lx slot=%u remaining=%lu\n",
                     unsigned(enrollment.phase(millis())), (unsigned long)enrollment.peer(),
                     enrollment.slot(), (unsigned long)enrollment.remaining(millis()));
        if (n > 0 && Serial.availableForWrite() >= n)
          Serial.write(reinterpret_cast<const uint8_t*>(line), n);
      } else
        Serial.println(
            "pair, offer, cancel, focus <0..3>, forget, volume <0..3>, mute, unmute, beep, status");
    }
  }
  uint8_t readButtons() {
    static const uint8_t pins[] = {5, 4, 3, 6, 2};  // up/down/left/right/center, direct GPIO.
    uint8_t value = 0;
    for (unsigned i = 0; i < 5; ++i)
      if (digitalRead(pins[i])) value |= 1 << i;
    return value;
  }
  void togglePower() {
    if (!powered) {
      // A clean restart rebuilds BLE/radio state and invalidates every old remote sample.
      gpio_set_level(GPIO_NUM_48, 1);
      ESP.restart();
      return;
    }
    powered = false;
    controls.consumePress();
    // Show the committed state and drop the latch before potentially slow peripheral teardown.
    // Holding the physical button still supplies power; releasing it can now switch off reliably.
    lcd.clearBuffer();
    training::drawInstructorPower(lcd, false, 0);
    lcd.sendBuffer();
    gpio_set_level(GPIO_NUM_48, 0);
    enrollment.cancel();
    receiver.cancel();
    climbAudio.reset();
    soundTest = false;
    ledcWriteTone(14, 0);
    playingTone = outputHz = 0;
    if (speakerControlReady) ioExpander.write8(0, volumeOutputs(0));
    appliedVolume = 0;
    // Standby cancels any in-flight packet; sleep also disables the RF switch.
    if (!radio.end(true)) Serial.println("INSTRUCTOR: radio shutdown failed");
    NimBLEDevice::getAdvertising()->stop();
    if (!NimBLEDevice::deinit(false)) Serial.println("INSTRUCTOR: BLE shutdown failed");
    restartAdvertising.store(false);
    outputLength = outputAt = 0;
    lcd.clearBuffer();
    training::drawInstructorPower(lcd, false, 0);
    lcd.sendBuffer();
    digitalWrite(21, LOW);  // Backlight and status LED off.
#ifdef LED_PIN
    digitalWrite(LED_PIN, HIGH);
#endif
    Serial.println("INSTRUCTOR: power off; hold center 3s to restart on USB");
    // Battery operation ends here once the user releases the physical power button.
    // USB can keep the MCU alive; its off loop accepts only the next power hold.
    gpio_set_level(GPIO_NUM_48, 0);
  }
  void uiAction(training::InstructorAction action, int8_t direction = 1, bool cycle = true) {
    using Action = training::InstructorAction;
    switch (action) {
      case Action::Focus:
        focus(navigation.row);
        break;
      case Action::Volume: {
        int level = muted ? 0 : volume;
        level = cycle ? (level + 1) % 4 : std::clamp<int>(level + direction, 0, 3);
        setVolume(level);
        break;
      }
      case Action::AltitudeUnits:
        units.altitudeFeet = !units.altitudeFeet;
        config.store.putBool("ui_alt_ft", units.altitudeFeet);
        break;
      case Action::SpeedUnits:
        units.speedMph = !units.speedMph;
        config.store.putBool("ui_speed_mph", units.speedMph);
        break;
      case Action::ClimbUnits:
        units.climbFpm = !units.climbFpm;
        config.store.putBool("ui_climb_fpm", units.climbFpm);
        break;
      case Action::Pair:
        pair(navigation.page == training::InstructorPage::Roster ? navigation.row
                                                                 : training::STUDENTS);
        break;
      case Action::AskRemove:
        if (receiver.students[receiver.focus].id)
          navigation.askRemove();
        else
          inform("No saved student");
        break;
      case Action::Remove:
        forget();
        navigation.page = training::InstructorPage::Roster;
        break;
      default:
        break;
    }
  }
  training::InstructorAction selectedAction() {
    return navigation.select(navigation.row >= training::STUDENTS ||
                             receiver.students[navigation.row].id != 0);
  }
  void buttons() {
    const auto event = controls.update(readButtons(), millis());
    // This gesture has priority in every view, including discovery and confirmation.
    if (event.power) {
      togglePower();
      return;
    }
    if (!powered) return;
    if (event.pressed) testSound(note::C4, vario_audio::FRAME_MS * FX_NOTE_SAMPLE_COUNT);
    if (controls.centerDown()) return;
    const uint8_t pressed = event.pressed;
    if (enrollment.active(millis())) {
      if (pressed & 1) enrollment.move(-1, millis());
      if (pressed & 2) enrollment.move(1, millis());
      if (pressed & 4) {
        enrollment.cancel();
        navigation.page = training::InstructorPage::Settings;
      }
      if ((pressed & 8) || event.click) chooseStudent();
      return;
    }
    if (pressed & 1) navigation.move(-1);
    if (pressed & 2) navigation.move(1);
    if (pressed & 4) {
      if (navigation.page == training::InstructorPage::Settings && navigation.setting < 4)
        uiAction(selectedAction(), -1, false);
      else
        navigation.back();
    } else if ((pressed & 8) || event.click) {
      uiAction(selectedAction(), 1, event.click);
    }
  }
  void display() {
    static uint32_t last = 0, lastHolding = 0;
    const uint32_t now = millis();
    const uint32_t holding = controls.holding(now);
    // Keep normal refresh at 4 Hz. Animate a power hold at 10 Hz; leave USB-off idle.
    if (!powered && holding < training::InstructorButtons::PREVIEW_MS &&
        lastHolding < training::InstructorButtons::PREVIEW_MS)
      return;
    if (uint32_t(now - last) < (holding >= training::InstructorButtons::PREVIEW_MS ? 100U : 250U) ||
        (powered && radio.busy()))
      return;
    last = now;
    lastHolding = holding;
    lcd.clearBuffer();
    if (!powered || holding >= training::InstructorButtons::PREVIEW_MS) {
      training::drawInstructorPower(lcd, powered, holding);
    } else if (enrollment.active(now)) {
      training::drawInstructorPairing(lcd, enrollment, roster(), now, training::deviceId());

    } else if (navigation.page != training::InstructorPage::Roster) {
      static const char* const names[] = {"Off", "Low", "Med", "High"};
      training::drawInstructorSettings(
          lcd, navigation, receiver.focus, receiver.students[receiver.focus].id,
          training::deviceId(), units,
          muted                  ? names[0]
          : !speakerControlReady ? "?"
                                 : names[volume],
          bleName, server->getConnectedCount(),
          notice[0] && uint32_t(now - noticeAt) < 2500 ? notice : nullptr,
          receiver.students[receiver.focus].pilotName);
    } else {
      training::drawInstructorRoster(
          lcd, receiver, now, navigation, units, radio.ready(), server->getConnectedCount(),
          muted ? 0 : volume, notice[0] && uint32_t(now - noticeAt) < 2500 ? notice : nullptr,
          &battery);
    }
    lcd.sendBuffer();
  }
}  // namespace
void setup() {
  gpio_set_level(GPIO_NUM_48, 1);
  gpio_set_direction(GPIO_NUM_48, GPIO_MODE_OUTPUT);  // Hold regulator during board setup.
#ifdef LED_PIN
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
#endif
  Serial.begin(115200);
  // HWCDC's unsigned retry counter underflows at zero when the host stops draining TX.
  // Keep all USB writes bounded even with a closed/unplugged serial monitor.
  Serial.setTxTimeoutMs(1);
  for (uint8_t pin : {2, 3, 4, 5, 6}) pinMode(pin, INPUT_PULLDOWN);
  controls.begin(readButtons(), millis());
  pinMode(10, OUTPUT);
  digitalWrite(10, HIGH);
  pinMode(46, OUTPUT);
  digitalWrite(46, HIGH);
  SPI.begin(12, 13, 11, 10);
  lcd.setBusClock(20000000);
  lcd.begin();
  lcd.setContrast(125);
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);
  if (!ledcAttach(14, 1000, 8)) Serial.println("INSTRUCTOR: speaker PWM init failed");
  ledcWriteTone(14, 0);
  config.begin();
  units.altitudeFeet = config.store.getBool("ui_alt_ft", false);
  units.speedMph = config.store.getBool("ui_speed_mph", false);
  units.climbFpm = config.store.getBool("ui_climb_fpm", false);
  volume = config.store.getUChar("volume", 2);
  // Always start audible: medium minimum, retaining a saved high setting.
  if (volume < 2 || volume > 3) volume = 2;
  initBoardControls();
  initBattery();
  updateBattery(millis());
  applyBoardVolume();
  loadRoster();
  loadNames();
  receiver.focus = config.store.getUChar("focus", 0);
  if (receiver.focus >= training::STUDENTS) receiver.focus = 0;
  navigation.row = receiver.focus;
  bus.subscribe(sink);
  session = esp_random();
  if (!session) session = 1;
  sequence = esp_random();
  snprintf(bleName, sizeof(bleName), "Inst%04lX", (unsigned long)(training::deviceId() & 0xffff));
  NimBLEDevice::init(bleName);
  server = NimBLEDevice::createServer();
  server->setCallbacks(&callbacks);
  auto* service = server->createService("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
  service->createCharacteristic("6E400002-B5A3-F393-E0A9-E50E24DCCA9E",
                                NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE);
  tx = service->createCharacteristic("6E400003-B5A3-F393-E0A9-E50E24DCCA9E",
                                     NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  service->start();
  auto* ad = NimBLEDevice::getAdvertising();
  // Flags + UART UUID + eight-character name fit the 31-byte primary advertisement.
  NimBLEAdvertisementData advertisement;
  const bool advertised =
      advertisement.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP) &&
      advertisement.addServiceUUID(service->getUUID()) && advertisement.setName(bleName) &&
      ad->reset() && ad->setAdvertisementData(advertisement) && ad->start();
  if (!advertised) Serial.println("INSTRUCTOR: BLE advertising failed");
  if (config.group) radio.begin();
  Serial.println(
      "INSTRUCTOR BENCH v1: Select a student or Settings; group/slots assigned automatically; "
      "audio starts enabled at medium or saved high volume.");
}
void loop() {
  buttons();
  updateBattery(millis());
  if (powered && battery.empty()) {
    Serial.println("INSTRUCTOR: low battery shutdown");
    togglePower();
  }
  if (!powered) {
    display();
    delay(5);
    return;
  }
  commands();
  const uint32_t now = millis();
  const auto expired = enrollment.expire(now);
  if (expired != training::InstructorLink::Phase::Closed)
    inform(expired == training::InstructorLink::Phase::Confirming ? "Saved; no data yet"
                                                                  : "Pair window ended");
  training::Packet packet;
  if (radio.receive(packet)) bus.receive(Received(packet, now));
  if (radio.ready() && !radio.busy()) {
    training::Packet control;
    if (enrollment.control(control, now, training::deviceId(), session)) {
      if (radio.send(control)) enrollment.sent(now, 500 + esp_random() % 375);
    } else if (config.group && uint32_t(now - lastFocusAt) >= training::FOCUS_PERIOD_MS) {
      control.header = {training::Type::Focus, config.group, training::deviceId(), session,
                        ++sequence};
      control.focusSlot = receiver.focus;
      if (radio.send(control)) lastFocusAt = now;
    }
  }
  applyBoardVolume();
  const auto& selected = receiver.students[receiver.focus];
  if (soundTest && uint32_t(now - soundTestAt) >= soundDuration) soundTest = false;
  const uint32_t varioTone = climbAudio.update(receiver, now, !muted, soundTest);
  const uint32_t tone = muted ? 0 : soundTest ? soundFrequency : varioTone;
  if (tone != playingTone) {
    outputHz = ledcWriteTone(14, tone);
    playingTone = tone;
  }
  static bool wasFresh = false;
  const bool isFresh = receiver.fresh(now);
  if (wasFresh != isFresh) {
    discardOutput();
    lastGps = now - 1000;
    lastVario = now - 250;
    gpsSentence = 0;
  }
  wasFresh = isFresh;
  bluetooth();
  display();
  static uint32_t lastStatus = 0;
  if (uint32_t(now - lastStatus) >= 5000) {
    lastStatus = now;
    char status[256];
    const int length = snprintf(
        status, sizeof(status),
        "TRAINING instructor id=%08lx group=%lu focus=%u radio=%u accepted=%lu rx=%lu tx=%lu "
        "errors=%lu code=%d volume=%u control=%u fresh=%u climb=%d vario=%lu output=%lu test=%u\n",
        (unsigned long)training::deviceId(), (unsigned long)config.group, receiver.focus,
        radio.ready(), (unsigned long)accepted, (unsigned long)radio.received,
        (unsigned long)radio.sent, (unsigned long)radio.errors, radio.lastError, muted ? 0 : volume,
        speakerControlReady, receiver.climbFresh(now), selected.data.climbCms,
        (unsigned long)varioTone, (unsigned long)outputHz, soundTest);
    // Drop diagnostics rather than allowing a full USB FIFO to hold up RF/UI/audio.
    if (length > 0 && size_t(length) < sizeof(status) && Serial.availableForWrite() >= length)
      Serial.write(reinterpret_cast<const uint8_t*>(status), length);
  }
  delay(1);
}
#endif
