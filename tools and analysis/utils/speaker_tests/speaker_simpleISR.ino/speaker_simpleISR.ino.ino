#define SPEAKER_PIN       41
#define PWM_CHANNEL       0   // ESP32 has many channels; we'll use the first

#define SPKR_CLK_DIV  40000   // max 65535
#define FX_NOTE_LENGTH  500*2 // timer ticks twice every ms, so multiply desired ms by 2 for proper number of ticks 

hw_timer_t *speaker_timer = NULL;
void onSpeakerTimerSimple(void);
volatile uint16_t tonal_frequency = 440;

void setup() {
  delay(1000);
  Serial.begin(115200);
  delay(1000);

  //configure pinouts and PWM channel
  pinMode(SPEAKER_PIN, OUTPUT);
  ledcSetup(PWM_CHANNEL, 1000, 8);
  ledcAttachPin(SPEAKER_PIN, PWM_CHANNEL);

  //setup speaker timer interrupt to track each "beat" of sound
	speaker_timer = timerBegin(1, SPKR_CLK_DIV, true);                    // timer#, clock divider, count up -- this just configures, but doesn't start.   NOTE: timerEnd() de-configures.
  timerAttachInterrupt(speaker_timer, &onSpeakerTimerSimple, true);     // timer, ISR call, rising edge                                                  NOTE: timerDetachInterrupt() does the opposite
}

void loop() {  
  onSpeakerTimerSimple();
  while(1);
}

void IRAM_ATTR onSpeakerTimerSimple() {
  Serial.print("ENTER ISR: "); Serial.print(tonal_frequency); Serial.print(" @ "); Serial.println(millis());
  ledcWriteTone(PWM_CHANNEL, tonal_frequency);        
  tonal_frequency += 100;

  timerAlarmWrite(speaker_timer, FX_NOTE_LENGTH, false);  // set timer for play period (don't reload; we'll trigger back into this ISR when time is up)
  timerWrite(speaker_timer, 0);                           // start at 0
  timerAlarmEnable(speaker_timer);                        // and.. go!       
}
