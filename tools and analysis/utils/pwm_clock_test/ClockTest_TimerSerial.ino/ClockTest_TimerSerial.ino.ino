



// Main system event/task timer
  hw_timer_t *task_timer = NULL;
  #define TASK_TIMER_FREQ 1000  // run at 1000Hz 
  #define TASK_TIMER_LENGTH 1000  // trigger the ISR every 10ms
  void onTaskTimer(void);

void setup() {

  pinMode(48, OUTPUT);
  digitalWrite(48, 1);

  // Start USB Serial Debugging Port
    Serial.begin(115200);
    delay(1000);
    Serial.println("Starting Setup");

  //Start Main System Timer for Interrupt Events (this will tell Main Loop to set tasks every interrupt cycle)
    task_timer = timerBegin(TASK_TIMER_FREQ);            
    timerAttachInterrupt(task_timer, &onTaskTimer); // timer, ISR call          NOTE: timerDetachInterrupt() does the opposite
    timerAlarm(task_timer, TASK_TIMER_LENGTH, true, 0);      // auto reload timer ever time we've counted a sample length

  // All done!
    Serial.println("Finished Setup");

}


uint8_t taskman_setTasks = 0;

// Main Timer Interrupt
void IRAM_ATTR onTaskTimer() {
  //do stuff every alarm cycle (default 10ms)
  taskman_setTasks = 1; // next time through main loop, set tasks to do!
  // wakeup();  // go back to main loop and keep processing stuff that needs doing!
}

uint8_t seconds = 0;
uint8_t minutes = 0;
uint8_t hours = 0;

void loop() {

  if (taskman_setTasks) {
    taskman_setTasks = 0;
    seconds++;
    if (seconds == 60) {
      seconds = 0;
      minutes++;
      if (minutes == 60) {
        minutes = 0;
        hours++;
      }
    }
    Serial.print(hours);
    Serial.print(":");
    Serial.print(minutes);
    Serial.print(":");
    Serial.println(seconds);
  }
  
}
