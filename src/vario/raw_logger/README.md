# Leaf raw flight recorder

Build either dedicated recorder target with:

```sh
pio run -e leaf_3_2_6_recorder
pio run -e leaf_3_2_7_recorder
```

The target initializes the display, SD card, GPS, MS5611, ICM-20948, AHT20 and power telemetry. It
does not link the normal UI, radio, Bluetooth, Wi-Fi, navigation, audio, logbook or USB mass-storage
services. Once GPS date, time and position are valid, press the center button to start a recording.
Press it again to seal the final chunk and close the file. Hold the center button for 3.5 seconds to
safely finish any active recording and turn the unit off.

Recordings are written under `/raw` with the `.lraw` extension. Sensor polling appends to a static
64 KiB chunk pool; a separate FreeRTOS task performs 4 KiB sequential SD writes. The display shows
the buffer high-water mark, longest SD write and any dropped records.

Each tape contains raw sensor records and canonical bus records. Inspect a tape, convert it to the
existing simulator bus-log format, or replay it over the firmware UDP injector with:

```sh
python sim/lraw.py RAW_20260823_120000.lraw
python sim/lraw.py RAW_20260823_120000.lraw --buslog flight.log
python sim/lraw.py RAW_20260823_120000.lraw --udp 192.0.2.10 --speed 1.0
```

The file starts with a CRC-protected 512-byte metadata header. The remainder is a sequence of
CRC-protected 4096-byte chunks. A chunk contains an ordered set of typed records whose microsecond
timestamps are relative to that chunk's monotonic time base. Sequence numbers, data-loss records and
per-chunk CRCs make interrupted or damaged recordings detectable.

All integers and floating-point values are little-endian. Every record begins with an 8-byte
`type, flags, payload_bytes, delta_us` header. Version 1 payloads are:

| Type | Payload |
| --- | --- |
| 1, 2 | MS5611 pressure D1 or temperature D2 (`uint32`) |
| 3 | ICM-20948 raw accelerometer X/Y/Z (`int16[3]`) |
| 4 | Raw gyro X/Y/Z and bias X/Y/Z (`int16[6]`) |
| 5 | Raw magnetometer X/Y/Z (`int16[3]`) |
| 6 | DMP quaternion Q1/Q2/Q3 in Q30 and accuracy (`int32[3], int16`) |
| 7 | Complete GPS NMEA sentence as ASCII, without a line ending |
| 8 | AHT20 raw 20-bit humidity and temperature counts (`uint32[2]`) |
| 9 | Battery mV, charge current mA, charging flag |
| 10 | GPS UTC microseconds, fix quality and satellites |
| 11 | UTF-8 event text |
| 12 | Cumulative dropped-record and dropped-byte counters |
| 64 | Canonical bus pressure (`int32`) |
| 65 | Canonical bus acceleration and quaternion (`flags, double[6]`) |
| 66 | Canonical bus temperature and relative humidity (`float[2]`) |

Replay uses the canonical records plus the original NMEA sentences, so it reproduces the exact bus
measurements seen during recording. The raw records remain available for new calibration or sensor
fusion experiments without constraining those algorithms to today's firmware.
