/*



Legend:
->  this is a priority
%   this is some percent implemented / already started
*#N* -- this is now listed as #N in GitHub issues list

Infrastructure Stuff
*#6* ->USM Mass storage for sdcard access when plugged into computer
*#4* OTA updates, or at least non-IDE updates
  More precise partition and memory plan (put fonts and stuff elsewhere?  Partition for OTA updates?
How many waypoints/routes can we store in NVM?  etc)
*#7* Power Saving / Sleep functions during operation (and can we do PWM speaker during sleep?)
  Wifi
*#8*  Bluetooth



GPS Logging
*#5* Additional file formats for logs (gpx, IGC, KML-with-timestamps, etc)

UI
*#9* ->Additional menu screens (choose GPX file to load; list of waypoints and routes; possibly
create/save/edit waypoint and create/save/edit route) Final touches on Thermal Page & Nav Page (need
to show "NO gps" when don't have fix for various fields) User editable fields on various pages Debug
screen?  / Advanced options?


Features and Performance
*#10* ->Wind estimate based on GPS speed/bearing vectors
->Improve tone updates (hold pitch for at least a few samples so it doesn't sound so 'wandering')
->Better altitude filter to reduce noise but speed up reaction time
*#13* ->Include accelerometer in vario signal
% Keep a running/filtered average of speed, glide, and climb, to use in various calculations
(especially time to intercept etc)
*#11* % Full logbook support (min/max/total values from each flight.  Where to save?)
% Fine tune ambient temp sensor (it's about 5C high because of self heating of the board, but test
over various ranges and with varied airflow)
*#12*  Zen mode (no beeping until flying)
  Various tone-modes (change pitch of beeping; making beeping follow major and minor scales for
climb/sink, etc) Possibly update parts of the display more often than every 0.5sec (like the
graphical bars and needles etc).  But keep text to 0.5sec or it's too hard to read as it changes
quickly Support mag and maybe gyro angles; need to calibrate and test how it works at various
installation angles





Bugs and obvservations
* GPS time shows valid and 4:59pm on boot up, and even keeps time (incrementing minutes), but is
incorrect until later (better signal?)
* Can't always reprogram even though ECO_MODE is turned off (no sleep)



*/