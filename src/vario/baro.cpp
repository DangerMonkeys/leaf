/*
 * baro.cpp
 *
 */
#include "baro.h"
#include "speaker.h"
#include "LinearRegression.h"
#include "settings.h"
#include "Leaf_I2C.h"
#include "TempRH.h"

#define DEBUG_BARO 0  // flag for printing serial debugging messages

// Filter values to average/smooth out the baro sensor

  // User Settings for Vario
  #define FILTER_VALS_MAX           20                    // total array size max; for both altitude and climb 
  #define ALTITUDE_FILTER_VALS_PREF 20                    // user setting for how many altitude samples to filter over, from 1 to 20
  #define CLIMB_FILTER_VALS_PREF    20                    // how many climb rate values to average over 
  int32_t altitudeFilterVals[FILTER_VALS_MAX+1]; // use [0] as the index / bookmark
  int32_t climbFilterVals[FILTER_VALS_MAX+1];       // use [0] as the index / bookmark
  
  // LinearRegression to average out noisy sensor readings
  LinearRegression<ALTITUDE_FILTER_VALS_PREF> alt_lr;
  
  // probably gonna delete these
    #define VARIO_SENSITIVITY 3 // not sure what this is yet :)
    #define CLIMB_AVERAGE 1
    #define PfilterSize		6			// pressure alt filter values (minimum 1, max 10)
    int32_t varioVals[25];
    int32_t climbVals[31];
    int32_t climbSecVals[9];

// Baro Values
  float baroAltimeterSetting = 29.920;

  int32_t CLIMB_RATE = 0;
  int32_t CLIMB_RATEfiltered = 0;      
  int32_t VARIO_RATEfiltered = 0;

  int32_t TEMP = 0;
  int32_t TEMPfiltered = 0;
  int32_t PRESSURE = 0;
  int32_t PRESSUREfiltered = 0;

  int32_t P_ALT = 0;
  int32_t P_ALTfiltered = 0;
  int32_t P_ALTregression = 0;
  int32_t P_ALTinitial = 0;
  int32_t P_ALTlaunch = 0;
  int32_t lastAlt = 0;
  int32_t P_ALT_launch = 0; // value to store for beginning of flight.  At first, this will be set to the altitude when the vario is turned on.  If the flight timer is started, then the altitude is reset to current when timer starts.

// Sensor Calibration Values (stored in chip PROM; must be read at startup before performing baro calculations)
  uint16_t C_SENS;
  uint16_t C_OFF;
  uint16_t C_TCS;
  uint16_t C_TCO;
  uint16_t C_TREF;
  uint16_t C_TEMPSENS;

// Digital read-out values
  uint32_t D1_P;								//  digital pressure value (D1 in datasheet)
  uint32_t D1_Plast = 1000;     //  save previous value to use if we ever get a mis-read from the baro sensor (initialize with a non zero starter value)
  uint32_t D2_T;								//  digital temp value (D2 in datasheet) 
  uint32_t D2_Tlast = 1000;     //  save previous value to use if we ever get a mis-read from the baro sensor (initialize with a non zero starter value)


// Temperature Calculations
  int32_t dT;

// Compensation Values
  int64_t OFF1;        // Offset at actual temperature
  int64_t SENS1;       // Sensitivity at actual temperature

  // Extra compensation values for lower temperature ranges
  int32_t TEMP2;
  int64_t OFF2;
  int64_t SENS2;







//*******************************************
// fake stuff for testing

int32_t fakeAlt = 0;
int32_t fakeClimbRate = 0;
int32_t fakeVarioRate = 0;
int32_t change = 1;

//143 test values
int32_t altitude_values[] = {
  1,  // index pointer to value place
5225	,
5402	,
5528	,
5605	,
5631	,
5609	,
5540	,
5426	,
5270	,
5076	,
4849	,
4593	,
4316	,
4022	,
3720	,
3418	,
3123	,
2843	,
2589	,
2368	,
2190	,
2063	,
1997	,
1999	,
2075	,
2234	,
2480	,
2816	,
3246	,
3771	,
4389	,
5098	,
5893	,
6767	,
7710	,
8712	,
9760	,
10838	,
11930	,
13018	,
14082	,
15103	,
16062	,
16940	,
17717	,
18378	,
18910	,
19301	,
19544	,
19636	,
19580	,
19383	,
19057	,
18621	,
18097	,
17516	,
16910	,
16314	,
15769	,
15312	,
14983	,
14818	,
14848	,
15096	,
15579	,
16302	,
17259	,
18431	,
19785	,
21277	,
22851	,
24442	,
25978	,
27385	,
28594	,
29540	,
30173	,
30462	,
30399	,
30000	,
29311	,
28406	,
27384	,
26362	,
25467	,
24827	,
24556	,
24744	,
25437	,
26636	,
28284	,
30267	,
32420	,
34542	,
36418	,
37844	,
38658	,
38772	,
38194	,
37040	,
35532	,
33975	,
32714	,
32076	,
32307	,
33505	,
35580	,
38246	,
41050	,
43461	,
44982	,
45287	,
44340	,
42449	,
40235	,
38491	,
37955	,
39050	,
41680	,
45171	,
48434	,
50339	,
50187	,
48114	,
45176	,
42996	,
43018	,
45674	,
49931	,
53646	,
54727	,
52582	,
48847	,
46593	,
48143	,
52937	,
57430	,
57848	,
53967	,
50113	,
51220	,
57001	,
61178	
};

// end fake stuff for testing

  
  void baro_updateFakeNumbers(void) {
    fakeAlt = (float)(100 * change);
    change *= 10;
    if (change >= 1000000) {
      change = -1;
    } else if (change <= -100000) {
      change = 1;
    }
  }
// Test Functions



  void baro_adjustAltOffset(int8_t dir, uint8_t count) {
    float increase = .001;              //     
    if (count >= 1) increase *= 5;
    if (count >= 8) increase *= 4;


    if (dir >= 1) baroAltimeterSetting += increase;
    else if (dir <= -1) baroAltimeterSetting -= increase;	
  }


// Get values (mainly for display and log purposes)
  int32_t baro_getTemp() {           return TEMPfiltered; }
  int32_t baro_getAlt() {            return P_ALTfiltered; }
  int32_t baro_getOffsetAlt() {      return P_ALTfiltered + ALT_OFFSET; }
  int32_t baro_getAltAtLaunch() {    return P_ALT_launch; }
  int32_t baro_getAltAboveLaunch() { return baro_getAlt() - P_ALT_launch; }
  int32_t baro_getAltInitial() {     return P_ALTinitial; }
  int32_t baro_getClimbRate() {      return CLIMB_RATEfiltered; }   // actual climb rate, for display on screen numerically, and saving in flight log
  int32_t baro_getVarioBar() {       return fakeVarioRate; }        // climb rate for vario var visuals and perhaps sound.  This is separate in case we want to average/filter it differently
// Get values (mainly for display and log purposes)

//Conversion functions to change units
  int32_t baro_altToUnits(int32_t alt_input, bool units_feet) {

    if (units_feet) alt_input = alt_input *100 / 3048; // convert cm to ft
    else alt_input /= 100;                             // convert cm to m

    return alt_input;
  }

  float baro_climbToUnits(int32_t climbrate, bool units_fpm) {
    float climbrate_converted;
    if (units_fpm) {
      climbrate_converted = (int32_t)(climbrate * 197 / 1000 * 10);    // convert from cm/s to fpm (lose one significant digit and all decimal places)
    } else {
      climbrate = (climbrate + 5) / 10;           // lose one decimal place and round off in the process (cm->decimeters)
      climbrate_converted = (float)climbrate/10;  // Lose the other decimal place (decimeters->meters) and convert to float for ease of printing with the decimal in place
    } 
    return climbrate_converted;
  }


// I2C Communication Functions

  uint8_t baro_sendCommand(uint8_t command) {
    Wire.beginTransmission(ADDR_BARO);
    Wire.write(command);
    uint8_t result = Wire.endTransmission();
    //if (DEBUG_BARO) { Serial.print("Baro Send Command Result: "); Serial.println(result); }
    return result;
  }

  uint16_t baro_readCalibration(uint8_t PROMaddress) {
    uint16_t value = 0;				// This will be the final 16-bit output from the ADC
    uint8_t command = 0b10100000;	// The command to read from the specified address is 1 0 1 0 ad2 ad1 ad0 0
    command += (PROMaddress << 1);		// Add PROM address bits to the command byte

    baro_sendCommand(command);
    Wire.requestFrom(ADDR_BARO, 2);
    value += (Wire.read() << 8);
    value += (Wire.read());

    return value;
  }

  uint32_t baro_readADC() {
    uint32_t value = 0;				// This will be the final 24-bit output from the ADC
    //if (DEBUG_BARO) { Serial.println("Baro sending Read ADC command"); }
    baro_sendCommand(0b00000000);
    Wire.requestFrom(ADDR_BARO, 3);
    value += (Wire.read() << 16);
    //if (DEBUG_BARO) { Serial.print("Baro ADC Value 16: "); Serial.println(value); }
    value += (Wire.read() << 8);
    //if (DEBUG_BARO) { Serial.print("Baro ADC Value 8: "); Serial.println(value); }
    value += (Wire.read());
    //if (DEBUG_BARO) { Serial.print("Baro ADC Value 0: "); Serial.println(value); }

    return value;
  }
// I2C Communication Functions



// Device Management

  //Initialize the baro sensor
  void baro_init(void)
  {  
    // probably don't need these
          uint8_t i=0;
        // initialize averaging arrays
          for (i=0;i<31;i++) {
            if (i<25) varioVals[i] = 0;
            if (i<9) climbSecVals[i]=0;    
            climbVals[i] = 0;
          }

    // reset baro sensor for initialization
      baro_reset();
      delay(2);

    // read calibration values
      C_SENS = baro_readCalibration(1);    
      C_OFF = baro_readCalibration(2);    
      C_TCS = baro_readCalibration(3);    
      C_TCO = baro_readCalibration(4);    
      C_TREF = baro_readCalibration(5);    
      C_TEMPSENS = baro_readCalibration(6);
      
    // after initialization, get first baro sensor reading to populate values
      baro_update(1, true);   // send convert-pressure command
      delay(10);					    // wait for baro sensor to process
      baro_update(0, true);   // read pressure, send convert-temp command
      delay(10);					    // wait for baro sensor to process
      baro_update(0, true);   // read temp, and calculate adjusted altitude

      // initialize all the other reference variables with current altitude to start
      lastAlt = P_ALT;		          // assume we're stationary to start (previous Alt = Current ALt, so climb rate is zero)
      P_ALTfiltered = P_ALT;			  // filtered value should start with first reading
      P_ALTinitial = P_ALT;	        // also save first value to use as starting point    
      P_ALT_launch = P_ALT;         // save the starting value as launch altitude (this will be updated when timer starts)
      P_ALTregression = P_ALT;

    // load the filter with our current start-up altitude
      for (int i = 1; i <= FILTER_VALS_MAX; i++) {
        altitudeFilterVals[i] = P_ALT;
        climbFilterVals[i] = 0;
      }
      // and set bookmark index to 1
      altitudeFilterVals[0] = 1;
      climbFilterVals[0] = 1;
    
    //fakeAlt = altitude_values[altitude_values[0]];
    //lastAlt = fakeAlt;

    baro_update(0, true);  

    //alt_lr.update((double)millis(), (double)P_ALTinitial);
    
    if(DEBUG_BARO) {
      Serial.println("Baro initialization values:");
      Serial.print("  C_SENS:"); Serial.println(C_SENS);
      Serial.print("  C_OFF:"); Serial.println(C_OFF);
      Serial.print("  C_TCS:"); Serial.println(C_TCS);
      Serial.print("  C_TCO:"); Serial.println(C_TCO);
      Serial.print("  C_TREF:"); Serial.println(C_TREF);
      Serial.print("  C_TEMPSENS:"); Serial.println(C_TEMPSENS);
      Serial.print("  D1:"); Serial.println(D1_P);
      Serial.print("  D2:"); Serial.println(D2_T);
      Serial.print("  dT:"); Serial.println(dT);
      Serial.print("  TEMP:"); Serial.println(TEMP);
      Serial.print("  OFF1:"); Serial.println(OFF1);
      Serial.print("  SENS1:"); Serial.println(SENS1);
      Serial.print("  P_ALT:"); Serial.println(P_ALT);
      
      Serial.println(" ");
    }
  }

  void baro_reset(void) {
    unsigned char command = 0b00011110;	// This is the command to reset, and for the sensor to copy calibration data into the register as needed
    baro_sendCommand(command);
    delay(3);						                // delay time required before sensor is ready
  }

  // Reset launcAlt to current Alt (when starting a new log file, for example)
  void baro_resetLaunchAlt() {
    P_ALT_launch = baro_getAlt();
  }

  uint32_t baroTimeStampPressure = 0;
  uint32_t baroTimeStampTemp = 0;
  uint32_t baroADCStartTime = 0;
  uint8_t process_step = 0;
  bool baroADCBusy = false;
  bool baroADCPressure = false;
  bool baroADCTemp = false;

  char baro_update(bool startNewCycle, bool doTemp) {    // (we don't need to update temp as frequently, so we can skip it most of the time)
    // the baro senor requires ~9ms between the command to prep the ADC and actually reading the value.
    // Since this delay is required between both pressure and temp values, we break the sensor processing 
    // up into several steps, to allow other code to process while we're waiting for the ADC to become ready.
    
    // First check if ADC is not busy (i.e., it's been at least 9ms since we sent a "convert ADC" command)
    if (micros() - baroADCStartTime > 9000) baroADCBusy = false;

    if (startNewCycle) process_step = 0;

    if (DEBUG_BARO) {
      Serial.print("baro step: ");
      Serial.print(process_step);
      Serial.print(" NewCycle? ");
      Serial.print(startNewCycle);
      Serial.print(" time: ");
      Serial.println(micros());
    }

    switch (process_step) {
      case 0:     // SEND CONVERT PRESSURE COMMAND
        if (!baroADCBusy) {
          baroADCStartTime = micros();
          baro_sendCommand(CMD_CONVERT_PRESSURE); // Prep baro sensor ADC to read raw pressure value (then come back for step 2 in ~10ms)        
          baroADCBusy = true;                     // ADC will be busy now since we sent a conversion command
          baroADCPressure = true;                 // We will have a Pressure value in the ADC when ready
          baroADCTemp = false;                    // We won't have a Temp value (even if the ADC was holding an unread Temperature value, we're clearning that out since we sent a Pressure command)
        }
        break;

      case 1:     // READ PRESSURE THEN SEND CONVERT TEMP COMMAND
        if (!baroADCBusy && baroADCPressure) {          
          D1_P = baro_readADC();                    // Read raw pressure value  
          baroADCPressure = false;
          //baroTimeStampPressure = micros() - baroTimeStampPressure; // capture duration between prep and read
          if (D1_P == 0) D1_P = D1_Plast;           // use the last value if we get an invalid read
          else D1_Plast = D1_P;                     // otherwise save this value for next time if needed          
          //baroTimeStampTemp = micros();
        
          if (doTemp) {
            baroADCStartTime = micros();
            baro_sendCommand(CMD_CONVERT_TEMP);     // Prep baro sensor ADC to read raw temperature value (then come back for step 3 in ~10ms)
            baroADCBusy = true;
            baroADCTemp = true;                     // We will have a Temperature value in the ADC when ready
            baroADCPressure = false;                // We won't have a Pressure value (even if the ADC was holding an unread Pressure value, we're clearning that out since we sent a Temperature command)
          }
        }
        break;

      case 2:     // READ TEMP THEN CALCULATE ALTITUDE
        if (doTemp) {
          if (!baroADCBusy && baroADCTemp) {  
            D2_T = baro_readADC();						        // read digital temp data
            baroADCTemp = false;
            //baroTimeStampTemp = micros() - baroTimeStampTemp; // capture duration between prep and read
            if (D2_T == 0) D2_T = D2_Tlast;             // use the last value if we get a misread
            else D2_Tlast = D2_T;                       // otherwise save this value for next time if needed
          }
        }
        // (even if we skipped some steps above because of mis-reads or mis-timing, we can still calculate a "new" altitude based on the old ADC values.  It will be a repeat value, but it keeps the filter buffer moving on time)
        P_ALT = baro_calculateAlt();                // calculate Pressure Altitude in cm
        break;

      case 3:     // FILTER VALUES TO AVERAGE OUT THE NOISE
        baro_filterALT();							              // filter pressure alt value
        baro_updateClimb();							            // update and filter climb rate
        if (DEBUG_BARO) baro_debugPrint();
        break;   
    }
    process_step++;
    //if(++process_step > 4) process_step = 0;  // prep for the next step in the process (if we just did step 4, we're done so set to 0.  Elsewhere, Interrupt timer will set to 1 again eventually)  
    return (process_step - 1); //return what step was just completed
  }
// Device Management 


// Device reading & data processing  
  int32_t FloatAltCMinHg;
  int32_t FloatAltCMinHgTemp;

  int32_t baro_calculateAlt() {
    // calculate temperature (in 100ths of degrees C, from -4000 to 8500)
      dT = D2_T - ((int32_t)C_TREF)*256;
      TEMP = 2000 + (((int64_t)dT)*((int64_t)C_TEMPSENS))  /  pow(2,23);
      
    // calculate sensor offsets to use in pressure & altitude calcs
      OFF1  = (int64_t)C_OFF*pow(2,16) + (((int64_t)C_TCO) * dT)/pow(2,7);
      SENS1 = (int64_t)C_SENS*pow(2,15) + ((int64_t)C_TCS * dT) /pow(2,8);
    
    // low temperature compensation adjustments
      TEMP2 = 0;
      OFF2 = 0;
      SENS2 = 0;
      if (TEMP < 2000) {      
        TEMP2 = pow((int64_t)dT,2) / pow(2,31);
        OFF2  = 5*pow((TEMP - 2000),2) / 2;
        SENS2 = 5*pow((TEMP - 2000),2) / 4; 
      }
      // very low temperature compensation adjustments
      if (TEMP < -1500) {          
        OFF2  = OFF2 + 7 * pow((TEMP + 1500),2);
        SENS2 = SENS2 + 11 * pow((TEMP +1500),2) / 2; 
      }
      TEMP = TEMP - TEMP2;
      OFF1 = OFF1 - OFF2;
      SENS1 = SENS1 - SENS2;

    //Filter Temp if necessary due to noise in values
      TEMPfiltered = TEMP;    //TODO: actually filter if needed	

    // calculate temperature compensated pressure (in 100ths of mbars)
      PRESSURE = ((uint64_t)D1_P * SENS1 / (int64_t)pow(2,21) - OFF1)/pow(2,15);

    // calculate pressure altitude in cm
    int32_t FloatAltCM = 4433100.0*(1.0-pow((float)PRESSURE/101325.0,(.190264)));   // TODO  check accuracy of this, seems to be off by ~900 ft over 10500 feet
    
// calculate pressure altitude in cm
    FloatAltCMinHg = 4433100.0*(1.0-pow((float)PRESSURE/(baroAltimeterSetting*3386.389),(.190264)));   // TODO  check accuracy of this, seems to be off by ~900 ft over 10500 feet

    FloatAltCMinHgTemp = (pow(((3386.389*baroAltimeterSetting)/(float)PRESSURE),(1/5.257))-1)*(tempRH_getTemp()+273.15)/0.0065;

    /*
    double FloatAltDouble = 4433100.0*(1.0-pow((double)PRESSURE/101325.0,(.190264)));   // TODO  check accuracy of this, seems to be off by ~900 ft over 10500 feet
    Serial.print(FloatAltCM);
    Serial.println(" float ");
    Serial.print(FloatAltDouble);
    Serial.println(" double ");
    Serial.println(" ");
    */

    return FloatAltCM;
  }

  uint8_t fakeAltCounter = 0;

  void baro_filterALT(void) {  

    // new way with regression:
      alt_lr.update((double)millis(), (double)P_ALT);
      LinearFit fit = alt_lr.fit();
      P_ALTregression = linear_value(&fit, (double)millis());

    
    //old way with averaging last N values equally:
      P_ALTfiltered = 0;
      int8_t filterBookmark = altitudeFilterVals[0];       // start at the saved spot in the filter array
      int8_t filterIndex = filterBookmark;                 // and create an index to track all the values we need for averaging

      altitudeFilterVals[filterBookmark] = P_ALT;          // load in the new value at the bookmarked spot
      if (++filterBookmark >= FILTER_VALS_MAX)    // increment bookmark for next time
        filterBookmark = 1;                                // wrap around the array for next time if needed
      altitudeFilterVals[0] = filterBookmark;              // and save the bookmark for next time

      // sum up all the values from this spot and previous, for the correct number of samples (user pref)
      for (int i = 0; i < ALTITUDE_FILTER_VALS_PREF; i++) {
        P_ALTfiltered += altitudeFilterVals[filterIndex];   
        filterIndex--;
        if (filterIndex <= 0) filterIndex = FILTER_VALS_MAX; // wrap around the array
      }
      P_ALTfiltered /= ALTITUDE_FILTER_VALS_PREF; // divide to get the average
    

    // temp testing stuff  
      fakeAlt = altitude_values[altitude_values[0]];
      if(++fakeAltCounter >=10) {
        fakeAltCounter = 0;
        altitude_values[0] = altitude_values[0] + 1;
        if (altitude_values[0] == 144) altitude_values[0] = 1;
        //baro_updateClimb();
      }

  }



  // Update Climb
  void baro_updateClimb() {
    //TODO: incorporate ACCEL for added precision/accuracy
    CLIMB_RATE = (P_ALTfiltered - lastAlt) * 20;	// climb is updated every 1/20 second, so climb rate is cm change per 1/20sec * 20
    lastAlt = P_ALTfiltered;								      // store last alt value for next time

    //filter climb rate
      CLIMB_RATEfiltered = 0;
      int8_t filterBookmark = climbFilterVals[0];       // start at the saved spot in the filter array
      int8_t filterIndex = filterBookmark;              // and create an index to track all the values we need for averaging

      climbFilterVals[filterBookmark] = CLIMB_RATE;     // load in the new value at the bookmarked spot
      if (++filterBookmark >= FILTER_VALS_MAX)    // increment bookmark for next time
        filterBookmark = 1;                             // wrap around the array for next time if needed
      climbFilterVals[0] = filterBookmark;              // and save the bookmark for next time

      // sum up all the values from this spot and previous, for the correct number of samples (user pref)
      for (int i = 0; i < CLIMB_FILTER_VALS_PREF; i++) {
        CLIMB_RATEfiltered += climbFilterVals[filterIndex];   
        filterIndex--;
        if (filterIndex <= 0) filterIndex = FILTER_VALS_MAX; // wrap around the array
      }
      CLIMB_RATEfiltered /= CLIMB_FILTER_VALS_PREF; // divide to get the average
    



    //fakeClimbRate = (fakeAlt - lastAlt) / 6;      // test value changes every 2 seconds, so climbrate needs to be halved
    //lastAlt = fakeAlt;
    
    //baro_filterCLIMB();									        // filter vario rate and climb rate displays
    
    speaker_updateVarioNoteSample(CLIMB_RATEfiltered);
  }

  /*
  void baro_filterCLIMB(void)
  {
    uint32_t sum = 0;
    unsigned char i = 0;

    //add new value to vario values
    varioVals[24]++;
    if (varioVals[24] > 4*VARIO_SENSITIVITY ||
      varioVals[24] >= 24) {
      varioVals[24] = 0;
    }
    varioVals[varioVals[24]] = CLIMB_RATE;


    for (i=0; i<4*VARIO_SENSITIVITY;i++) {
      sum += varioVals[i];
      //if (i>=24) break;						// just a safety check in case VARIO_SENS.. got set too high
    }
    VARIO_RATEfiltered = sum/(4*VARIO_SENSITIVITY);

    if (CLIMB_AVERAGE == 0) {
      CLIMB_RATEfiltered = VARIO_RATEfiltered;
    } else {
      // filter Climb Rate over 1 second...
      climbSecVals[8]++;
      if (climbSecVals[8] >= 8) {climbSecVals[8] = 0;}
      climbSecVals[climbSecVals[8]] = CLIMB_RATE;

      // ...and then every second, average over 0-30 seconds for the numerical display
      if (climbSecVals[8] == 0) {
        sum = 0;
        for (i=0; i<8;i++) {
          sum += climbSecVals[i];
        }

        climbVals[30]++;
        if (climbVals[30] >= CLIMB_AVERAGE*10) {climbVals[30] = 0;}
        climbVals[climbVals[30]] = sum/8;

        sum = 0;
        for (i=0; i<CLIMB_AVERAGE*10;i++) {
          sum += climbVals[i];
        }
        CLIMB_RATEfiltered = sum/(CLIMB_AVERAGE*10);
      }
    }
  /*
    // vario average setting is stored in 1/2 seconds, but vario samples come in every 1/8th second,
    // so multiply [1/2 sec] setting by 4, to get number of [1/8th rate] samples.
    VARIO_RATEfiltered = (VARIO_RATEfiltered * (4*VARIO_SENSITIVITY - 1) + CLIMB_RATE) / (4*VARIO_SENSITIVITY);  // filter by weighting old values higher

  }
  */
// Device reading & data processing  



// Test Functions

  char process_step_test = 0;

  void baro_debugPrint() {
    
    Serial.print("D1_P:");
    Serial.print(D1_P);
    Serial.print(", D2_T:");
    Serial.print(D2_T);         //has been zero, perhaps because GPS serial buffer processing delayed the ADC prep for reading this from baro chip
    
    Serial.print(" LastAlt:");
    Serial.print(lastAlt);// - P_ALTinitial);
    Serial.print(", ALT:");
    Serial.print(P_ALT);// - P_ALTinitial);
    Serial.print(", FILTERED:");
    Serial.print(P_ALTfiltered);// - P_ALTinitial);
    Serial.print(", REGRESSED:");
    Serial.print(P_ALTregression);// - P_ALTinitial);
    //Serial.print(", TEMP:");
    //Serial.print(TEMP);
    Serial.print(", CLIMB:");
    Serial.print(CLIMB_RATE);
    Serial.print(", CLIMB_FILTERED:");
    Serial.println(CLIMB_RATEfiltered);
  }

  void baro_test(void) {
    delay(10);  // delay for ADC processing bewteen update steps
    process_step_test = baro_update(process_step_test, true);
    if (process_step_test == 0) {
      process_step_test++;  
      Serial.print("PressureAltCm:");
      Serial.print(P_ALT);
      Serial.print(",");
      Serial.print("FilteredAltCm:");
      Serial.print(P_ALTfiltered);
      Serial.print(",");
      Serial.println(P_ALTregression);
    }
  }