# lynk-tuya-esp-local
Control Tuya Devices from ESP32 over local network


## Setup:
1. Install libs:
*  [FastBot2](https://github.com/GyverLibs/FastBot2/)

2. Create secrets.h and define next variables:
```
IPAddress plugIP(192, 168, 1, 11);
const uint8_t localKey[16] = {/*key from tinyTuya */}

const String WIFI_SSID = "MY_WIFI";
const String WIFI_PASS = "MY_PASSWORD";
const String BOT_TOKEN = "1234567890:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
const String CHAT_ID = "123456789";
```
