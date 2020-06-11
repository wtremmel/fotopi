//
// SleepyPi Routines for the FotoPi Project
//
//  Idea is that we have stages:
// setup --> starting --> running --> stopping --> sleeping --> Starting
// also there is an error state
//

#define S_SETUP 0
#define S_STARTING 1
#define S_RUNNING 2
#define S_STOPPING 3
#define S_SLEEPING 4
#define S_ERROR 99



// **** INCLUDES *****
#include "SleepyPi2.h"
#include <TimeLib.h>
#include <LowPower.h>
#include <PCF8523.h>
#include <Wire.h>
#include <ArduinoLog.h>

// Constants
const int LED_PIN = 13;
const int sleepFor = 60;

uint8_t state = S_SETUP;
DateTime stateChange;

// Global variables
bool have_rtc = false;


// Logging helper routines
void printTimestamp(Print* _logOutput) {
  char c[20];
  if (!have_rtc) {
    sprintf(c, "%10lu ", millis());
  } else {
    DateTime now = SleepyPi.readTime();
    sprintf(c,"%4d-%02d-%02d %02d:%02d:%02d ",
      now.year(),
      now.month(),
      now.day(),
      now.hour(),
      now.minute(),
      now.second());
  }
  _logOutput->print(c);
}

void printNewline(Print* _logOutput) {
  _logOutput->print('\n');
}

void blink() {
  digitalWrite(LED_PIN,LOW);
  digitalWrite(LED_PIN,HIGH);
  delay(50);
  digitalWrite(LED_PIN,LOW);
}

void setup_serial() {
  Serial.begin(9600);
}

void setup_logging() {
  Log.begin(LOG_LEVEL_VERBOSE, &Serial);
  Log.setPrefix(printTimestamp);
  Log.setSuffix(printNewline);
  Log.verbose(F("Logging has started"));
}

void setup_sleepy() {
  SleepyPi.enablePiPower(true);
  SleepyPi.enableExtPower(true);
  have_rtc = SleepyPi.rtcInit(true);
}

void setup() {
  state=S_SETUP;
  setup_sleepy();
  setup_serial();
  setup_logging();
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN,LOW);            // Switch off LED
  state=S_STARTING;
  stateChange = SleepyPi.readTime();
}

bool loop_reportVoltage() {
  float v = SleepyPi.supplyVoltage(),
        a = SleepyPi.rpiCurrent();
  bool retval = false;

  if (v > 0.0) {
    Log.notice(F("Voltage: %F"), v);
    retval = true;
  }
  if (a > 0.0) {
    Log.notice(F("Current: %F"), a);
    retval = true;
  }
  return retval;
}

void loop() {
  DateTime now = SleepyPi.readTime();
  TimeSpan lastChange = now - stateChange;

  // Every minute report current and voldate
  if (now.second() == 0) {
    if (loop_reportVoltage()) {
      delay(1000);
    }
  }

  if (state == S_STARTING) {
    if (SleepyPi.checkPiStatus(false)) {
      // State change to S_RUNNING
      state = S_RUNNING;
      Log.notice(F("PI is now running"));
      stateChange = now;
    } else {
      // PI is still starting - check time
      if (lastChange.totalseconds() > 120) { // longer than 2 minutes startup
        state = S_ERROR;
        Log.notice(F("PI taking too long to start up"));
        stateChange = now;
      }
    }
  }
  else if (state == S_RUNNING) {
    if (SleepyPi.checkPiStatus(false)) {
      // still running
      blink();
    } else {
      // shutting down
      state = S_STOPPING;
      stateChange = now;
      Log.notice("PI is stopping");
    }
  }
  else if (state == S_STOPPING) {
    // cut power if stopped
    if (SleepyPi.checkPiStatus(false)) {
      // oops, we are still running
      state = S_RUNNING;
      stateChange = now;
      Log.notice(F("PI is still running"));
    } else {
      state = S_SLEEPING;
      stateChange = now;
      Log.notice(F("PI has stopped"));
      SleepyPi.enablePiPower(false);
      SleepyPi.enableExtPower(false);
    }
  }
  else if (state == S_SLEEPING){
    // check if we are really sleeping
    if (SleepyPi.checkPiStatus(false)) {
      // no we are not sleeping, we are running
      Log.notice(F("PI woke up by itself"));
      state = S_RUNNING;
      stateChange = now;
    }
    // check how long we are sleeping
    else if (lastChange.totalseconds() >= sleepFor) {
      // wake up
      SleepyPi.enablePiPower(true);
      SleepyPi.enableExtPower(true);
      state = S_STARTING;
      stateChange = now;
      Log.notice(F("PI waking up after %ls sleep"),lastChange.totalseconds());
    } else {
      blink();
    }
  }
  else if (state == S_ERROR) {
    // something is wrong
    if (SleepyPi.checkPiStatus(false)) {
      state = S_STARTING;
      stateChange = now;
      Log.notice(F("PI recovered"));
    }
  }
}
