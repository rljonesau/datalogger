/*
  SD card datalogger

 This example shows how to log data from 6 analog sensors
 to an SD card using the SD library.

 The circuit:
 * voltage sensing on analog ins 0,1,2,3,6 & 7. - Requires NANO or equivalent QFP package
 * SD card attached to SPI bus as follows:
 ** MOSI - pin 11
 ** MISO - pin 12
 ** CLK - pin 13
 ** CS - pin 4 (for MKRZero SD: SDCARD_SS_PIN)
 * 
 * DS3231 RTC attached to I2C bus
 ** SDA - pin A4
 ** SCL - pin A5
 * 
 * A Red LED, Anode via R to D2
 * A Green LED, Anode via R to D3

 created  24 Nov 2010
 modified 9 Apr 2012
 by Tom Igoe
 modified 18 Feb 2018
 by Ray Jones

 This example code is in the public domain.

 */

//#define TEST

#ifndef TEST
#include <SPI.h>
#include <SD.h>
#include <DS3231.h>
#else
#define FILE_WRITE 1
struct File {
  void print(const __FlashStringHelper* str) {};
  void println(const __FlashStringHelper* str) {};
  void print(String& str) {};
  void println(String& str) {};
  void flush() {};
  void close() {};
  operator bool() {return true; };
};
struct SD {
  File file;
  int begin(int CS) { return 1;};
  int exists(const char* name) { return 1; };
  File open(const char* name, int mode) { return file; };
  void end() {};
} SD;
struct DS3231 {
  DS3231(int DA, int CL) {};
  void begin() {};
  String getDateStr() { return "03-03-2018"; };
  String getDOWStr() { return "Saturday"; };
  String getTimeStr() { return "00:00:00"; };
};
#endif

#include <MsTimer2.h>

//#define SET_RTC    // uncomment this, and set the correct time/date below - only do this ince, then undefine again
#define DOW  THURSDAY
#define DAY  22
#define MON  2
#define YEAR 2018
#define HR   20
#define MIN  46
#define SEC 0


// function prototypes
void SampleADCs();
void FlashLED(int pin, int times, int ontime = 100, int offtime = 100);

// pin definitions
const int LED_RED = 2;
const int LED_GRN = 3;
const int SDChipSelect = 10;      // SPI chip select pin
const int updateInterval = 60;    // interval to update the logging string
const int writeInterval = 600;   // interval to write to SD card (multiple of update interval)
const int TIMER_RATE_ms = 5;        // interval to sample an ADC, (x6 to cycle thru all)


// structure to record max/min values, and an average if desired
struct sMaxMin {
  int maxVal;
  int minVal;
  int count;
  int Acc;
  void Copy(sMaxMin &a) {
    maxVal = a.maxVal;
    minVal = a.minVal;
    count = a.count;
    Acc = a.Acc;
  }
  void Reset() {
    maxVal = -1;            // deliberate out of range
    minVal = 10000;         // deliberate out of range
    count = 0;
    Acc = 0;
  };
  void record(int ipVal) {
    if(ipVal > maxVal)      // track max values
      maxVal = ipVal;
    if(ipVal < minVal)      // track min values
      minVal = ipVal;
    Acc += ipVal;
    count++;
  };
  int getAvg() {
    if(count) {
      return Acc / count;
    }
    else {
      return 0;
    }
  };
};


// structure holds results for all ADCs sampled
struct sSamples {
  sMaxMin  Ip1;   // ADC0
  sMaxMin  Ip2;   // ADC1
  sMaxMin  Ip3;   // ADC2
  sMaxMin  Ip4;   // ADC3
  // NOTE : ADC4 & ADC4 pins are used for I2C interface to RTC
  // Inputs 5 & 6 are actually ADC 6 & 7
  sMaxMin  Ip5;   // ADC6
  sMaxMin  Ip6;   // ADC7
  int      Count;
  int      MaxCount;
  void Copy(sSamples &a) {
    Ip1 = a.Ip1;
    Ip2 = a.Ip2;
    Ip3 = a.Ip3;
    Ip4 = a.Ip4;
    Ip5 = a.Ip5;
    Ip6 = a.Ip6;
    Count = a.Count;
    MaxCount = a.MaxCount;
  }
  void Reset(int max) {
    Ip1.Reset();
    Ip2.Reset();
    Ip3.Reset();
    Ip4.Reset();
    Ip5.Reset();
    Ip6.Reset();
    MaxCount = max;
    Count = 0;
  }
  sSamples() {
    Reset(updateInterval / TIMER_RATE_ms);
  }
  bool Done() {
    return Count >= MaxCount;
  }
};


// global variables
DS3231  RTC(SDA, SCL);              // DS3231 I2C Real Time Clock device
sSamples Samples;                   // record ADC samples here
static int bFirstPass = 1;          // only set upon first run  
static int bCardOut = 0;            // set if the card is removed
 

// callback routine
// typ. called by MsTimer2 once every 10ms
void SampleIps()
{
  static int chnl = 0;   // state machine variable

  switch(chnl) {
    case 0:
      Samples.Ip1.record(analogRead(0));   // sample ADC0 for Ip1
      chnl = 1;                            // ADC1 next 
      break;
    case 1:
      Samples.Ip2.record(analogRead(1));   // sample ADC1 for Ip2
      chnl = 2;                            // ADC2 next 
      break;
    case 2:
      Samples.Ip3.record(analogRead(2));   // sample ADC2 for Ip3
      chnl = 3;                            // ADC3 next 
      break;
    case 3:
      Samples.Ip4.record(analogRead(3));   // sample ADC3 for Ip4
      chnl = 4;                            // ADC6 next 
      break;
    case 4:
      Samples.Ip5.record(analogRead(6));   // sample ADC6 for Ip5
      chnl = 5;                            // ADC7 next 
      break;
    case 5:
      Samples.Ip6.record(analogRead(7));   // sample ADC7 for Ip6
    default:   // safeguard!
      chnl = 0;                            // restart from ADC0
      break;
  }
  Samples.Count++;
}

void setup() {
  // Open serial communications and wait for port to open:
  Serial.begin(115200);
 
  RTC.begin();
  // The following lines can be #defined to set the date and time
#ifdef SET_RTC  
  RTC.setDOW(DOW);     // Set Day-of-Week 
  RTC.setTime(HR, MIN, SEC);     // Set the time to 12:00:00 (24hr format)
  RTC.setDate(DAY, MON, YEAR);  // Set the date ( D / M / Y )
  Serial.println(F("Set RTC time/date"));
#endif  // SET_RTC

  // ensure SPI chip select is an output!
  pinMode(SDChipSelect, OUTPUT);
  digitalWrite(SDChipSelect, HIGH);

  // setup LEDs
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GRN, OUTPUT);
  digitalWrite(LED_RED, HIGH);   // light up upon startup to prove LEDs for 1 sec
  digitalWrite(LED_GRN, HIGH);
  delay(1000);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GRN, LOW);

  MsTimer2::set(TIMER_RATE_ms, SampleIps);
  MsTimer2::start();
}

String LoggerString;

void loop() {

  static int cyclecount = 0;
  
  // sampling is driven by a timer callback
  // wait until a set has been completed before processing
  if(!Samples.Done())
    return;


  // copy the samples for reporting purposes
  // reset the sample set
  sSamples Report;
  cli();
  Report.Copy(Samples);
  Samples.Reset(updateInterval / TIMER_RATE_ms);  // reset the max/min values for the next set 
  sei();


  String DateStr = RTC.getDateStr();
  String DOWStr = RTC.getDOWStr();

  // make a string for assembling the data to log:
  String logStr = ",";   // initial comma for data lines allows reserves status fields in the first column in excel
    
  // Set time in log string
  logStr += RTC.getTimeStr();
  logStr += ",";
    
  // read value of sensors and append to the string:
  logStr += String(Report.Ip1.getAvg());
  logStr += ",";
  logStr += String(Report.Ip2.getAvg());
  logStr += ",";
  logStr += String(Report.Ip3.getAvg());
  logStr += ",";
  logStr += String(Report.Ip4.getAvg());
  logStr += ",";
  logStr += String(Report.Ip5.getAvg());
  logStr += ",";
  logStr += String(Report.Ip6.getAvg());
  logStr += "\n";

  LoggerString += logStr;

  cyclecount++;
  if(cyclecount >= (writeInterval / updateInterval)) {
    cyclecount = 0;

    if(SD.begin(SDChipSelect)) {
    
      //
      // create a filename based upon the current date, convert dots to dashes, 
      // truncate year from 4 to 2 digits
      String FileName;
      FileName = DateStr.c_str();
      FileName.replace(".", "-");
      FileName.remove(6, 2);
      FileName += ".txt";

      // check if a new filename, if so flag it
      bool bNewFile = !SD.exists(FileName.c_str());  

      // report transition events, new day, new card etc
      if (bCardOut) {
        Serial.println(F("\n\n*** CARD INSERTED ***"));
      }
      if(bNewFile) {
        Serial.println(F("*** NEW FILE ***"));
        Serial.println(FileName);
      }
      else if(bFirstPass) {
        Serial.println(F("*** RESET ***"));
        Serial.print(F(" Appending to: "));
        Serial.println(FileName);
      }
      else if(bCardOut) {
        Serial.print(F("Appending to: "));
        Serial.println(FileName);
      }
     
    
      // Open the file. 
      // Note that only one file can be open at a time,
      // so you have to close this one before opening another.
      File dataFile = SD.open(FileName.c_str(), FILE_WRITE);
  
      // if the file is available, write to it, blip LED green:
      if (dataFile) {
        digitalWrite(LED_GRN, HIGH);

        Serial.print(F("Opened: "));
        Serial.print(FileName);

        if(bNewFile) {
          // as this is a new file, add some human friendly headers at the start:
          // a general header for ID'ing the colums
          dataFile.println(F("Status,Time,Ip1,Ip2,Ip3,Ip4,Ip5,Ip6"));
          dataFile.println(F("------,----,----,----,----,----,----"));
          String StartInfo = DOWStr + " " + DateStr;
          // insert start Day/Date of file (may not be top of the day 00:00:00)
          dataFile.println(StartInfo);
        }
        // insert status info; *should only appear in the first column*
        if(bFirstPass) {
          dataFile.println(F("*** RESET ***"));
        }
        if(bCardOut) {
          dataFile.println(F("*** CARD INSERTED ***"));
        }

        // log the readings to the SD card
        dataFile.print(LoggerString);
        
        // print to the serial port too:
        Serial.print(F(" >> "));
        Serial.println(LoggerString);

        LoggerString = "";

        dataFile.close();
        dataFile.flush();
      
        digitalWrite(LED_GRN, LOW);
      }
      // if the file can't be opened, pop up an error, blip LED red:
      else {
        Serial.print(F("error opening "));
        Serial.println(FileName);
        FlashLED(LED_RED, 10);
      }
      SD.end();
      bCardOut = 0;
    } 
    else {
      Serial.println(F("SD card inserted?"));
      FlashLED(LED_RED, 3);
      bCardOut = 1;
    }
    bFirstPass = 0;
  }
}


// flash the selected LED x times at specified rate
void FlashLED(int pin, int times, int Ton, int Toff)
{
  while(times--) {
    digitalWrite(pin, HIGH);
    delay(Ton);
    digitalWrite(pin, LOW);
    delay(Toff);
  }
}

