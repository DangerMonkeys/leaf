#pragma once

constexpr uint8_t FX_NOTE_SAMPLE_COUNT = 2;  // number of samples to play per FX Note

// each "beep beep" cycle is a "measure", made up of play-length followed by rest-length, then
// repeat

// CLIMB TONE DEFINITIONS
constexpr int32_t CLIMB_CONTINUOUS = 800;  // pauses cease at this climb rate in cm/s
constexpr int32_t CLIMB_MAX = 1200;        // maximum climb endpoint in cm/s
constexpr int32_t CLIMB_NOTE_START = 523;  // tone pitch when climb tones begin
constexpr uint16_t CLIMB_NOTE_MAX = 2093;  // tone pitch at CLIMB_MAX

// SINK TONE DEFINITIONS
constexpr int32_t SINK_CONTINUOUS = -800;  // pauses cease at this sink rate in cm/s
constexpr int32_t SINK_MAX = -1070;        // maximum sink endpoint in cm/s
constexpr int32_t SINK_NOTE_START = 330;   // tone pitch at settings.vario_sinkAlarm
constexpr uint16_t SINK_NOTE_MIN = 131;    // tone pitch at SINK_MAX

// FOR APPROACH #1 (fixed sample-size length speaker timer)
constexpr uint16_t CLIMB_PLAY_SAMPLES_MAX = 10;
constexpr uint16_t CLIMB_REST_SAMPLES_MAX = 6;

constexpr int32_t SINK_PLAY_SAMPLES_MIN = 8;  // play 8, rest 20, flytec 4030
constexpr int32_t SINK_REST_SAMPLES_MIN = 20;
