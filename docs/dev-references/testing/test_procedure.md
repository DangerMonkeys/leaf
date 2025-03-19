---
title: Firmware Test Procedure
description: How to test functionality before submitting a PR or Release
---

# Firmware Test Procedure
Follow these steps to test new firmware before submitting PR or building a release

## Initial Power Functions

| Action | Intended Result |
|------|-------|
|Immediately after uploading| Charging screen visible |
|Hold center button|Unit turns on to main thermal page|
|Unplug USB| Unit stays on|
|Hold center button|Unit powers off|
|Plug in USB|Charging screen visible|

## Vario Functions
Precondition: Ensure vario volume is unmuted and quiet mode is off / unchecked

    * In main menu (far right screen), go to Vario page
        * "Beep Vol" -> not X
        * "QuietMode" -> unchecked

| Action | Intended Result |
|------|-------|
|Raise unit| climb beeps and positive climb rate|
|Lower unit| negative climb rate (and sink beeps if sink alarm on & triggered)|
|Hold unit steady| zero (or near-zero) climb rates|
|For remainder of testing| climb rates seem appropriate. (unexpected crazy spikes indicate timing and/or memory issues)|

## Timer and Log functions
| Action | Intended Result |
|------|-------|
|From main page, press UP to select timer then CENTER to start|Timer Starts counting up (will be flashing if no GPS fix and/or no SD card; solid if fix AND SD card)|
|Press UP to select timer then **HOLD** CENTER to stop|Timer Stops, Flight Summary Page displayed|
|Press CENTER|Summary Page is dismissed|

## DONE!