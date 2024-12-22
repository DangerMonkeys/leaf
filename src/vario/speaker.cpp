#include <Arduino.h>

#include "speaker.h"
#include "Leaf_SPI.h"
#include "settings.h"
// use this to switch between method 1 (fixed sample length approach) and method 2 (adjustable timer length)
#define FIXED_SAMPLE_APPROACH true

uint8_t speakerVolume = 1;

// Sound Effects 
uint16_t fx_silence[] = {NOTE_END};

uint16_t fx_increase[] = {NOTE_C4, NOTE_G4, NOTE_END};
uint16_t fx_decrease[] = {NOTE_C4, NOTE_F3, NOTE_END};   //110 140, END_OF_TONE};
uint16_t fx_neutral[] = {NOTE_C4, NOTE_C4, NOTE_END};    //110, 110, END_OF_TONE};
uint16_t fx_neutralLong[] = {NOTE_C4, NOTE_C4, NOTE_C4, NOTE_C4, NOTE_C4, NOTE_C4, NOTE_C4, NOTE_C4, NOTE_END}; //  {110, 110, 110, 110, 110, 110, 110, 110,10, 110, 1110, 110, 110, 110, 110, 110, 110, 110, 110, 110, END_OF_TONE};
uint16_t fx_double[] = {NOTE_C4, NOTE_NONE, NOTE_C4, NOTE_END};  //110, 0, 110, END_OF_TONE};

uint16_t fx_enter[] = {NOTE_A4, NOTE_C4, NOTE_E4, NOTE_END};     //150, 120, 90, END_OF_TONE};
uint16_t fx_exit[] = {NOTE_C5, NOTE_A5, NOTE_F4, NOTE_C4, NOTE_END};       //65, 90, 120, 150, END_OF_TONE};
uint16_t fx_confirm[] = {NOTE_C3, NOTE_G3, NOTE_B3, NOTE_C4, NOTE_END};
uint16_t fx_cancel[] = {NOTE_C4, NOTE_G4, NOTE_C3, NOTE_END};
uint16_t fx_on[] = {250, 200, 150, 100, 50, NOTE_END};
uint16_t fx_off[] = {50, 100, 150, 200, 250, NOTE_END};

uint16_t fx_buttonpress[] = {NOTE_F3, NOTE_A3, NOTE_C4, NOTE_END};
uint16_t fx_buttonhold[] = {150, 200, 250, NOTE_END};
uint16_t fx_goingup[] = {55, 54, 53, 52, 51, 50, 49, 47, 44, 39, NOTE_END};
uint16_t fx_goingdown[] = {31, 31, 32, 33, 34, 35, 36, 38, 41, 46, NOTE_END};
uint16_t fx_octavesup[] = {45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, NOTE_END};
uint16_t fx_octavesdown[] = {31, 31, 40, 40, 45, 45, 65, 65, 90, 90, NOTE_END};

uint16_t single_note[] = {0, NOTE_END};   // this is to allow playing single notes by changing random_note[0], while still having a NOTE_END terminator following.



hw_timer_t *speaker_timer = NULL;

// volatile pointer to the sound sample to play
volatile uint16_t* snd_index;

// notes we should play, and if we're currently playing
volatile uint16_t sound_varioNote = 0;			    // note to play for vario beeps
volatile uint16_t sound_varioNoteLast = 0;		// last note played for vario beeps
volatile uint16_t sound_fxNoteLast = 0;		// last note played for sound effects
volatile bool sound_varioResting = 0;		              // are we resting (silence) between beeps?
volatile bool sound_currentlyPlaying = 0;         	// are we playing a sound?
volatile bool sound_fx = 0;                      // track if we have any sound effects to play (button presses, etc)

//trackers for fixed-sample length speaker timer approach #1
  volatile uint16_t sound_vario_play_samples = CLIMB_PLAY_SAMPLES_MAX;		// amount of samples we should play for
  volatile uint16_t sound_vario_rest_samples = CLIMB_REST_SAMPLES_MAX;		// amount of samples we should rest for
  volatile uint8_t  sound_vario_play_sample_count = 0; // track how many samples (beats) we've played per note when playing sound effects (using method #1 -- fixed sample length)
  volatile uint8_t  sound_vario_rest_sample_count = 0; // track how many samples (beats) we've played per note when playing sound effects (using method #1 -- fixed sample length)
  volatile uint8_t  sound_fx_sample_count = 0; // track how many samples (beats) we've played per note when playing sound effects (using method #1 -- fixed sample length)

// trackers for adjustable length speaker timer approach #2
  volatile uint16_t sound_varioNoteLastUpdate = 0;		// last note updated in climb analysis
  volatile uint16_t sound_varioPlayLength = CLIMB_PLAY_MAX;		// 
  volatile uint16_t sound_varioRestLength = CLIMB_REST_MAX;		// 
//



void speaker_init(void)
{
  //configure speaker pinout and PWM channel
  pinMode(SPEAKER_PIN, OUTPUT);
  ledcAttach(SPEAKER_PIN, 1000, 10);

	//set Volume to proper default setting
  pinMode(SPEAKER_VOLA, OUTPUT);
  pinMode(SPEAKER_VOLB, OUTPUT);
	//speaker_setVolume(VOLUME);      // use saved user prefs

  speaker_setDefaultVolume();

  if (FIXED_SAMPLE_APPROACH) {
  
    //METHOD 1 (fixed sample length speaker timer)
  
    //setup speaker timer interrupt to track each "beat" of sound
    speaker_timer = timerBegin(SPEAKER_TIMER_FREQ);            
    timerAttachInterrupt(speaker_timer, &onSpeakerTimerSample);          // timer, ISR call          NOTE: timerDetachInterrupt() does the opposite
    timerAlarm(speaker_timer, SPEAKER_SAMPLE_LENGTH, true, 0);      // auto reload timer ever time we've counted a sample length
  
  } else {

  //METHOD 2 (time-adjusted speaker timer)
    
    //setup speaker timer interrupt to track each "beat" of sound
    speaker_timer = timerBegin(SPEAKER_TIMER_FREQ);            
    timerAttachInterrupt(speaker_timer, &onSpeakerTimerAdjustable);          // timer, ISR call          NOTE: timerDetachInterrupt() does the opposite
  
  }

	snd_index = fx_silence;  
}

void speaker_sleep() {
  speaker_setVolume(0);
  timerStop(speaker_timer);
}

void speaker_wake() {
  speaker_setDefaultVolume();
  timerStart(speaker_timer);
}

void speaker_setDefaultVolume() {
  speaker_setVolume(1);  
}

void speaker_incVolume() {
  if (++speakerVolume > 3) speakerVolume = 3;
  speaker_setVolume(speakerVolume);  
}

void speaker_decVolume() {
  if (speakerVolume == 0) {} //do nothing
  else speakerVolume--;
  speaker_setVolume(speakerVolume);
}

void speaker_setVolume(unsigned char volume) {
  speakerVolume = volume;
	switch (volume) {
		case 0:     // No Volume -- disable piezo speaker driver
      digitalWrite(SPEAKER_VOLA, 0);
      digitalWrite(SPEAKER_VOLB, 0);
      break;
		case 1:     // Low Volume -- enable piezo speaker driver 1x (3.3V)
			digitalWrite(SPEAKER_VOLA, 1);
      digitalWrite(SPEAKER_VOLB, 0);      
			break;
		case 2:     // Med Volume -- enable piezo speaker driver 2x (6.6V)
			digitalWrite(SPEAKER_VOLA, 0);
      digitalWrite(SPEAKER_VOLB, 1);      
			break;
		case 3:     // High Volume -- enable piezo spaker driver 3x (9.9V)
			digitalWrite(SPEAKER_VOLA, 1);
      digitalWrite(SPEAKER_VOLB, 1);
			break;
	}
}


void speaker_playSound(uint16_t * sound)
{	
  if (VOLUME_SYSTEM != 0) { // only play sound if the volume is non-zero

    // Dont' need this anymore since volume is set inside ISR as needed
    //speaker_setVolume(VOLUME_SYSTEM);   // set volume to system level for playing system sounds.  (ISR will set speaker volume back to VOLUME_VARIO when the sound finishes);

    snd_index = sound;
    Serial.print("setting snd_index to: ");
    Serial.print(*snd_index);
    Serial.print(" @ ");
    Serial.println(millis());
    sound_fx = 1;
    if (!FIXED_SAMPLE_APPROACH) onSpeakerTimerAdjustable();
  }
}

void speaker_playNote(uint16_t note) {
  single_note[0] = note;
  snd_index = single_note;
  sound_fx = 1;
  if (!FIXED_SAMPLE_APPROACH) onSpeakerTimerAdjustable();
}

unsigned char climbToneSpread;
unsigned int climbRateSpread;
unsigned int climbPlaySpread;
unsigned int climbSilenceSpread;

unsigned char sinkToneSpread;
unsigned int sinkRateSpread;
unsigned int sinkPlaySpread;
unsigned int sinkSilenceSpread;

unsigned char liftyToneSpread;
unsigned int liftyRateSpread;
unsigned int liftySilenceSpread;
unsigned char liftyAirGap = 1;		// track if we're playing the gap between double-beeps or the longer silence
unsigned char liftyAirNow = 0;		// track if we're playing lifty air tone

void speaker_updateClimbToneParameters(void)
{
  /*
	climbToneSpread = CLIMB_TONE_MIN - CLIMB_TONE_MAX;
	climbRateSpread = CLIMB_MAX - CLIMB_START;
	climbPlaySpread = CLIMB_PLAY_MAX - CLIMB_PLAY_MIN;
	climbSilenceSpread = CLIMB_SILENCE_MAX - CLIMB_SILENCE_MIN;

	sinkToneSpread = SINK_TONE_MIN - SINK_TONE_MAX;
	sinkRateSpread = SINK_MAX - ((unsigned int)SINK_ALARM * 100);
	sinkPlaySpread = SINK_PLAY_MAX - SINK_PLAY_MIN;
	sinkSilenceSpread = SINK_SILENCE_MAX - SINK_SILENCE_MIN;

	liftyToneSpread = LIFTYAIR_TONE_MIN - LIFTYAIR_TONE_MAX;
	liftyRateSpread = CLIMB_START + LIFTY_AIR;
	liftySilenceSpread = LIFTYAIR_SILENCE_MAX - LIFTYAIR_SILENCE_MIN;
  */
}

//entry point to direct the approach
void speaker_updateVarioNote(int32_t verticalRate) {
  if (FIXED_SAMPLE_APPROACH) speaker_updateVarioNoteSample(verticalRate);
  else speaker_updateVarioNoteAdjustable(verticalRate);
}

void speaker_updateVarioNoteSample(int32_t verticalRate) {
  sound_varioNoteLastUpdate = sound_varioNote;

  uint16_t sound_varioNoteTEMP;
  uint16_t sound_vario_play_samplesTEMP;
  uint16_t sound_vario_rest_samplesTEMP;

  if(verticalRate > CLIMB_START) {

    // first clamp to thresholds if climbRate is over the max
    if (verticalRate >= CLIMB_MAX) {
      sound_varioNoteTEMP = verticalRate * (CLIMB_NOTE_MAX - CLIMB_NOTE_MIN) / CLIMB_MAX + CLIMB_NOTE_MIN;
      if (sound_varioNoteTEMP > CLIMB_NOTE_MAXMAX) sound_varioNoteTEMP = CLIMB_NOTE_MAXMAX;
      sound_vario_play_samplesTEMP = CLIMB_PLAY_SAMPLES_MIN;
      sound_vario_rest_samplesTEMP = 0;    // just hold a continuous tone, no rest in between
    } else {
      sound_varioNoteTEMP = verticalRate * (CLIMB_NOTE_MAX - CLIMB_NOTE_MIN) / CLIMB_MAX + CLIMB_NOTE_MIN;
      sound_vario_play_samplesTEMP = CLIMB_PLAY_SAMPLES_MAX - (verticalRate * (CLIMB_PLAY_SAMPLES_MAX - CLIMB_PLAY_SAMPLES_MIN) / CLIMB_MAX);
      sound_vario_rest_samplesTEMP = CLIMB_REST_SAMPLES_MAX - (verticalRate * (CLIMB_REST_SAMPLES_MAX - CLIMB_REST_SAMPLES_MIN) / CLIMB_MAX);
    }

  // if we trigger sink threshold
  } else if (verticalRate < (SINK_ALARM * 100) ) {    // sink alarm is in meters, so *100 to get cm to compare to verticalRate
    // first clamp to thresholds if sinkRate is over the max
    if (verticalRate <= SINK_MAX) {
      sound_varioNoteTEMP = SINK_NOTE_MIN - verticalRate * (SINK_NOTE_MIN - SINK_NOTE_MAX) / SINK_MAX;
      if (sound_varioNoteTEMP < SINK_NOTE_MAXMAX || sound_varioNoteTEMP > SINK_NOTE_MAX) sound_varioNoteTEMP = SINK_NOTE_MAXMAX;  // the second condition (|| > SINK_NOTE_MAX) is to prevent uint16 wrap-around to a much higher number
      sound_vario_play_samplesTEMP = SINK_PLAY_SAMPLES_MAX;
      sound_vario_rest_samplesTEMP = 0;    // just hold a continuous tone, no pulses
    } else {
      sound_varioNoteTEMP = SINK_NOTE_MIN - verticalRate * (SINK_NOTE_MIN - SINK_NOTE_MAX) / SINK_MAX;
      sound_vario_play_samplesTEMP = SINK_PLAY_SAMPLES_MIN + (verticalRate * (SINK_PLAY_SAMPLES_MAX - SINK_PLAY_SAMPLES_MIN) / SINK_MAX);
      sound_vario_rest_samplesTEMP = SINK_REST_SAMPLES_MIN + (verticalRate * (SINK_REST_SAMPLES_MAX - SINK_REST_SAMPLES_MIN) / SINK_MAX);
    }

  } else {
    sound_varioNote = 0;
  }

  // stop the timer to copy values, to ensure the ISR doesn't trigger in the middle of these steps
  timerStop(speaker_timer);
  //cli();
  sound_varioNote = sound_varioNoteTEMP;
  sound_vario_play_samples = sound_vario_play_samplesTEMP;
  sound_vario_rest_samples = sound_vario_rest_samplesTEMP;
  //sei();
  timerStart(speaker_timer);
  


  //speaker_debugPrint();

}

int microsLast = 0;
int millisLast = 0;
  
void speaker_debugPrint() {
  int timeNowMillis = millis();
  int timeNowMicros = micros();

  Serial.print(timeNowMillis);
  Serial.print(" : ");
  Serial.print(timeNowMillis-millisLast);
  microsLast = timeNowMicros;
  millisLast = timeNowMillis;

  Serial.print("  SPKR UPDT -- Note: ");
  Serial.print(sound_varioNote);
  Serial.print(" Play: ");

  if (FIXED_SAMPLE_APPROACH) Serial.print(sound_vario_play_samples);
  else Serial.print(sound_varioPlayLength);

  Serial.print(" Rest: ");
  
  if (FIXED_SAMPLE_APPROACH) Serial.print(sound_vario_rest_samples);
  else Serial.print(sound_varioRestLength);

  Serial.println(" !");
  //Serial.println("END OF UPDATE VARIO NOTE --> this is a whole bunch of text to see if additional serial printing is causing the interrupt routine to fail on the watchdog timer.  woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop woop ");


}


void speaker_updateVarioNoteAdjustable(int32_t verticalRate)
{
  sound_varioNoteLastUpdate = sound_varioNote;

  if(verticalRate > CLIMB_START) {
    // first clamp to thresholds if climbRate is over the max
    if (verticalRate >= CLIMB_MAX) {
      sound_varioNote = verticalRate * (CLIMB_NOTE_MAX - CLIMB_NOTE_MIN) / CLIMB_MAX + CLIMB_NOTE_MIN;
      if (sound_varioNote > CLIMB_NOTE_MAXMAX) sound_varioNote = CLIMB_NOTE_MAXMAX;
      sound_varioPlayLength = CLIMB_PLAY_MIN;
      sound_varioRestLength = 0;    // just hold a continuous tone, no pulses
    } else {
      sound_varioNote = verticalRate * (CLIMB_NOTE_MAX - CLIMB_NOTE_MIN) / CLIMB_MAX + CLIMB_NOTE_MIN;
      sound_varioPlayLength = CLIMB_PLAY_MAX - (verticalRate * (CLIMB_PLAY_MAX - CLIMB_PLAY_MIN) / CLIMB_MAX);
      sound_varioRestLength = CLIMB_REST_MAX - (verticalRate * (CLIMB_REST_MAX - CLIMB_REST_MIN) / CLIMB_MAX);
    }
  } else if (verticalRate < SINK_ALARM) {    
    // first clamp to thresholds if sinkRate is over the max
    if (verticalRate <= SINK_MAX) {
      sound_varioNote = SINK_NOTE_MIN - verticalRate * (SINK_NOTE_MIN - SINK_NOTE_MAX) / SINK_MAX;
      if (sound_varioNote < SINK_NOTE_MAXMAX || sound_varioNote > SINK_NOTE_MAX) sound_varioNote = SINK_NOTE_MAXMAX;  // the second condition (|| > SINK_NOTE_MAX) is to prevent uint16 wrap-around to a much higher number
      sound_varioPlayLength = SINK_PLAY_MAX;
      sound_varioRestLength = 0;    // just hold a continuous tone, no pulses
    } else {
      sound_varioNote = SINK_NOTE_MIN - verticalRate * (SINK_NOTE_MIN - SINK_NOTE_MAX) / SINK_MAX;
      sound_varioPlayLength = SINK_PLAY_MIN + (verticalRate * (SINK_PLAY_MAX - SINK_PLAY_MIN) / SINK_MAX);
      sound_varioRestLength = SINK_REST_MIN + (verticalRate * (SINK_REST_MAX - SINK_REST_MIN) / SINK_MAX);
    }
  } else {
    sound_varioNote = 0;
  }



  //speaker_debugPrint();

  // start the vario beeps if there's something to play, and we haven't been playing, and there are no FX playing
  if (sound_varioNote) {
    if (sound_varioNoteLastUpdate == 0) {
      if (!sound_fx) onSpeakerTimerAdjustable(); // if there's somethign to play
    }
  }
  
  




  /*
	unsigned char percentToMax = 0;

	sndCLIMB_tone = 0;	// assume no sound to play
	liftyAirNow = 0;	// assume no lifty air

	if (VOLUME != 0) {
		// if climbing
		if (VARIO_RATEfiltered >= CLIMB_START) {
			// calculate % between CLIMB_THRESHOLD (min climb) and CLIMB_MAX
			percentToMax = (VARIO_RATEfiltered - CLIMB_START)*100 / climbRateSpread;  //TODO: probably take out subtraction of the threshold, it's only 10cm
			if (percentToMax > 100) percentToMax = 100;

			// calculate sndCLIMB_tone, sndCLIMB_playCountMax, and sndCLIMB_silenceCountMax
			sndCLIMB_tone = CLIMB_TONE_MIN - (percentToMax * climbToneSpread / 100);
			sndCLIMB_playCountMax = CLIMB_PLAY_MAX - (percentToMax * climbPlaySpread / 100);
			sndCLIMB_silenceCountMax = CLIMB_SILENCE_MAX - (percentToMax * climbSilenceSpread / 100);

		// if Sink Alarm is ON and sinking > ALARM
		} else if (SINK_ALARM != 0 && VARIO_RATEfiltered <= -100*((long)SINK_ALARM)) {
			// calculate % between SINK_ALARM and SINK_MAX
			percentToMax = (-VARIO_RATEfiltered - ((unsigned int)SINK_ALARM*100)) * 100 / sinkRateSpread;
			if (percentToMax > 100) percentToMax = 100;

			// calculate sndCLIMB_tone, sndCLIMB_playCountMax, and sndCLIMB_silenceCountMax
			sndCLIMB_tone = SINK_TONE_MAX + (percentToMax * sinkToneSpread / 100);
			sndCLIMB_playCountMax = SINK_PLAY_MIN + (percentToMax * sinkPlaySpread / 100);
			sndCLIMB_silenceCountMax = SINK_SILENCE_MAX - (percentToMax * sinkSilenceSpread / 100);

		// if LiftyAir is ON and we're in that sink/climb range
		} else if (LIFTY_AIR != 0 && VARIO_RATEfiltered >= -LIFTY_AIR) {
			percentToMax = (-VARIO_RATEfiltered + CLIMB_START)*100  / liftyRateSpread;
			if (percentToMax > 100) percentToMax = 100;

			sndCLIMB_tone = LIFTYAIR_TONE_MAX + (percentToMax * liftyToneSpread / 100);
			sndCLIMB_playCountMax = LIFTYAIR_PLAY;
			sndCLIMB_silenceCountMax = LIFTYAIR_SILENCE_MIN + (percentToMax * liftySilenceSpread / 100);
			liftyAirNow = 1;
		}
	} 
  */
}


// timerWrite(timer, 0)


/* 

//run once when booting up:
timerSetup() {
  ..attach timer to interrupt (do_this_when_time_expires);
  ..set timer to auto-renew itself when it expires
  ..set timer time to default 1 second.
}

//run every ~500ms to adjust values based on pressure sensor output
// note: all three of these variables are declared volatile (not even sure if that's needed since they are only ever set here; but the interrupt could be reading them while this function is changing them)
void updateNote() {
  ..do calculations on climbRate to set the following:
  noteToPlay = stuff;
  noise_time = stuff;
  silence_time = stuff;
}

ISR: do_this_when_time_expires() {
  if (noteToPlay > 0)
      if (beeping) {
      ..set timer to appropriate length of rest (silence_time);
      ..set pwm_speakerOutput(0);
      beeping = false;
      } else {
      ..set timer to appropriate length of beep (noise_time);
      ..set pwm_speakerOutput(noteToPlay);
      beeping = true;
      }
  else {
    ..set pwm_speakerOutput(0);
    // timer will keep cycling at whatever the last set time-period was
  }
}




*/

// Speaker Driver for method 1 (fixed sample length)
void IRAM_ATTR onSpeakerTimerSample() {
  //spi_checkFlag(2);
  //Serial.print("ENTER ISR: ");
  
  //prioritize sound effects from UI & Button etc before we get to vario beeps
  if (sound_fx) {					
    speaker_setVolume(VOLUME_SYSTEM);    // play system sound effects at system volume setting
    
    //Serial.print("FX: "); Serial.print(*snd_index); Serial.print(" @ "); Serial.println(millis());	
		if (*snd_index != NOTE_END) {
			if (*snd_index != sound_fxNoteLast) ledcWriteTone(SPEAKER_PIN, *snd_index);   // only change pwm if it's a different note, otherwise we get a little audio blip between the same notes           
      sound_fxNoteLast = *snd_index;      //save last note and move on to next

      //if we've played this note for enough samples
      if (++sound_fx_sample_count >= FX_NOTE_SAMPLE_COUNT) {        
        snd_index++;
        sound_fx_sample_count = 0;          //and reset sample count    
      }   

		} else {									// Else, we're at END_OF_TONE
      ledcWriteTone(SPEAKER_PIN, 0);    			
			sound_fx = 0;
      sound_fxNoteLast = 0;
		} 

  } else if (sound_varioNote > 0 && VOLUME_VARIO) {   // if there's a note to play, and the vario volume isn't zero
    speaker_setVolume(VOLUME_VARIO);    // play vario sounds at vario volume setting

    //Serial.print("Vario: "); Serial.print(sound_varioNote); Serial.print(" @ "); Serial.print(millis());
    // Handle the beeps and rests of a vario sound "measure"
    if (sound_varioResting) {
      //Serial.println("  VAR_REST");
      ledcWriteTone(SPEAKER_PIN, 0);                              // "play" silence since we're resting between beeps

      // stop playing rest if we've done it long enough
      if (++sound_vario_rest_sample_count >= sound_vario_rest_samples) {
        sound_vario_rest_sample_count = 0;
        sound_varioNoteLast = 0;
        sound_varioResting = false;                                 // next time through we want to play sound          
      }   
      
    } else {
      //Serial.println("  VAR_BEEP");
      if (sound_varioNote != sound_varioNoteLast) ledcWriteTone(SPEAKER_PIN, sound_varioNote);  // play the note, but only if different, otherwise we get a little audio blip between the same successive notes (happens when vario is pegged and there's no rest period between)
      sound_varioNoteLast = sound_varioNote;

      if (++sound_vario_play_sample_count >= sound_vario_play_samples) {
        sound_vario_play_sample_count = 0;        
        if (sound_vario_rest_samples) sound_varioResting = true;                              // next time through we want to play sound          
      }   
    }
  } else {
    //Serial.println("ISR NO SOUND");
    ledcWriteTone(SPEAKER_PIN, 0);  // play silence (if timer is configured for auto-reload, we have to do this here because sound_varioNote might have been set to 0 while we were beeping, and then we'll keep beeping)  
  }  
}

// Speaker Driver for method 2 (adjustable length)
void IRAM_ATTR onSpeakerTimerAdustable() {
  //Serial.print("ENTER ISR: ");
  timerWrite(speaker_timer, 0);  // reset timer as we enter interrupt

  //prioritize sound effects from UI & Button etc before we get to vario beeps
  if (sound_fx) {								
    Serial.print("FX: "); Serial.print(*snd_index); Serial.print(" @ "); Serial.println(millis());	
		if (*snd_index != NOTE_END) {
			if (*snd_index != sound_fxNoteLast) ledcWriteTone(SPEAKER_PIN, *snd_index);   // only change pwm if it's a different note, otherwise we get a little audio blip between the same notes           
      sound_fxNoteLast = *snd_index;
      snd_index++;
      timerWrite(speaker_timer, 0);                           // start at 0
      timerAlarm(speaker_timer, FX_NOTE_LENGTH, false, 0);    // set timer for play period (don't reload; we'll trigger back into this ISR when time is up)
            
		} else {									// Else, we're at END_OF_TONE
      Serial.println("FX NOTE END");
      ledcWriteTone(SPEAKER_PIN, 0);    			
			sound_fx = 0;
      sound_fxNoteLast = 0;

      //next, play vario sounds if any
      if (sound_varioNote > 0) {
        timerWrite(speaker_timer, 0);                           // start at 0
        timerAlarm(speaker_timer, 500, false, 0);    // set timer for play period (don't reload; we'll trigger back into this ISR when time is up)
      }
		}    
  } else if (sound_varioNote > 0) {
    //Serial.print("Vario: "); Serial.print(sound_varioNote); Serial.print(" @ "); Serial.print(millis());
    // Handle the beeps and rests of a vario sound "measure"
    if (sound_varioResting) {
      //Serial.println("  VAR_REST");
      ledcWriteTone(SPEAKER_PIN, 0);                              // "play" silence since we're resting between beeps
      sound_varioNoteLast = 0;
      sound_varioResting = false;                                 // next time through we want to play sound
      
      timerWrite(speaker_timer, 0);                               // start at 0
      timerAlarm(speaker_timer, sound_varioRestLength, false, 0); // set timer for play period (don't reload; we'll trigger back into this ISR when time is up)	
    } else {
      //Serial.println("  VAR_BEEP");
      if (sound_varioNote != sound_varioNoteLast) ledcWriteTone(SPEAKER_PIN, sound_varioNote);  // play the note, but only if different, otherwise we get a little audio blip between the same successive notes (happens when vario is pegged and there's no rest period between)
      sound_varioNoteLast = sound_varioNote;
      if (sound_varioRestLength) sound_varioResting = true;       // next time through we want to rest (play silence), unless climb is maxed out (ie, rest length == 0)
      timerWrite(speaker_timer, 0);                               // start at 0
      timerAlarm(speaker_timer, sound_varioPlayLength, false, 0); // set timer for play period (don't reload; we'll trigger back into this ISR when time is up)			
    }
  } else {
    //Serial.println("ISR NO SOUND");
    ledcWriteTone(SPEAKER_PIN, 0);  // play silence (if timer is configured for auto-reload, we have to do this here because sound_varioNote might have been set to 0 while we were beeping, and then we'll keep beeping)  
  }  
/*

  
	




	} else if (sound_climbNote > 0) {					// Play climb sound if desired, if there were no FX to play
		if (!sndCLIMB_silence) {					// 	if not playing silence..
			
      timerAlarmWrite(speaker_timer, 1000, true);

			if (!sndCurrentlyPlaying) {				// if we're not playing a beep currently, then go ahead and allow a frequency/pitch change
				ledcWriteTone(PWM_CHANNEL, sound_climbNote);				//  this is the note we play
				sndCurrentlyPlaying = 1;			//	and note that we're playing something
			}
			
			sndCLIMB_playCount++;					//	  log this play of the note

			if (sndCLIMB_playCount >= sndCLIMB_playCountMax) {	// if we've played the note enough times
				sndCLIMB_playCount = 0;							//   reset play count
				sndCurrentlyPlaying = 0;
				if (sndCLIMB_silenceCountMax >= 1)				//   and if we need silence
					sndCLIMB_silence = 1;						//    get ready to play silence
			}

		} else {
			speaker_disableTimer();					// play silence			
			sndCLIMB_silenceCount++;				// log this play of silence
			if (sndCLIMB_silenceCount >= sndCLIMB_silenceCountMax) { 	// if we've played silence long enough
				sndCLIMB_silence = 0;									//  time for noise
				sndCLIMB_silenceCount = 0;								//  reset silence count
				if (liftyAirNow) liftyAirGap = 1;
			} else if (liftyAirNow && liftyAirGap && sndCLIMB_silenceCount >= LIFTYAIR_GAP) {
				sndCLIMB_silence = 0;									//  time for noise
				sndCLIMB_silenceCount = 0;								//  reset silence count
				liftyAirGap = 0;
			}
		}

	} else {										// No sound to play
		speaker_disableTimer();						//   turn off sound
	}
  */
}

void speaker_TEST(void) {
  //Serial.println('0');
  
  //Explore sounds
  speaker_playPiano();  
  

  //Test sound fx
  /*
  for (int i=0; i<20; i++) {
    sound_fx = 1;
    Serial.print("now called playSound()");
    Serial.println(i);
    speaker_playSound(i);
    delay(4000);
  }
  */


  //Test Vario Beeps
  /*
  for (int16_t i=11; i<900; i+=50) {
    speaker_updateVarioNote(i);  
    delay(3000);
  }
  */
}

void speaker_playPiano(void) {
if (Serial.available() > 0) {
    char letter = char(Serial.read());
    while (Serial.available() > 0) {
      Serial.read();
    }

    Serial.println(letter);
    int fx = 0;
    switch (letter) {
      case 'z': fx = NOTE_A2; break;
      case 'x': fx = NOTE_B2; break;
      case 'c': fx = NOTE_C3; break;
      case 'v': fx = NOTE_D3; break;
      case 'b': fx = NOTE_E3; break;
      case 'n': fx = NOTE_F3; break;
      case 'm': fx = NOTE_G3; break;
      case ',': fx = NOTE_A3; break;
      case '.': fx = NOTE_B3; break;      
      case 'a': fx = NOTE_A3; break;
      case 's': fx = NOTE_B3; break;
      case 'd': fx = NOTE_C4; break;
      case 'f': fx = NOTE_D4; break;
      case 'g': fx = NOTE_E4; break;
      case 'h': fx = NOTE_F4; break;
      case 'j': fx = NOTE_G4; break;
      case 'k': fx = NOTE_A4; break;
      case 'l': fx = NOTE_B4; break;
      case ';': fx = NOTE_C5; break;            
      case 'q': fx = NOTE_A4; break;
      case 'w': fx = NOTE_B4; break;
      case 'e': fx = NOTE_C5; break;
      case 'r': fx = NOTE_D5; break;
      case 't': fx = NOTE_E5; break;
      case 'y': fx = NOTE_F5; break;
      case 'u': fx = NOTE_G5; break;
      case 'i': fx = NOTE_A5; break;
      case 'o': fx = NOTE_B5; break;
      case 'p': fx = NOTE_C6; break;      
      case '[': fx = NOTE_D6; break;      
      case ']': fx = NOTE_E6; break;      
      case '0': speaker_setVolume(0); break;      
      case '1': speaker_setVolume(1); break;      
      case '2': speaker_setVolume(2); break;      
      case '3': speaker_setVolume(3); break;            
      case '4': speaker_playSound(fx_buttonpress); break;            
      case '5': speaker_playSound(fx_buttonhold); break;   
      case '6': speaker_playSound(fx_confirm); break;   
      case '7': speaker_playSound(fx_goingdown); break;   
      case '8': speaker_playSound(fx_octavesup); break;   
      case '9': speaker_playSound(fx_octavesdown); break;   
    }
    if (fx) speaker_playNote(fx);    
  }
}

