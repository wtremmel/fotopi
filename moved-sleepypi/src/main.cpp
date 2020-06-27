//
// SleepyPi Routines for the FotoPi Project
//
//  Idea is that we have stages:
// setup --> starting --> running --> stopping
//  --> stopped --> sleeping --> Starting
// also there is an error state
// in this version SleepyPi wakes up only when there is movement
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
#include <EnableInterrupt.h>


// using 100 for zero
#define RUNNING_THRESHOLD 100

// Constants
const int LED_PIN = 13;
const uint8_t pirInput = 9;

uint8_t state = S_SETUP;
uint8_t pirState = LOW;
uint8_t motion = 0, motion_change = 0;
unsigned long stateChange;

// Global variables
bool have_rtc = false;


// Logging helper routines
void printTimestamp(Print* _logOutput) {
  char c[40];
  sprintf(c, "%10lu ", millis());
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

void motion_start() {
  motion = 1;
  motion_change = 1;
}

void setup_serial() {
  Serial.begin(9600);
}

void setup_logging() {
  Log.begin(LOG_LEVEL_VERBOSE, &Serial, true);
  // Log.setPrefix(printTimestamp);
  Log.setSuffix(printNewline);
  Log.verbose(F("Logging has started"));
}

// setup PIR motion detector
void setup_pir() {
  pinMode(pirInput, INPUT);
  pirState = digitalRead(pirInput);
  enableInterrupt(pirInput,motion_start,RISING);
}

void setup_sleepy() {
  have_rtc = SleepyPi.rtcInit(false);
  delay(10);
  SleepyPi.enablePiPower(true);
  SleepyPi.enableExtPower(true);
}

void setup() {
  state=S_SETUP;
  setup_serial();
  setup_logging();
  setup_sleepy();
  setup_pir();
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

static unsigned long lastLog = 0l;
void loop() {
  unsigned long currentMillis = millis();

  // check for motion
  if (motion_change) {
    if (motion) {
      Log.verbose(F("motion started"));
    } else {
      Log.verbose(F("motion stopped"));
    }
    motion_change = 0;
  }

  pirState = digitalRead(pirInput);
  // High -> Low
  if (motion && pirState == LOW) {
    motion = 0;
    motion_change = 1;
  }


  if (currentMillis - lastLog > 10000) {
    loop_reportVoltage();
    Log.verbose(F("state is %d"),state);
    blink(state);
    lastLog = currentMillis;
  }

  // check if we already have stopped, if yes, cut power
  if (state != S_STOPPED &&
      (currentMillis - stateChange) > (120l*1000l) && 
      !SleepyPi.checkPiStatus(RUNNING_THRESHOLD,false)) {
    state = S_STOPPING;
    stateChange = currentMillis;
    Log.notice(F("PI seems no longer running"));
    blink(state);
  }

  // check if we are more than 5 minutes in the same state -> ERROR
  if ((currentMillis - stateChange) > 5l*60l*1000l) {
    state = S_ERROR;
    Log.notice(F("PI too long not state change"));
    stateChange = currentMillis;
    blink(state);
  }

  // we keep running during motion
  if (motion && state != S_STARTING) {
    state = S_RUNNING;
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
    // use RTC to go to sleep
    // attachInterrupt(0,alarm_isr,FALLING);
    enableInterrupt(2,alarm_isr,FALLING);
    SleepyPi.enableWakeupAlarm(true);
    SleepyPi.setAlarm(0);
    Log.notice(F("stopping Controller until next full hour"));
    delay(500);
    digitalWrite(LED_PIN,HIGH);
    SleepyPi.powerDown(SLEEP_FOREVER,ADC_OFF,BOD_OFF);
    disableInterrupt(2);
    SleepyPi.ackAlarm();
    digitalWrite(LED_PIN,LOW);
    if (motion) {
      Log.verbose(F("woke up because of motion"));
    } else {
      Log.verbose(F("woke up because of time"));
    }
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
  } else {
    // unknown state, should never happen
    blink(state);
    Log.notice(F("PI unknown state"));
    SleepyPi.enablePiPower(false);
    SleepyPi.enableExtPower(false);
    delay(1000);
    SleepyPi.enablePiPower(true);
    SleepyPi.enableExtPower(true);
    state = S_STARTING;
  }
}
