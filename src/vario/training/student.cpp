#include "student.h"
#ifdef LEAF_TRAINING_STUDENT
#include <math.h>
#include <algorithm>
#include "comms/fanet_radio.h"
#include "esp_random.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "logbook/logbook_io.h"
#include "power.h"
#include "profiles/profile_store.h"
#include "storage/sd_card.h"
namespace training {
  Student student;
  void Student::begin() {
    config_.begin();
    link_.binding = {config_.group, config_.instructor, config_.slot};
    uint32_t saved[3]{};
    if (config_.store.getBytesLength("binding") == sizeof(saved) &&
        config_.store.getBytes("binding", saved, sizeof(saved)) == sizeof(saved)) {
      link_.binding = {saved[0], saved[1], uint8_t(saved[2])};
      if (saved[2] >= STUDENTS) link_.binding = {};
    }
    // Migrate a previously paired bench device without losing its working connection.
    link_.enabled = config_.store.getBool("enabled", link_.binding.valid());
    session_ = esp_random();
    if (!session_) session_ = 1;
    lastHandoff_ = millis() - 250;
    Serial.println(
        "TRAINING STUDENT: Leaf Labs > Training; enable 0/1, pair, accept, cancel, forget, status");
  }
  bool Student::saveBinding() {
    const auto& b = link_.binding;
    const uint32_t values[] = {b.group, b.instructor, b.slot};
    storageError_ = config_.store.putBytes("binding", values, sizeof(values)) != sizeof(values);
    return !storageError_;
  }
  bool Student::setEnabled(bool value) {
    storageError_ = config_.store.putBool("enabled", value) != 1;
    if (storageError_) return false;
    link_.setEnabled(value);
    lastHandoff_ = millis() - 250;
    return true;
  }
  bool Student::startPair() {
    if (!enabled() || !fanetRadio.isDetected()) return false;
    uint32_t nonce = esp_random();
    link_.beginPair(millis(), nonce ? nonce : 1);
    pairTxAt_ = millis();
    nextPairTxMs_ = 100 + esp_random() % 400;
    return true;
  }
  bool Student::confirmPair() {
    if (!link_.acceptOffer(millis())) return false;
    pairTxAt_ = millis() - 500;
    nextPairTxMs_ = 0;
    return true;
  }
  void Student::cancelPair() { link_.cancelPair(); }
  bool Student::unpair() {
    const Binding previous = link_.binding;
    link_.unpair();
    if (saveBinding()) return true;
    link_.binding = previous;
    return false;
  }
  const char* Student::status() const {
    if (storageError_) return "Save failed";
    if (!enabled()) return "Disabled";
    if (accepting()) return "Linking...";
    if (offered()) return "Confirm pair";
    if (pairing()) return "Searching...";
    if (!paired()) return "Not paired";
    if (!reserved_ || !radio_.ready()) return "Radio waiting";
    return link_.connected(millis()) ? "Connected" : "Connecting...";
  }
  void Student::on_receive(const GpsReading& message) {
    // The normal GPS instrument publishes this parsed bus value. No RF/SPI/formatting here.
    TinyGPSPlus g = message.gps;
    gps_.valid = 0;
    gpsAt_ = millis();
    if (g.location.isValid() && g.location.age() < 2500 && g.altitude.isValid() &&
        g.speed.isValid() && g.course.isValid()) {
      gps_.valid |= Position;
      gps_.gpsAge = std::min<uint32_t>(65535, g.location.age());
      gps_.latitudeE7 = lround(g.location.lat() * 1e7);
      gps_.longitudeE7 = lround(g.location.lng() * 1e7);
      gps_.gpsAltitudeCm = lround(g.altitude.meters() * 100);
      gps_.speedCms = std::min<double>(65535, std::max(0.0, g.speed.mps() * 100));
      gps_.courseCdeg = uint16_t(lround(g.course.deg() * 100)) % 36000;
    }
    if (g.date.isValid() && g.time.isValid() && g.time.age() < 2500) {
      gps_.valid |= Utc;
      gps_.date = g.date.year() * 10000 + g.date.month() * 100 + g.date.day();
      gps_.time = g.time.value();
    }
  }
  void Student::update() {
    const uint32_t now = millis();
    if (const char* line = console_.read()) {
      uint32_t value = 0;
      if (argument(line, "enable", value) && value <= 1)
        setEnabled(value);
      else if (!strcmp(line, "pair"))
        startPair();
      else if (!strcmp(line, "accept"))
        confirmPair();
      else if (!strcmp(line, "cancel"))
        cancelPair();
      else if (!strcmp(line, "forget"))
        unpair();
      statusPage_ = 0;
      lastStatus_ = now - 5000;
    }
    const bool wantsRadio = enabled() && power.info().onState == PowerState::On;
    // All SPI/IRQ transitions occur outside button/bus callbacks, with bounded retries.
    if (uint32_t(now - lastHandoff_) >= 250) {
      lastHandoff_ = now;
      if (wantsRadio && !reserved_) reserved_ = fanetRadio.reserveForTraining();
      if (!wantsRadio && reserved_ && radio_.end()) {
        reserved_ = false;
        fanetRadio.releaseFromTraining();
      }
      if (wantsRadio && reserved_ && !radio_.ready()) radio_.begin();
    }
    Packet poll;
    if (wantsRadio && reserved_ && radio_.receive(poll)) {
      const Binding previous = link_.binding;
      if (link_.enroll(poll, now, deviceId(), session_) && !saveBinding()) link_.binding = previous;
      if (link_.poll(poll, now)) {
        Packet response;
        response.header = {Type::Telemetry, link_.binding.group, deviceId(), session_,
                           poll.header.sequence};
        response.data = gps_;
        auto& d = response.data;
        d.slot = link_.binding.slot;
        d.gpsAge = std::min<uint32_t>(65535, uint32_t(millis() - gpsAt_) + gps_.gpsAge);
        if (!gps.hasUsableFix()) d.valid &= ~Position;
        if (d.gpsAge >= 2500) d.valid &= ~(Position | Utc);
        if (baro.pressureFresh()) {
          d.valid |= Baro;
          d.pressurePa = int32_t(baro.pressure());
          d.baroAltitudeCm = baro.alt();
          d.pressureAge = std::min<uint32_t>(65535, uint32_t(millis() - baro.lastPressureTime()));
          if (baro.climbRateFilteredValid()) {
            d.valid |= Climb;
            d.climbCms = std::clamp<int32_t>(baro.climbRateFiltered(), -32768, 32767);
          }
        }
        // V1 keeps the legacy tone bytes for wire compatibility; audio is synthesized from climb.
        d.toneHz = 0;
        radio_.send(response);
      }
    }
    if (wantsRadio && reserved_ && radio_.ready() && !radio_.busy() && pairing() &&
        uint32_t(now - pairTxAt_) >= nextPairTxMs_) {
      // Jitter bounds collisions when several students open discovery together.
      Packet control;
      if (accepting()) {
        control = link_.acceptance(deviceId(), session_);
      } else {
        control = link_.hello(deviceId(), session_);
        // Setup only: release the profile/JSON and SD lock before sending. Never read profiles
        // or retain a pilot-name cache during the flight telemetry path.
        logbook_io::Guard transaction;
        if (!transaction) return;
        if (sdcard.firmwareCanAccessFilesystem()) {
          PilotProfile pilot;
          if (ProfileStore::activePilot(pilot))
            copyPilotName(control.pilotName, pilot.name.c_str(), HELLO_NAME_BYTES);
        }
      }
      if (radio_.send(control)) {
        pairTxAt_ = now;
        nextPairTxMs_ = accepting() ? 500 : 750 + esp_random() % 500;
      }
    }
    if (uint32_t(millis() - lastStatus_) >= 5000) {
      lastStatus_ = millis();
      // One short line per interval fits the nonblocking USB FIFO without delaying flight work.
      char statusLine[160]{};
      switch (statusPage_) {
        case 0:
          snprintf(statusLine, sizeof(statusLine),
                   "TRAINING rf=%u on=%u own=%u fanet=%u pwr=%u err=%d\n", radio_.ready(),
                   enabled(), reserved_, unsigned(int(fanetRadio.getState())),
                   unsigned(power.info().onState), radio_.lastError);
          break;
        case 1:
          snprintf(statusLine, sizeof(statusLine), "TRAINING peer id=%08lx instructor=%08lx\n",
                   (unsigned long)deviceId(), (unsigned long)instructor());
          break;
        case 2:
          snprintf(statusLine, sizeof(statusLine), "TRAINING counts rx=%lu tx=%lu errors=%lu\n",
                   (unsigned long)radio_.received, (unsigned long)radio_.sent,
                   (unsigned long)radio_.errors);
          break;
        case 3:
          snprintf(statusLine, sizeof(statusLine), "TRAINING g=%lu s=%u on=%u %s\n",
                   (unsigned long)link_.binding.group, slot(), enabled(), status());
          break;
      }
      const size_t length = strlen(statusLine);
      if (Serial.availableForWrite() >= int(length))
        Serial.write(reinterpret_cast<const uint8_t*>(statusLine), length);
      statusPage_ = (statusPage_ + 1) % 4;
    }
  }
}  // namespace training
#endif
