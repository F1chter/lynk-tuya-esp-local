/* IMPORTS */
#include "time.h"

/* CONSTANTS */
#define NTP_SYNC_TIME_INTERVAL 86400000  //1d
#define NTP_SERVER "pool.ntp.org"

/* VARIABLES */
uint32_t localTimestamp;        //in seconds since 1/1/1970)
uint32_t lastTimeUpdateMillis;  //localTimestamp update millis
uint32_t lastTimeSyncMillis;  //time sync millis
byte timebuf[10];

bool syncWithNTP() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return false;
  }
  time(&now);
  localTimestamp = now;
  return true;
}

void timeBegin() {
   configTime(0, 0, NTP_SERVER);
   syncWithNTP();
}

void updateTime() {
  uint32_t now = millis();
  if(now - lastTimeSyncMillis > NTP_SYNC_TIME_INTERVAL) {
      //TODO
      lastTimeSyncMillis = now;
  }
  
  localTimestamp = localTimestamp + ((now - lastTimeUpdateMillis) / 1000);
  lastTimeUpdateMillis = now;
}

void updateTimeAndAppend(String &s) {
  updateTime();
  char locbuf[10];
  ltoa(localTimestamp, locbuf, 10); //fun fact, it will broken in 2287 year;)
  s+=locbuf;
}