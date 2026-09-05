
//    Included libraries and local files
#include <MillisTimer.h>  // millisecond timer
#include <ArduinoLowPower.h> // Arduino Low Power by Adafruit.  "Install All" if a warning to install dependencies pops up
#include <SPI.h>
#include <SdFat.h> // SdFat – Adafruit Fork by Bill Greiman. (note: SD.h doesn't put SD card to sleep right.  Try SdFat.h card writing header file. Using Arduino version
#include "sdios.h"
#include <Wire.h>
#include <time.h>
#include <RTClib.h>  // RTClib by Adafruit. works MUCH better than the finicky/difficult DS3231 library.  Also an Arduino supported lib
#include "MMUS.h"
#include "MMUSmessages.cpp"


//          SETUP            //
//          SETUP            //
//          SETUP            //
void setup() {

  // Begin I2C communication  (required for RTC)
  Wire.begin();
  Wire.setClock(400000); // Set the I2C clock to 400kHz.  Wire defaults to 100 kHz and the RTC handles 400kHz

  // set up ADC
  analogReadResolution( numADCbits );

  #ifdef ECHO_TO_SERIAL
    // initializing the serial monitor// THIS DOESN't REALLY WORK. Serial is uniquely defined and isn't initialized by initSerial and CAN'T RECOVER after sleep.  Struggled for hours on it.
    initSerial( Serial, serialPortSpeed );
    Serial.println(versionInfo);  // only print to serial port if enabled
  #endif // ECHO_TO_SERIAL


  // check RTC
  if (! rtc.begin()) error( 7 );     // set SOS flashing -- write to file & warns user but now RTC will be reset to last compile time so data won't stop

  // if lost power, then we have to reset it after flashing
  if (rtc.lostPower()) {
    error( 7 );
    rtcLostPowerErr = true;
    #ifdef ECHO_TO_SERIAL
    Serial.println("RTC lost power!");  // Only if Serial now
    Serial.print("\nRTC is ");
    printTimeNow( Serial, rtc, true, ymdhmsDateFormat );
    Serial.println("RTC was not initialized or has lost power");
    cout << "\n\nSetting RTC to the last compile time for this sketch: ";
    #endif // ECHO_TO_SERIAL
    // changed to keep storing records even if RTC battery dies 5/22/24.  Had bad batch of RTC batteries and it killed all data if power was cycled
    // error( 7 ); // send SOS to user interface; used to have an rtc.adjust here.
    // Set the time to the last compile time
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // pin setups
  pinMode( RTCalarm1_INT, INPUT_PULLUP );  // active low RTCtimer interrupt
  pinMode( photoQDrivePin, INPUT) ; // now use 56 kOhm for high range. Turn off when not in use to save power
  pinMode( photoQReadPin, INPUT );  // define as input.  Uses analog read, so this is inconsequential and may cause error
  
  #ifdef PIR_ENABLE
  pinMode( PIRpin_INT, INPUT_PULLDOWN );  // pin to use as interrupt for PIR-- original tested as an input pullup  keeps pin low if nothing attached, which is a real possibility
  // pinMode( PIRpin_INT, INPUT );  // pin to use as interrupt for PIR-- REMOVE PULLDOWN TO SAVE POWER LATER, but it is active high so necessary?  Will it use more power?
  #endif // end PIR_enable

  pinMode( stsCheckPin, INPUT_PULLUP );  // pulled down by user status check switch
  pinMode( wheelPin, INPUT_PULLUP );  // direct connection to wheel pin
  pinMode( greenHighPwrLEDpin, OUTPUT ); // green 3.3 mA LED drive pin
  pinMode( redLowPwrLEDpin, OUTPUT ); // red 1.5 mA LED drive pin; built in def is LED_BUILTIN
  pinMode( usrStsGreen, OUTPUT ); // green external indication when the user checks
  pinMode( usrStsYellow, OUTPUT ); // yellow external indication when the user checks
  pinMode( usrStsRed, OUTPUT ); // red external indication when the user checks

  // initialize output pins
  digitalWrite(greenHighPwrLEDpin, LOW); // set the LED low
  digitalWrite(redLowPwrLEDpin, LOW); // set the LED low
  digitalWrite(usrStsGreen, LOW); // set the user status LED low
  digitalWrite(usrStsYellow, LOW); // set the user status LED low
  digitalWrite(usrStsRed, LOW); // set the user status LED low

  // show starting setup by blinking red and green LED once
  blinkLED( greenHighPwrLEDpin, longBlinkDuration, usrStsGreen );  // longer blink for setup
  blinkLED( usrStsYellow, longBlinkDuration, 0 );  // longer blink for setup
  blinkLED( redLowPwrLEDpin, longBlinkDuration, usrStsRed );  // longer blink for setup
  delay ( 500 );  // delay 500 ms so can see if we reach the end of setup ok

  #ifdef WRITE_TO_SDCARD
  /* new SD config version from "QuickStart" */
  if (DISABLE_CHIP_SELECT < 0) {
    #ifdef ECHO_TO_SERIAL
    cout << SD_chipSelectErr;
    #endif // ECHO_TO_SERIAL
  }
  else {
    #ifdef ECHO_TO_SERIAL
    cout << F("\nDisabling SPI device on pin ");
    cout << int(DISABLE_CHIP_SELECT) << endl;
    #endif // ECHO_TO_SERIAL
    pinMode(DISABLE_CHIP_SELECT, OUTPUT);
    digitalWrite(DISABLE_CHIP_SELECT, HIGH);
  }

  if (!sd.begin(chipSelectSD, SPI_SPEED)) {
    if (sd.card()->errorCode()) {
      #ifdef ECHO_TO_SERIAL
      cout << SD_otherError;
      cout << F("\nerrorCode: ") << hex << showbase;
      cout << int(sd.card()->errorCode());
      cout << F(", errorData: ") << int(sd.card()->errorData());
      cout << dec << noshowbase << endl;
      #endif // ECHO_TO_SERIAL
      error( 2 );     // Two red flashes means no card or card init failed.
    }
    #ifdef ECHO_TO_SERIAL
    cout << F("\nCard successfully initialized.\n");
    #endif // ECHO_TO_SERIAL
    if (sd.vol()->fatType() == 0) {
      #ifdef ECHO_TO_SERIAL
      cout << F("Can't find a valid FAT16/FAT32/exFAT partition.\n");
      cout  << SD_reformatMsg;
      #endif // ECHO_TO_SERIAL
      error( 2 );     // Two red flashes means no card or card init failed.
    }
    #ifdef ECHO_TO_SERIAL
    cout << F("Can't determine error type\n");
    #endif // ECHO_TO_SERIAL
    error( 2 );     // Two red flashes means no card or card init failed.
  }
  
  // prepare file named unused so far to save new data (e.g., Mouse001.CSV, Mouse002, etc) *ONLY 8 CHARS with SD.H- shitcanned that one!
  
  #ifdef csvFormatted // different filename format
    snprintf( csvFileName, stdBuffSize, fileNameFormat, versionNum, MMUS_ID, rtc.now().toString( fNameDateFormat ), csvAlphaType, csvExtension );
    createLogFile( csvFileName, csvLogFile );
    csvLogFile.println(versionInfo); // put the code version level in the file header.  May delete later
    csvLogFile.print("Test start:,");
    if ( rtcLostPowerErr ) {
      printTimeNow( csvLogFile, rtc, false, ymdhmsDateFormat );
      csvLogFile.println( ",!!! WARNING: REAL TIME CLOCK RESET TO CODE COMPILE DATE. MANUALLY SET IT CORRECTLY !!!");
    }
    printTimeNow( csvLogFile, rtc, false, ymdhmsDateFormat );  // print the actual start time
    csvLogFile.print( ", MMUS ID #:,");  // go over and add the MMUS_ID number text sticking with csv
    csvLogFile.println( MMUS_ID );  // ID number plus a blank CRLF
    csvLogFile.println( csvHeaderInfo );  // data header
    csvLogFile.flush();
  #endif // csvFormatted
  #ifdef rtivity // different filename length
  // NOTE: if we got the date-time during csvFormatted, rtc.now().toString( fNameDateFormat ) will actually be the same fNameDateFormat as it will not longer be a format.  That's fine.
    snprintf( tsvFileName, stdBuffSize, fileNameFormat, versionNum, MMUS_ID, rtc.now().toString( fNameDateFormat ), tsvAlphaType, tsvExtension );  // fname
    createLogFile( tsvFileName, tsvLogFile );
  #endif // rtivity 
#endif // WRITE_TO_SDCARD
  
  // get the Unixtime from the RTC
  delay(100);  // added 7/15/26 to ensure that the I2C bus isn't being overrun when making rtc.now() calls.  Ridiculous i know, but better safe than sorry.
  startUnixTime = rtc.now().unixtime();
  cycleUnixStartTime = startUnixTime;

  // blink LED Green-Red-Green-Green to show that we are done with setup (and blick a pattern to user interface)
  blinkLED( greenHighPwrLEDpin, shortBlinkDuration, usrStsGreen );
  delay( 100 );
  blinkLED(redLowPwrLEDpin,mediumBlinkDuration, usrStsYellow );
  delay( 250 );
  blinkLED(greenHighPwrLEDpin,mediumBlinkDuration, usrStsRed );
  delay( 150 );
  blinkLED(greenHighPwrLEDpin,longBlinkDuration, usrStsGreen );
  
  // set up timers and wheel interrupts
  setRTCtimers( ); // initialize the RTCtimers.  See the function to change settings  *** MUST be after cycleUnixStartTime is initialized to now()
  setUpTC4debounce_timer( ); // set up the debounce timer-- it is DISABLED on initialization
  setUpInterrupts( ); // for the RTC / SD / User status check.  Does NOT turn on the TC4 counter
  
}

//          MAIN LOOP          //      
//          MAIN LOOP          //      
//          MAIN LOOP          //      
//          MAIN LOOP          //      
void loop() {
  unsigned int tempLockedWheelCount = 0;
  uint32_t tempPIR_duration = -1;  // if PIR is ifdef'd out, we leave this in for the function calls and leave at -1 for an invalid reading (indicating it isn't valid)
  

  // wheel interrupts & debounce are all handled in wheel interrupts and TC4 timer now
  unixTimeNowInLoop = rtc.now().unixtime();  // get the unix time at the beginning of the loop.  It won't change during the active processing time

  #ifdef PIR_ENABLE
  // PIR housekeeping outside of the dataNeedsWriting section except when it is active during a write cycle (handled in dataNeedsWriting)
    if( PIR_started) {
      // need to get the start time for the PIR, indicate a count is active, then clear the start flag
      PIR_startTime = unixTimeNowInLoop;
      noInterrupts( );  // be safe just to avoid possible race condition
      PIR_active = true;
      PIR_started = false;
      interrupts( );  // be safe just to avoid possible race condition
    }
    if( PIR_stopped) {
      // need to get & update the PIR duration, indicate the PIR cycle is done, then clear the active flag
      PIR_duration += (unixTimeNowInLoop - PIR_startTime);
      noInterrupts( );  // be safe just to avoid possible race condition
      PIR_active = false;
      PIR_stopped = false;
      interrupts( );  // be safe just to avoid possible race condition
    }
  #endif  // end PIR_ENABLE
  
  if ( dataNeedsWriting ) { // this means the RTC timer has expired and caused an interrupt
    // store the temporary value so the main counters will be able to continue without loss
    // wheelcount housekeeping
    tempLockedWheelCount = wheelCount;  // store the temporary value so the main counters will be able to continue without loss
    // Reset the counters so that increments can begine again clean
    wheelCount = 0;  // Reset the wheelcount counter as it is now stored in a buffer

    #ifdef PIR_ENABLE
      // PIR housekeeping iff PIR cycle is ongoing- continues being on for next write storage period
      if( PIR_active ) {  // the PIR is active and a timer is ongoing
        tempPIR_duration = PIR_duration + unixTimeNowInLoop - PIR_startTime;  // lock time in case there is another interrupt
        // reset everything-- must restart the timer since PIR is active
        PIR_startTime = unixTimeNowInLoop;
      }
      else {
        tempPIR_duration = PIR_duration;  // not active, so copy the global into the local
      }
      PIR_duration = 0;
    #endif  // end PIR_ENABLE

    // #ifdef ECHO_TO_SERIAL
    //   dataOutput( Serial, true, tempLockedWheelCount, tempPIR_duration );
    // #endif // ECHO_TO_SERIAL
    
    dumpTheData( tempLockedWheelCount, tempPIR_duration, unixTimeNowInLoop ); // send out data; if syncCntr exceeds maxLinesBeforeSDSync, flushes logFile stream
    dataNeedsWriting = false;  // clear the data write flag since the data has been dumped.
  }  // end of if ( dataNeedsWriting )

  if( stsCheckInProgress ) {
    if ( ! checkBatteryAndFlushFiles( ) ) { //there was an error in the filesystem.  call the error() routine
      error( 8 );
    }
  }
  
  #ifdef SLEEP_ENABLE
    if ( ! checkingDebounce && ! dataNeedsWriting && ! stsCheckInProgress ) {  // neither a debounce flag nor a timer expired before we want to back to sleep
                        // PIR counting and analysis is done inside the interrupt.  Only cleared when data is written- regardless of pinStaut
      noInterrupts( );  // disable the interrupts going into sleep.  They will get queued up- MUST re-enable in sleepNow()
      sleepNow( );  // this does everything to go to sleep & set up for waking on the next timer interval, including setting up the interrupt

      #ifdef ECHO_TO_SERIAL
        // Coming back out of sleep now.  If ECHO_TO_SERIAL is enabled, have to restart the USB serial interface
        initSerial( Serial );  // this DOESN'T WORK
      #endif // ECHO_TO_SERIAL

      }
  #endif // SLEEP_ENABLE
}

//          ISRs          //      
//          ISRs          //      
//          ISRs          //

void setUpInterrupts( void ) {  // turns on the interrupts and enables the ones we use

  interrupts( );           // enables interrupts in case they were turned off
  // timer alarm interrupt
  #ifdef SLEEP_ENABLE
  LowPower.attachInterruptWakeup( digitalPinToInterrupt(RTCalarm1_INT), RTCtimerAlarmIRQ, FALLING );
  #endif // SLEEP_ENABLE end
  // Attach interrupt to the wheel reed switch when it goes from high (normal) to low (pulled down by reed switch)
  LowPower.attachInterruptWakeup( digitalPinToInterrupt( wheelPin ), wheel_ISR, FALLING );
  #ifdef PIR_ENABLE
  LowPower.attachInterruptWakeup( digitalPinToInterrupt( PIRpin_INT ), PIR_ISR, CHANGE );
  #endif // PIR_ENABLE
  LowPower.attachInterruptWakeup( digitalPinToInterrupt( stsCheckPin ), stsCheck_ISR, FALLING );

}  // end setUpInterrupts()

/* wheel_ISR() gets called when the stated pin goes from normally high to low
   If stays high for (global) debounceUsecs, true is returned and the supplied counter is incremented.
   If does not stay high, false is returned and the supplied counter is unchanged  
   Can't use millis() or micros() in an ISR, so pass it back to a debounce timer */
void wheel_ISR ( void ) {

  // micros() calls in an interrupt can be WAY off.  In some cases that's the best i can do.
  checkingDebounce = true;  // started debounce cycle
  debounceCount = 0;  // reset debounceCount
  TC4Enable ( true );  // start the TC4 debounce interrupt timer 
} // end wheel_ISR

// Called whenever our RTC alarm triggers
void RTCtimerAlarmIRQ( void ) {

  RTCtimerExpired = rtc.alarmFired( recordDataAlarm );  // set after we have verified the interrupt was valid
  dataNeedsWriting = RTCtimerExpired;
  rtc.clearAlarm( recordDataAlarm );
}  // end RTCtimerAlarmIRQ()

void TC4_Handler( void ) {                       // Interrupt Service Routine (ISR) for timer TC4
// THIS IS A LOT FOR AN INT HANDLER.  Consider moving things around if it works
  // Check for overflow (OVF) interrupt (could be from a different source)
  if (TC4->COUNT16.INTFLAG.bit.OVF && TC4->COUNT16.INTENSET.bit.OVF) {  // the interrupt is valid & from TC4.  Otherwise is for someone else (ouch)
    if( digitalRead( wheelPin ) == LOW ) {  // still a valid state for debounce, so incement debounce counter
      debounceCount++; 
      if( debounceCount >= numDebounceTicksToCountWheel ) { // we have made the number of cycles through debounce, increment wheel counter
        // note: debounceCount reset is in wheel_ISR
        TC4Enable ( false );
        checkingDebounce = false;
        wheelCount++;
      }
    }
    else {   // did not maintain the low wheel state long enough, so it was a bounce
      // note: debounceCount reset is in wheel_ISR
      TC4Enable ( false );
      checkingDebounce = false;
    }
  }
  REG_TC4_INTFLAG = TC_INTFLAG_OVF;         // Clear the TC4 OVF interrupt flag
} // end TC4Handler

#ifdef PIR_ENABLE
// handles when motion is detected in the mouse cage
// The PIR is active high; the ISR is called upon any change in state.
// if high, then assume is the beginning of the mouse activity.  Store the time in a volatile; if not, it is the end of the cycle-- clear status and update the mouse active counter
// Minimum time for the sensor varies between 2 and 4 seconds
void PIR_ISR( void ) {
  bool PIR_status = false;
  PIR_status = digitalRead( PIRpin_INT );
  if ( ! PIR_active && PIR_status ) { // we have a new rising interrupt and need to set PIR_started
    PIR_started = true;  // note: PIR_active is ONLY handled inside the time storage section
  }
  else if ( PIR_active && ! PIR_status ) { // we have a new falling interrupt and need to set PIR_stopped
    PIR_stopped = true;  // note: PIR_active is ONLY handled inside the time storage section
  }
}
#endif // PIR_ENABLE

void stsCheck_ISR ( void ) {  // The user pushed the status check button (stsCheckPin pulled low)
  stsCheckInProgress = true;  // will be cleared after the status check process is completed
}


//          FUNCTIONS          //      
//          FUNCTIONS          //      

void sleepNow( void ) {  // this is only called if sleep is enabled via an #ifdef.  Do not need to do additional checks
  // Normal mode fires every minute.  Incremental mode requires updating the interrupt timer each cycle
  // we have disabled interrupts before entry

  if( RTCtimerExpired ) {  // an RTCtimer INT caused us to go to sleep, NOT a wheelINT
    // update the sleep timer
    if( alarm1IntervalNeedsUpdate ) syncNextRTCirq( cycleUnixStartTime, recordDataTimeInterval );
    RTCtimerExpired = false;  // clear the RTCTimer flag since we've updated the incremental timer
  }
  interrupts( );  // turn on the interrupts
  LowPower.sleep( );  // go to sleep; data has been written, wheel interrupts handled, RTC incremented if needed
}  // end sleepNow()

// setRTCtimer rtc is set up and operating upon entry
void setRTCtimers( void ) {
  // clear & disable the unused alarm-- warnings say weird things can happen if these aren't done (example is in this order)
  rtc.clearAlarm( unusedAlarm );
  rtc.disableAlarm( unusedAlarm );
  rtc.disable32K( );  //we don't need the 32K Pin, so disable it

  // stop oscillating signals at SQW Pin.  Examples say otherwise setAlarm1 will fail
  rtc.writeSqwPinMode( DS3231_OFF );

  initializeRTCtimerIntervals( recordDataTimeInterval );  // program the time mask and default settings.  
  // For variable (incremental) time, the time goes off every recordDataTimeInterval seconds and the timer value has to be updated.
  if( alarm1IntervalNeedsUpdate ) syncNextRTCirq( cycleUnixStartTime, recordDataTimeInterval );
 
  // check to see if the alarm has immediately fired-- if so, clear
  if (rtc.alarmFired( recordDataAlarm ) ) {
    rtc.clearAlarm( recordDataAlarm );
  }
}   // end setRTCtimers()

// just a simple blink with the on-time duration in mS.  Uses delay()-- i.e. NOPs so don't make the value large
// make the pinNum equal to 0 to not do anything
void blinkLED( uint32_t pinNum1, unsigned long OnTimeDuration, uint32_t pinNum2 ) { //TIME IN MILLISECONDS!
  if( pinNum1 > 0 ) digitalWrite( pinNum1, HIGH ); // Turn on LED described by pinNum1
  if( pinNum2 > 0 ) digitalWrite( pinNum2, HIGH ); // Turn on LED described by pinNum2
  delay( OnTimeDuration );
  if( pinNum1 > 0 ) digitalWrite( pinNum1, LOW ); // Make sure the LED is off when we leave the routine. Additional delay must be done outside
  if( pinNum2 > 0 ) digitalWrite (pinNum2, LOW ); // Make sure the LED is off when we leave the routine. Additional delay must be done outside
}

// // Gets the photocell value and converts to an int "percentage"
float getPhotoCellVal( float Rpullup, float photoSensorMult ) {
  float pTransValue = 0;
  float photoSensorValuePcnt = 0;
  float photoSensorValue_uA = 0;

  pinMode( photoQDrivePin, OUTPUT ); // turn on drive (power savings for it to be input only
  digitalWrite( photoQDrivePin, HIGH ); // turn on low range drive
  delay ( phototransDwell ); // let output settle for a mS before reading: response time for BPW96C is 2uS and Cortex rise time is in the low nS. w/ capacitance 500 uS is plenty
  pTransValue = getAvgAnalogIn( photoQReadPin );
  // try an average of multiple readings
  pinMode( photoQDrivePin, INPUT ); // turn off drive (power savings)
  // calculate values
  photoSensorValue_uA = (  1 - photoSensorMult * pTransValue / float( maxADCval ) ) * VFeather / ( Rpullup / 1000 );  // to get uA, convert Rpullup to MOhms. get the current through the phototransistor
  photoSensorValuePcnt = mCurrentPcnt * photoSensorValue_uA + mInterceptPcnt;  // convert to a percent based off the current flowing through the phototransistor

  // check for out of bounds
  if( photoSensorValuePcnt > 100 ) photoSensorValuePcnt = 100;
  else if( photoSensorValuePcnt < 0) photoSensorValuePcnt = 0;
  return ( photoSensorValuePcnt );  // int() typecast truncates the float, so we use roundf
}  // end getPhotoCellVal


// gives a much more stable value by averaging the analog input and throwing out the high & low outliers
// uses global maxADCVal for the highest analog value
float getAvgAnalogIn( uint32_t inputPin ) {
  int i;
  int AinArray[ numAnalogAvgCycles ] = { 0 };
  int AinSum = 0;
  int avgCount = 0;
  int AinLow  = maxADCval;
  int AinHigh = 0;
  float AinAvg = 0;

  AinArray[0] = analogRead( inputPin );  // get first reading after turning on & throw out
  // collect multiple readings and record outliers
  // get several readings in a row.  Variation of up to 50 bits can be seen
  for( i = 0; i < numAnalogAvgCycles; i++ ) {
    AinArray[ i ] = analogRead( inputPin );  // get reading
    if( AinArray[ i ] < AinLow  ) AinLow  = AinArray[ i ]; // identify the outliers
    if( AinArray[ i ] > AinHigh ) AinHigh = AinArray[ i ]; // identify the outliers
  }

  // average the readings and throw out the upper and lower outliers
  if ( AinLow  == AinHigh ) {
    AinAvg = (float) AinLow ; // they are all the same, so the average is already conmputed
  }
  else {
    for( i = 0; i < numAnalogAvgCycles; i++ ) {
      if( ( AinArray[ i ] > AinLow  ) && ( AinArray[ i ] < AinHigh ) ) {
        AinSum += AinArray[ i ];
        avgCount++;
      }
    }
    AinAvg = (float) AinSum / (float) avgCount;
  }

  return ( AinAvg );  // int() typecast truncates the float, so we use roundf
}  // end getAvgAnalogIn




// MAY HAVE TO CHANGE &filePointer TO DIFFERENT TYPE IF USE DIFFERENT SD FORMATS?
void dumpTheData( unsigned int storedWheelCount, uint32_t storedPIR_duration, uint32_t timeNowInLoop ) { // writes data to all defined files as defined in the #ifdefs.  Serial is handled differently
  uint32_t deltaTime = cycleUnixStartTime; // store the beginning of the old cycle start time in deltaTime
  uint32_t elapsedTime = 0;
  float totalDistance = 0;
  float avgVelocity = 0;
  float cageTemperature = 0;
  float photoTransValPcnt = 0;

  blinkLED( greenHighPwrLEDpin,shortBlinkDuration, usrStsGreen );  // just a QUICK flash to indicate record is being stored
  cycleUnixStartTime = timeNowInLoop;  //The next cycle starts now - any PIR or wheel interrupts are processed after this point.  TimeNowInLoop was set upon the start of this loop.  No extra calls needed


  if ( firstWriteCycle ) {  // discard first cycle - the data set is likely from a shorter (potentially much shorter) time interval
    // #ifdef ECHO_TO_SERIAL
    // cout << "\n\n time is less than " << waitMoreThanThisToWrite << " seconds, not writing file";  // do this to warn the user that there is a delay if a full storage period hasn't happened.
    // #endif // ECHO_TO_SERIAL
    firstWriteCycle = false;
    // indicate first data set thrown out
    blinkLED( 0,shortBlinkDuration,usrStsGreen ); 
    delay( shortBlinkDuration );
    blinkLED( usrStsYellow,shortBlinkDuration, 0 );
    startUnixTime = cycleUnixStartTime; // reset the start time to when the data has finally started getting recorded, not when the unit was powered on.
  }
  
  else { // this is a valid set of data, send it to any devices wanting it
  // do the shared calculations
    elapsedTime = cycleUnixStartTime - startUnixTime;
    deltaTime = cycleUnixStartTime - deltaTime;  // subtract the original cycle beginning from the time that this set is being stored
    totalDistance = (float) storedWheelCount * wheelCircumference;  // in cm
    if( deltaTime > 0 ) avgVelocity = totalDistance / (float) deltaTime; // in cm per seconds.  Ensure no problem with deltatime
    else avgVelocity = 0;
    cageTemperature = rtc.getTemperature( );
    photoTransValPcnt = getPhotoCellVal( photoTransPullup, photoSensorCalFactor );  // now a value in % that can be corrected for each phototransistor
    
    #ifdef WRITE_TO_SDCARD
      #ifdef csvFormatted 
        if(sd.exists( csvFileName )) dataOutputCSV( csvLogFile, false, storedWheelCount, storedPIR_duration, totalDistance, avgVelocity, cageTemperature, photoTransValPcnt, elapsedTime );  // sd.exists ensures file exists, write the data to the file cache- no CRLF if going to a file
        else error( 4 ); // sd file did not exist. card was ejected or another error.  Stop writing and blink error 4 times
      #endif // csvFormatted
      #ifdef rtivity
        if(sd.exists( tsvFileName )) dataOutputRtivity( tsvLogFile, false, storedWheelCount, storedPIR_duration, totalDistance, avgVelocity, cageTemperature, photoTransValPcnt, elapsedTime );  // sd.exists ensures file exists, write the data to the file cache- no CRLF if going to a file
        else error( 4 ); // sd file did not exist. card was ejected or another error.  Stop writing and blink error 4 times
      #endif // rtivity
    #endif // WRITE_TO_SDCARD
    if ( ++syncCntr >= maxLinesBeforeSDSync ) {  // flush counter has been reached so flush()
      if ( syncAllFiles( &syncCntr ) ) { // all went well,inform user.  
        delay( shortBlinkDuration );
        blinkLED( greenHighPwrLEDpin,shortBlinkDuration,usrStsGreen );  // a second flash to indicate flush has happened successfully (double flash)
      }
      else {
        blinkLED( redLowPwrLEDpin,twoSecBlinkDuration,usrStsRed );  // huge error- will go to error() but just in case long red flash
        error( 9 ); // an sd file sync or timestamp update failed. card was ejected or another error.  Stop writing and blink error 9 times
      }
    }
  }
} // end dumpTheData

#ifdef csvFormatted 
// the calling code must check that the stream is valid. 
void dataOutputCSV( Stream &serialPort, bool printCRLF, unsigned int storedWheelCount, uint32_t storedPIR_duration, float totalDistance, float avgVelocity, float cageTemperature, float lightPercent, uint32_t elapsedTime ) { // prints csv formatted when called. Does NOT reset the counter, Stream MUST BE CHECKED before calling
  char tempCharHold[ hugeBuffSize ] = "";
  int snprintfRC = 0;
  
  snprintfRC = snprintf( tempCharHold, hugeBuffSize, csvDataFormat, elapsedTime, lightPercent >= darkToLightPcnt, lightPercent, cageTemperature, storedWheelCount, totalDistance, avgVelocity, storedPIR_duration );
  checksnprintfRC( snprintfRC, hugeBuffSize, serialPort );
  printTimeNow( serialPort, rtc, false, ymdhmsDateFormat ); // have to have a separate buffer for rtc.now() conversion, so use existing routine
  serialPort.print( tempCharHold );
  checkBat( serialPort ); // prints bat sts & value to serialPort. Originally returned String but there are warnings that can cause memory corruption
}
#endif // csvFormatted 

#ifdef rtivity
void dataOutputRtivity( Stream &serialPort, bool printCRLF, unsigned int storedWheelCount, uint32_t storedPIR_duration, float totalDistance, float avgVelocity, float cageTemperature, float lightPercent, uint32_t elapsedTime ) { // prints csv formatted when called. Does NOT reset the counter, Stream MUST BE CHECKED before calling
  // All floating point numbers have to be converted to integers for rtivity
  serialPort.print( ++recNumber );  // col1 (A) give the record number and increment (initialized to zero)
  serialPort.print( "\t");
  printTimeNow( serialPort, rtc, false, rtivityDate );  // col2 (B) print date in rtivity format without a crlf
  serialPort.print( "\t");
  printTimeNow( serialPort, rtc, false, rtivityTime );  // col3 (C) print time in rtivity format without a crlf
  serialPort.print( "\t1\t0\t" );  // col4 (D) data is valid, col5 (E) extras
  serialPort.print( MMUS_ID ); // col6 (F) Monitor Number
  serialPort.print( "\t0\tCt\t0\t" );  // col7 (G) mon tube (unused), col8 (H) is the data type defined in the rtivity manual, col9 (I) unused
  serialPort.print( lightPercent >= darkToLightPcnt );  // col 10 (J) is binary light sensor with "1" indicating bright enough and 0 indicating "darkness"
  serialPort.print( "\t" );
  serialPort.print( storedWheelCount ); // col11 (K) is the wheel count
  serialPort.print( "\t" );
  serialPort.print( (int) roundf (lightPercent) ); // col 12 (L) is the actual light level in percent of the available dynamic range
  serialPort.print( "\t" );
  serialPort.print( (int) roundf( 100 * cageTemperature ) );  // col 13 (M) is the temperature; switch to centigrees C since Rtivity can't deal with float. The RTC reports the temperature in 0.25C increments so we shouldn't need to round, but do it anyway.
  serialPort.print( "\t" );
  serialPort.print( (int) roundf( totalDistance ) );  // col 14 (N) is the total distance in cm. (int) typecast truncates without roundf()
  serialPort.print( "\t" );
  serialPort.print( (int) roundf( avgVelocity ) );  // col15 (O) is average speed in cm per sec (int) typecast truncates without roundf()
  serialPort.print( "\t" );
  serialPort.print( elapsedTime );  // col16 (P) = the test duration in Unix time (seconds)
  serialPort.print( "\t" );
  serialPort.print( getBatValue( ) ); // col 17 (Q) is the battery Voltage in mV
  #ifdef PIR_ENABLE
  serialPort.print( "\t" );
  serialPort.print( 1 );  // col18 (R) indicates if the PIR is enabled
  serialPort.print( "\t" );
  serialPort.print( storedPIR_duration );  // col19 (S) is total time the animal is active during the cycle period as determined by PIR (if applicable)
  #endif // PIR_ENABLE
  #ifndef PIR_ENABLE
  serialPort.print( "\t0\t0" );  // col18 (R) indicates PIR is disabled and col19 (S) is total time the animal activity cannot be measured
  #endif // not( PIR_ENABLE )
  serialPort.print( "\t" );
  serialPort.print( rtcLostPowerErr );  // col20 (T) is set if the RTC was reset
  serialPort.println( "\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0" );  // now 23 zeroes and crlf (cols T through AP)
} // end dataOutput
#endif // rtivity


// syncs all of the existing global files using the ifdefs
// sets the counter to 0
// returns the error() code if there is one, if not is set to 0
bool syncAllFiles( unsigned int *counter ) {
  bool fileOK = true;

  #ifdef WRITE_TO_SDCARD
    #ifdef csvFormatted 
      fileOK = fileOK && syncAndTimestampFile ( csvLogFile, csvFileName );  // is csv file in good shape? (both must be if applicable)
    #endif // csvFormatted
    #ifdef rtivity
      fileOK = fileOK && syncAndTimestampFile ( tsvLogFile, tsvFileName );  // is tsv file in good shape? (both must be if applicable)
    #endif // rtivity
  #endif // WRITE_TO_SDCARD
  *counter = 0;
  return( fileOK );
}

#ifdef WRITE_TO_SDCARD
bool syncAndTimestampFile ( file_t &syncFile, String syncFileName  ) {
  // update the modification time from https://forums.adafruit.com/viewtopic.php?f=31&t=17964
  DateTime now = rtc.now( );

  // write date-time
  if ( !syncFile.timestamp( T_WRITE,now.year(),now.month(),now.day(),now.hour(), now.minute(),now.second() )) {
    #ifdef  ECHO_TO_SERIAL  // FORCE SERIAL ALIVE IF ERROR TO CAUSE SOLID RED
      Serial.print( "Could not write modification date for " ); 
      Serial.println( syncFileName );
    #endif // ECHO_TO_SERIAL
    return( false );
  }
  if( syncFile.sync() ) return ( true );  // may only need flush. According to documents, sync() pushes all SD info (ANY file open), flush() does only the argument file
  else {
    // error( 9 );  // could not sync file - let error() write the files if poossible
    return( false );  // 
  } 
}  // end syncAndTimestampFile()
#endif // WRITE_TO_SDCARD


// assumes stream is valid.  Writes csv battery text & battery Voltage.  If critically low, calls error().
void checkBat ( Stream &serialPort ) {
  // from on line source: // built-in double-100K resistor divider on the BAT pin connected to D9 (a.k.a analog #7 A7). 
  // Read A7 voltage, double it  --> battery voltage.
  float measuredVBat = 0;

  measuredVBat = getBatValue( );
  serialPort.print( measuredVBat );  // send battery value
  if ( measuredVBat > battFull_ChargeVoltage ) { // likely no battery and running off USB, fully charged, or charging a battery and connected to USB.  Uses V divider and if no batt goes to USB Voltage
    serialPort.println( ",USB pwr/charging or fully charged" );   // status and termination
  }
  else if( measuredVBat >= battGoodVoltage ) {
    serialPort.println( ",Battery Charged" );   // status and termination
  }
  else if ( measuredVBat >= battLowVoltage ) {
    serialPort.println( ",Battery OK" );
  }
  else if  ( measuredVBat > battDeadVoltage ) {
    // battery is too low.  Return false for BatOK and warn user
    serialPort.println( ",Battery is low-- CHARGE IT!" );  // status and termination
    blinkLED( redLowPwrLEDpin,mediumBlinkDuration, usrStsYellow );  // yellow flash to warn use that battery is dying in case they can see it.
    serialPort.flush( );  // add a flush() 8/23/23 just to be sure everything is up to date on the file.  Doesn't sync everything (incl file time), so review better option later
  }
  else { // battery is dangerously low (battDeadVoltage or below).  Post error and flush data so we don't lose anything.
    // the minimum Vbatt that can be read without ADC error is 3.45V. Below this, the value
    serialPort.println( ",BATTERY CRITICALLY LOW!!!" );  // status and termination
    error( 1 );  // error will sync all files and post the LED error
  }
} // end checkBat


int getBatValue ( void ) {
  // built-in double-100K resistor divider on the BAT pin connected to D9 (a.k.a analog #7 A7). 
  // Read A7 voltage, double it  --> battery voltage.
  // Actual Feather Calculated Battery Value including ADC error as Vcc drops
  // Vbatt 4.00V --> ADCval 4.00 V, 
  // 3.75 V	--> 3.75 V, 3.50 V -->	3.50 V, 3.25 V -->	3.42 V, 3.00 V -->	3.43 V

  int measuredVBat = 0;

  measuredVBat =  (int) roundf ( 1000 * ( getAvgAnalogIn( VBatPin ) * 2 * VFeather / maxADCval) );    // resistor-divider is Vbat/2, so double it.  Mult by ref Voltage (3.3). Convert AD -> Voltage
  return ( measuredVBat );
} // end getBatValue


void printTimeNow( Stream &serialPort , RTC_DS3231 rtc, bool useCRLF, char *dateFormat ) {
  
  // Per the toString readme: Before calling this method, the buffer should be initialized by the user with the format string. 
  // The method will ***overwrite*** the buffer with the formatted date and/or time.
  char localDateFormat[ strlen( dateFormat ) ];
  
  strcpy( localDateFormat, dateFormat); // copy dateFormat from the original string into a local string.  Problems if use original w/o reinit
  if( useCRLF ) {
    serialPort.println( rtc.now( ).toString( localDateFormat ));
  }
  else {
    serialPort.print( rtc.now( ).toString( localDateFormat ));
  }
} // end printTimeNow


void error( uint8_t errno ) {
  uint8_t i;
  int numLoopsWithUserPinLow = 0;
  bool stopAllOperationError = true;
  char tempCharHold[ hugeBuffSize ] = "";

    
  #ifdef ECHO_TO_SERIAL
    postErrorToStream ( Serial, errno );
  #endif // ECHO_TO_SERIAL


  #ifdef WRITE_TO_SDCARD
    #ifdef csvFormatted 
      if( sd.exists( csvFileName )) postErrorToStream ( csvLogFile, errno );
    #endif // csvFormatted
    #ifdef rtivity
      if( sd.exists( tsvFileName )) {
        snprintf( tempCharHold, hugeBuffSize, rtivityFileInvalidData, ++recNumber, rtc.now().toString( rtivityDate ), rtc.now().toString( rtivityTime ), MMUS_ID );
        tsvLogFile.println( tempCharHold ); // Put the string with the invalid data tag code 51 in column 4 (invalid data) along with date, time, and MMUS_ID
        postErrorToStream ( tsvLogFile, errno );  // Better to contaminate the data anyway so user has to parse?
        // really should make this be a tag code 51 in column 4 (invalid data), but it would still process.  Better to contaminate the data so user has to parse?
      }
    #endif // rtivity
  #endif // WRITE_TO_SDCARD

  syncAllFiles( &syncCntr );

  noInterrupts ();  // Got an error.  Disable interrupts so user will see error indicator and nothing can stop the blinking
  
  while( stopAllOperationError ) {  // infinite loop, blink error code forever
    if (errno == 7 ) {
      // give SOS so user will know something is wrong (need to initialize RTC)
      for( int j = 0; j < 3; j++) {  // repeat three times then move on
        delayWhenInterruptsDisabled( oneSecBlinkDuration ); // MILLIS
        for (i=0; i <3; i++) {  // DOT-DOT-DOT-dash-dash-dash-dash-dash-dash
          blinkLEDWhenInterruptsDisabled( redLowPwrLEDpin, shortBlinkDuration, usrStsRed );  // MILLIS
          delayWhenInterruptsDisabled(longBlinkDuration); // MILLIS
        }
        for (i=0; i <3; i++) {  // dot-dot-dot-DASH-DASH-DASH-dot-dot-dot
          blinkLEDWhenInterruptsDisabled( redLowPwrLEDpin, longBlinkDuration, usrStsRed );  // MILLIS
          delayWhenInterruptsDisabled(longBlinkDuration); // MILLIS
          }
        for (i=0; i <3; i++) {  // dot-dot-dot-dash-dash-dash-DOT-DOT-DOT
          blinkLEDWhenInterruptsDisabled( redLowPwrLEDpin, shortBlinkDuration, usrStsRed );  // MILLIS
          delayWhenInterruptsDisabled(longBlinkDuration); // MILLIS
        }
      }
      // last part of the RTC error.  Return to calling program so we don't lose data
      stopAllOperationError = false;
      interrupts ();  // Instead of doing an infinite loop, re-enable interrupts and go back to code and continue collecting data
      delayWhenInterruptsDisabled(longBlinkDuration); // MILLIS
    }
    else { // do the number of blinks required
      for (i=0; i<errno; i++) {
      blinkLEDWhenInterruptsDisabled( redLowPwrLEDpin, longBlinkDuration, usrStsRed );  // MILLIS
      delayWhenInterruptsDisabled(shortBlinkDuration); // MILLIS
      }
    }
    delayWhenInterruptsDisabled(1000);  // MILLIS
    // IF the user button is pressed and held while we have all the interrrupts disabled and in "bad problem" mode,
    // we want to restart the program completely.  This could be because the user has pulled out and replaced the 
    // SD card, wanting to start a new file, etc.
    if( digitalRead( stsCheckPin ) == LOW ) { // we've found that the user status pin is low.  They need to hold it down to do a full reset
      numLoopsWithUserPinLow++;
      blinkLEDWhenInterruptsDisabled( redLowPwrLEDpin, shortBlinkDuration, usrStsRed );
      if( numLoopsWithUserPinLow >= 3 ) { // we've been through 3 consecutive cycles. reset the system!
        // warn user and reboot the entire system
        systemReset();
      }
    }
    else {
      numLoopsWithUserPinLow = 0;
    }
  }  // end whileStopOperationError
}

void postErrorToStream ( Stream &serialPort, int errNum ) {
  serialPort.print("ERROR REPORTED, error number ");
  serialPort.println( errNum );
  switch (errNum) {
    case 1:  // 
      serialPort.println( LowBatVoltageErrorMsg );
      break;
    case 2:
      serialPort.println( SDinitErrorMsg );
      break;
    case 3:
      serialPort.println( SDfileCreationErrorMsg );
      break;
    case 4:
      serialPort.println( SDfileEjectOrOtherErrorMsg );
      break;
    case 5:
      serialPort.println( dumpTheDataStreamErrorMsg );
      break;
    case 6:
      serialPort.println( debounceCatastrophicErrorMsg );
      serialPort.print( micros() );
      serialPort.print("wheelCnt= ,");
      serialPort.println( wheelCount );
      break;
    case 7:
      serialPort.println( RTCnotSetUpErrorMsg );
      break;    
    case 8:
      serialPort.println( RTCinterruptConfigErrorMsg );
    case 9:
      serialPort.println( writeTimeSyncErrorMsg );
    case 10:
      serialPort.println( writeTimeIntervalErrorMsg );
    default:
      serialPort.println( undocumentedErrorMsg );
      break;
  }
}  // end postErrorToStream()

// blink that works when interrupts are disabled with the on-time duration in microS.  millis() needs interrupts enabled.
// Only works when interrupts are disabled to use as an alternative for blinkLED(), which *requires* enabled interrupts
// will blink up to two outputs-- if pinNumvalue is 0, nothing will be blinked for that input.  NOTE: 0 can cause troubles!
void blinkLEDWhenInterruptsDisabled( uint32_t pinNum1, unsigned long OnTimeDuration, uint32_t pinNum2 ) { //TIME IN MILLISECONDS!
  if( pinNum1 > 0 ) digitalWrite( pinNum1, HIGH ); // Turn on LED described by pinNum2
  if( pinNum2 > 0 ) digitalWrite( pinNum2, HIGH ); // Turn on LED described by pinNum2
  delayWhenInterruptsDisabled( OnTimeDuration );
  if( pinNum1 > 0 ) digitalWrite( pinNum1, LOW ); // Make sure the LED is off when we leave the routine. Additional delay must be done outside
  if( pinNum2 > 0 ) digitalWrite( pinNum2, LOW ); // Make sure the LED is off when we leave the routine. Additional delay must be done outside
}  // end blinkLEDWhenInterruptsDisabled


// Works when interrupts are disabled or don't want to use them; use as an alternative for delay(), which *requires* enabled interrupts
// millis() needs an interrupt to work. *** micros() ONLY goes to 999 *reliably* with interrupts disabled. 2 mS is guaranteed failure ***
void delayWhenInterruptsDisabled ( int timeDelayMilliS ) {
  unsigned long lastMicros;
  int elapsedTimeNow = 0;
  unsigned long noOverrunTrigTime;  // time that is 1mS later than now().
  boolean notYet = true;

  noOverrunTrigTime = micros() + 1000;
  while ( notYet ) {
    lastMicros = micros();  
    if ( micros() < lastMicros ) {  // we've overrun after crossing one microsecond boundary.  It's inconsistent
      elapsedTimeNow += 1; // we've crossed over 1 mS so increase the elapsed time.  Counter rolls over at 1 mS with interrupts off.
    }
    else if (lastMicros >= noOverrunTrigTime ) { // we've crossed 1 mS the standard way  
      elapsedTimeNow += 1;
      noOverrunTrigTime = lastMicros + 1000;
    }
    if ( elapsedTimeNow > timeDelayMilliS ) { 
      notYet = false;
    }
  }
}

// assumes the clock has been correctly configured
void TC4Enable ( bool enableINT ) {
    
  if ( enableINT ) {
    // Serial.println("\nTC4 has been enabled !!!!" );
    // turn timer TC4 interrupts ON
    NVIC_SetPriority(TC4_IRQn, 0);    // Set the Nested Vector Interrupt Controller (NVIC) priority for TC4 to 0 (highest)
    NVIC_EnableIRQ(TC4_IRQn);         // Connect TC4 to Nested Vector Interrupt Controller (NVIC)
    REG_TC4_INTFLAG |= TC_INTFLAG_OVF;            // Clear the interrupt flags
    REG_TC4_INTENSET = TC_INTENSET_OVF;           // Enable TC4 interrupts
  }
  else {
    // Serial.println("TC4 has been disabled ___" );
    NVIC_DisableIRQ(TC4_IRQn);
    NVIC_ClearPendingIRQ(TC4_IRQn); // turn timer TC4 interrupts OFF
    REG_TC4_INTFLAG |= TC_INTFLAG_OVF;           // Clear the interrupt flags
    REG_TC4_INTENCLR = TC_INTENCLR_OVF;          // Disable TC4 interrupts
  }
}

// Sets the TC4 & TC5 clocks to the speed of the clock ticks
// It does NOT enable any interrupts
void setUpTC4debounce_timer ( void ) {  
    
  // Configure the timer TC4 to call the TC4_Handler as described below
  // Set up the generic clock (GCLK4) used to clock timers
  REG_GCLK_GENDIV = GCLK_GENDIV_DIV(1) |          // Divide the 48MHz clock source by divisor 1: 48MHz/1=48MHz
                    GCLK_GENDIV_ID(4);            // Select Generic Clock (GCLK) 4
  while (GCLK->STATUS.bit.SYNCBUSY);              // Wait for synchronization

  REG_GCLK_GENCTRL = GCLK_GENCTRL_IDC |           // Set the duty cycle to 50/50 HIGH/LOW
                     GCLK_GENCTRL_GENEN |         // Enable GCLK4
                     GCLK_GENCTRL_SRC_DFLL48M |   // Set the 48MHz clock source
                     GCLK_GENCTRL_ID(4);          // Select GCLK4
  while (GCLK->STATUS.bit.SYNCBUSY);              // Wait for synchronization

  // Feed GCLK4 to TC4 and TC5
  REG_GCLK_CLKCTRL = GCLK_CLKCTRL_CLKEN |         // Enable GCLK4 to TC4 and TC5
                     GCLK_CLKCTRL_GEN_GCLK4 |     // Select GCLK4
                     GCLK_CLKCTRL_ID_TC4_TC5;     // Feed the GCLK4 to TC4 and TC5
  while (GCLK->STATUS.bit.SYNCBUSY);              // Wait for synchronization

  REG_TC4_INTFLAG |= TC_INTFLAG_OVF;              // Clear the interrupt flags

 // prescaler options are my notes: TC_CTRLA_PRESCALER_DIV1024, 256, 64, 16, 8, 4, 2, and 1
 // 16 bit counter can go from 1 to 0xFFFF (0 is invalid)
 // notes: w/ 1024 prescaler: 1 sec = 0xB71A, 0.1sec = 0x124E, 500uS = 0x16, 450uS=0x14; 400uS=0x11 fastest is approx 50uS; 
 // each drop in prescaler value makes the time longer

  REG_TC4_COUNT16_CC0 = 0x14;                   // Set the TC4 CC0 register as the TOP value in match frequency mode and to 450uS.  With three cycles to make a cycle count,
                                                // this counts as a angular velocity of 3.49krad/sec at a magnet pickup at 3.3cm or about 157 meters/sec max mouse speed with a 4.5cm wheel radius.
  REG_TC4_CTRLA |= TC_CTRLA_PRESCALER_DIV1024 |   // Set prescaler, 48MHz/1024 = 46.875kHz; at 4 it is 12 MHz
                   TC_CTRLA_WAVEGEN_MFRQ |        // Put the timer TC4 into match frequency (MFRQ) mode 
                   TC_CTRLA_ENABLE;               // Enable TC4
  while (TC4->COUNT16.STATUS.bit.SYNCBUSY);       // Wait for synchronization
}

// per the Cortex-M0 programming manual, this will do a full hardware reset after a countdown
void systemReset( void ) { 
  float warningBlinkDuration = 5000; // how long to do the blinks before resetting in milliseconds (lets user let go of the button)
  
  #ifdef  ECHO_TO_SERIAL
  cout << F("\n\n\n SYSTEM RESETTING IN 5 SECONDS!\n\n\n");
  #endif  // ECHO_TO_SERIAL
  #ifdef WRITE_TO_SDCARD
  #ifdef csvFormatted
    if ( csvLogFile ) {  // Skip write/flush if SD problem
      csvLogFile.println( "\nWARNING: SYSTEM IS BEING RESET BY USER OR INTERNAL CALL");
      csvLogFile.flush();  // make sure it gets onto the logFile if it is still valid
    }
  #endif // csvFormatted
  #ifdef rtivity
    if ( tsvLogFile ) {  // Skip write/flush if SD problem
      tsvLogFile.println( "\nWARNING: SYSTEM IS BEING RESET BY USER OR INTERNAL CALL");
      tsvLogFile.flush();  // make sure it gets onto the logFile if it is still valid
    }
  #endif // rtivity
  #endif // WRITE_TO_SDCARD
  for ( float i = 0; i <= warningBlinkDuration; i += shortBlinkDuration + 2*mediumBlinkDuration + longBlinkDuration )  {  // two fast green/user blinks times per cycle, Keep blinking until the warningBlinkDuration timeout.
    blinkLEDWhenInterruptsDisabled( greenHighPwrLEDpin, mediumBlinkDuration, usrStsRed );  //TIME IN MILLISECONDS!
    blinkLEDWhenInterruptsDisabled( redLowPwrLEDpin, shortBlinkDuration, usrStsYellow ); //TIME IN MILLISECONDS!
    blinkLEDWhenInterruptsDisabled( greenHighPwrLEDpin, mediumBlinkDuration, usrStsRed );  //TIME IN MILLISECONDS!
    delayWhenInterruptsDisabled( longBlinkDuration );
    }
  NVIC_SystemReset( );  // This does a FULL reset per the Cortex user manual
} // end systemReset()

#ifdef WRITE_TO_SDCARD
// creates logFile with name fileName using the following restraints (need to be made more adaptable)
// See the csvLogName or tsvLogName in test.h for more information.
// expected format is 36 characters with definition "aaaa_Axxx_YYYYMMDD_HHMM_stnd_zz.kkk".  Definition should add the null character at [37]
// the only important thing for this file create code is that "zz" be at array locations [30] and [31]
bool createLogFile( char *fileName, file_t &logFile ) {
  bool fileSuccess = true;

  for (int i = 0; i < 100; i++) {  // kludgey.  Fix later
    fileName[30] = '0' + i/10; // has to be the first numerical value-- the '0' is the text value for 0.  Adding the int to it gives ascii
    fileName[31] = '0' + i;
    // create if the fileName does not exist, do not open existing, write, sync after write
    if (! sd.exists(fileName)) {  // keep cycling until we get an empty filename
      break;
    }
    else if( i == 99 ) {
      // #ifdef ECHO_TO_SERIAL
      // cout << "\n Halting: All available filenames are used 0 to " << i;
      // #endif // ECHO_TO_SERIAL
      fileSuccess = false;
      error( 3 );
    }
  }

  logFile = sd.open( fileName, FILE_WRITE );
  // check if the log file was created 
  if( ! logFile ) {
    #ifdef  ECHO_TO_SERIAL  // FORCE SERIAL ALIVE IF ERROR TO CAUSE SOLID RED LED if it isn't already set up
    Serial.print( "Couldn't create " ); 
    Serial.println( fileName );
    #endif // ECHO_TO_SERIAL
    fileSuccess = false;
    sd.errorHalt( );
    error ( 3 );
  }

  // copied directly from https://forums.adafruit.com/viewtopic.php?f=31&t=17964
  // fetch the time to set the creation date
  DateTime now = rtc.now( );

  // set creation date time
  if ( !logFile.timestamp( T_CREATE,now.year( ),now.month( ),now.day( ),now.hour( ), now.minute( ),now.second( ) ) ) {
    // #ifdef  ECHO_TO_SERIAL  // FORCE SERIAL ALIVE IF ERROR TO CAUSE SOLID RED
    Serial.print( "Could not write creation date for " ); 
    Serial.println( fileName );
    // #endif // ECHO_TO_SERIAL
  }

  // the file was created (otherwise would go to error()
  #ifdef ECHO_TO_SERIAL
    cout << "Logging to " << fileName << "\n";
    Serial.println( versionInfo ); // put the code version level in the file header.  May delete later
    Serial.print( "Start time is ");
    printTimeNow( Serial, rtc, true, ymdhmsDateFormat );
    Serial.println( csvHeaderInfo );  // data header
  #endif // ECHO_TO_SERIAL
  return( fileSuccess );
}
#endif // WRITE_TO_SDCARD

// routine to use the globals to check the battery status & indicate to user.  Then check files, synchronize them, and indicate to user
bool checkBatteryAndFlushFiles( void ) {
// user status request is pending, handle.  Don't mess with any of the interrupts so this can run in the background
  // Give a one second battery status, 1/4 second delay, then file status for one second
    int presentBattVoltage = 0;
    bool fileOK = true;

    // check the battery
    presentBattVoltage = getBatValue( );  
    if( presentBattVoltage <= battDeadVoltage ) {
      blinkLEDWhenInterruptsDisabled( usrStsRed, oneSecBlinkDuration, redLowPwrLEDpin );
    }
    else if( presentBattVoltage <= battLowVoltage ) {
      blinkLEDWhenInterruptsDisabled( usrStsYellow, oneSecBlinkDuration, redLowPwrLEDpin );
    }
    else if( presentBattVoltage < battGoodVoltage ) {
      blinkLEDWhenInterruptsDisabled( usrStsGreen, oneSecBlinkDuration, usrStsYellow );
    }
    else if( presentBattVoltage < battFull_ChargeVoltage ) {
      blinkLEDWhenInterruptsDisabled( usrStsGreen, oneSecBlinkDuration, greenHighPwrLEDpin );
    }
    else {  // battery all the way to max
      blinkLEDWhenInterruptsDisabled( usrStsGreen, oneSecBlinkDuration, greenHighPwrLEDpin );
    }

    delayWhenInterruptsDisabled( 250 );  // 250 milliseconds delay between statuses

    if ( firstWriteCycle ) {  // discard first cycle - the data set is likely from a shorter (potentially much shorter) time interval
      // indicate still in first data set that will get thrown out
      blinkLED( usrStsYellow, mediumBlinkDuration,usrStsGreen ); 
      delay( mediumBlinkDuration );
      blinkLED( usrStsYellow,mediumBlinkDuration, 0 );
    }
    if( syncAllFiles( &syncCntr ) ) blinkLEDWhenInterruptsDisabled( usrStsGreen, oneSecBlinkDuration, greenHighPwrLEDpin );  // file(s) are good
    else {
      blinkLEDWhenInterruptsDisabled( usrStsRed, oneSecBlinkDuration, redLowPwrLEDpin );  // file(s) are not in good shape
      fileOK = false;
    }
    stsCheckInProgress = false;  // reset status
    return( fileOK );
  }


#ifdef ECHO_TO_SERIAL
// this doesn't do anything on an m0.  m0 opens serial on the USB port automatically on call and times out if it isn't there.
// After sleep, it won't reopen it at all and times out.
void initSerial( Stream &serialPort, unsigned long baudRate ) {
  // this starts the serial interface. have to restart the serial interface if went to sleep.  SAMD uses Serial for the USB port
  if ( &serialPort == & Serial ) { // decide which port type, USB or RX/TX
    Serial.begin( baudRate ); // use the built in redirected Serial device
    // while (! serialPort); // Wait until Serial is ready-- but this hangs FOREVER if the terminal doesn't connect.  Use a delay instead- no terminal, it moves on
    delay( 1 );  // give time for host to respond - for MMUS we want to move on quickly.
    serialPort.setTimeout( 50 );  // milliseconds until serial time out for use in the program in case serial port drops out
    Serial.println("\nSAMD USB Serial initialized.");
  }
  else if (&serialPort == &Serial1) {
    Serial1.begin(baudRate);
    Serial1.println("Serial1 initialized.");
  }
}
#endif // ECHO_TO_SERIAL

void syncNextRTCirq( uint32_t lastAlarm, uint32_t timerInterval ) {
  uint32_t nextAlarm = 0;
  bool localUpdateNeeded = alarm1IntervalNeedsUpdate;

  nextAlarm = lastAlarm + timerInterval;
  while ( localUpdateNeeded ) {
    if( nextAlarm % timerInterval != 0 ) { // we have gotten off track and are no longer on an exact multiple of the step time and need to correct it
      nextAlarm = (nextAlarm / timerInterval + 1) * timerInterval;  // integer division truncates.  Add one for the next interval.  Now multiply by the interval and we are all set.
      // verify haven't gone backwards in time for the next interrupt
    }
    if( rtc.now().unixtime() < ( nextAlarm + 1 ) ) localUpdateNeeded = false;  // all is well; if not increase the next alarm by one recordtimerInterval again
    else nextAlarm = nextAlarm + timerInterval;  // increase by another time interval

    if ( !rtc.setAlarm1( nextAlarm, alarm1Mask )) {
      error( 11 );
    }
  }
} // end syncNextRTCirq

// Called at RTC timer1 initialization & does two things
// 1. checks to see if RTC can be set up on a standard interval (does not require updates each cycle), clears update required flag
// 2. sets the alarm1Mask so it will be correct for both standard intervals and any custom intervals between 1 second and 1 day inclusive
void initializeRTCtimerIntervals( uint32_t timerInterval ) {
  uint32_t nextAlarm = 0;

  if( alarm1IntervalNeedsUpdate ) {  //a larm1IntervalNeedsUpdate is set globally to true upon first run.  This is really a wasted if
    if( timerInterval <=  1 ) {
      alarm1Mask = DS3231_A1_PerSecond;  // fires every second, this is all we need & is a standard interval -- DO NOT recommend this short of a time interval because overhead becomes significant
      alarm1IntervalNeedsUpdate = false;   // no further need for interval updates - fires every second  ** HIGHLY ILL ADVISED!
    }
    else if( timerInterval <= 60 ) {  // less than or equal to a minute?
      alarm1Mask = DS3231_A1_Second;  // fires at a specific second; minutes /hours/etc don't matter.   If second field is 00, will fire at top of minute only & is a standard interval
      if( timerInterval == 60 ) alarm1IntervalNeedsUpdate = false;  // no further need for interval updates - always at the top of the minute
    }
    else if( timerInterval <= 3600 ) {  // less than or equal to an hour?
      alarm1Mask = DS3231_A1_Minute;  // fires at a specific minute; hours/etc don't matter.  If minute field is 00, will fire at top of hour only & is a standard interval
      if( timerInterval == 3600 ) alarm1IntervalNeedsUpdate = false;  // no further need for interval updates - always at the top of the hour
    }
    else if( timerInterval <= SECONDS_PER_DAY ) {  // less than or equal to an hour?
      alarm1Mask = DS3231_A1_Hour;  // fires at a specific minute; hours/etc don't matter.  If minute field is 00, will fire at top of hour only
      if( timerInterval == SECONDS_PER_DAY ) alarm1IntervalNeedsUpdate = false;  // no further need for interval updates - always at the top of the day
    }
    else {
      alarm1IntervalNeedsUpdate = false;
      error( 10 );  // post error- time interval is too large to handle with this code.  You can modify it to handle longer than 24 hours, but is ridiculous for this application
    }

    if( ! alarm1IntervalNeedsUpdate ) { // standard interval.  Set it the the first time through and be done with it
      if ( !rtc.setAlarm1( baseRecTime, alarm1Mask )) {
        // if not set, warn user.  Otherwise, all is well and continue
        error( 11 );  // tell user fatal error in write code
      }
    }
  }
}


void checksnprintfRC( int snprintfRC, int bufflen, Stream &serialPort ) {
  if( snprintfRC > bufflen - 1 ) {
    serialPort.print( "snprintf buffer of ");
    serialPort.print( bufflen );
    serialPort.print( " was not large enough to print " );
    serialPort.print( snprintfRC );
    serialPort.println(" characters of information.  Send copy of output file and tell coder to expand buffer." );
  }
  else if ( snprintfRC < 0 ) {
    serialPort.println( "snprintf buffer translation error. Send copy of output file and precipitating conditions to coder for analysis." );
  }
}



//  CITATIONS-- ONLY DS3231 in 8/21/23.  Need to add more

// DS3231
//## How to Cite
//
//If you use this library in a publication, please cite it in one or both of the following two ways:
//1. Through the `CITATION.cff` file here, which should be up to date with the permanent archive available from Zenodo
//2. If you need an academic journal reference and/or you are discussing the integration of the DS3231 into a larger hardware + firmware ecosystem,<br/>
//**Wickert, A. D., Sandell, C. T., Schulz, B., & Ng, G. H. C. (2019), [Open-source Arduino-compatible data loggers designed for field research](https://hess.copernicus.org/articles/23/2065/2019/), *Hydrology and Earth System Sciences*, *23*(4), 2065-2076, doi:10.5194/hess-23-2065-2019.**<br/>
//This option should not be the only one used because it does not credit the original library developer, Eric Ayars.
