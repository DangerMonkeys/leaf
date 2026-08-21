---
title: Bluetooth Low Energy
description: Leaf BLE serial protocol and diagnostics
---

# Bluetooth Low Energy

Leaf exposes a Nordic UART Service (NUS) so that flight applications can treat it as a BLE serial
device. The implementation has been tested with XCSoar, XCTrack, and SeeYou Navigator.

## GATT interface

| Item | UUID | Properties | Purpose |
| --- | --- | --- | --- |
| Nordic UART service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | Service | BLE serial container |
| RX characteristic | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | Write without response, 64-byte maximum | Data written by the phone to Leaf |
| TX characteristic | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | Read and notify | Data sent by Leaf to the phone |

The RX characteristic is present because some BLE serial clients require the complete bidirectional
NUS interface during service discovery. Leaf currently accepts but does not interpret RX data. It
also does not echo received data.

Only one BLE central connection is supported at a time. When an application disconnects, Leaf
restarts advertising so another application can connect.

## Transmitted data

All TX records are ASCII NMEA-style sentences ending in an XOR checksum and `\r\n`. Incoming GPS
GGA and RMC records have their checksum recalculated before transmission, so a checksum inherited
from the GPS module cannot cover stale or modified content.

Leaf sends the following record types:

- `$LK8EX1` at up to 10 Hz, containing pressure in Pa, pressure altitude in metres, filtered climb
  rate in cm/s, ambient temperature in degrees Celsius, and battery percentage encoded as
  `1000 + percentage`. The comma before the checksum is intentional and required for compatibility
  with some clients. Temperature is `99` until the ambient sensor is ready.
- `$GPGGA`/`$GNGGA` and `$GPRMC`/`$GNRMC` GPS records, limited to two of each type per second.
- `$PFLAA` traffic records generated from received FANET tracking packets when Leaf has a usable
  local GPS fix.

Leaf does not expose the standard Battery Service or Environmental Sensing Service. Battery and
temperature are carried in `$LK8EX1`; humidity and wind are not currently transmitted over BLE.

## Diagnostics

BLE diagnostics use the existing `diagnostics/system_events.csv` file. No separate BLE log or
in-memory history buffer is created. The instrumentation records:

- connection and disconnection checkpoints, including the NimBLE disconnect reason;
- advertising restart success or failure;
- cumulative NUS notification submission counts at disconnect;
- immediately reported notification-submission failures;
- full periodic, GPS, or FANET BLE queues; and
- heap and registered-task stack snapshots at important connection lifecycle events.

Leaf also checks heap integrity every five seconds while the BLE task is running. A healthy check is
silent; only a failure produces a `ble-heap-invalid` event. Consequently, normal operation adds no
per-sentence SD writes and only a few records around each connection transition.

NimBLE's characteristic-update API is asynchronous. A successful `notify()` result means the update
was submitted to NimBLE; it is not confirmation that the phone received the notification over the
air.
