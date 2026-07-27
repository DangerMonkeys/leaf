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

#define SPEAKER_PIN       7
#define SPEAKER_VOLA      8
#define SPEAKER_VOLB      9

#define SPKR_CLK_DIV  40000   // max 65535
#define FX_NOTE_LENGTH  125  // timer ticks per note play

hw_timer_t *speaker_timer = NULL;
void onSpeakerTimerSimple(void);
void onSpeakerTimer(void);
volatile uint16_t tonal_frequency = 440;

// sound effects to play:
#define NOTE_END 1
#define NOTE_NONE 0
uint16_t fx_silence[] = {NOTE_END};
uint16_t fx_enter[] = {NOTE_A4, NOTE_C4, NOTE_E4, NOTE_END};
uint16_t fx_exit[] = {NOTE_C5, NOTE_A5, NOTE_F4, NOTE_C4, NOTE_END};   

volatile uint16_t*  snd_index;              // volatile pointer to the sound sample to play
volatile uint16_t   sound_fxNoteLast = 0;		// last note played for sound effects
volatile bool       sound_fx = 0;           // track if we have any sound effects to play (button presses, etc)

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

void setup() {
  delay(1000);
  Serial.begin(115200);
  delay(1000);



  //configure pinouts and PWM channel
  pinMode(SPEAKER_PIN, OUTPUT);
  ledcAttach(SPEAKER_PIN, 1000, 10);

  //set volume to 1/Low
  pinMode(SPEAKER_VOLA, OUTPUT);
  pinMode(SPEAKER_VOLB, OUTPUT);
  digitalWrite(SPEAKER_VOLA, 1);
  digitalWrite(SPEAKER_VOLB, 0); 
  
  //setup speaker timer interrupt to track each "beat" of sound
	speaker_timer = timerBegin(1000);                    
  timerAttachInterrupt(speaker_timer, &onSpeakerTimer);     // timer, ISR call
}

void loop() {  
  
  speaker_playFx(fx_enter);
  delay(4000);
}

void IRAM_ATTR onSpeakerTimerSimple() {
  Serial.print("ENTER ISR: "); Serial.print(tonal_frequency); Serial.print(" @ "); Serial.println(millis());
  ledcWriteTone(SPEAKER_PIN, tonal_frequency);        
  tonal_frequency += 100;
  if (tonal_frequency >= 2200) tonal_frequency = 100;

  timerWrite(speaker_timer, 0);                           // start at 0
  timerAlarm(speaker_timer, FX_NOTE_LENGTH, false, 0);    // set timer for play period (don't reload; we'll trigger back into this ISR when time is up)

}


void IRAM_ATTR onSpeakerTimer() {
  Serial.print("ENTER ISR: ");
  if (sound_fx) {								
    Serial.print("FX: "); Serial.print(*snd_index); Serial.print(" @ "); Serial.println(millis());			

		if (*snd_index != NOTE_END) {
			ledcWriteTone(SPEAKER_PIN, *snd_index);   // only change pwm if it's a different note, otherwise we get a little audio blip between the same notes      if (*snd_index != sound_fxNoteLast)       
      sound_fxNoteLast = *snd_index;
      snd_index++;

      timerWrite(speaker_timer, 0);                           // start at 0
      timerAlarm(speaker_timer, FX_NOTE_LENGTH, false, 0);    // set timer for play period (don't reload; we'll trigger back into this ISR when time is up)
       
		} else {									// Else, we're at END_OF_TONE
      Serial.println("FX NOTE END");
      ledcWriteTone(SPEAKER_PIN, 0);    
			sound_fx = 0;
      sound_fxNoteLast = 0;
		}    
  } else {
    Serial.println("NO SOUND");
    ledcWriteTone(SPEAKER_PIN, 0); 
  }
}
