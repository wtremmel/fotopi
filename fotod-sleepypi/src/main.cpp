//
// SleepyPi Routines for the FotoPi Project
//
//  Idea is that we have stages:
// setup --> starting --> running --> stopping
//  --> stopped --> sleeping --> Starting
// also there is an error state
//

#define S_SETUP 0
#define S_STARTING 1
#define S_RUNNING 2
#define S_STOPPING 3
#define S_STOPPED 4
#define S_SLEEPING 5
#define S_ERROR 99



// **** INCLUDES *****
#include "SleepyPi2.h"
#include <TimeLib.h>
#include <LowPower.h>
#include <PCF8523.h>
#include <Wire.h>
#include <ArduinoLog.h>

#define RUNNING_THRESHOLD 200

// Constants
const int LED_PIN = 13;
const uint8_t sleepFor = 120; // in seconds

uint8_t state = S_SETUP;
unsigned long stateChange;

// Global variables
bool have_rtc = false;


// Logging helper routines
void printTimestamp(Print* _logOutput) {
  char c[40];
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

void blink(int howoften=1) {
  int i;
  digitalWrite(LED_PIN,LOW);
  delay(100);
  for (i=0; i < howoften; i++) {
    delay(100);
    digitalWrite(LED_PIN,HIGH);
    delay(100);
    digitalWrite(LED_PIN,LOW);
  }
}

void alarm_isr() {
}

uint8_t CalcNextWakeTime(uint8_t minutes) {
  DateTime now = SleepyPi.readTime();
  uint8_t nextWakeTime;

  nextWakeTime = now.minute() + minutes;
  if(nextWakeTime == 60){
      nextWakeTime = 0;
  }
  else if (nextWakeTime > 60){
      nextWakeTime = nextWakeTime - 60;
  }

  return nextWakeTime;
}

void setup_serial() {
  Serial.begin(9600);
}

void setup_logging() {
  Log.begin(LOG_LEVEL_VERBOSE, &Serial, true);
  Log.setPrefix(printTimestamp);
  Log.setSuffix(printNewline);
  Log.verbose(F("Logging has started"));
}

void setup_sleepy() {
  SleepyPi.enablePiPower(true);
  SleepyPi.enableExtPower(true);
  have_rtc = SleepyPi.rtcInit(false);
}

void setup() {
  state=S_SETUP;
  setup_sleepy();
  setup_serial();
  setup_logging();
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN,LOW);            // Switch off LED
  state=S_STARTING;
  stateChange = millis();
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
  unsigned long currentMillis = millis();

  // check if we already have stopped, if yes, cut power
  if (state != S_STOPPED &&
      (currentMillis - stateChange) > (120l*1000l) && 
      !SleepyPi.checkPiStatus(RUNNING_THRESHOLD,false)) {
    state = S_STOPPING;
    stateChange = currentMillis;
    Log.notice(F("PI seems no longer running"));
  }

  // check if we are more than 5 minutes in the same state -> ERROR
  if ((currentMillis - stateChange) > 5l*60l*1000l) {
    state = S_ERROR;
    Log.notice(F("PI too long not state change"));
    stateChange = currentMillis;
  }

  if ((currentMillis % 10000) == 0) {
    loop_reportVoltage();
    Log.verbose(F("state is %d"),state);
    blink(state);
  }

  if (state == S_STARTING) {
    if (SleepyPi.checkPiStatus(false)) {
      // State change to S_RUNNING
      state = S_RUNNING;
      Log.notice(F("PI is now running"));
      stateChange = currentMillis;
    } else {
      // PI is still starting - check time
      if (currentMillis - stateChange > 120000) { // longer than 2 minutes startup
        state = S_ERROR;
        Log.notice(F("PI taking too long to start up"));
        stateChange = currentMillis;
      }
    }
  }
  else if (state == S_RUNNING) {
    if (SleepyPi.checkPiStatus(false)) {
      // still running
      // blink();
    } else {
      // shutting down
      state = S_STOPPING;
      stateChange = currentMillis;
      Log.notice(F("PI is stopping"));
    }
  }
  else if (state == S_STOPPING) {
    // cut power if stopped
    if (SleepyPi.checkPiStatus(false)) {
      // oops, we are still running
      // are we?
      if (SleepyPi.checkPiStatus(RUNNING_THRESHOLD,false)) {
        // really running
        Log.notice(F("PI is still running, forcing stop"));
      } else {
        Log.notice(F("PI has stopped"));
      }
    }
    state = S_STOPPED;
    stateChange = currentMillis;
  }
  else if (state == S_STOPPED) {
    state = S_SLEEPING;
    stateChange = currentMillis;
    Log.notice(F("cutting power"));
    SleepyPi.enablePiPower(false);
    SleepyPi.enableExtPower(false);
  }
  else if (state == S_SLEEPING){
    uint8_t wakeMin = CalcNextWakeTime(sleepFor/60);
    // use RTC to go to sleep
    attachInterrupt(0,alarm_isr,FALLING);
    SleepyPi.enableWakeupAlarm(true);
    SleepyPi.setAlarm(wakeMin);
    Log.notice(F("stopping Controller until %d"),wakeMin);
    delay(500);
    digitalWrite(LED_PIN,HIGH);
    SleepyPi.powerDown(SLEEP_FOREVER,ADC_OFF,BOD_OFF);
    detachInterrupt(0);
    SleepyPi.ackAlarm();
    digitalWrite(LED_PIN,LOW);
    Log.notice(F("starting Controller"));
    state = S_STARTING;
    SleepyPi.enablePiPower(true);
    SleepyPi.enableExtPower(true);
    stateChange = currentMillis;
  }
  else if (state == S_ERROR) {
    // something is wrong
    if (SleepyPi.checkPiStatus(false)) {
      Log.notice(F("PI recovered"));
    } else {
      // cut power and see what happens
      Log.notice(F("PI power cycling"));
      SleepyPi.enablePiPower(false);
      SleepyPi.enableExtPower(false);
      delay(1000);
      SleepyPi.enablePiPower(true);
      SleepyPi.enableExtPower(true);
    } 
    stateChange = currentMillis;
    state = S_STARTING;
  }
}
