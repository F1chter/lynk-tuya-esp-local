#define NTP_SERVER "pool.ntp.org"

enum class SkipGroup {
  None,
  River,
  Heaters,
  Charge
};

struct ScenarioStep {
  const char* name;
  bool (*executeFunction)();
  uint32_t delayMs;
  SkipGroup skipGroup;
};

void timeBegin() {
  configTime(0, 0, NTP_SERVER);

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
  }
}

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
