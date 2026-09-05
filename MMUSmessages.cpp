// error messages
/*err 1 */ const char LowBatVoltageErrorMsg[] = "\nBattery Voltage dangerously low: stopped data collection & stored all data.";
/*err 2 */ const char SDinitErrorMsg[] = "\nSD initialization error: no SD card present or failure to initialize SD card (wrong or no format).";
/*err 3 */ const char SDfileCreationErrorMsg[] = "\nCould not create the file on the SD card.";
/*err 4 */ const char SDfileEjectOrOtherErrorMsg[] = "\nFile was not opened: was card ejected or some other error?";
/*err 5 */ const char dumpTheDataStreamErrorMsg[] = "\nError in dumpTheData: not found as serial or file";
/*err 6 */ const char debounceCatastrophicErrorMsg[] = "\nCatastrophic error in deBounce(): micros() less than uSecDuringInterrupt: \nReport this to developer with the following information:\nmicros() at error posting = ";
/*err 7 */ const char RTCnotSetUpErrorMsg[] = "\nRTC is not set up-- posting error but continuing/starting data if i can with last compile time ***You need to take to programmer to reinitialize RTC***";
/*err 8 */ const char RTCinterruptConfigErrorMsg[] = "\nRTC interrupt could not be configured in setRTCtimers()";
/*err 9 */ const char writeTimeSyncErrorMsg[] = "\nProblem synchronizing file write time";
/*err 10 */ const char writeTimeIntervalErrorMsg[] = "\nWriteTimeInterval too large- greater than one day";
/*err 11 */ const char settingRTCalarmErrorMsg[] = "\nError setting RTC Alarm 1.";
/*err default */ const char undocumentedErrorMsg[] = "Undocumented Error Code: please report to developer with any known conditions";

const char bufferOverrunMsg[] = "Buffer Overrun during snprintf";  // not used //

#ifdef ECHO_TO_SERIAL  // error messages only used when using serial IO

#define SD_chipSelectErr "\nAssuming the SD is the only SPI device.\nEdit DISABLE_CHIP_SELECT to disable another device.\n"
#define SD_otherError "\nSD initialization failed.\nDo not reformat the card!\nIs the card correctly inserted?\nIs chipSelectSD set to the correct value?\nDoes another SPI device need to be disabled?\nIs there a wiring/soldering problem?\n"
#define SD_reformatMsg "Try reformatting the card.  For best results use format on a PC or use\nthe SdFormatter program in SdFat/examples or download\nand use SDFormatter from www.sdcard.org/downloads.\n"

#endif // ECHO_TO_SERIAL



// data output formats

#ifdef csvFormatted  // NO header file allowed in the rtivity data
#define csvHeaderInfo  "\nDate-Time,Test Len,Is Day?,%Light,RTC Temp,Rotations,cm Traveled,Avg cm/s,Active Time,Batt V (mV),Batt Sts"
#define csvDataFormat  ",%d,%u,%0.1f,%0.2f,%d,%0.1f,%0.2f,%d,"  // date-time called first, add comma. PIR reading and batt sts must be checked & posted  separately
// old version before switching uint_32t from %d to %u   #define csvDataFormat  ",%d,%d,%0.1f,%0.2f,%d,%0.1f,%0.2f,%d,"  // date-time called first, add comma. PIR reading and batt sts must be checked & posted  separately
#endif // csvFormatted

#ifdef rtivity
// we only want the record number, the date-times, the 51 error code, and the board ID
#define rtivityFileInvalidData "%d\t%s\t%s\t51\t0\t%d\t0\tCt\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0"
#define rtivityDataFormat "%d\t%s\t%s\t0\t0\t%d\t0\tCt\t0\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%u\t%d\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0"
// old version before switching uint_32t from %d to %u   #define rtivityDataFormat "%d\t%s\t%s\t0\t0\t%d\t0\tCt\t0\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0"
#endif // rtivity
