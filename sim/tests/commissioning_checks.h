#pragma once

#include <cstdio>
#include <string>
#include "diagnostics/self_test/selfTest.h"
#include "instruments/gps.h"
#include "ui/audio/speaker.h"
#include "ui/settings/settings.h"

// Exercise the real parser, GPS self-test and page-close audio lifecycle together.
inline bool checkCommissioning() {
  struct Access : SelfTest { using SelfTest::testGPSfix; };
  struct Confirmation : SelfTest_PageCommissioningConfirmation {
    void dismiss() { pop_page(); }
  };
  struct Results : SelfTest_PageResults { void dismiss() { pop_page(); } };
  struct Overlay : SimpleSettingsMenuPage {
    const char* get_title() const override { return "Overlay"; }
    void show() { push_page(this); }
    void dismiss() { pop_page(); }
  };
  int failures = 0;
  auto check = [&](bool pass, const char* message) {
    if (!pass) { std::printf("commissioning: FAIL %s\n", message); ++failures; }
  };
  auto nmea = [](const char* body) {
    unsigned checksum = 0;
    for (const char* p = body; *p; ++p) checksum ^= *p;
    char suffix[8];
    std::snprintf(suffix, sizeof(suffix), "*%02X", checksum);
    const std::string sentence = std::string("$") + body + suffix;
    gps.on_receive(GpsMessage(NMEAString(sentence.c_str())));
  };
  const auto savedVolume = settings.vario_volume;
  settings.vario_volume = 3;
  selfTest.restoreVarioAudio();
  nmea("GNGGA,120000,3723.2475,N,12158.3416,W,1,08,0.9,100.0,M,0,M,,");
  check(Access::testGPSfix() == SelfTest::Status::Running, "old GGA must not pass new test");
  const auto original = gps.fixObservation();
  nmea("GNRMC,120001,A,3723.2475,N,12158.3416,W,0,0,211026,,,A");
  nmea("GPGSV,1,1,00");
  check(gps.fixObservation().sequence == original.sequence, "RMC/GSV must not replace GGA");
  check(Access::testGPSfix() == SelfTest::Status::Running, "RMC alone must not pass");
  gps.on_receive(GpsMessage(NMEAString("$GNGGA,120002,3723.2475,N,12158.3416,W,1,09,0.9,100,M,0,M,,*00")));
  check(gps.fixObservation().sequence == original.sequence, "bad checksum must not update fix");
  nmea("GNGGA,120003,3723.2475,N,12158.3416,W,0,00,0.9,100,M,0,M,,");
  check(Access::testGPSfix() == SelfTest::Status::Running, "no-fix GGA must not pass");
  nmea("GNGGA,120004,3723.2475,N,12158.3416,W,1,08,0.9,100,M,0,M,,");
  check(gps.fixInfo.numberOfSats == 0, "GSV count must still be zero for regression");
  check(gps.fixObservation().satellitesUsed == 8, "GGA count must be eight");
  check(Access::testGPSfix() == SelfTest::Status::Pass, "fresh GGA must pass despite zero GSV");
  check(speaker.volume(Speaker::SoundChannel::Vario) == SpeakerVolume::Off, "GPS completion must stay quiet");
  Confirmation confirmation;
  confirmation.show();
  selfTest.confirmCommissioningComplete();
  check(speaker.volume(Speaker::SoundChannel::Vario) == SpeakerVolume::Off, "COMPLETE must stay quiet");
  Overlay overlay;
  overlay.show();
  overlay.dismiss();
  check(speaker.volume(Speaker::SoundChannel::Vario) == SpeakerVolume::Off, "covering final page must stay quiet");
  confirmation.dismiss();
  check(speaker.volume(Speaker::SoundChannel::Vario) == SpeakerVolume::High, "dismissal must restore configured audio");

  for (auto volume : {SpeakerVolume::Low, SpeakerVolume::Off}) {
    settings.vario_volume = static_cast<uint8_t>(volume);
    check(Access::testGPSfix() == SelfTest::Status::Running, "another GPS test starts");
    nmea("GPGGA,120005,3723.2475,N,12158.3416,W,1,07,0.9,100,M,0,M,,");
    check(Access::testGPSfix() == SelfTest::Status::Pass, "GP talker GGA must pass");
    Results results;
    results.show();
    results.dismiss();
    check(speaker.volume(Speaker::SoundChannel::Vario) == volume, "results dismissal must restore settings including Off");
  }
  settings.vario_volume = savedVolume;
  speaker.setVolume(Speaker::SoundChannel::Vario, (SpeakerVolume)savedVolume);
  Serial.println(failures ? "commissioning: FAIL" : "commissioning: PASS");
  return failures == 0;
}
