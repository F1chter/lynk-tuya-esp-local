# lynk-tuya-esp-local
Control Tuya Devices from ESP32 over local network

Supported v3.4 and v3.5 devices

## Setup:
1. Use [TinyTuya](https://github.com/jasonacox/tinytuya/) to define device ip and Local Key

2. Create secrets.h and define next variables:
```
IPAddress plug1IP(192, 168, 1, 11); /*device ip, recommended to make it predefined on router */
#define PLUG1KEY "$`pfDH'nzZKIe]|]" /*local key from tinyTuya */
#define WIFI_SSID "MY_WIFI"
#define WIFI_PASS "MY_PASSWORD"

```
