#include <Arduino.h>
#include <WiFi.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <ld2410.h>

// =====================================================
// Config
// =====================================================
#define WEBAPP_PREFS_NAMESPACE "webapp"
#define WEBAPP_IP_KEY "ip"

#define WIFI_PREFS_NAMESPACE "wifi"
#define WIFI_SSID_KEY "ssid"
#define WIFI_PASS_KEY "password"

#define DHTPIN1 2
#define DHTTYPE1 DHT22

#define CURRENT_PIN 32
#define PIR_PIN 5

// Speed relays
#define RELAY1_PIN 13
#define RELAY2_PIN 12
#define RELAY3_PIN 23

// Master relay (independent, NOT included in payload)
#define MASTER_RELAY_PIN 14   // active LOW

// Buttons
#define BUTTON1_PIN 26
#define BUTTON2_PIN 19
#define BUTTON3_PIN 33
#define BUTTON4_PIN 27
#define BUTTON5_PIN 25   // master relay only

// Radar UART
#define LD2410C_RX_PIN 16
#define LD2410C_TX_PIN 17

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// Timing
const unsigned long DEBOUNCE_DELAY_MS = 250;
const unsigned long LED_STATE_SYNC_INTERVAL_MS = 5000;
const unsigned long COMMAND_CHECK_INTERVAL_MS = 50;
const unsigned long RECONNECT_INTERVAL_MS = 5000;
const unsigned long RADAR_UPDATE_INTERVAL_MS = 50;
const unsigned long RADAR_PRESENCE_HOLD_MS = 3000;
const unsigned long OLED_UPDATE_INTERVAL_MS = 200;
const unsigned long ENV_UPDATE_INTERVAL_MS = 2000;
const unsigned long BROADCAST_INTERVAL_MS = 2000;

// Automatic control thresholds
const float AUTO_TEMP_LOW = 0.0f;
const float AUTO_TEMP_MED = 29.0f;
const float AUTO_TEMP_HIGH = 31.0f;
const float AUTO_HUMIDITY_BUMP = 75.0f;

// =====================================================
// Globals
// =====================================================
WiFiServer server(80);
Preferences preferences;
DHT dht1(DHTPIN1, DHTTYPE1);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
ld2410 radar;

String webAppHost = "192.168.10.102";
const int webAppPort = 5000;

String storedSSID = "";
String storedPassword = "";
String lastBroadcastJson = "";

float temperature1 = NAN;
float humidity1 = NAN;
float current = NAN;

volatile bool motionDetected = false;
bool motionToSend = false;
bool wifiScanInProgress = false;
bool stateChanged = true;

// Button states
bool button1State = false;
bool button2State = false;
bool button3State = false;
bool button4State = false;

// Relay states
bool relay1State = false;
bool relay2State = false;
bool relay3State = false;
bool masterRelayState = true;

// Automation state
bool automationEnabled = true;
bool manualOverrideActive = false;

// Debounce timers
unsigned long lastButton1Time = 0;
unsigned long lastButton2Time = 0;
unsigned long lastButton3Time = 0;
unsigned long lastButton4Time = 0;
unsigned long lastButton5Time = 0;
unsigned long lastLedStateSyncTime = 0;
unsigned long lastOledUpdateTime = 0;
unsigned long lastEnvUpdateMs = 0;

// Radar state
int cachedPresenceValue = 0;
unsigned long lastRadarReadMs = 0;
unsigned long lastRadarSeenMs = 0;

// =====================================================
// Forward declarations
// =====================================================
void IRAM_ATTR pirISR();
void updateOLED(bool motion);

void setMasterRelay(bool on);
void turnAllSpeedRelaysOff();
void setSpeedRelay(int relayNum);
void readRelayStates();
void postLedStatesSync();

void updateEnvSensors();
void updateRadarPresence();

void disableAutomationForManualControl();
void controlFanAutomatically();

void handleButtons(unsigned long now);
void handleWebClient();
void handleBackendCommands();

void handleWifiConnect(WiFiClient& client, String body);
void handleSetWebAppIp(WiFiClient& client, String body);
void handleGetWebAppIp(WiFiClient& client);

void broadcastTask(void *pvParameters);
bool sendSensorDataToWebApp();

// =====================================================
// Relay helpers
// =====================================================
void setMasterRelay(bool on) {
  // active LOW
  digitalWrite(MASTER_RELAY_PIN, on ? LOW : HIGH);
  masterRelayState = on;
}

void turnAllSpeedRelaysOff() {
  digitalWrite(RELAY1_PIN, LOW);
  digitalWrite(RELAY2_PIN, LOW);
  digitalWrite(RELAY3_PIN, LOW);

  relay1State = false;
  relay2State = false;
  relay3State = false;
}

void setSpeedRelay(int relayNum) {
  turnAllSpeedRelaysOff();

  button1State = false;
  button2State = false;
  button3State = false;
  button4State = false;

  switch (relayNum) {
    case 1:
      digitalWrite(RELAY1_PIN, HIGH);
      relay1State = true;
      button1State = true;
      break;
    case 2:
      digitalWrite(RELAY2_PIN, HIGH);
      relay2State = true;
      button2State = true;
      break;
    case 3:
      digitalWrite(RELAY3_PIN, HIGH);
      relay3State = true;
      button3State = true;
      break;
    default:
      break;
  }

  stateChanged = true;
}

void readRelayStates() {
  relay1State = (digitalRead(RELAY1_PIN) == HIGH);
  relay2State = (digitalRead(RELAY2_PIN) == HIGH);
  relay3State = (digitalRead(RELAY3_PIN) == HIGH);
}

void postLedStatesSync() {
  static bool lastR1 = false, lastR2 = false, lastR3 = false;

  readRelayStates();

  if (relay1State == lastR1 && relay2State == lastR2 && relay3State == lastR3) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient stateClient;
  if (stateClient.connect(webAppHost.c_str(), webAppPort)) {
    String body = "{\"states\":{";
    body += "\"led1\":" + String(relay1State ? "true" : "false");
    body += ",\"led2\":" + String(relay2State ? "true" : "false");
    body += ",\"led3\":" + String(relay3State ? "true" : "false");
    body += "}}";

    stateClient.println("POST /api/led-states HTTP/1.1");
    stateClient.print("Host: ");
    stateClient.print(webAppHost);
    stateClient.print(":");
    stateClient.println(webAppPort);
    stateClient.println("Content-Type: application/json");
    stateClient.print("Content-Length: ");
    stateClient.println(body.length());
    stateClient.println("Connection: close");
    stateClient.println();
    stateClient.println(body);

    unsigned long start = millis();
    while (!stateClient.available() && (millis() - start < 300)) {
      delay(1);
    }

    while (stateClient.available()) {
      stateClient.read();
    }

    stateClient.stop();
    Serial.println(">>> LED states synced to server");
  }

  lastR1 = relay1State;
  lastR2 = relay2State;
  lastR3 = relay3State;
}

// =====================================================
// Sensors
// =====================================================
void updateEnvSensors() {
  if (millis() - lastEnvUpdateMs < ENV_UPDATE_INTERVAL_MS) return;
  lastEnvUpdateMs = millis();

  float newTemp = dht1.readTemperature();
  float newHum = dht1.readHumidity();

  if (!isnan(newTemp) && !isnan(newHum)) {
    temperature1 = newTemp;
    humidity1 = newHum;
  } else {
    temperature1 = NAN;
    humidity1 = NAN;
    Serial.println("Failed to read from DHT22 sensor");
  }

  int numSamples = 10;
  float totalVoltage = 0.0f;

  for (int i = 0; i < numSamples; i++) {
    int adcValue = analogRead(CURRENT_PIN);
    totalVoltage += adcValue * 3.3f / 4095.0f;
    delay(2);
  }

  float voltage = totalVoltage / numSamples;
  if (voltage < 0.3f || voltage > 3.0f) {
    current = NAN;
  } else {
    current = (voltage - 1.65f) / 0.185f;
  }
}

void updateRadarPresence() {
  unsigned long now = millis();
  if (now - lastRadarReadMs < RADAR_UPDATE_INTERVAL_MS) return;

  lastRadarReadMs = now;
  radar.read();

  bool detected = false;
  if (radar.isConnected() && radar.presenceDetected()) {
    detected = true;
    lastRadarSeenMs = now;
  }

  cachedPresenceValue = ((now - lastRadarSeenMs) <= RADAR_PRESENCE_HOLD_MS) ? 1 : 0;

  static int lastPrintedPresence = -1;
  if (cachedPresenceValue != lastPrintedPresence) {
    Serial.print("[RADAR] Presence: ");
    Serial.println(cachedPresenceValue);
    lastPrintedPresence = cachedPresenceValue;
  }

  static unsigned long lastRadarPrint = 0;
  if (now - lastRadarPrint >= 1000) {
    lastRadarPrint = now;

    if (detected) {
      if (radar.stationaryTargetDetected()) {
        Serial.print("Stationary target: ");
        Serial.print(radar.stationaryTargetDistance());
        Serial.print("cm energy:");
        Serial.print(radar.stationaryTargetEnergy());
        Serial.print(' ');
      }
      if (radar.movingTargetDetected()) {
        Serial.print("Moving target: ");
        Serial.print(radar.movingTargetDistance());
        Serial.print("cm energy:");
        Serial.print(radar.movingTargetEnergy());
      }
      Serial.println();
    } else {
      Serial.println("No target");
    }
  }
}

// =====================================================
// Automation
// =====================================================
void disableAutomationForManualControl() {
  automationEnabled = false;
  manualOverrideActive = true;
  stateChanged = true;
}

void controlFanAutomatically() {
  if (!automationEnabled || manualOverrideActive) return;

  if (isnan(temperature1) || isnan(humidity1)) {
    turnAllSpeedRelaysOff();
    button1State = false;
    button2State = false;
    button3State = false;
    button4State = false;
    stateChanged = true;
    return;
  }

  if (cachedPresenceValue != 1) {
    turnAllSpeedRelaysOff();
    button1State = false;
    button2State = false;
    button3State = false;
    button4State = false;
    stateChanged = true;
    return;
  }

  int speed = 0;

  if (temperature1 >= AUTO_TEMP_HIGH) speed = 3;
  else if (temperature1 >= AUTO_TEMP_MED) speed = 2;
  else if (temperature1 >= AUTO_TEMP_LOW) speed = 1;
  else speed = 0;

  if (humidity1 >= AUTO_HUMIDITY_BUMP && speed > 0 && speed < 3) {
    speed++;
  }

  bool changed = false;

  if (speed == 0) {
    if (relay1State || relay2State || relay3State) {
      turnAllSpeedRelaysOff();
      button1State = false;
      button2State = false;
      button3State = false;
      button4State = false;
      changed = true;
      Serial.println("[AUTO] Fan OFF");
    }
  } else {
    bool alreadyCorrect =
      (speed == 1 && relay1State) ||
      (speed == 2 && relay2State) ||
      (speed == 3 && relay3State);

    if (!alreadyCorrect) {
      setSpeedRelay(speed);
      changed = true;
      Serial.print("[AUTO] Fan speed set to ");
      Serial.println(speed);
    }
  }

  if (changed) {
    stateChanged = true;
  }
}

// =====================================================
// Buttons
// =====================================================
void handleButtons(unsigned long now) {
  static bool lastB1 = HIGH;
  static bool lastB2 = HIGH;
  static bool lastB3 = HIGH;
  static bool lastB4 = HIGH;
  static bool lastB5 = HIGH;

  bool b1 = digitalRead(BUTTON1_PIN);
  bool b2 = digitalRead(BUTTON2_PIN);
  bool b3 = digitalRead(BUTTON3_PIN);
  bool b4 = digitalRead(BUTTON4_PIN);
  bool b5 = digitalRead(BUTTON5_PIN);

  if (lastB1 == HIGH && b1 == LOW && now - lastButton1Time > DEBOUNCE_DELAY_MS) {
    disableAutomationForManualControl();

    if (relay1State) {
      turnAllSpeedRelaysOff();
      button1State = false;
      button2State = false;
      button3State = false;
      button4State = false;
      Serial.println("Button1 pressed: Relay1 OFF");
    } else {
      setSpeedRelay(1);
      Serial.println("Button1 pressed: Relay1 ON");
    }

    lastButton1Time = now;
    if (WiFi.status() == WL_CONNECTED) postLedStatesSync();
  }

  if (lastB2 == HIGH && b2 == LOW && now - lastButton2Time > DEBOUNCE_DELAY_MS) {
    disableAutomationForManualControl();

    if (relay2State) {
      turnAllSpeedRelaysOff();
      button1State = false;
      button2State = false;
      button3State = false;
      button4State = false;
      Serial.println("Button2 pressed: Relay2 OFF");
    } else {
      setSpeedRelay(2);
      Serial.println("Button2 pressed: Relay2 ON");
    }

    lastButton2Time = now;
    if (WiFi.status() == WL_CONNECTED) postLedStatesSync();
  }

  if (lastB3 == HIGH && b3 == LOW && now - lastButton3Time > DEBOUNCE_DELAY_MS) {
    disableAutomationForManualControl();

    if (relay3State) {
      turnAllSpeedRelaysOff();
      button1State = false;
      button2State = false;
      button3State = false;
      button4State = false;
      Serial.println("Button3 pressed: Relay3 OFF");
    } else {
      setSpeedRelay(3);
      Serial.println("Button3 pressed: Relay3 ON");
    }

    lastButton3Time = now;
    if (WiFi.status() == WL_CONNECTED) postLedStatesSync();
  }

  if (lastB4 == HIGH && b4 == LOW && now - lastButton4Time > DEBOUNCE_DELAY_MS) {
    disableAutomationForManualControl();

    turnAllSpeedRelaysOff();
    button1State = false;
    button2State = false;
    button3State = false;
    button4State = true;

    stateChanged = true;
    Serial.println("Button4 pressed: All speed relays OFF");

    lastButton4Time = now;
    if (WiFi.status() == WL_CONNECTED) postLedStatesSync();
  }

  // Master relay only, not included in payload
  if (lastB5 == HIGH && b5 == LOW && now - lastButton5Time > DEBOUNCE_DELAY_MS) {
    setMasterRelay(!masterRelayState);
    Serial.print("Button5 pressed: Master relay ");
    Serial.println(masterRelayState ? "ON" : "OFF");
    lastButton5Time = now;
  }

  lastB1 = b1;
  lastB2 = b2;
  lastB3 = b3;
  lastB4 = b4;
  lastB5 = b5;
}

// =====================================================
// Backend commands
// Supports:
//   led1_on / led1_off
//   led2_on / led2_off
//   led3_on / led3_off
//   automation_on / automation_off
// =====================================================
void handleBackendCommands() {
  static unsigned long lastCommandCheck = 0;
  static String lastProcessedCommand = "";

  unsigned long now = millis();
  if (WiFi.status() != WL_CONNECTED || now - lastCommandCheck < COMMAND_CHECK_INTERVAL_MS) return;

  lastCommandCheck = now;
  WiFiClient cmdClient;

  if (!cmdClient.connect(webAppHost.c_str(), webAppPort)) return;

  cmdClient.println("GET /api/command HTTP/1.1");
  cmdClient.print("Host: ");
  cmdClient.print(webAppHost);
  cmdClient.print(":");
  cmdClient.println(webAppPort);
  cmdClient.println("Connection: close");
  cmdClient.println();

  unsigned long startWait = millis();
  while (!cmdClient.available() && (millis() - startWait < 1000)) {
    delay(1);
  }

  String responseBody = "";

  while (cmdClient.available()) {
    String line = cmdClient.readStringUntil('\n');
    if (line == "\r" || line == "") break;
  }

  while (cmdClient.available()) {
    responseBody += (char)cmdClient.read();
  }

  cmdClient.stop();

  if (responseBody.length() == 0) return;

  int p = responseBody.indexOf("\"command\"");
  if (p < 0) return;

  int colon = responseBody.indexOf(':', p);
  if (colon < 0) return;

  String val = responseBody.substring(colon + 1);
  val.trim();
  if (val.endsWith("}")) val.remove(val.length() - 1);
  val.trim();

  if (val.startsWith("\"") && val.endsWith("\"")) {
    val = val.substring(1, val.length() - 1);
  }

  if (val.length() == 0 || val == "none" || val == lastProcessedCommand) {
    return;
  }

  lastProcessedCommand = val;

  if (val == "automation_on") {
    automationEnabled = true;
    manualOverrideActive = false;
    stateChanged = true;
    Serial.println(">>> Command: AUTOMATION ON");
    return;
  }

  if (val == "automation_off") {
    automationEnabled = false;
    manualOverrideActive = true;
    stateChanged = true;
    Serial.println(">>> Command: AUTOMATION OFF");
    return;
  }

  if (!val.startsWith("led")) return;

  int us = val.indexOf('_');
  if (us < 0) return;

  int ledNum = val.substring(3, us).toInt();
  String cmd = val.substring(us + 1);

  if (cmd == "on") {
    disableAutomationForManualControl();
    setSpeedRelay(ledNum);
    postLedStatesSync();
    Serial.print(">>> Command: LED");
    Serial.print(ledNum);
    Serial.println(" ON");
  } else if (cmd == "off") {
    disableAutomationForManualControl();

    if ((ledNum == 1 && relay1State) ||
        (ledNum == 2 && relay2State) ||
        (ledNum == 3 && relay3State)) {
      turnAllSpeedRelaysOff();
    }

    button1State = false;
    button2State = false;
    button3State = false;
    button4State = false;
    stateChanged = true;

    postLedStatesSync();
    Serial.print(">>> Command: LED");
    Serial.print(ledNum);
    Serial.println(" OFF");
  }
}

// =====================================================
// Web app POST
// Button 5 / Master relay NOT included
// =====================================================
bool sendSensorDataToWebApp() {
  WiFiClient client;
  if (!client.connect(webAppHost.c_str(), webAppPort)) {
    Serial.println(">>> POST failed: cannot connect to web app");
    return false;
  }

  String controlMode = "automatic";
  if (manualOverrideActive) {
    controlMode = "manual";
  }

  String jsonPayload = "{";
  jsonPayload += "\"temperature\":" + String(isnan(temperature1) ? 0 : temperature1, 1);
  jsonPayload += ",\"temperature1\":" + String(isnan(temperature1) ? 0 : temperature1, 1);
  jsonPayload += ",\"humidity1\":" + String(isnan(humidity1) ? 0 : humidity1, 1);
  jsonPayload += ",\"current\":" + String(isnan(current) ? 0 : current, 2);
  jsonPayload += ",\"motion\":" + String(motionToSend ? "true" : "false");
  jsonPayload += ",\"ld2410cHumanPresent\":" + String(cachedPresenceValue);
  jsonPayload += ",\"button1\":" + String(button1State ? "true" : "false");
  jsonPayload += ",\"button2\":" + String(button2State ? "true" : "false");
  jsonPayload += ",\"button3\":" + String(button3State ? "true" : "false");
  jsonPayload += ",\"button4\":" + String(button4State ? "true" : "false");
  jsonPayload += ",\"led1\":" + String(relay1State ? "true" : "false");
  jsonPayload += ",\"led2\":" + String(relay2State ? "true" : "false");
  jsonPayload += ",\"led3\":" + String(relay3State ? "true" : "false");
  jsonPayload += ",\"automationEnabled\":" + String(automationEnabled ? "true" : "false");
  jsonPayload += ",\"manualOverrideActive\":" + String(manualOverrideActive ? "true" : "false");
  jsonPayload += ",\"controlMode\":\"" + controlMode + "\"";
  jsonPayload += ",\"timestamp\":" + String(millis());
  jsonPayload += "}";

  client.println("POST /api/sensor-data HTTP/1.1");
  client.print("Host: ");
  client.print(webAppHost);
  client.print(":");
  client.println(webAppPort);
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(jsonPayload.length());
  client.println("Connection: close");
  client.println();
  client.println(jsonPayload);

  unsigned long timeoutStart = millis();
  while (!client.available() && millis() - timeoutStart < 1000) {
    delay(1);
  }

  while (client.available()) client.read();
  client.stop();

  lastBroadcastJson = jsonPayload;
  //Serial.println(">>> Task: HTTP POST sent");
  return true;
}

// =====================================================
// Broadcast task
// =====================================================
void broadcastTask(void *pvParameters) {
  (void)pvParameters;
  unsigned long lastBroadcastTime = 0;
  unsigned long lastReconnectAttempt = 0;

  for (;;) {
    updateEnvSensors();
    updateRadarPresence();
    motionToSend = motionDetected;

    if (automationEnabled && !manualOverrideActive) {
      controlFanAutomatically();
    }

    if (WiFi.status() == WL_CONNECTED && !wifiScanInProgress) {
      unsigned long now = millis();
      bool sendNow = stateChanged || (now - lastBroadcastTime > BROADCAST_INTERVAL_MS);

      if (sendNow) {
        if (sendSensorDataToWebApp()) {
          stateChanged = false;
          lastBroadcastTime = now;
        } else {
          if (now - lastReconnectAttempt > RECONNECT_INTERVAL_MS) {
            Serial.println(">>> Broadcast retry will continue...");
            lastReconnectAttempt = now;
          }
        }
      }

      handleBackendCommands();
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// =====================================================
// Setup
// =====================================================
void setup() {
  Preferences webappPrefs;
  webappPrefs.begin(WEBAPP_PREFS_NAMESPACE, true);
  String storedIp = webappPrefs.getString(WEBAPP_IP_KEY, "");
  if (storedIp.length() > 0) webAppHost = storedIp;
  webappPrefs.end();

  Serial.begin(115200);
  Serial.println("ESP32 Environmental Monitor starting...");

  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(RELAY3_PIN, OUTPUT);
  pinMode(MASTER_RELAY_PIN, OUTPUT);

  turnAllSpeedRelaysOff();
  setMasterRelay(true);

  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);
  pinMode(BUTTON3_PIN, INPUT_PULLUP);
  pinMode(BUTTON4_PIN, INPUT_PULLUP);
  pinMode(BUTTON5_PIN, INPUT_PULLUP);

  pinMode(PIR_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIR_PIN), pirISR, RISING);

  Serial1.begin(256000, SERIAL_8N1, LD2410C_RX_PIN, LD2410C_TX_PIN);
  radar.debug(Serial);
  delay(1000);

  Serial.print(F("\nConnect LD2410 radar TX to GPIO: "));
  Serial.println(LD2410C_RX_PIN);
  Serial.print(F("Connect LD2410 radar RX to GPIO: "));
  Serial.println(LD2410C_TX_PIN);
  Serial.print(F("LD2410 radar sensor initialising: "));

  if (radar.begin(Serial1)) {
    Serial.println(F("OK"));
    Serial.print(F("LD2410 firmware version: "));
    Serial.print(radar.firmware_major_version);
    Serial.print('.');
    Serial.print(radar.firmware_minor_version);
    Serial.print('.');
    Serial.println(radar.firmware_bugfix_version, HEX);
  } else {
    Serial.println(F("not connected"));
  }

  dht1.begin();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;) {}
  }

  display.clearDisplay();
  display.display();

  preferences.begin(WIFI_PREFS_NAMESPACE, false);
  storedSSID = preferences.getString(WIFI_SSID_KEY, "");
  storedPassword = preferences.getString(WIFI_PASS_KEY, "");

  if (storedSSID.length() > 0 && storedPassword.length() > 0) {
    WiFi.mode(WIFI_OFF);
    delay(200);
    WiFi.mode(WIFI_AP_STA);
    WiFi.disconnect(true);
    delay(300);

    wifiScanInProgress = true;
    WiFi.begin(storedSSID.c_str(), storedPassword.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
      delay(500);
      attempts++;
    }

    wifiScanInProgress = false;
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("ESP32-Config", "password123");

  server.begin();
  Serial.println("[DEBUG] Web server started.");

  if (WiFi.status() == WL_CONNECTED) {
    postLedStatesSync();
  }

  xTaskCreatePinnedToCore(
    broadcastTask,
    "broadcastTask",
    6144,
    NULL,
    1,
    NULL,
    1
  );
}

// =====================================================
// Web handlers
// =====================================================
void handleWifiConnect(WiFiClient& client, String body) {
  int ssidStart = body.indexOf("\"ssid\"");
  int passStart = body.indexOf("\"password\"");
  if (ssidStart < 0 || passStart < 0) {
    client.print("HTTP/1.1 400 Bad Request\r\n\r\nMissing SSID or password");
    return;
  }

  int ssidColon = body.indexOf(':', ssidStart);
  int ssidQuote1 = body.indexOf('"', ssidColon);
  int ssidQuote2 = body.indexOf('"', ssidQuote1 + 1);
  String ssid = body.substring(ssidQuote1 + 1, ssidQuote2);

  int passColon = body.indexOf(':', passStart);
  int passQuote1 = body.indexOf('"', passColon);
  int passQuote2 = body.indexOf('"', passQuote1 + 1);
  String password = body.substring(passQuote1 + 1, passQuote2);

  Preferences wifiPrefs;
  wifiPrefs.begin(WIFI_PREFS_NAMESPACE, false);
  wifiPrefs.putString(WIFI_SSID_KEY, ssid);
  wifiPrefs.putString(WIFI_PASS_KEY, password);
  wifiPrefs.end();

  WiFi.disconnect(true);
  delay(200);
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    client.print("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n");
    client.print("{\"connected\":true,\"ip\":\"");
    client.print(WiFi.localIP());
    client.print("\"}");
  } else {
    client.print("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n");
    client.print("{\"connected\":false,\"error\":\"Failed to connect\"}");
  }

  client.stop();
}

void handleSetWebAppIp(WiFiClient& client, String body) {
  int ipStart = body.indexOf("\"ip\"");
  if (ipStart < 0) {
    client.print("HTTP/1.1 400 Bad Request\r\n\r\n");
    return;
  }

  int colon = body.indexOf(':', ipStart);
  int quote1 = body.indexOf('"', colon);
  int quote2 = body.indexOf('"', quote1 + 1);
  String ip = body.substring(quote1 + 1, quote2);

  Preferences webappPrefs;
  webappPrefs.begin(WEBAPP_PREFS_NAMESPACE, false);
  webappPrefs.putString(WEBAPP_IP_KEY, ip);
  webappPrefs.end();

  webAppHost = ip;
  client.print("HTTP/1.1 200 OK\r\n\r\nOK");
}

void handleGetWebAppIp(WiFiClient& client) {
  client.print("HTTP/1.1 200 OK\r\n\r\n" + webAppHost);
}

void handleWebClient() {
  WiFiClient client = server.available();
  if (!client) return;

  String requestLine = client.readStringUntil('\r');
  client.readStringUntil('\n');

  String line = "";
  while ((line = client.readStringUntil('\r')) != "") {
    client.readStringUntil('\n');
  }

  String body = "";

  if (requestLine.indexOf("POST /wifi-connect") != -1) {
    while (client.available()) body += (char)client.read();
    handleWifiConnect(client, body);
    return;
  }

  if (requestLine.indexOf("POST /set-webapp-ip") != -1) {
    while (client.available()) body += (char)client.read();
    handleSetWebAppIp(client, body);
    return;
  }

  if (requestLine.indexOf("GET /get-webapp-ip") != -1) {
    handleGetWebAppIp(client);
    return;
  }

  if (requestLine.indexOf("GET /data") == 0) {
    String json = "{";
    json += "\"temperature1\":" + String(isnan(temperature1) ? 0 : temperature1, 2);
    json += ",\"humidity1\":" + String(isnan(humidity1) ? 0 : humidity1, 2);
    json += ",\"current\":" + String(isnan(current) ? 0 : current, 2);
    json += ",\"motion\":" + String(motionToSend ? "true" : "false");
    json += ",\"ld2410cHumanPresent\":" + String(cachedPresenceValue);
    json += ",\"automationEnabled\":" + String(automationEnabled ? "true" : "false");
    json += ",\"manualOverrideActive\":" + String(manualOverrideActive ? "true" : "false");
    json += "}";

    client.print("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n");
    client.print(json);
    client.stop();
    return;
  }

  if (requestLine.indexOf("GET /wifi-status") == 0) {
    String json = "{";
    json += "\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
    json += ",\"ssid\":\"" + WiFi.SSID() + "\"";
    json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
    json += "}";

    client.print("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n");
    client.print(json);
    client.stop();
    return;
  }

  if (requestLine.indexOf("GET /wifi-scan") == 0) {
    int n = WiFi.scanNetworks();
    String json = "[";

    for (int i = 0; i < n; ++i) {
      if (i > 0) json += ",";
      json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
              ",\"secure\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
    }
    json += "]";

    client.print("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n");
    client.print(json);
    client.stop();
    return;
  }

  client.print("HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nNot Found");
  client.stop();
}

// =====================================================
// Loop
// =====================================================
void loop() {
  unsigned long now = millis();

  handleButtons(now);

  motionToSend = motionDetected;

  if (now - lastOledUpdateTime >= OLED_UPDATE_INTERVAL_MS) {
    updateOLED(motionDetected);
    lastOledUpdateTime = now;
  }

  motionDetected = false;

  handleWebClient();

  if (WiFi.status() == WL_CONNECTED && now - lastLedStateSyncTime > LED_STATE_SYNC_INTERVAL_MS) {
    postLedStatesSync();
    lastLedStateSyncTime = now;
  }

  yield();
}

// =====================================================
// ISR / OLED
// =====================================================
void IRAM_ATTR pirISR() {
  motionDetected = true;
}

void updateOLED(bool motion) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(2);
  display.println("Motion");
  display.println("Detection");
  display.println("");
  display.setTextSize(3);
  display.println(motion ? "YES" : "NO");
  display.display();
}