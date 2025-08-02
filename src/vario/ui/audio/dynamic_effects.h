#pragma once

#define FX_NOTE_SAMPLE_COUNT 2  // number of samples to play per FX Note

// each "beep beep" cycle is a "measure", made up of play-length followed by rest-length, then
// repeat

// FOR APPROACH #2 (time-adjusted speaker timer)
// CLIMB TONE DEFINITIONS
// #define CLIMB_AUDIO_THRESHOLD  10 	// don't play unless climb rate is over this value (cm/s)
#define CLIMB_MAX 800       // above this cm/s climb rate, note doesn't get higher
#define CLIMB_NOTE_MIN 523  // min tone pitch in Hz for >0 climb
#define CLIMB_NOTE_MAX \
  1568  // max tone pitch in Hz for CLIMB_MAX (when vario peaks and starts holding a solid tone)
#define CLIMB_NOTE_MAXMAX \
  2093  // max tone pitch in Hz when vario is truly pegged, even in solid-tone mode
#define CLIMB_PLAY_MAX 60   // 1200 	// ms play per measure (at min climb)
#define CLIMB_PLAY_MIN 2    // 200   // ms play per measure (at max climb)
#define CLIMB_REST_MAX 100  // 1000		// ms silence per measure (at min climb)
#define CLIMB_REST_MIN 2    // 100		// ms silence per measure (at max climb)

// SINK TONE DEFINITIONS
// #define SINK_ALARM           -200   // cm/s sink rate that triggers sink alarm audio
#define SINK_MAX -800      // at this sink rate, tone doesn't get lower
#define SINK_NOTE_MIN 392  // highest tone pitch for sink >  settings.vario_sinkAlarm
#define SINK_NOTE_MAX \
  196  // lowest tone pitch for sink @ SINK_MAX (when vario bottoms out and starts holding a solid
       // tone)
#define SINK_NOTE_MAXMAX \
  131  // bottom tone pitch for sink (when vario is truly pegged, even in solid tone mode)

#define SINK_PLAY_MIN 100  // 1200   // ms play per measure (at min sink)
#define SINK_PLAY_MAX 2    // 2000 	// ms play per measure (at max sink)
#define SINK_REST_MIN 100  // 1000		// silence samples (at min sink)
#define SINK_REST_MAX 2    // 1000		// silence samples (at max sink)

// FOR APPROACH #1 (fixed sample-size length speaker timer)
#define CLIMB_PLAY_SAMPLES_MAX 10
#define CLIMB_PLAY_SAMPLES_MIN 1
#define CLIMB_REST_SAMPLES_MAX 6
#define CLIMB_REST_SAMPLES_MIN 1

#define SINK_PLAY_SAMPLES_MIN 8  // play 8, rest 20, flytec 4030
#define SINK_PLAY_SAMPLES_MAX 8
#define SINK_REST_SAMPLES_MIN 20
#define SINK_REST_SAMPLES_MAX 20

// LiftyAir DEFINITIONS    (for air rising slower than your sinkrate, so net climbrate is negative,
// but not as bad as it would be in still air)
#define LIFTYAIR_TONE_MIN 180  // min pitch tone for lift air @ -(setting)m/s
#define LIFTYAIR_TONE_MAX 150  // max pitch tone for lifty air @ .1m/s climb
#define LIFTYAIR_PLAY 1        //
#define LIFTYAIR_GAP 1
#define LIFTYAIR_REST_MAX 20
#define LIFTYAIR_REST_MIN 10
