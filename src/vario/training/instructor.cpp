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
#include "instructor_link.h"
#include "instructor_pairing_screen.h"
#include "protocol.h"
#include "radio.h"
#include "ui/audio/notes.h"
namespace {
  training::Config config;
  training::Console console;
  training::Radio radio;
  training::Receiver receiver;
  training::ClimbAudio climbAudio;
  training::InstructorLink enrollment;
  U8G2_ST75256_JLX19296_F_4W_HW_SPI lcd(U8G2_R1, 10, 17, 18);
  uint32_t session = 0, sequence = 0, lastPollAt = 0;
  uint8_t tick = 0;
  bool muted = false;
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
    receiver.cancel();
    climbAudio.reset();
    tick = 0;
    lastPollAt = millis() - training::SLOT_MS;
    ledcWriteTone(14, 0);
    playingTone = 0;
    outputHz = 0;
    discardOutput();
    lastGps = millis() - 1000;
    gpsSentence = 0;
  }
  void pair() {
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
    enrollment.open(millis());
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
  void buttons() {
    static const uint8_t pins[] = {5, 4, 3, 6, 2};  // up/down/left/right/center, all direct GPIO.
    static uint8_t raw = 0, stable = 0;
    static uint32_t changed = 0, centerAt = 0;
    static bool forgotten = false;
    uint8_t value = 0;
    for (unsigned i = 0; i < 5; ++i)
      if (digitalRead(pins[i])) value |= 1 << i;
    if (value != raw) {
      raw = value;
      changed = millis();
    }
    if (value != stable && uint32_t(millis() - changed) >= 20) {
      uint8_t pressed = value & ~stable;
      stable = value;
      if (enrollment.active(millis())) {
        if (pressed & 1) enrollment.move(-1, millis());
        if (pressed & 2) enrollment.move(1, millis());
        if (pressed & 4) {
          enrollment.cancel();
          inform("Pairing closed");
        }
        if (pressed & 8) chooseStudent();
      } else {
        if (pressed & 1) focus((receiver.focus + 3) % 4);
        if (pressed & 2) focus((receiver.focus + 1) % 4);
        if (pressed & 4) setVolume(muted ? 1 : (volume + 1) % 4);
        if (pressed & 8) pair();
      }
      if (pressed) testSound(note::C4, vario_audio::FRAME_MS * FX_NOTE_SAMPLE_COUNT);
      if (pressed & 16) {
        centerAt = millis();
        forgotten = enrollment.active(millis());
      }
    }
    if (enrollment.active(millis())) forgotten = true;
    if ((stable & 16) && !forgotten && !enrollment.active(millis()) &&
        uint32_t(millis() - centerAt) >= 3000) {
      forget();
      forgotten = true;
    }
  }
  void display() {
    static uint32_t last = 0;
    if (uint32_t(millis() - last) < 250 || radio.busy()) return;
    last = millis();
    lcd.clearBuffer();
    lcd.setFont(u8g2_font_5x7_tf);
    char line[32];
    if (enrollment.active(millis())) {
      training::drawInstructorPairing(lcd, enrollment, roster(), millis(), training::deviceId());
      if (notice[0] && uint32_t(millis() - noticeAt) < 2500) lcd.drawStr(0, 140, notice);
      lcd.sendBuffer();
      return;
    }
    lcd.drawStr(0, 8, "LEAF INSTRUCTOR");
    snprintf(line, sizeof(line), "Inst %08lx", (unsigned long)training::deviceId());
    lcd.drawStr(0, 20, line);
    if (radio.ready())
      snprintf(line, sizeof(line), "RX %lu", (unsigned long)accepted);
    else
      snprintf(line, sizeof(line), "Radio off/fault");
    lcd.drawStr(0, 32, line);
    for (unsigned i = 0; i < 4; ++i) {
      const auto& s = receiver.students[i];
      if (!s.id)
        snprintf(line, sizeof(line), "%c%u Unpaired", receiver.focus == i ? '>' : ' ', i + 1);
      else
        snprintf(line, sizeof(line), "%c%u %08lx %s", receiver.focus == i ? '>' : ' ', i + 1,
                 (unsigned long)s.id,
                 s.received && uint32_t(millis() - s.receivedAt) <
                                   (i == receiver.focus ? training::LINK_STALE_MS : 2500)
                     ? "live"
                     : "lost");
      lcd.drawStr(0, 48 + i * 14, line);
    }
    if (receiver.climbFresh(millis()))
      snprintf(line, sizeof(line), "Climb %+.2f m/s",
               receiver.students[receiver.focus].data.climbCms / 100.0);
    else
      snprintf(line, sizeof(line), "No fresh climb");
    lcd.drawStr(0, 110, line);
    static const char* const volumeNames[] = {"Audio muted", "Volume low", "Volume medium",
                                              "Volume high"};
    lcd.drawStr(0, 124,
                muted                  ? volumeNames[0]
                : !speakerControlReady ? "Volume unavailable"
                                       : volumeNames[volume]);
    lcd.drawStr(0, 138, server->getConnectedCount() ? "Phone connected" : bleName);
    lcd.drawStr(0, 152,
                notice[0] && uint32_t(millis() - noticeAt) < 4000 ? notice : "Right: add student");
    lcd.drawStr(0, 164, "Up/down: focus");
    lcd.drawStr(0, 176, "Left: volume");
    lcd.drawStr(0, 188, "Hold OK: forget");
    lcd.sendBuffer();
  }
}  // namespace
void setup() {
  gpio_set_level(GPIO_NUM_48, 1);
  gpio_set_direction(GPIO_NUM_48, GPIO_MODE_OUTPUT);  // Hold regulator during board setup.
  pinMode(47, OUTPUT);
  digitalWrite(47, HIGH);
  Serial.begin(115200);
  // HWCDC's unsigned retry counter underflows at zero when the host stops draining TX.
  // Keep all USB writes bounded even with a closed/unplugged serial monitor.
  Serial.setTxTimeoutMs(1);
  for (uint8_t pin : {2, 3, 4, 5, 6}) pinMode(pin, INPUT_PULLDOWN);
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
  volume = config.store.getUChar("volume", 2);
  // Always start audible: medium minimum, retaining a saved high setting.
  if (volume < 2 || volume > 3) volume = 2;
  initBoardControls();
  applyBoardVolume();
  loadRoster();
  receiver.focus = config.store.getUChar("focus", 0);
  if (receiver.focus >= training::STUDENTS) receiver.focus = 0;
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
      "INSTRUCTOR BENCH v1: Right adds a student; group/slots assigned automatically; "
      "audio starts enabled at medium or saved high volume.");
}
void loop() {
  commands();
  buttons();
  const uint32_t now = millis();
  const auto expired = enrollment.expire(now);
  if (expired != training::InstructorLink::Phase::Closed)
    inform(expired == training::InstructorLink::Phase::Confirming ? "Saved; no data yet"
                                                                  : "Pair window ended");
  training::Packet packet;
  if (radio.receive(packet)) bus.receive(Received(packet, now));
  if (config.group && radio.ready() && !radio.busy() &&
      uint32_t(now - lastPollAt) >= training::SLOT_MS) {
    lastPollAt = now;
    receiver.cancel();
    const uint8_t slot = training::scheduledStudent(tick, receiver.focus);
    tick = (tick + 1) % 8;
    training::Packet control;
    const bool controlSlot =
        slot >= training::STUDENTS || !receiver.students[slot].id ||
        (enrollment.active(now) && receiver.students[slot].id == enrollment.peer());
    if (controlSlot && enrollment.control(control, now, training::deviceId(), session)) {
      // Jitter plus the pending peer's slots avoids phase-locking against its fixed 500 ms Accept
      // retries.
      if (radio.send(control)) enrollment.sent(now, 500 + esp_random() % 375);
    } else if (slot < training::STUDENTS && receiver.students[slot].id) {
      training::Packet poll;
      poll.header = {training::Type::Poll, config.group, training::deviceId(), session, ++sequence};
      poll.requestedSlot = slot;
      poll.focusSlot = receiver.focus;
      if (radio.send(poll)) receiver.expect(slot, sequence, now);
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
