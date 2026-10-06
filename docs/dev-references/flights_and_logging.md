---
title: Flights and Logging
description: Definitions and intended behavior around flights and logging
---

# Leaf Logging Functions

<!-- Adopted from https://docs.google.com/document/d/1Rp0IslOAHZlb1cdwXtGMdCtgvGe01PvQ_RnXXEvWxjQ/edit?usp=sharing -->

There are various terms currently used in “Leaf-Land” that have overlap or confusing definitions, such as:

> Flight, Timer, Log, Logging, LogBook, Track, TrackLog, Records, Flight Summary, etc…

This document aims to clarify the design intent, and be a helpful guide in implementing feature improvements (and code clarification) on Leaf.

## Main Operational States of Leaf

Leaf has two main operational states:
* Idle (not tracking a Flight), and
* Logging (actively tracking a Flight).

Transition between these states can be auto-detected (via motion, speed, altitude change, etc), or manually switched (by starting/stopping the flight timer).
```mermaid
graph TD
    Idle
    Logging
    class Idle,Logging State
    
    IdleToLogging["Auto-detect flight start<br>--OR--<br>Manually start timer"]
    LoggingToIdle["Auto-detect flight stop<br>--OR--<br>Manually stop timer"]
    class IdleToLogging,LoggingToIdle Transition

    Idle --> IdleToLogging --> Logging --> LoggingToIdle --> Idle

    classDef State fill:#f7ff00
    classDef Transition fill:#f0f0f0
```


### What Happens During Each State

The table below outlines what actions do/don't happen during each state:

<table>
  <thead>
    <tr>
      <th>Idle State</th>
      <th>Logging State</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><ul>
        <li>Vario Beeping is muted (if QuietMode is on)</li>
        <li>Timer is stopped at 0:00</li>
        <li>No active Flight Object</li>
        <li>No Tracking</li>
        <li>Leaf is detecting if a new Flight has started (if AutoStart is enabled)</li>
      </ul></td>
      <td><ul>
        <li>Vario Beeping is unmuted</li>
        <li>Timer is running</li>
        <li>Flight object is active</li>
        <li>Flight stats are being recorded(duration, min/max alt, etc)</li>
        <li>Logbook contains one additional LogbookEntry than before (may be a preliminary or constantly-updating Entry as the flight continues)</li>
        <li>GPSTrack file is being written (if user has enabled Tracking and GPS is available)</li>
        <li>Leaf is detecting if the current Flight has ended (if AutoStop is enabled)</li>
      </ul></td>
    </tr>
  </tbody>
</table>


## Logging Terms and Definitions

For each Logging Session (i.e., a Flight), Leaf will save a __LogbookEntry__ inside the __Logbook__.

### LogbookEntry

A LogbookEntry includes:
* Flight Summary Info
  * Flight number/index (perhaps, though maybe not visible to the user?)
  * Date, Time
  * Duration
  * Max/min values (alt, climbrate, speed, etc)
* GPSTrack File Link/Reference (if chosen to save)
  * A link to this separate GPSTrack File will be one of the fields in the LogbookEntry.  The GPSTrack File will be a separate IGC file (perhaps additional formats supported in the future)

__NOTE ON GPS REQUIREMENT:__ While the GPSTrack file strictly requires a GPS fix, the LogbookEntry itself does not strictly need GPS data.  However, certain GPS information (date, time, launch location, etc) make the LogbookEntry more informative and robust.  While we anticipate usually having a GPS fix when logging, the Logbook (and each Entry) should function to a minimum level without GPS.

### Logbook

The __Logbook__ includes the collection of all __LogbookEntries__, as well as possible summations/totals of the entries, such as:
* Overall Date Range (earliest entry and latest entry)
* Total Flight Time
* Total Number of Flights
* Etc 

## Logbook User Experience

Leaf users will have a few ways of interacting with the logbook
* Viewing LogbookEntries on the Leaf display (scrolling through each previous flight in [reverse] chronological order)
  * Perhaps the option to delete an entry (typically for when it was made in error; for example accidentally starting the timer)

* Exporting the Logbook into a user-friendly format like JSON or CSV for viewing on computer in a spreadsheet, etc.

* (perhaps) Web page served by Leaf

* (perhaps) uploading a LogbookEntry over wifi to an online repository (for examples: leonardo or XCcontest)






### Current Logbook Status
Presently (8/18/2025) Leaf will only show a brief flight summary after a flight has ended (timer stopped).  Once that summary page is dismissed, it is no longer accessible.  Additionally, a GPSTrack file can be saved.  

There are no other logbook functions currently supported.  The following content is an attempt to sketch out what such additional features could look like.

### Logbook Menu
The "Log/Timer" Menu should likely be changed to "Logging" or similar.  This can incorporate both Logbook settings as well as accessing the Logbook of previously-logged flights.

The revised Logbook menu might look like this:

<pre>
___/LOGGING\______
--Settings--------
Auto Start    ☐
Auto Stop     ☐
Save GPSTrack ☐
Format     IGC
------------------
Previous Flights➡
Export Logbook  ➡
------------------
Back            ⏎
</pre>

The __Settings__ would work as they do currently.

__"Previous Flights"__ (or perhaps "View Logbook") would take you into a minimalistic viewer on the Leaf display, where you would essentially see the "Flight Summary" page of the most recently logged flight (including date, time, etc so you could tell which flight that was).  Scrolling left/right would take you between logged flights.  
* Possibly, the first thing you see when selecting "previous flights" might be an overall logbook summary, with total flights, total flight time, etc etc. 

__"Export Logbook"__ would take the JSON logbook stored inside Leaf (or more likely on the SD card) and export a CSV file that would be easily human-readable & editable in a spreadsheet application.

### Flight Summary Page

The (while-viewing-logbook) Flight Summary Page might look something like this.  Perhaps there's an "erase flight" option to clean up the logbook (in case you accidentally started the timer and/or saved a really short non-flight flight) 

<pre>
____/LOGBOOK\_____
Flight      X of Y 
11:45am Mo/Dy/Year
Duration:  01h:55m
MaxAlt:     5,590'
AboveLaunch:1,590'
MaxClimb: 1,230fpm
------------------
Erase Flight    ➡
------------------
⇦Prev|Back⏎|Next⇨
</pre>

# G-load diagnostic IGC records

The g-load experiment writes three Leaf manufacturer comment (`LXLF`) records per logging
update (approximately once per second), immediately after the PR's airborne detector updates.
These records use device milliseconds so detector checks can be replayed without GPS timestamp
rounding. They continue during GPS outages once an IGC file is open, including ground and takeoff
exclusion periods. GPS B records and the existing flight detection / filtering are unchanged.

The file header includes `LXLFGLCFG V=1 SLEW=3.0 AVG=10 IGNORE=15000 BUCKET=30000 LAND=5`.
The settings describe slew in g/s, averaging samples, launch exclusion and pending bucket duration
in milliseconds, and consecutive ground checks to declare landing.

Example diagnostic group:

```text
LXLFGL T=123450 S=3.580 C=-32 SV=1 CV=1 FS=1 VC=0 L=1
LXLFGA T=123450 RT=123400 FT=123400 R=1.020 F=1.080 RV=1 FV=1
LXLFGS T=123450 ON=1 A=1 G=0 I=0 P=1 B=120000 E=1
```

- Common `T`: detector-check time in milliseconds since boot (wraps with the device clock).
- `GL`: detector inputs. `S` is ground speed in m/s, rounded to three decimals; `C` is the exact
  filtered climb in cm/s. `SV` / `CV` indicate usable speed / climb inputs. `FS` is the actual
  speed-above-8-mph result; `VC` is the actual absolute-climb-above-50-cm/s result. `L` is their
  OR result, before landing debounce. Replay using these flags, not rounded `S`.
- `GA`: `R` is the current `imu.getAccel()` magnitude in g; `F` is `gLoad().filtered()` in g.
  `RT` / `FT` are their respective latest sample timestamps, allowing stale values to be identified.
  `RV` is IMU acceleration validity; `FV` means the filter window has produced a value. `FV` does
  not guarantee freshness: compare `T` and `FT` on hardware. Unavailable values are `NA`;
  an acceleration too large for the fixed-width diagnostic field is `OVF`.
- `GS`: state after the detector check. `ON` is tracker activity; `A` is debounced airborne state;
  `G` is the consecutive ground-check count; `I` is remaining launch exclusion in milliseconds
  on the IMU sample clock. `P` indicates a pending bucket; `B` is its start timestamp (0 without
  a bucket); `E` indicates that committed extrema exist. `P` remains 0 until an eligible IMU
  sample starts a bucket. Older committed extrema survive landing and relaunch within one log.

These are once-per-second snapshots, so raw acceleration spikes between checks can still be
missed. The records are diagnostic comments and require a reader that understands this format.

The host regression test is `sim/tests/g_load_diagnostics.cpp` (compile with `-Isrc/vario`).
For an end-to-end check, run the simulator with `sim/recordings/g-load-diagnostics.json`,
`sim/scripts/g-load-diagnostics.txt`, `--accept-warning --setting AUTO_STOP=0 --setting TRACK_SAVE=1`,
and fresh SD/state directories. Run `python sim/tests/check_g_load_igc.py <generated.igc>` afterward.
It verifies complete 1 Hz groups, detector-state replay, landing/relaunch, and continued records
through the script's GPS outage.
