#ifndef settings_h
#define settings_h

#include <Arduino.h>

// Setting bounds and definitions
  // Vario 
    // Sink Alarm
      #define SINK_ALARM_MAX 	  	   -6
      #define SINK_ALARM_MIN		 	   -2
    // Vario Sensitivity
      #define VARIO_AVERAGE_MAX 	    6			// units of 1/2 seconds, so 3sec max
      #define VARIO_AVERAGE_MIN	      1			// units of 1/2 seconds, so .5sec min
    // Lifty Air Thermal Sniffer
      #define LIFTY_AIR_MAX			    -80		  // max sinkrate to begin lifty air (so -.8m/s sinkrate and higher will trigger)
    // Climb settings
      #define CLIMB_AVERAGE_MAX		    3			// units of 10 seconds, so max 30 sec averaging
      #define CLIMB_START_MAX   		 50			// cm/s when climb note begins 
  // System
    // Display Contrast
      #define CONTRAST_MAX		        14		// TODO: find max reasonable value for display we're using 
      #define CONTRAST_MIN		        4		  // TODO: find min reasonable value for display we're using
    // Volume (max for both vario and system volume settings)
      #define VOLUME_MAX			 	      3
    // Time Zone Offsets from UTC
      #define TIME_OFFSET_MIN			    -120
      #define TIME_OFFSET_MAX		 	    140

// Default Settings
  // Default Vario Settings  
    #define DEF_SINK_ALARM		    	 -4	    // m/s sink
    #define DEF_VARIO_AVERAGE 		    2	    // in half-seconds.  1s seems to be good, so use value of 2 for default
    #define DEF_CLIMB_AVERAGE			    1	    // in units of 5-seconds.  (def = 1 = 5sec)
    #define DEF_CLIMB_START   		    5			// cm/s when climb note begins 
    #define DEF_VOLUME_VARIO		      1	    // 0=off, 1=low, 2=med, 3=high
    #define DEF_LIFTY_AIR				    -40	    // In units of cm/s (a sink rate of 30cm/s means the air itself is going up).  '0' is off.  (lift air will apply from the lifty_air setting up to the climb_start value)
    #define DEF_ALT_OFFSET				    0	    // cm altitude offset from pressure altitude
  // Default GPS & Track Log Settings
    #define DEF_DISTANCE_FLOWN			  0	    // 0 = xc distance, 1 = path distance
    #define DEF_GPS_SETTING           1	    // 0 = GPS off, 1 = GPS on, 2 = power save every N sec, etc
    #define DEF_TRACK_SAVE				    1	    // save track log?
    #define DEF_AUTO_START		        1	    // 1 = ENABLE, 0 = DISABLE
  // Default System Settings
    #define DEF_TIME_ZONE     				0     // HHh  (-070 for PDT, +115 for india (11 hours, 30 minutes))
    #define DEF_VOLUME_SYSTEM		      1	    // 0=off, 1=low, 2=med, 3=high
    #define DEF_CONTRAST			        10
    #define DEF_ENTER_BOOTLOAD		    0	    // by default, don't enter bootloader on reset		      
    #define DEF_AUTO_OFF				      1	    // 1 = ENABLE, 0 = DISABLE
  // Default Unit Values
    #define DEF_UNITS_climb				    0	    // 0 (m per second), 	1 (feet per minute)
    #define DEF_UNITS_alt				      0	    // 0 (meters), 				1 (feet)
    #define DEF_UNITS_temp				    0	    // 0 (celcius), 			1 (fahrenheit)
    #define DEF_UNITS_speed 			    0	    // 0 (kph), 				  1 (mph)
    #define DEF_UNITS_heading			    0	    // 0 (342 deg), 		  1 (NNW)
    #define DEF_UNITS_distance			  0	    // 0 (km, or m for <1km), 	1 (miles, or ft for < 1000 feet)
    #define DEF_UNITS_hours				    1	    // 0 (24-hour time),  1 (12 hour time), 		    


// Global Variables for Current Settings
  // Vario Settings    
    extern int8_t SINK_ALARM;
    extern int8_t VARIO_AVERAGE;
    extern int8_t CLIMB_AVERAGE;
    extern int8_t CLIMB_START;
    extern int8_t VOLUME_VARIO;
    extern int8_t LIFTY_AIR;
    extern int32_t ALT_OFFSET;
  // GPS & Track Log Settings
    extern bool DISTANCE_FLOWN;
    extern int8_t GPS_SETTING;
    extern bool TRACK_SAVE;
    extern bool AUTO_START;
  // System Settings
    extern int16_t TIME_ZONE;
    extern int8_t VOLUME_SYSTEM;
    extern int16_t CONTRAST;
    extern bool ENTER_BOOTLOAD;
    extern bool AUTO_OFF;
  // Unit Values
    extern bool UNITS_climb;
    extern bool UNITS_alt;
    extern bool UNITS_temp;
    extern bool UNITS_speed;
    extern bool UNITS_heading;
    extern bool UNITS_distance;
    extern bool UNITS_hours;

// manage-settings functions
void settings_init(void);
void settings_loadDefaults(void);
void settings_reset(void);
void settings_save(void);
void settings_retrieve(void);
void factoryResetVario(void);


// adjust-settings functions
void settings_adjustContrast(uint16_t val);
void settings_setAltOffset(int32_t value);
void settings_adjustAltOffset(int8_t dir, uint8_t count);
void settings_adjustSinkAlarm(int8_t dir);
void settings_adjustVarioAverage(int8_t dir);
void settings_adjustClimbAverage(int8_t dir);
void settings_adjustClimbStart(int8_t dir);
void settings_adjustLiftyAir(int8_t dir);
void settings_adjustVolumeVario(int8_t dir);
void settings_adjustVolumeSystem(int8_t dir);

void settings_toggleUnits(bool * unitSetting);

/*
// Timer/Stopwatch Variable
extern unsigned long TIMER;				// GLOBAL
extern unsigned char TIMER_RUNNING;		// GLOBAL


// Waypoint Input and Edit Variables
extern char WYPT_tempName[];			// 8 char string name plus end character
extern signed char WYPT_tempAltSign;
extern unsigned long WYPT_tempAlt;
extern unsigned char WYPT_tempAltUnits;
extern signed char WYPT_tempLatSign;
extern unsigned long WYPT_tempLatDeg;
extern unsigned long WYPT_tempLatFrac;
extern signed char WYPT_tempLongSign;
extern unsigned long WYPT_tempLongDeg;
extern unsigned long WYPT_tempLongFrac;
extern unsigned char WYPT_editCursor;

enum settings_units {units_alt, units_climb, units_speed, units_distance, units_heading, units_temp, units_hours};

// Alt offset




void settings_init(void);
void settings_adjustContrast(signed char dir);

void settings_resetToDefaults(void);
void settings_readSettings(void);
void settings_saveSettings(void);

void settings_gps_cycle(void);

void settings_altOffset(signed char dir, unsigned int count, signed long value);

void settings_sinkAlarm_cycle(void);
void settings_varioAverage_cycle(void);
void settings_climbAverage_cycle(void);
void settings_volume_cycle(void);
void settings_liftyAir_cycle(void);
void settings_track_cycle(void);


void settings_units_cycle(unsigned char whichUnits);
void settings_timeOffset(signed char dir, unsigned int count);
void settings_timerAutoStart_cycle(void);
void settings_powerAutoOff_cycle(void);

unsigned char settings_cycle_1and0(unsigned char input);

//waypoint menu/subpages
void settings_waypointEnter(signed char enteredFrom);		// get into the waypoint sub-screens
void settings_waypointExit(void);							// leave waypoint sub-screens
void settings_waypointSelect(void);							// handle a selected waypoint
void settings_waypointListScroll(signed char dir);			// scroll the waypoint list

// editing waypoints
void settings_waypointEdit_shift(void);
void settings_waypointEdit_inc(signed char dir);
signed long settings_cycleDigit(signed long val, char place, signed char dir);





// Default Settings on First Boot Up

  // Pargliding Vario
    
    
  

  // Off Roading
    uint8_t rollover_max_fore
    uint8_t rollover_max_rear
    uint8_t rollover_max_left
    uint8_t rollover_max_right
    uint8_t rollover_warning        // degrees before max to receive warnings

      

  // Aircraft 
    //


  // Data Logger
    //



    //


*/










#endif