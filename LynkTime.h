#include "time.h"

uint32_t loctimestamp;  // local time stamp ( Recup from php get msg = unix time in seconds since 1/1/1970)
uint32_t lastupdate;    // local time stamp ( Recup from php get msg = unix time in seconds since 1/1/1970)
byte timebuf[10];


unsigned long getTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    //Serial.println("Failed to obtain time");
    return (0);
  }
  time(&now);
  return now;
}

void updateTime() {
  
char locbuf[10] ;

    loctimestamp  = loctimestamp  + ( ( millis() - lastupdate ) / 1000 ) ;  // Timestamp adjust (from connection)
    lastupdate    = millis() ; 
    ltoa( loctimestamp, locbuf, 10 ); 
    Serial.print("---- loctimestamp :"); Serial.println(loctimestamp); 
    Serial.print("----  timebuf:");
    for ( int i=0; i < 10   ; i++)
    {
        timebuf[i] = locbuf[i];
        if ( timebuf[i] < 16) {Serial.print("0");}
        Serial.print(timebuf[i],HEX);
    }
    Serial.println(); 
}