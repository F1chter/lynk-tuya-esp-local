#include "secrets.h"
#include "time.h"
#include "helper.h"
#include <WiFi.h>
#include "LynkFile.h"
#include "LynkTuya.h"
#include "LynkTelegramBot.h"


#define DEFAULT_DELAY_BETWEEN_STEPS 30000    //30s
#define DEFAULT_DELAY_BETWEEN_RETRIES 10000  //10s
#define DELAY_BEFORE_HEATERS 300000          //5m
#define LED_BUILTIN 8
#define RETRY_COUNT 3

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

bool shouldSkip(SkipGroup group) {
  switch (group) {
    case SkipGroup::River: return config.skipRiver;
    case SkipGroup::Heaters: return config.skipHeaters;
    case SkipGroup::Charge: return config.skipCharge;
    default: return false;
  }
}

#define STEP_COUNT 7
constexpr ScenarioStep scenario[] = {
  { "Towel dryer", []() {
     return towelDryerPlug.turnOn();
   },
    DEFAULT_DELAY_BETWEEN_STEPS, SkipGroup::None },
  { "River", []() {
     return riverPlug.turnOn();
   },
    DEFAULT_DELAY_BETWEEN_STEPS, SkipGroup::River },
  { "Heater 1", []() {
     return firstHeaterPlug.turnOn();
   },
    DELAY_BEFORE_HEATERS, SkipGroup::Heaters },
  { "Heater 2", []() {
     return secondHeaterPlug.turnOn();
   },
    DEFAULT_DELAY_BETWEEN_STEPS, SkipGroup::Heaters },
  { "Heater 3", []() {
     return thirdHeaterPlug.turnOn();
   },
    DEFAULT_DELAY_BETWEEN_STEPS, SkipGroup::Heaters },
  { "Battery charger", []() {
     return batteryChargerPlug.turnOn();
   },
    DEFAULT_DELAY_BETWEEN_STEPS, SkipGroup::Charge },
  { "Boiler", []() {
     return boilerPlug.turnOn();
   },
    DEFAULT_DELAY_BETWEEN_STEPS, SkipGroup::None }
};

uint8_t scenarioStep = 0;
uint32_t lastScenarioStepMillis = 0;
bool skipChargeFlag = false;
uint8_t retries = 0;

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

void tickScenario() {
  if (scenarioStep >= STEP_COUNT) return;
  uint32_t now = millis();
  auto& step = scenario[scenarioStep];
  if (shouldSkip(step.skipGroup)) {
    scenarioStep++;
    return;
  }
  if (now - lastScenarioStepMillis >= step.delayMs) {
    if (step.executeFunction()) {
      scenarioStep++;
      retries = 0;
      lastScenarioStepMillis = now;
    } else {
      retries++;
      lastScenarioStepMillis += DEFAULT_DELAY_BETWEEN_RETRIES;
    }
    if (retries >= RETRY_COUNT) {
      String s = "Не вдалося виконати крок ";
      s += step.name;
      sendToAdmin(s);
      scenarioStep++;
      retries = 0;
      lastScenarioStepMillis = now;
    }

    if (scenarioStep >= STEP_COUNT) {  //Scenario finished
      sendToAdmin(F("Виконання сценарію завершено"));
      digitalWrite(LED_BUILTIN, true);
    }
  }
}

//TODO track charger
//Received Payload: {'protocol': 4, 't': 1782206843, 'data': {'dps': {'23': 2148, '21': 611, '22': 1273}}, 'dps': {'23': 2148, '21': 611, '22': 1273}}
//23 voltage*10, 21 mA, 22 watt*10

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
  if (scenarioStep >= STEP_COUNT) s += F("завершено");
  else {
    s += F("на кроці ");
    s += scenario[scenarioStep].name;
  }
  sendToChat(s);
}
