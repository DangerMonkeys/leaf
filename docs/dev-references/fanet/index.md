---
title: Fanet
description: Structure of Fanet Support
---

## Fanet Support

The Leaf supports Fanet for the purposes of:

- Sending periodic flying updates
- Receiving broadcasted positional packets
  - Update traffic using FANET nmea strings to Bluetooth devices
- Sending periodic ground tracking positions for:
  - Landed Safe
  - Walking
  - Need A Ride
  - Need Technical Support.

As I believe we should not be encouraging the use of Fanet for emergency updates (voice radio and satellite preferred) a design decision was made to omit any emergency modes from the vario.

The leaf does NOT support any service (weather) or messaging capabilities of Fanet yet. It also does not support any form of message signatures and will not send them, and ignore the signature on any received packets.

The Leaf will also participate in forwarding on any received Fanet packets received with the requested forward flags set.

## Neighbor CSV logging

On units with a detected FANET chip, Menu > Developer shows **Fanet ReTX** and, directly
below it, **Log FANET**. Log FANET is a persistent checkbox, off by default. With Developer
mode and this checkbox enabled, the flight timer starts an independent neighbor CSV session
even when IGC track recording is disabled. Stopping the timer or switching the checkbox off
flushes the pending observations. Enabling it during a flight starts a new CSV segment.

Each session creates a new `/fanetlogs/FANET_<UTC date-time>_<sequence>.csv`; before GPS time
is available the date-time portion is `unsynced`. Existing files are never overwritten. Four
comment lines beginning with `#` record firmware version, receiver ID, radio region and the
local forwarding setting at session start. Read the remaining content as CSV (for example,
with `pandas.read_csv(path, comment="#")`):

```text
elapsed_ms,neighbor_id,rssi_dbm,snr_db,neighbor_lat,neighbor_lon,distance_km,receiver_lat,receiver_lon,queue_drops
```

Elapsed time is measured from flight-timer start; neighbor IDs are hexadecimal. Each row is
one received airborne or ground tracking packet with a location payload. Other packet types
are excluded. Signal strength and neighbor position are retained without a usable own GPS fix,
but distance and receiver coordinates are left blank. Distance is horizontal GPS separation,
not slant range. Map transmitter IDs to the antennas fitted for each test separately.

The radio receive task queues at most 32 numeric observations without waiting for SD writes.
The main flight-logging update drains the queue once per second and closes the file after each
batch. `queue_drops` is the cumulative number of observations dropped by a full queue in that
CSV session; it is not an RF packet-loss measurement. SD/open failures stop that session and
print an error on serial; toggle Log FANET off and on to retry, or start a new flight.

This logger neither starts nor subscribes to the bus logger. FANET `fanet_rx` comments have
been removed from bus logs; use the separate CSV for antenna tests. Leave the other optional
diagnostic logs disabled when comparing radio performance.

## Architecture

FANET menus, status icons, address display and radio actions require detected hardware. The
web app developer card has no FANET button: opening the web app pauses the radio, so it cannot
serve as a live neighbor monitor. The old `/app/debug/fanet` diagnostics page has been removed;
requests to that URL use the normal not-found response without touching the radio manager.

the fanet_radio.h and fanet_radio.cpp files contain the FanetRadio class. This class will initialize the SX1262 chip, the Fanet::Manager, and any buffers to interact with them.

The FanetRadio class will also provide a map of the last received neighbor to (ground or flying) tracking packet to keep state of where traffic is around, as well as a list of callbacks when this information updates to be able to be consumed by Bluetooth modules and the like.

### Fanet::Manager

To keep the airwaves clear, there are rules around how long we should hold packets before we forward them, and when we should forward or drop packets. We should also ensure that as the network becomes more busy, we send updates with less frequency to try and keep the airwaves as open as possible. We use the Fanet::Manager library for this. It has the following purposes:

- Parses any receive bytes into RX packets
- Keeps an internal txQueue for packets to egress and timers for those packets to go out
- Keeps track of positional information, and will request a send at the appropriate time, given the business of the network.
- Forward received packets back on the Fanet network.

### FanetRadio tasks

To interface with the Fanet::Manager library, the FanetRadio module keeps an instance of the manager, mutex to allow multiple tasks can interact with it in a thread-safe manner, and for simplicity of coding, two tasks:

**FanetRx:**

This task is responsible for reading packets from the LoRa radio and passing them to the Manager to parse.

In the event that positional updates are received, the task will update the position database and notify any modules that new positional updates are available. This is intended to be used by the Bluetooth modules.

**FanetTx:**

This task is responsible for moving packets from the FanetManager library to the LoRa wire, and reporting back success. It should sleep until the next scheduled wake up time, or woken up to process the new sleep time (after a request to send a packet was made).
