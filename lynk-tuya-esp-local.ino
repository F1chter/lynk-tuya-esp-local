// ESP32 + Arduino IDE
// Tuya local protocol v3.4 - Step 1: open TCP connection, send Command 3
// (negotiate session key). This is the first message of the handshake
// described in GetKey.ino of FrBerger83/EspTuya, reimplemented from scratch
// using mbedtls (already bundled with the ESP32 Arduino core).

#include "secrets.h"
#include <WiFi.h>
#include "LynkTime.h"
#include "LynkTuya.h"

LynkTuyaDevice<TUYA_V34> plug1(plug1IP,PLUG1KEY);

WiFiClient client2;

void setup() {
  //String s;
  //s.getBytes(unsigned char *buf, unsigned int bufsize)
  Serial.begin(115200);
  delay(3000);
  client2.read();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print(F("Connecting to WiFi"));
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print('.');
  }
  Serial.println();
  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());
  WiFi.setSleep(false);  // power-save mode can delay/drop fast LAN replies

  timeBegin();

  //if(!plug1.connectToPlug()) return;

  plug1.getStatus();

  delay(5000);

  updateTime();
  plug1.enable(localTimestamp);

  delay(5000);

  updateTime();
  plug1.disable(localTimestamp);
}

void loop() {
  delay(100);
}
