// Demonstrates control of a 192x96 ST75256 LCD via hardware SPI

#include <Arduino.h>
#include <SPI.h>

#define LCD_PIN_RS          17
#define LCD_PIN_RESET       18
#define LCD_PIN_CS          15

#define SPI_PIN_MOSI        11
#define SPI_PIN_CLK         12
#define SPI_PIN_MISO        13
#define SPI_CLOCK_FREQUENCY 1000000

const SPISettings SPI_SETTINGS(SPI_CLOCK_FREQUENCY, MSBFIRST, SPI_MODE0);

#define N_ROWS (96)
#define N_COLS (192)
#define N_PAGES (N_ROWS / 8)
#define N_BLOCKS (N_PAGES * N_COLS)
#define N_PIXELS (N_ROWS * N_COLS)
#define FIRST_PAGE (8)

void setup() {
  delay(500);
  Serial.begin(115200);
  Serial.println("starting setup");

  initLCD();

  Serial.println("done with setup");
}

void loop() {
  Serial.println("GO!");

  while (1) {
    Serial.println("painting LCD");
    SPI.beginTransaction(SPI_SETTINGS);
    digitalWrite(LCD_PIN_CS, LOW);
    lcdInstruction(0b01011100);  // write data starting from origin
    digitalWrite(LCD_PIN_RS, HIGH);
    for (uint8_t page = 0; page < N_PAGES; page++) {
      for (uint8_t column = 0; column < N_COLS; column++) {
        uint8_t value = (1 << (column % 8)) | (1 << (page % 8));
        if (column < page * 10 && column / 5 % 2 == 1) {
          value = 0xff;
        }
        SPI.transfer(value);
      }
    }
    digitalWrite(LCD_PIN_CS, HIGH);
    SPI.endTransaction();

    delay(500);

    Serial.println("clearing LCD");
    SPI.beginTransaction(SPI_SETTINGS);
    digitalWrite(LCD_PIN_CS, LOW);
    lcdInstruction(0b01011100);  // write data starting from origin
    digitalWrite(LCD_PIN_RS, HIGH);
    for (uint16_t i = 0; i < N_BLOCKS; i++){
      SPI.transfer(0);
      delay(1);
    }
    digitalWrite(LCD_PIN_CS, HIGH);
    SPI.endTransaction();
  }
}

void initLCD(void)
{
  // Setup SPI
  SPI.begin(SPI_PIN_CLK, SPI_PIN_MISO, SPI_PIN_MOSI, LCD_PIN_CS);

  // Setup LCD control pins
  pinMode(LCD_PIN_CS, OUTPUT);
  digitalWrite(LCD_PIN_CS, HIGH);
  pinMode(LCD_PIN_RESET, OUTPUT);
  pinMode(LCD_PIN_RS, OUTPUT);

  // Reset LCD
  pinMode(LCD_PIN_RESET, OUTPUT);
  digitalWrite(LCD_PIN_RESET, HIGH);
  delay(5);
  digitalWrite(LCD_PIN_RESET, LOW);
  delay(5);
  digitalWrite(LCD_PIN_RESET, HIGH);
  delay(5);

  // Initialize LCD
  SPI.beginTransaction(SPI_SETTINGS);
  digitalWrite(LCD_PIN_CS, LOW);
  
  delay(20);

  lcdInstruction(0x30);  // select 00 commands
  lcdInstruction(0x94);  // sleep out
  lcdInstruction(0xae);  // display off

  lcdInstruction(0x31);  // select 01 commands
  lcdInstruction(0xd7, 0x9f);  // disable auto read
  lcdInstruction(0x32,  // analog circuit set
    0x00,  // code example: OSC Frequency adjustment
    0x01,  // Frequency on booster capacitors 1 = 6KHz?
    0x03   // Bias: 1: 1/13, 2: 1/12, 3: 1/11, 4:1/10, 5:1/9
  );
  lcdInstruction(0x20,  // gray levels
    0x01, 0x03, 0x05, 0x07, 0x09, 0x0b, 0x0d, 0x10, 0x11, 0x13, 0x15, 0x17, 0x19, 0x1b, 0x1d, 0x1f
  );
  
  lcdInstruction(0x30);  // select 00 commands
  lcdInstruction(0x75, FIRST_PAGE, FIRST_PAGE + N_PAGES - 1);  // page address range (rows * 8)
  lcdInstruction(0x15, 0, N_COLS - 1);  // column address range (columns)
  lcdInstruction(0x0c);  // data format LSB top
  lcdInstruction(0xbc,  // data scan direction
    0x00  // normal scan directions
  );
  lcdInstruction(0xca,  // display control
    0x00,  // no clock division
    0x9f,  // 1/160 duty value from the DS example code
    0x20   // nline off
  );
  lcdInstruction(0xf0, 0x10);  // monochrome mode  = 0x010
  lcdInstruction(0x81, 0x2e, 0x03);  // Volume control
  lcdInstruction(0x20, 0x0b);  // Power control: Regulator, follower & booster on
  delay(100);
  lcdInstruction(0xaf);  // Display on

  digitalWrite(LCD_PIN_CS, HIGH);
  SPI.endTransaction();
}

template <typename... Args>
void lcdInstruction(byte cmd, Args... di) {
    digitalWrite(LCD_PIN_RS, LOW);
    SPI.transfer(cmd);
    if constexpr (sizeof...(di) > 0) {
        digitalWrite(LCD_PIN_RS, HIGH);
    }
    (SPI.transfer(di), ...);
}
