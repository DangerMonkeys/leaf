#include "hardware/aht20.h"

#include "diagnostics/fatal_error.h"
#include "dispatch/message_types.h"
#include "hardware/Leaf_I2C.h"
#include "utils/magic_enum.h"

AHT20 aht20;

#define DEBUG_TEMPRH 0  // flag for outputting debugf messages on UBS serial port

#define ADDR_AHT20 0x38
// used for subtracting out any self-heating errors in the temp/humidity sensor
#define TEMP_OFFSET -3

enum registers {
  sfe_aht20_reg_reset = 0xBA,
  sfe_aht20_reg_initialize = 0xBE,
  sfe_aht20_reg_measure = 0xAC,
};

void AHT20::beginInit() {
  assertState("AHT20::beginInit", State::Uninitialized);

  if (!isConnected()) {
    fatalError("AHT20 temp humidity sensor not found (isConnected is false)");
  }

  if (DEBUG_TEMPRH) Serial.println("Temp_RH - AHT20 Temp Humidity sensor FOUND!");

  // Wait 40 ms after power-on before reading temp or humidity. Datasheet pg 8
  tLastAction_ = millis();
  state_ = State::WaitingForPowerOn;
}

void AHT20::waitForPowerOn() {
  assertState("AHT20::waitForPowerOn", State::WaitingForPowerOn);

  if (millis() - tLastAction_ < 40) {
    return;
  }

  // Check if the calibrated bit is set. If not, init the sensor.
  if (!isCalibrated()) {
    // Send 0xBE0800
    initialize();

    // Immediately trigger a measurement.
    triggerMeasurement(State::WaitingForCalMeasurement, State::WaitingForPowerOn);
    return;
  }

  startFirstMeasurement();
}

void AHT20::waitForCalMeasurement() {
  assertState("AHT20::waitForCalMeasurement", State::WaitingForCalMeasurement);

  if (millis() - tLastAction_ < 75) {
    return;
  }

  if (!isBusy()) {
    // This calibration sequence is not completely proven. It's not clear how and when the cal bit
    // clears This seems to work but it's not easily testable
    if (!isCalibrated()) {
      fatalError(
          "AHT20 initialization failure: device indicates not calibrated after calibration and "
          "initial measurement");
    }
    startFirstMeasurement();
    return;
  }

  if (millis() - tLastAction_ > 175) {
    // Give up after 100ms
    fatalError("AHT20 initialization failure: initial measurement did not complete after 175ms");
  }
}

void AHT20::startFirstMeasurement() {
  assertState("AHT20::startFirstMeasurement", State::WaitingForPowerOn,
              State::WaitingForCalMeasurement);

  // Get a fresh initial measurement
  dtMeasurement_ = 100;  // Wait 100ms for first measurement
  triggerMeasurement(State::WaitingForInitialMeasurement, State::Uninitialized);
  if (state_ == State::Uninitialized) {
    fatalError("AHT20 first measurement could not be started");
  }
}

const unsigned long MEASUREMENT_PERIOD_MS = 75;

void AHT20::update() {
  if (state_ == State::Uninitialized) {
    beginInit();
  } else if (state_ == State::WaitingForPowerOn) {
    waitForPowerOn();
  } else if (state_ == State::WaitingForCalMeasurement) {
    waitForCalMeasurement();
  } else if (state_ == State::WaitingForInitialMeasurement) {
    completeMeasurement();
  } else if (state_ == State::Idle) {
    maybeTriggerMeasurement();
  } else if (state_ == State::Measuring) {
    completeMeasurement();
  } else {
    fatalError("AHT20::update with unsupported state %s (%u)", nameOf(state_).c_str(), state_);
  }
}

void AHT20::maybeTriggerMeasurement() {
  assertState("AHT20::maybeTriggerMeasurement", State::Idle);

  // don't call more often than every 1-2 seconds, or sensor will heat up slightly above
  // ambient
  if (millis() - tLastAction_ > 1000) {
    dtMeasurement_ = MEASUREMENT_PERIOD_MS;
    triggerMeasurement(State::Measuring, State::Idle);
  }
}

void AHT20::completeMeasurement() {
  assertState("AHT20::completeMeasurement", State::Measuring, State::WaitingForInitialMeasurement);

  if (millis() - tLastAction_ <= dtMeasurement_) {
    return;
  }
  if (isBusy()) {
    if (DEBUG_TEMPRH) Serial.println("Temp_RH - missed values due to sensor busy");
    return;
  }

  SensorData sensorData;
  if (!readData(sensorData)) {
    Serial.println("Temp_RH did not successfully readData");
    return;
  }
  float temperature = ((float)sensorData.temperature / 1048576) * 200 - 50;
  temperature += TEMP_OFFSET;
  float rh = ((float)sensorData.humidity / 1048576) * 100;
  state_ = State::Idle;
  if (DEBUG_TEMPRH) {
    Serial.print("Temp_RH - Temp: ");
    Serial.print(temperature);
    Serial.print("  Humidity: ");
    Serial.println(rh);
  }
  etl::imessage_bus* bus = bus_;
  if (bus) {
    bool sane = true;
    if (isnan(temperature) | isinf(temperature) || temperature < -90 || temperature > 180) {
      char msg[100];
      snprintf(msg, sizeof(msg), "AHT20 invalid temp %X", sensorData.temperature);
      Serial.println(msg);
      bus->receive(CommentMessage(msg));
      sane = false;
    }
    if (isnan(rh) || isinf(rh) || rh < 0 || rh > 100) {
      char msg[100];
      snprintf(msg, sizeof(msg), "AHT20 invalid RH %X", sensorData.humidity);
      Serial.println(msg);
      bus->receive(CommentMessage(msg));
      sane = false;
    }
    if (sane) {
      bus->receive(AmbientUpdate(temperature, rh));
    }
  }
}

bool AHT20::softReset() {
  Wire.beginTransmission(ADDR_AHT20);
  Wire.write(sfe_aht20_reg_reset);
  if (Wire.endTransmission() == 0) return true;
  return false;
}

bool AHT20::readData(SensorData& sensorData) {
  sensorData.humidity = 0;
  sensorData.temperature = 0;

  if (Wire.requestFrom(ADDR_AHT20, (uint8_t)6) <= 0) {
    return false;
  }

  Wire.read();  // Read and discard state

  uint32_t incoming = 0;
  incoming |= (uint32_t)Wire.read() << (8 * 2);
  incoming |= (uint32_t)Wire.read() << (8 * 1);
  uint8_t midByte = Wire.read();

  incoming |= midByte;
  sensorData.humidity = incoming >> 4;

  sensorData.temperature = (uint32_t)midByte << (8 * 2);
  sensorData.temperature |= (uint32_t)Wire.read() << (8 * 1);
  sensorData.temperature |= (uint32_t)Wire.read() << (8 * 0);

  // Need to get rid of data in bits > 20
  sensorData.temperature = sensorData.temperature & ~(0xFFF00000);

  return true;
}

void AHT20::triggerMeasurement(State onSuccess, State onFailure) {
  Wire.beginTransmission(ADDR_AHT20);
  Wire.write(sfe_aht20_reg_measure);
  Wire.write((uint8_t)0x33);
  Wire.write((uint8_t)0x00);
  bool success = Wire.endTransmission() == 0;
  tLastAction_ = millis();
  state_ = success ? onSuccess : onFailure;
}

bool AHT20::initialize() {
  Wire.beginTransmission(ADDR_AHT20);
  Wire.write(sfe_aht20_reg_initialize);
  Wire.write((uint8_t)0x08);
  Wire.write((uint8_t)0x00);
  if (Wire.endTransmission() == 0) return true;
  return false;
}

bool AHT20::isBusy() { return (getStatus() & (1 << 7)); }

bool AHT20::isCalibrated() { return (getStatus() & (1 << 3)); }

uint8_t AHT20::getStatus() {
  Wire.requestFrom(ADDR_AHT20, (uint8_t)1);
  if (Wire.available()) return (Wire.read());
  return (0);
}

bool AHT20::isConnected() {
  Wire.beginTransmission(ADDR_AHT20);
  if (Wire.endTransmission() == 0) return true;

  // If IC failed to respond, give it 20ms more for Power On Startup
  // Datasheet pg 7
  delay(20);

  Wire.beginTransmission(ADDR_AHT20);
  if (Wire.endTransmission() == 0) return true;

  return false;
}

void AHT20::onUnexpectedState(const char* action, State actual) const {
  fatalError("%s while %s (%u)", action, nameOf(actual).c_str(), actual);
}
