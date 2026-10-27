#include <cassert>
#include <cstdio>
#include <cstdint>
#include <initializer_list>
#include "training/climb_audio.h"
using namespace training;

static void sample(Receiver& r, uint32_t at, int16_t climb, uint8_t slot = 0) {
  r.group = 1;
  auto& s = r.students[slot];
  s.id = slot + 100;
  s.session = 1;
  s.received = true;
  s.receivedAt = at;
  s.data.valid = Climb | Tone;
  s.data.pressureAge = 0;
  s.data.climbCms = climb;
  s.data.toneHz = 0;  // Student volume may be off; it must not gate the instructor.
}
int main() {
  const VarioAudioProfile profile;
  for (int rate : {-249, -1, 0, 4})
    assert(vario_audio::pattern(rate, profile, 5, -250).frequency == 0);
  auto p = vario_audio::pattern(5, profile, 5, -250);
  assert(p.frequency == 523 && p.play == 10 && p.rest == 6);
  p = vario_audio::pattern(-250, profile, 5, -250);
  assert(p.frequency == 330 && p.play == 8 && p.rest == 20);
  p = vario_audio::pattern(1200, profile, 5, -250);
  assert(p.frequency == 2093 && p.play == 1 && p.rest == 0);
  assert(vario_audio::pattern(32767, profile, 5, -250).frequency == 2093);
  assert(vario_audio::pattern(-32768, profile, 5, -250).frequency == 131);
  assert(vario_audio::pattern(-600, profile, 5, 0).frequency == 0);
  auto invalid = profile;
  invalid.climbMax = invalid.climbContinuous = 5;
  assert(!vario_audio::pattern(100, invalid, 5, -250).frequency);

  // Golden default climb envelope: 400 ms on / 240 ms off; 4 Hz packets do not reset it.
  Receiver r;
  ClimbAudio audio;
  for (uint32_t t = 0; t < 1920; t += 10) {
    if (!(t % 250)) sample(r, t, 5);
    const auto tone = audio.update(r, t, true);
    assert(tone == ((t / 40) % 16 < 10 ? 523 : 0));
  }
  // The production fast-climb envelope has 40 ms pulses at 12.5 Hz, despite 4 Hz RF updates.
  audio.reset();
  for (uint32_t t = 0; t < 1000; t += 10) {
    if (!(t % 250)) sample(r, t, 799);
    const auto tone = audio.update(r, t, true);
    assert(bool(tone) == ((t / 40) % 2 == 0));
  }
  // A missed packet must not stretch/reset the envelope. Stale climb must silence it.
  Receiver held, control;
  ClimbAudio heldAudio, controlAudio;
  for (uint32_t t = 0; t < 250 + LINK_STALE_MS; ++t) {
    if (t == 0 || t == 250) sample(held, t, 799);
    if (!(t % 250)) sample(control, t, 799);
    assert(heldAudio.update(held, t, true) == controlAudio.update(control, t, true));
  }
  for (uint32_t t = 250 + LINK_STALE_MS; t < 350 + LINK_STALE_MS; ++t) assert(!heldAudio.update(held, t, true));
  sample(held, 350 + LINK_STALE_MS, 1200);
  assert(heldAudio.update(held, 350 + LINK_STALE_MS, true) == 2093);
  // Legacy student PWM / UI effects do not influence local audio, including a silent student.
  Receiver a, b;
  ClimbAudio aa, ab;
  for (uint32_t t = 0; t < 1500; t += 10) {
    if (!(t % 250)) { sample(a, t, 5); sample(b, t, 5); }
    b.students[0].data.toneHz = 1000 + t;
    b.students[0].data.valid ^= Tone;
    assert(aa.update(a, t, true) == ab.update(b, t, true));
  }
  // Tone alone cannot authorize audio. Original pressure age, invalid data and mute apply.
  sample(r, 2000, 1200);
  r.students[0].data.valid = Tone;
  assert(!audio.update(r, 2000, true));
  sample(r, 2010, 1200);
  r.students[0].data.pressureAge = LINK_STALE_MS;
  assert(!audio.update(r, 2010, true));
  sample(r, 2020, 1200);
  assert(!audio.update(r, 2020, false));
  assert(audio.update(r, 2021, true) == 2093);
  // Focus/session changes start a new source, not a glide from the previous student's pitch.
  sample(r, 2030, -1070, 1);
  r.focus = 1;
  assert(audio.update(r, 2030, true) == 131);
  r.focus = 2;
  assert(!audio.update(r, 2031, true));
  r.focus = 0;
  r.students[0].session++;
  assert(audio.update(r, 2032, true) == 2093);
  // Local effects temporarily pause the vario; their end must not leak a cached tone.
  assert(!audio.update(r, 2040, true, true));
  assert(!audio.update(r, 2041, true));
  assert(audio.update(r, 2080, true) == 2093);
  // Clock rollover must neither create silence nor a false fresh timeout.
  ClimbAudio rollover;
  Receiver wrapped;
  for (uint32_t step = 0; step < 1000; step += 10) {
    const uint32_t at = 0xffffff00U + step;
    if (!(step % 250)) sample(wrapped, at, 1200);
    assert(rollover.update(wrapped, at, true) == 2093);
  }
  // Pitch smoothing remains the production 4% step, with immediate starts from silence.
  vario_audio::Voice voice;
  voice.set({1000, 1, 0});
  assert(voice.frame() == 1000);
  voice.set({2000, 1, 0});
  assert(voice.smooth() == 1040);
  voice.set({100, 1, 0});
  assert(voice.smooth() == 999);
  voice.reset();
  voice.set({2000, 1, 0});
  assert(voice.frame() == 2000);
  puts("Training audio: production envelopes, sub-4Hz packet independence, loss/stale/mute/focus, legacy SFX exclusion and rollover passed");
}
