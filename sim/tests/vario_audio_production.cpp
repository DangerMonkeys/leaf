#include <Arduino.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include "sim/board.h"
#include "sim/clock.h"
#include "ui/audio/speaker.h"
#include "ui/settings/settings.h"
int main() {
  sim::clock().setSpeed(0);
  settings.vario_quietMode = false;
  settings.vario_sinkAlarm = -2.5f;
  settings.vario_sinkAlarm_units = false;
  settings.vario_climbStart = 5;
  settings.varioAudio = {};
  speaker.setVolume(Speaker::SoundChannel::Vario, SpeakerVolume::Medium);
  speaker.setVolume(Speaker::SoundChannel::FX, SpeakerVolume::Medium);
  speaker.update();
  speaker.updateVarioNote(5);
  for (unsigned t = 10; t <= 1280; t += 10) {
    sim::clock().advanceUs(10000);
    speaker.update();
    if (t >= 40) {
      const bool on = ((t / 40 - 1) % 16) < 10;
      assert(sim::board().currentTone() == (on ? 523U : 0U));
    }
  }
  speaker.updateVarioNote(1200);
  for (unsigned i = 0; i < 100; ++i) { sim::clock().advanceUs(10000); speaker.update(); }
  assert(std::abs(int(sim::board().currentTone()) - 2093) < 5);
  speaker.playSound(fx::click);
  bool heardClick = false, resumedVario = false;
  for (unsigned i = 0; i < 60; ++i) {
    sim::clock().advanceUs(10000);
    speaker.update();
    heardClick |= sim::board().currentTone() == 262;
    if (heardClick) resumedVario |= sim::board().currentTone() > 2000;
  }
  assert(heardClick && resumedVario);
  speaker.updateVarioNote(0);
  for (unsigned i = 0; i < 5; ++i) { sim::clock().advanceUs(10000); speaker.update(); }
  assert(!sim::board().currentTone());
  speaker.updateVarioNote(-1070);
  for (unsigned i = 0; i < 5; ++i) { sim::clock().advanceUs(10000); speaker.update(); }
  assert(sim::board().currentTone() == 131);
  speaker.mute();
  assert(!sim::board().currentTone());
  puts("Production speaker: golden cadence, continuous climb/sink, smoothing, SFX priority/resume and mute passed");
}
