#include "secrets.h"
#include <WiFi.h>
#include "LynkFile.h"
#include "LynkTime.h"
#include "LynkTuya.h"
#include "LynkTelegramBot.h"


#define DEFAULT_DELAY_BETWEEN_STEPS 30000  //30s
#define LED_BUILTIN 8

LynkTuyaDevice<TUYA_V34> towelDryerPlug(towelip, TOWELKEY);
LynkTuyaDevice<TUYA_V35> riverPlug(riverip, RIVERKEY);
LynkTuyaDevice<TUYA_V35> firstHeaterPlug(heater1ip, HEATER1KEY);
LynkTuyaDevice<TUYA_V35> secondHeaterPlug(heater2ip, HEATER2KEY);
LynkTuyaDevice<TUYA_V34> thirdHeaterPlug(heater3ip, HEATER3KEY);
LynkTuyaDevice<TUYA_V34> batteryChargerPlug(chargerIP, CHARGERKEY);
LynkTuyaDevice<TUYA_V35> boilerPlug(boilerIP, BOILERKEY);

struct ConfigStruct {
  bool skipCharge = false;   //skip batteryChargerPlug
  bool skipRiver = false;    //skip riverPlug
  bool skipHeaters = false;  //skip firstHeaterPlug, secondHeaterPlug, thirdHeaterPlug
} config;
LynkFile configFile(&LittleFS, "/config.cfg", 1, &config, sizeof(config));
uint8_t scenarioStep = 0;
uint32_t lastScenarioStepMillis = 0;
bool skipChargeFlag = false;

void setup() {
  Serial.begin(115200);
  LittleFS.begin(true);
  FileStatus fileStatus = configFile.init();
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, false);
  delay(1000);
  connectWifi();
  timeBegin();
  _sendStatusFunction = sendStatus;
  _rerunFunction = rerunScenario;
  _skipChargeFunction = skipCharge;
  _skipRiverFunction = skipRiver;
  _skipHeatersFunction = skipHeaters;
  setupTelegram();
  lastScenarioStepMillis = millis();
}

void loop() {
  tickTelegram();
  tickScenario();
  delay(1);
}

//TODO ENUM and status function
void tickScenario() {
  if (scenarioStep > 6) return;
  uint32_t now = millis();
  if (scenarioStep == 0 && now - lastScenarioStepMillis > DEFAULT_DELAY_BETWEEN_STEPS) {
    updateTime();
    towelDryerPlug.turnOn(localTimestamp);
    scenarioStep++;
    lastScenarioStepMillis = now;
  } else if (scenarioStep == 1 && config.skipRiver) {
    scenarioStep++;
  } else if (scenarioStep == 1 && now - lastScenarioStepMillis > DEFAULT_DELAY_BETWEEN_STEPS) {
    updateTime();
    riverPlug.turnOn(localTimestamp);
    scenarioStep++;
    lastScenarioStepMillis = now;
  } else if ((scenarioStep == 2 || scenarioStep == 3 || scenarioStep == 4) && config.skipHeaters) {
    scenarioStep = 5;
  } else if (scenarioStep == 2 && now - lastScenarioStepMillis > 300000) {  //5m
    updateTime();
    firstHeaterPlug.turnOn(localTimestamp);
    scenarioStep++;
    lastScenarioStepMillis = now;
  } else if (scenarioStep == 3 && now - lastScenarioStepMillis > DEFAULT_DELAY_BETWEEN_STEPS) {
    updateTime();
    secondHeaterPlug.turnOn(localTimestamp);
    scenarioStep++;
    lastScenarioStepMillis = now;
  } else if (scenarioStep == 4 && now - lastScenarioStepMillis > DEFAULT_DELAY_BETWEEN_STEPS) {
    updateTime();
    thirdHeaterPlug.turnOn(localTimestamp);
    scenarioStep++;
    lastScenarioStepMillis = now;
  } else if (scenarioStep == 5 && config.skipCharge) {
    scenarioStep++;
  } else if (scenarioStep == 5 && now - lastScenarioStepMillis > DEFAULT_DELAY_BETWEEN_STEPS) {
    updateTime();
    batteryChargerPlug.turnOn(localTimestamp);
    scenarioStep++;
    lastScenarioStepMillis = now;
  } else if (scenarioStep == 6 && now - lastScenarioStepMillis > DEFAULT_DELAY_BETWEEN_STEPS) {
    updateTime();
    boilerPlug.turnOn(localTimestamp);
    scenarioStep++;
    lastScenarioStepMillis = now;
    digitalWrite(LED_BUILTIN, true);
  }
}

//TODO track charger
//Received Payload: {'protocol': 4, 't': 1782206843, 'data': {'dps': {'23': 2148, '21': 611, '22': 1273}}, 'dps': {'23': 2148, '21': 611, '22': 1273}}
//23 voltage*10, 21 mA, 22 watt*10
void connectWifi() {
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
}

void rerunScenario() {
  digitalWrite(LED_BUILTIN, false);
  scenarioStep = 0;
  lastScenarioStepMillis = millis();
  sendToChat("Перезапускаю...");
}

void skipCharge(bool skip) {
  if (skip != config.skipCharge) {
    config.skipCharge = skip;
    configFile.commit();
    sendToChat("Ок");
  } else sendToChat("Це ж було вже");
}

void skipRiver(bool skip) {
  if (skip != config.skipRiver) {
    config.skipRiver = skip;
    configFile.commit();
    sendToChat("Ок");
  } else sendToChat("Це ж було вже");
}

void skipHeaters(bool skip) {
  if (skip != config.skipHeaters) {
    config.skipHeaters = skip;
    configFile.commit();
    sendToChat("Ок");
  } else sendToChat("Це ж було вже");
}

void sendStatus() {
  String s = F("Сценарій ");
  if (scenarioStep > 6) s += F("завершено");
  else {
    s += F("на кроці ");
    s += scenarioStep;
  }
  sendToChat(s);
}
