## Required libraries

See list of required libraries in the `libraries` section of the [Arduino workflow](../../.github/workflows/arduino.yaml).

## ESP32 configuration

ESP Board Manager Package 3.0.2

- USB CDC On Boot -> "Enabled"
- Flash Size -> 8MB
- Partition Scheme -> 8M with spiffs (3MB APP/1.5MB SPIFFS)

Arduino board: ESP32S3 Dev Module

(see [Arduino workflow](../../.github/workflows/arduino.yaml) to confirm specifics)

## Programming with PlatformIO

See [leaf README](../README.md)

## Programming with Arduino

To program the current hardware:

- Install Arduino IDE and [ESP Board Manager Package](#esp32-configuration)
- Install [required libraries](#required-libraries)
- Plug board into USB
- Select the [appropriate board](#esp32-configuration) and port in the Arduino IDE
- Configure the [appropriate board settings](#esp32-configuration)

## Notes

tried but not going to use:

- NeoGPS: 4.2.9
  -> change GPSport.h to define HWSerial:
  #define gpsPort Serial0
  #define GPS_PORT_NAME "Serial0"
  #define DEBUG_PORT Serial
  -> enable PARSE_GSV string and PARSE_SATELLITES and PARSE_SATELLITE_INFO
  -> enable NMEAGPS_INTERRUPT_PROCESSING
- ICM20948_WE
- AdaFruit ICM20948 (and associated dependencies)

## System diagram

```mermaid
flowchart LR
    subgraph Physical HW components
        MS5611dev>MS5611]
        AHT20dev>AHT20]
        ICM20948dev>ICM20948]
        GPSdev>GPS]
    end

    subgraph Hardware abstraction
        AHT20hw["AHT20 (<code>IPollable</code>)"]
        MS5611["MS5611"]
        subgraph ICM20948["ICM20948 (<code>IPollable</code>)"]
            ICM_20948_I2C
        end
        LC86G
    end

    subgraph Mocks
        MessagePlayback["Message log playback<br>(<code>IPollable</code>)"]
    end

    subgraph Instruments
        Ambient["Ambient<br>(<code>ambient</code> singleton)"]
        Barometer["Barometer<br>(<code>barometer</code> singleton)"]
        IMU["IMU<br>(<code>imu</code> singleton)"]
        subgraph LeafGPS["LeafGPS (<code>gps</code> singleton)"]
            TinyGPSPlus
        end
    end

    subgraph Loggers
        MessageLogger["Message logger"]
    end

    subgraph MessageBus["Message bus"]
        AmbientUpdateMessage["<code>AmbientUpdate</code><br>message"]
        MotionUpdateMessage["<code>MotionUpdate</code><br>message"]
        GpsMessage["<code>GpsMessage</code><br>message"]
    end

    subgraph VarioLogic
    end

    MS5611dev <-->|Wire| MS5611 -->|IPressureSource| Barometer
    AHT20dev <-->|Wire| AHT20hw --> AmbientUpdateMessage --> Ambient
    GPSdev <-->|Serial0| LC86G --> GpsMessage --> LeafGPS

    MessagePlayback -.-> MessageBus -.-> MessageLogger

    ICM20948dev <-->|TwoWire| ICM_20948_I2C["ICM_20948_I2C<br>(Sparkfun lib)"]
    ICM20948 --> MotionUpdateMessage --> IMU

    Barometer --> VarioLogic
    IMU --> VarioLogic
    Ambient --> VarioLogic
    LeafGPS <--> VarioLogic
```
