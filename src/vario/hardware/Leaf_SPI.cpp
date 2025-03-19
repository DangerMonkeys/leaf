#include "hardware/Leaf_SPI.h"

#include <SPI.h>

static const int spiClk = 1000000;  // TODO: see if we can speed up clock
SemaphoreHandle_t SpiLockGuard::spiMutex = NULL;

void spi_init(void) {
  // SPI is a shared buffer and doesn't have a lock.
  SpiLockGuard::spiMutex = xSemaphoreCreateMutex();

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
