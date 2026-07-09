# lynk-tuya-esp-local
An ESP32-based automation controller that powers on multiple Tuya smart plugs in a predefined sequence with configurable delays, retry handling, and Telegram remote control. It uses after grid appear to not turn on all load simultaneously.

## Features

* Sequential startup of multiple electrical devices
* Configurable delays between startup steps
* Automatic retry on failed commands
* Telegram bot integration

## Setup:
1.Install libs:
-[LynkTuyaLocal](https://github.com/F1chter/LynkTuyaLocal)
-[Fastbot2](https://github.com/GyverLibs/FastBot2)

2. Use [TinyTuya](https://github.com/jasonacox/tinytuya/) to define device ip and Local Key

3. Create secrets.h and define next variables:
```
IPAddress plug1IP(192, 168, 1, 11); /*device ip, recommended to make it predefined on router */
#define PLUG1KEY "$`pfDH'nzZKIe]|]" /*local key from tinyTuya */
#define WIFI_SSID "MY_WIFI"
#define WIFI_PASS "MY_PASSWORD"
#define BOT_TOKEN "123456789:AABBCCDDEEFFAABBCCDDEEFFAABBCCDDEEFF" //from FatherBot
#define ADMIN_CHAT_ID "123456789"
#define GROUP_CHAT_ID "-123456789"
```




## Scenario Logic

Each step consists of:

* Device name
* Function that turns the device on
* Delay before execution
* Optional skip group

Example:

```
Heater 2
 ├─ Wait 30 s
 ├─ Send ON command
 ├─ Success → next step
 └─ Failure → retry
```

---

## Retry Logic

If a device fails to turn on:

* wait **10 seconds**
* retry
* maximum **3 attempts**

After three failed attempts:

* administrator receives a Telegram notification
* the scenario continues with the next device

This prevents one faulty device from blocking the entire startup sequence.

---

## Skip Groups

Some devices can be permanently skipped until the setting is changed.

Available groups:

| Group   | Devices                      |
| ------- | ---------------------------- |
| Charge  | Battery charger              |
| River   | River plug                   |
| Heaters | Heater 1, Heater 2, Heater 3 |

The skip configuration is stored in **LittleFS**, so it survives reboots.

---

## Telegram Commands

The firmware exposes several actions through the Telegram bot.

### Status

Returns the current scenario state.

Example:

```
Scenario at step Heater 2
```

or

```
Scenario finished
```

---

### Restart Scenario

Resets the scenario to the beginning.

Actions performed:

* turns off completion LED
* resets current step
* starts execution from the first device

---

### Enable/Disable Skip Groups

Available for:

* Battery charger
* River plug
* Heaters


## Timing Constants

| Constant                        | Value | Description                   |
| ------------------------------- | ----: | ----------------------------- |
| `DEFAULT_DELAY_BETWEEN_STEPS`   |  30 s | Default delay between devices |
| `DEFAULT_DELAY_BETWEEN_RETRIES` |  10 s | Retry interval                |
| `DELAY_BEFORE_HEATERS`          | 5 min | Extra delay before heaters    |
| `RETRY_COUNT`                   |     3 | Maximum retries               |

---

