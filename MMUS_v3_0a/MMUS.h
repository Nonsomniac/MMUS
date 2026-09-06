#define versionInfo "MMUS ver 3.0.a last update 20260715 w/ avg light values and syncTime; MMUS.h & MMUSmessages.c; RTC reset upon failure; 60 sec & 5 min flush; address elapsed time overrun & convert to %u."
// CHANGE THIS VALUE TO UNIQUELY IDENTIFY EACH DEVICE
const int MMUS_ID = 24;  // valid from 0 to 999, but do not put leading 0 (compiler autoconverts to octal) {3 digits per filename space}, must be numeric between 1 and 120 for Rtivity compatibility,
const char versionNum[] = "3.0a";  // reserved to FOUR CHARACTERS.  This documents any code revisions

// Written by xxxx
// Originally inspired by  https://doi.org/10.1523/ENEURO.0260-21.2021 "A Novel Microcontroller-Based System for the Wheel-Running Activity in Mice" by Zhu, et. al.
// w/ avg light values and syncTime; MMUS.h & MMUSmessages.c; RTC reset upon failure; 60 sec & 5 min flush, address elapsed time overrun & convert to %u."

//    Included libraries & files
#include <MillisTimer.h>  // millisecond timer
#include <ArduinoLowPower.h> // Arduino Low Power by Adafruit.  "Install All" if a warning to install dependencies pops up
#include <SPI.h>
#include <SdFat.h>  // SdFat – Adafruit Fork by Bill Greiman. (note: SD.h doesn't put SD card to sleep right.  Try SdFat.h card writing header file. Using Arduino version
#include "sdios.h"
#include <Wire.h>
#include <time.h>
#include <RTClib.h>  // RTClib by Adafruit. works MUCH better than the finicky/difficult DS3231 library.  Also an Arduino supported lib

//        HERE ARE THE #DEFINES THAT ENABLE DIFFERENT FEATURES          //    
// BY COMMENTING OUT the #define <xxxxx> the code associated with that will be disabled

// enables writing to the SD Card  Comment to disable SD access
#define WRITE_TO_SDCARD

// file types to create - you can have both.  File writes use a lot of power
// csvFormatted tells code to make a more readable csv formatted file with headers. Commenting out will not make a csv file and save power
#define csvFormatted

// rtivity tells code to make an rtivity .tsv format file  Commenting out will not make an Rtivity tsv file and save power
#define rtivity

// disabling SLEEP mode hasn't really been tested out since version 1.0.  DO NOT USE - LOCKS UP.  ALWAYS LEAVE ACTIVE
#define SLEEP_ENABLE // when set the sleep functions.  Comment to disable sleep

// WARNING: A) MUST HAVE A DEVICE ON THE USB PORT FOR PROGRAM TO RESTART
//          B) SERIAL PORT DOES NOT RECOVER AFTER SLEEP.  TRIED VARIOUS THINGS. NADA.  If you echo to serial, sleep cannot be enabled
// #define ECHO_TO_SERIAL // Comment to disable serial. Serial output if this line uncommented, BUT SOLID RED UNTIL SERIAL IS CONNECTED 

// Passive Infrared  motion detector
//#define PIR_ENABLE  // comment out if you don't want the PIR to interrupt the system ON 1/24/24. Program locks up doing it that way.  Switch to disabling by pulling PIRpin_INT low

// Debug mode hasn't really been tested out in a long time.  Results unpredictable
// #define DEBUG // turns on debug code when not commented out.  Comment to disable debug information

/// /// /// !!! CRITICAL VARIABLES THAT YOU WANT TO ACCESS EASILY !!! /// /// ///
String MMUS_IDtext = String ( MMUS_ID );

//////  DATA RECORDING SETUP //////
const int32_t recordDataTimeInterval = 60; // number of seconds between record storage- over one year is allowed. Not advised to go below 5 seconds
// To save power, files are written to a buffer.  When maxLinesBeforeSDSync is reached, the records
// and timestamp are automatically written via a sync() or flush().
#define maxLinesBeforeSDSync 5 // the number of records written between syncs. MAX = 65,535 (flush uses more power, so using sync)  System auto-syncs at 512 bytes.



////// LIGHT LEVEL SETUP //////

// unfortunately, each unit would have to be calibrated to be accurate at low levels and we'd need better (more expensive) parts
// 5% is roughly 20 lumens using the 20.5k resistor (candlelight levels). This is VERY spotty because of the extremely low light levels at the lower cages
// I recommend not going below 3 as the variance between parts becomes extremely significant.
const float darkToLightPcnt = 5;  // Sets darkToLightPcnt to level you want to use for "darkness".  Below this is "dark".  Greater than or equal to is "light"  


// Light and ADC settings
  const float VFeather = 3.3;

// see earlier versions for light calibration work. With the 20.5kOhm 1% resistor, these are reasonably accurate.  See manual for level conversions
  const float PDcalFactor_default = 1.0;  // if we want to calibrate, this will help some.  Phototransistor gain is all over the place
  const float lowLightLevelCurrentuA = 0.38;  // current in mA for as dark as measured in testing for a 20.5kOhm resistor- not light NONLINEAR
  // max current = (Vfeather - VCEsat)/Rpullup = (3.3 - 0.025)/20.5kOhm = 159.2 uA   TEFT4400{Vce(SAT)}max = 0.1V per spec, measured at 25 mV actual
  const float highLightLevelCurrentuA = 159.9;  // current in mA for extremely bright light at saturation. Readings invalid above saturation 
  const float mCurrentPcnt = (100 - 0) / (highLightLevelCurrentuA-lowLightLevelCurrentuA);  // CALCULATED FROM GIVEN low and high light levels
  const float mInterceptPcnt = 0 - mCurrentPcnt * lowLightLevelCurrentuA;  // CALCULATED FROM GIVEN low and high light levels
  // C(TEFT4400) = 16 pF, C(M0) = 3.5 pF, Rin=20.5kOhm --> RC = 0.4uS.  Therefore 1 mS (minimum delay) is fine
  const int phototransDwell = 1; // integer in milliseconds for pullup to be on (see above)
  const int numAnalogAvgCycles = 25;  // the number of times for the phototransistor to read for an accurate average. Over 25 (25 takes 1.7 milliseconds) doesn't seem to help; 10 seems OK and takes 1.3 milliseconds. 50 takes 2.3 ms. 

// DATA HEADER INFORMATION
  #ifdef rtivity
  // // for rtivity eliminate headers
  // // Would be rtivity headerinfo[] but is unused
  // // 1 – Index; 2 – Date; 3 – Time; 4 – Monitor status; 5 – Extras; 6 – Monitor number; 7 – Tube number; 8 – Data type; 9 – unused; 10 –Light sensor; 
  // // 11 to 42 – Activity data from user
  //   // 11 - <#rotations>; 12 - add % light level>; 13 - <temperature>; 14 - <cm traveled>; 15 - average speed;
  //   // 16 - <elapsed time since start>; 17 = <Battery Voltage>; 18 - <future PIR active time-- UNUSED NOW>; 19 - RTC reset; remainders 0 and unused
  #endif // rtivity
  
  #define stdBuffSize 40
  #define hugeBuffSize 250

//  SERIAL PORT #DEFINES AND STREAMS NOTE: serial is needed for SDFat, but can't use or locks up w/o serial port
  #define serialPortSpeed 115200 // make this a #define 115200 is fast.  Use default of 9600.  
  
  //  This makes us able to use cout to go to serial port, etc
  // Serial streams for cout and cin
  #ifdef  ECHO_TO_SERIAL
  ArduinoOutStream cout(Serial);  // output stream
  char cinBuf[ stdBuffSize ];  // input buffer
  ArduinoInStream cin(Serial, cinBuf, sizeof(cinBuf));  // input stream
  #endif // ECHO_TO_SERIAL


//          SPI (serial peripheral interface) and/or I2C BUS DEVICE ADDRESSES          //      
//   Use reduced SPI speed for breadboards or long busses.
//   SD_SCK_MHZ(4) will select the highest speed supported by the board that is not over 4 MHz.
//   Change SPI_SPEED to SD_SCK_MHZ(50) for best performance.  SDFat says to use 8; was using 4.
#define SPI_SPEED SD_SCK_MHZ(8)  // ADALOGGER BUILT-IN SD will not work at 50 MHz. May work faster than 4MHz, but haven't tried. Suggestions are that 12 MHz may become unreliable
//   Set DISABLE_CHIP_SELECT to disable a second SPI device.
//   For example, with the Ethernet shield, set DISABLE_CHIP_SELECT
//   to 10 to disable the Ethernet controller.  
const int8_t DISABLE_CHIP_SELECT = -1;  // i.e. no other device to disable when this is -1

// Note: SDCARD_SS_PIN is the SD Card chip select.  ** Need to disable other devices when using multiple devices
#define chipSelectSD 4  // Set the pin used for uSD (chipSelect in "QuickStart")

//  DISK SETUP INFORMATION
const int fNameLen = 40;
#ifdef WRITE_TO_SDCARD
  // from sdFat-->QuickStart in the arduino standard example sketch library
  // SD_FAT_TYPE = 0 for SdFat-File (small), = 1 for FAT16/FAT32, 2 for exFAT, 3 for FAT16/FAT32 and exFAT. 
  // exFAT will read older types, but not always.  Force user to reformat if can't read
  #define SD_FAT_TYPE 3  // "3" allows us to use the larger SD cards.  Try 0 to see if it will work with old card (3 doesn't).  Filename in SDFAT limited to 8.3
  SdFs sd;  // this is the definition for the sd card itself
  typedef FsFile file_t;  // defile file_t to point to the file type so we can use it as a variable throughout the program

// WARNING: The later code assumes a 36 character + 1 null character (37 total) as defined below:
// expected format is "vaaaa_Mxxx_YYYYMMDD_HHMM_yyyy_zz.kkk" where
//   "_" is the isolation character
//   aaaa is the 4 character code in versionNum[] the version of the code
//   Mxxx is the letter M followed by the three digit code defined in "MMUS_ID" for the MMUS IO board
//   YYYYMMDD_HHMM is the 13 character date-time using the "fNameDateFormat" defined in this document
//   yyyy is the 4 character alphanumeric type of data.  Presently there is "stnd" (for standard) and "rtiv" (for rtivity).  This can be set as desired
//   zz is the two digit number used to create a unique filename if multiples exist.  [29] and [30].  These will be made unique by code if duplicates, so don't move zz without changing the file naming code
const char fileNameFormat[] = "v%04s_M%03d_%013s_%04s_zz%3s";
#ifdef csvFormatted  // rtivity filename and extension is different
char csvFileName[ stdBuffSize ]; // must be stdBuffSize less one MAXIMUM if making changes  See above for restrictions
file_t csvLogFile;  // csv logfile structure
const char csvAlphaType[] = "stnd";
const char csvExtension[] = ".csv";
#endif // csvFormatted
#ifdef rtivity  // rtivity filename and extension is different
char tsvFileName[ stdBuffSize ];  // must be stdBuffSize less one MAXIMUM if making changes  See above for restrictions
file_t tsvLogFile;  // rtivity logfile structure
const char tsvAlphaType[] = "rtiv";
const char tsvExtension[] = ".tsv";
#endif // rtivity

// write intervals are set in the RTC section
#endif // WRITE_TO_SDCARD

unsigned int syncCntr = maxLinesBeforeSDSync; // counts the number of cycles between data writes syncs. MAX = 65,535.  Set so will flush on first pass
							   


// // // // // // DEFINITIONS
  // Pin INFORMATION and cross reference
  // P1 is the 16 pin header
  // P2 is the 12 pin header
  // Any of the digital DX (~X) pins are software mapped to "X" where X is 5 to 13 on an M0 (some are pre-defined)
  // Any of the analog AY pins are software mapped to "Y" where Y is 1 to 7 on an M0 ("1" is the only analog output)

// // // PIN DEFINITIONS
const uint32_t photoQDrivePin = PIN_A1;  // pin to power the photodiode for reading  -- was having a crash between the RTC interrupt that was also set to A1.  Move RTC to A2
const uint32_t photoQReadPin = PIN_A3;  // pin to photodiode to read value when photoCellDrive is on

// interrupt pins
const uint32_t wheelPin = 5;  // wheel interrupt D5 pin with pullup for the unfiltered mouse wheel reed switch input * pin D5 translates to "5" and is P2-10 (called D5, PA15, or 15 on schematics)
const uint32_t RTCalarm1_INT = PIN_A2;  // the clock interrupt input pin.  Active low. (called A2, PB09, 9, AIN3, or Y15 on schematics)
// pin 9 is RESERVED for the vbat monitor
const uint32_t stsCheckPin = 10; // connects to the pushbutton for the student/user to check functional status. Is P2-7
#ifdef PIR_ENABLE
const uint32_t PIRpin_INT = 6;  // D6 pin as the input for the PIR: NOTE PIR stays on for a min of 2 sec and stays OFF for a min of 2 sec (P2-9)
#endif // end PIR_ENABLE

// battery
const uint32_t VBatPin = PIN_A7;  // THIS IS ALSO THE D9 BATTERY CHECK PIN - has built in 100k/100k divider (called D9, ~9, PA07, Y5, or AIN7 on schematics) note: AIN7 doesn't work!
const int battDeadVoltage = 3550; // we need to flush the SD Card and post flashing light- ADC inflates below 3.5V.  Data loss is impending.
const int battLowVoltage = 3675; // This value in mV sets where the warning starts for Vbat being low
const int battGoodVoltage = 3710; // This value in mV sets where the battery Voltage is good when not connected to a charger
const int battFull_ChargeVoltage = 4170; // If there is a 3.7V (3700 mV) lithium battery, if the value is higher than this it is probably not connected to the M0

// LEDs
// built-in LEDs
const uint32_t greenHighPwrLEDpin = 8; //on-CPU green 3.3 mA LED drive pin
const uint32_t redLowPwrLEDpin = 13;  // on-CPU red 1.5 mA LED drive pin; built in def is LED_BUILTIN

//exterior LEDs
const uint32_t usrStsGreen = 12; // for external LED; max 2mA to 3.3V unless put in high power mode (ref https://lowpowerlab.com/forum/moteino-m0/m0-gpio-output-power-setting/ )  Lady Ada says 7 mA
const uint32_t usrStsYellow = 11; // for external LED; max 2mA to 3.3V unless put in high power mode (ref https://lowpowerlab.com/forum/moteino-m0/m0-gpio-output-power-setting/ )  Lady Ada says 7 mA
const uint32_t usrStsRed = PIN_A5; // for external LED; max 2mA to 3.3V unless put in high power mode (ref https://lowpowerlab.com/forum/moteino-m0/m0-gpio-output-power-setting/ )  Lady Ada says 7 mA

//Time durations for LED blinks
#define twoSecBlinkDuration 2000 // super duper long blink (on for this duration in mS)
#define oneSecBlinkDuration 1000 // super long blink (on for this duration in mS)
#define longBlinkDuration 300 // long blink (on for this duration in mS)
#define mediumBlinkDuration 100 // medium blink (on for this duration in mS)
#define shortBlinkDuration 25 // fast blink (on for this duration in mS)

// Interrupt driven or general volatiles/globals
// Wheel based interrupts
#define numDebounceTicksToCountWheel 2 // # TC4 tick marks (debounce interrupts) before a wheel count is made
volatile bool checkingDebounce = false; // flags if an ISR has been received on the wheel's reed switch.
volatile unsigned int wheelCount = 0; // number of complete rotations of the mouse wheel. MAX = 65,535. Should count at LEAST 3 hours of data before overflow
volatile int debounceCount = 0;   // how many times the debounceCounter interrupt (TC4) has fired-- after numDebounceTicksToCountWheel occurances, the wheel rotation is true

#ifdef PIR_ENABLE
// PIR interrupts    // True when motion detected by PIR. Active high.  Delay time: 2 seconds, blocking time: 2 seconds
volatile bool PIR_started = false;  // The PIR pin went high
volatile bool PIR_stopped = false;  // The PIR pin went low
volatile bool PIR_active = false;  // The most recent read of the PIR pin
volatile uint32_t PIR_startTime = 0;
volatile uint32_t PIR_duration = 0;
#endif // PIR_ENABLE

// RTC & file write alarm interrupts
volatile bool dataNeedsWriting = false;  // set after an RTC interrupt occurs. Stays set until all file data has been written.   First time through DON'T want to write the data.
volatile bool RTCtimerExpired = false;  // Gets set whenever the RTC timer interrupt has fired.  Cleared just before going back to sleep when all is done & timer interrupt set correctly

// user status request interrupts
volatile bool stsCheckInProgress = false; // gets set whenever the stsCheckPin is pulled low and causes an interrupt

// Wheel based globals  updated 1/24/24 from Fusion360 model.
const float wheelRadius = 4.318; // the mouse wheel radius in cm.  Changing this will change distance & velocity calculations
float wheelCircumference = 2 * PI * wheelRadius;

//         RTC SETUP INFORMATION          //      
RTC_DS3231 rtc;    // Create RTC object using the DS3231
                  // detailed info at https://adafruit.github.io/RTClib/html/class_r_t_c___d_s3231.html
uint32_t startUnixTime;  // System Global start time
uint8_t recordDataAlarm = 1;  // initialized to on so the system will sync when the first cycle occurs.
uint8_t unusedAlarm = 2;  // available for another alarm.  This one only supports minute intervals or longer
uint32_t cycleUnixStartTime; // initialized with start Unix time, updated to now() each cycle
bool rtcLostPowerErr = false;

// Light Sensor Globals
#define numADCbits 12 // options are 8, 10, and 12 for the Cortex M0 Feather Adalogger.  10 is the default-- update to 12 for better resolution
int maxADCval = pow( 2, numADCbits) - 1;
float photoSensorCalFactor = PDcalFactor_default;  // set to the default.  It can be changed to match each MMUS if desired
float photoTransPullup = 20.1;  // phototransistor series resistor value in kOhms.  Change to adjust sensitivity


// Alarm 1 firing parameters can range from once per second up to day/date + hrs + min match
// select the mode for the data storage intervals using Alarm 1

const DateTime baseRecTime = ( "00:00:00" );  // we don't need days etc for this application.  That might look like ("Jan 01 2023", "00:00:00" )
Ds3231Alarm1Mode alarm1Mask = DS3231_A1_Second;  // default , will be set by code when looking at the desired write time interval
bool alarm1IntervalNeedsUpdate = true;
// see  recordDataTimeInterval at the top of the header to set the number of seconds between record storage- over one year is allowed. Not advised to go below 5 seconds

bool firstWriteCycle = true; // this flag is set if it is the first write cycle -- we want to skip it so we are ensured of a complete cycle (typ 1 minute) before recording

// NOTE rtc.now().toString( <fmat> ) will overwrite the character string <fmat>, so it has to be copied into the char array.  See printTimeNow() fo example.
char stdDateFormat[] =  "MM/DD/YY hh:mm:ss";  // format for printing to the data file
char ymdhmsDateFormat[] = "YYYY-MM-DD hh:mm:ss";  // used for printing to the data file
char fNameDateFormat[ ] = "YYYYMMDD_hhmm"; // use in all of the filenames
char rtivityDate[] ="DD MMM YY";  // rtivity date format
char rtivityTime[] ="hh:mm:ss";  // rtivity time format
uint32_t unixTimeNowInLoop = 0;  // is set to the time at the beginning of each loop cycle.  Saves having to call RTC several times

unsigned int recNumber = 0;




