#include "Wire.h"

#define gpsPort Serial0
#define GPSBaud 115200
#define GPS_BACKUP_EN     40 
#define GPS_RESET         45
#define GPS_1PPS          46  // INPUT


//Pinout for Leaf V3.2.2      // V3.2.0
#define SPEAKER_PIN       14  // 7
#define SPEAKER_VOLA      15  // 8
#define SPEAKER_VOLB      16  // 9


void setup() {
  Serial.begin(115200);
  Wire.begin();


  pinMode(GPS_BACKUP_EN, OUTPUT);
  digitalWrite(GPS_BACKUP_EN, 1);
  pinMode(GPS_RESET, OUTPUT);
  digitalWrite(GPS_RESET, LOW); 
  delay(100);
  digitalWrite(GPS_RESET, HIGH);
  delay(300);

  gpsPort.begin(GPSBaud);


}

void loop() {
  //if (gpsPort.available()) Serial.print(char(gpsPort.read()));    


  ScanTwoWireBus();
  //echoGPSSerial();
  //speakerTest();
}


void ScanTwoWireBus() {
  byte error, address;
  int nDevices = 0;

  Serial.println("Scanning for I2C devices ...");
  for (address = 0x01; address < 0x7f; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0) {
      Serial.printf("I2C device found at address 0x%02X\n", address);
      nDevices++;
    } else if (error != 2) {
      Serial.printf("Error %d at address 0x%02X\n", error, address);
    }
  }
  if (nDevices == 0) {
    Serial.println("No I2C devices found");
  }
}

void echoGPSSerial() {
  Serial.print("echo GPS serial: ");

  // Rebroadcast GPS serial data to debugger port
  while (gpsPort.available() > 0) {
    Serial.print(char(gpsPort.read()));    
  }
  Serial.println(" ");
  Serial.println("GPS echo finished");

}


void speakerTest() {
  Serial.print("Begin Speaker Test.");
  pinMode(SPEAKER_PIN, OUTPUT);
  ledcAttach(SPEAKER_PIN, 1000, 10);

  pinMode(SPEAKER_VOLA, OUTPUT);
  pinMode(SPEAKER_VOLB, OUTPUT);

  digitalWrite(SPEAKER_VOLA, 1);
  digitalWrite(SPEAKER_VOLB, 0);

  Serial.print(".low.");
  ledcWriteTone(SPEAKER_PIN, 440);

  delay(1000);
  Serial.print(".med.");
  digitalWrite(SPEAKER_VOLA, 0);
  digitalWrite(SPEAKER_VOLB, 1);

  delay(1000);
  Serial.print(".high.");
  digitalWrite(SPEAKER_VOLA, 1);
  digitalWrite(SPEAKER_VOLB, 1);

  delay(1000);
  Serial.print(".off.");
  digitalWrite(SPEAKER_VOLA, 0);
  digitalWrite(SPEAKER_VOLB, 0);
  
  delay(1000);
  ledcWriteTone(SPEAKER_PIN, 0);
  Serial.println(".Done!");
}