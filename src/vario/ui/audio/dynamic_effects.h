#pragma once

constexpr uint8_t FX_NOTE_SAMPLE_COUNT = 2;  // number of samples to play per FX Note

// each "beep beep" cycle is a "measure", made up of play-length followed by rest-length, then
// repeat

// CLIMB TONE DEFINITIONS
constexpr int32_t CLIMB_MAX = 800;       // above this cm/s climb rate, note doesn't get higher
constexpr int32_t CLIMB_NOTE_MIN = 523;  // min tone pitch in Hz for >0 climb
// max tone pitch in Hz for CLIMB_MAX (when vario peaks and starts holding a solid tone)
constexpr int32_t CLIMB_NOTE_MAX = 1568;
// max tone pitch in Hz when vario is truly pegged, even in solid-tone mode
constexpr uint16_t CLIMB_NOTE_MAXMAX = 2093;

// SINK TONE DEFINITIONS
constexpr int32_t SINK_MAX = -800;      // at this sink rate, tone doesn't get lower
constexpr int32_t SINK_NOTE_MIN = 392;  // highest tone pitch for sink >  settings.vario_sinkAlarm
// lowest tone pitch for sink @ SINK_MAX (when vario bottoms out and starts holding a solid tone)
constexpr int32_t SINK_NOTE_MAX = 196;
// bottom tone pitch for sink (when vario is truly pegged, even in solid tone mode)
constexpr uint16_t SINK_NOTE_MAXMAX = 131;

// FOR APPROACH #1 (fixed sample-size length speaker timer)
constexpr uint16_t CLIMB_PLAY_SAMPLES_MAX = 10;
constexpr uint16_t CLIMB_PLAY_SAMPLES_MIN = 1;
constexpr uint16_t CLIMB_REST_SAMPLES_MAX = 6;
constexpr uint16_t CLIMB_REST_SAMPLES_MIN = 1;

constexpr int32_t SINK_PLAY_SAMPLES_MIN = 8;  // play 8, rest 20, flytec 4030
constexpr int32_t SINK_PLAY_SAMPLES_MAX = 8;
constexpr int32_t SINK_REST_SAMPLES_MIN = 20;
constexpr int32_t SINK_REST_SAMPLES_MAX = 20;
