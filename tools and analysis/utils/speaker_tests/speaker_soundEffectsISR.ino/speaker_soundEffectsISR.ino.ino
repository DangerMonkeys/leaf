#define NOTE_C4 262
#define NOTE_CS4 277
#define NOTE_D4 294
#define NOTE_DS4 311
#define NOTE_E4 330
#define NOTE_F4 349
#define NOTE_FS4 370
#define NOTE_G4 392
#define NOTE_GS4 415
#define NOTE_A4 440
#define NOTE_AS4 466
#define NOTE_B4 494
#define NOTE_C5 523
#define NOTE_CS5 554
#define NOTE_D5 587
#define NOTE_DS5 622
#define NOTE_E5 659
#define NOTE_F5 698
#define NOTE_FS5 740
#define NOTE_G5 784
#define NOTE_GS5 831
#define NOTE_A5 880
#define NOTE_AS5 932
#define NOTE_B5 988

// sound effects to play:
#define NOTE_END 1
#define NOTE_NONE 0
uint16_t fx_silence[] = {NOTE_END};
uint16_t fx_enter[] = {NOTE_A4, NOTE_C4, NOTE_E4, NOTE_END};
uint16_t fx_exit[] = {NOTE_C5, NOTE_A5, NOTE_F4, NOTE_C4, NOTE_END};   

volatile uint16_t*  snd_index;              // volatile pointer to the sound sample to play
volatile uint16_t   sound_fxNoteLast = 0;		// last note played for sound effects
volatile bool       sound_fx = 0;           // track if we have any sound effects to play (button presses, etc)

//Pinout for Leaf V3.2.0
#define SPEAKER_PIN       7
#define SPEAKER_VOLA      8
#define SPEAKER_VOLB      9
#define PWM_CHANNEL       0   // ESP32 has many channels; we'll use the first

#define FX_NOTE_LENGTH  500*2 // timer ticks twice every ms, so multiply desired ms by 2
#define SPKR_CLK_DIV  40000   // max 65535

hw_timer_t *speaker_timer = NULL;
void onSpeakerTimer(void);

volatile uint16_t tonal_frequency = 440;

void setup() {
  delay(1000);
  Serial.begin(115200);
  delay(1000);

  //configure pinouts and PWM channel
  pinMode(SPEAKER_PIN, OUTPUT);
  pinMode(SPEAKER_VOLA, OUTPUT);
  pinMode(SPEAKER_VOLB, OUTPUT);

  //set volume to 1/Low
  digitalWrite(SPEAKER_VOLA, 1);
  digitalWrite(SPEAKER_VOLB, 0); 

  ledcSetup(PWM_CHANNEL, 1000, 8);
  ledcAttachPin(SPEAKER_PIN, PWM_CHANNEL);

  //setup speaker timer interrupt to track each "beat" of sound
	speaker_timer = timerBegin(1, SPKR_CLK_DIV, true);              // timer#, clock divider, count up -- this just configures, but doesn't start.   NOTE: timerEnd() de-configures.
  timerAttachInterrupt(speaker_timer, &onSpeakerTimer, true);     // timer, ISR call, rising edge                                                  NOTE: timerDetachInterrupt() does the opposite
  //timerAlarmWrite(speaker_timer, 1000, false);                    // timer, value, reload
}

void loop() {  
  Serial.print("playing sound now @ ");
  Serial.println(millis());
  speaker_playFx(fx_enter);

  delay(4000);
}


void speaker_playFx(uint16_t * fx)
{	
	snd_index = fx;
  Serial.print("setting snd_index to: ");
  Serial.print(*snd_index);
  Serial.print(" @ ");
  Serial.println(millis());
  sound_fx = 1;
  onSpeakerTimer();
}

void IRAM_ATTR onSpeakerTimer() {
  Serial.print("ENTER ISR: ");
  if (sound_fx) {								
    Serial.print("FX: "); Serial.print(*snd_index); Serial.print(" @ "); Serial.println(millis());			

		if (*snd_index != NOTE_END) {
			ledcWriteTone(PWM_CHANNEL, *snd_index);   // only change pwm if it's a different note, otherwise we get a little audio blip between the same notes      if (*snd_index != sound_fxNoteLast)       
      sound_fxNoteLast = *snd_index;
      snd_index++;

      timerAlarmWrite(speaker_timer, FX_NOTE_LENGTH, false); // start timer for play period
      timerWrite(speaker_timer, 0);
      timerAlarmEnable(speaker_timer);
       
		} else {									// Else, we're at END_OF_TONE
      Serial.println("FX NOTE END");
      ledcWriteTone(PWM_CHANNEL, 0);    
			sound_fx = 0;
      sound_fxNoteLast = 0;
		}    
  } else {
    Serial.println("NO SOUND");
    ledcWriteTone(PWM_CHANNEL, 0); 
  }
}