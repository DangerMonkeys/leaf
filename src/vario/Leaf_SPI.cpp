#include "Leaf_SPI.h"

#include <SPI.h>

static const int spiClk = 1000000;  // TODO: see if we can speed up clock

void spi_init(void) {
  pinMode(SPI_SS_LCD, OUTPUT);
  digitalWrite(SPI_SS_LCD, HIGH);
  SPI.begin(SPI_CLK, SPI_MISO, SPI_MOSI, SPI_SS_LCD);
}

void spi_writeGLCD(byte data) {
  Serial.print("starting GLCD write. ");

  SPI.beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE0));
  Serial.print("Began tx. ");
  digitalWrite(SPI_SS_LCD, LOW);
  Serial.print("SS low. ");
  SPI.transfer(data);
  Serial.print("sent data. ");
  digitalWrite(SPI_SS_LCD, HIGH);
  Serial.print("SS high. ");
  SPI.endTransaction();
  Serial.println("end tx. ");
}
