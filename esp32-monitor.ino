#include <Arduino.h>
#include <WiFi.h>
#include <DHT.h>
#include "web.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>

// Forward declarations for functions used before definition
void updateOLED(float temp1, float hum1, float temp2, float hum2, float curr, bool motion);
void IRAM_ATTR pirISR();

// Web app server details (IP is now user-configurable)
#define WEBAPP_PREFS_NAMESPACE "webapp"
#define WEBAPP_IP_KEY "ip"
String webAppHost = "192.168.10.102"; // Default, will be loaded from Preferences
const int webAppPort = 5000;

WiFiServer server(80);                       
Preferences preferences;
String storedSSID = "";
String storedPassword = "";

// --- State broadcast tracking ---
String lastBroadcastJson = "";
bool stateChanged = true;
bool ld2410cHumanPresent = false;

#define DHTPIN1 2
#define DHTTYPE1 DHT22
DHT dht1(DHTPIN1, DHTTYPE1);


#define CURRENT_PIN 32
#define VOLTAGE_PIN 34  // MLE00983 AC Voltage sensor
#define PIR_PIN 5
#define LED1_PIN 13
#define LED2_PIN 12
#define LED3_PIN 14
#define BUTTON1_PIN 26
#define BUTTON2_PIN 19
#define BUTTON3_PIN 33
#define BUTTON4_PIN 27
// LD2410C wave sensor UART pins
#define LD2410C_RX_PIN 16  // ESP32 RX (connects to LD2410C TX)
#define LD2410C_TX_PIN 17  // ESP32 TX (connects to LD2410C RX)

float voltageSensor = 0.0;
float voltageSensorRaw = 0.0;
const float VOLTAGE_SMOOTHING_ALPHA = 0.4; // Smoothing factor (higher = faster, 0.4 for quick response)
// --- Fan knob detection additions ---
const float FAN_ON_THRESHOLD = 1.55;  // Voltage above which knob is ON
const float FAN_OFF_LOW = 1.52;       // Lower bound for OFF range
const float FAN_OFF_HIGH = 1.55;      // Upper bound for OFF rang+e
const int FAN_OFF_SAMPLE_WINDOW = 30; // Number of samples (3s at 100ms)
const int FAN_OFF_SAMPLE_REQUIRED = 25; // Require at least 25/30 samples in range
bool fanKnobOn = false;

// OLED display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

float temperature1 = 0.0;
float humidity1 = 0.0;
float current = 0.0;
volatile bool motionDetected = false;
bool motionToSend = false;
bool wifiScanInProgress = false;  // Flag to prevent sensor broadcast during WiFi operations

// Button and LED state tracking (mirrors actual GPIO state)
bool button1State = false;
bool button2State = false;
bool button3State = false;
bool button4State = false;
bool led1State = false;
bool led2State = false;
bool led3State = false;

// --- Button debounce timing ---
unsigned long lastButton1Time = 0;
unsigned long lastButton2Time = 0;
unsigned long lastButton3Time = 0;
unsigned long lastButton4Time = 0;
const unsigned long debounceDelay = 300; // ms
unsigned long lastLedStateSyncTime = 0;
const unsigned long LED_STATE_SYNC_INTERVAL = 5000;  // Sync every 5 seconds

// Helper: read actual LED pin states
void readLedStates() {
  led1State = (digitalRead(LED1_PIN) == HIGH);
  led2State = (digitalRead(LED2_PIN) == HIGH);
  led3State = (digitalRead(LED3_PIN) == HIGH);
}

// Helper: apply mutual exclusion locally (ensure only one LED is on)
void applyLocalMutualExclusion(int ledNum) {
  if (ledNum == 1 && led1State) {
    digitalWrite(LED2_PIN, LOW);
    digitalWrite(LED3_PIN, LOW);
    led2State = false;
    led3State = false;
  } else if (ledNum == 2 && led2State) {
    digitalWrite(LED1_PIN, LOW);
    digitalWrite(LED3_PIN, LOW);
    led1State = false;
    led3State = false;
  } else if (ledNum == 3 && led3State) {
    digitalWrite(LED1_PIN, LOW);
    digitalWrite(LED2_PIN, LOW);
    led1State = false;
    led2State = false;
  }
}

// Helper: POST full LED state to server
void postLedStatesSync() {
  static bool lastLed1 = false, lastLed2 = false, lastLed3 = false;
  readLedStates();
  // Only sync if state changed
  if (led1State != lastLed1 || led2State != lastLed2 || led3State != lastLed3) {
    WiFiClient stateClient;
    if (stateClient.connect(webAppHost.c_str(), webAppPort)) {
      String body = "{\"states\":{";
      body += "\"led1\":" + String(led1State ? "true" : "false");
      body += ",\"led2\":" + String(led2State ? "true" : "false");
      body += ",\"led3\":" + String(led3State ? "true" : "false");
      body += "}}";

      stateClient.println("POST /api/led-states HTTP/1.1");
      stateClient.print("Host: "); stateClient.print(webAppHost); stateClient.print(":"); stateClient.println(webAppPort);
      stateClient.println("Content-Type: application/json");
      stateClient.print("Content-Length: "); stateClient.println(body.length());
      stateClient.println("Connection: close");
      stateClient.println();
      stateClient.println(body);
      stateClient.flush();

      unsigned long start = millis();
      while (!stateClient.available() && (millis() - start < 200)) {
        vTaskDelay(pdMS_TO_TICKS(5));
      }
      // discard response
      while (stateClient.available()) {
        stateClient.read();
      }
      stateClient.stop();
      Serial.println(">>> LED states synced to server");
    }
    lastLed1 = led1State;
    lastLed2 = led2State;
    lastLed3 = led3State;
  }
}

// Background FreeRTOS task: sends sensor data to web app so HTTP server stays responsive
void broadcastTask(void *pvParameters) {
  (void) pvParameters;
  WiFiClient persistentClient;
  unsigned long lastBroadcastTime = 0;
  const unsigned long BROADCAST_INTERVAL = 500;  // Send every 500ms
  unsigned long lastReconnectAttempt = 0;
  const unsigned long RECONNECT_INTERVAL = 10000;  // Wait 10 seconds between reconnect attempts
  unsigned long connectionEstablishedTime = 0;
  const unsigned long POST_CONNECT_DELAY = 500;  // Wait 500ms after connecting before sending

  for (;;) {
    // Only operate when not scanning and WiFi connected
    if (WiFi.status() == WL_CONNECTED && !wifiScanInProgress) {
      unsigned long now = millis();

      // Reconnect logic (throttled)
      if (!persistentClient.connected()) {
        if (now - lastReconnectAttempt > RECONNECT_INTERVAL) {
          Serial.println(">>> Reconnecting to web app server from task...");
          persistentClient.setTimeout(500); // Tight timeout
          bool connected = persistentClient.connect(webAppHost.c_str(), webAppPort);
          if (!connected) {
            Serial.println(">>> Task: failed to connect to web app server");
            lastReconnectAttempt = now;
            persistentClient.stop(); // Always free socket
  // Print first 10 bytes received from LD2410C after boot for debug
  Serial.println("LD2410C startup debug: waiting for first 10 bytes...");
  int startupByteCount = 0;
  while (startupByteCount < 10) {
    if (Serial2.available()) {
      uint8_t b = Serial2.read();
      Serial.print("Startup UART byte: 0x");
      Serial.println(b, HEX);
      startupByteCount++;
    }
    delay(10);
  }
            // Socket exhaustion safeguard: if too many failures, delay longer
            static int failCount = 0;
            failCount++;
            if (failCount > 10) {
              Serial.println(">>> Too many failed connections, backing off");
              vTaskDelay(pdMS_TO_TICKS(2000));
              failCount = 0;
            } else {
              vTaskDelay(pdMS_TO_TICKS(200));
            }
            continue;
          }
          Serial.println(">>> Task: connected to web app server");
          connectionEstablishedTime = now;
          lastReconnectAttempt = now;
        }
      }

      // Wait briefly after connect
      if (persistentClient.connected() && (millis() - connectionEstablishedTime) >= POST_CONNECT_DELAY) {
        // Sync led state with button state before sending
        led1State = button1State;
        led2State = button2State;
        led3State = button3State;
        // Build JSON payload from latest sensor values (all property names quoted, all values valid JSON)
        String jsonPayload = "{";
          jsonPayload += "\"temperature\":";
          jsonPayload += (isnan(temperature1) ? "0" : String(temperature1, 1));
          jsonPayload += ",\"temperature1\":";
          jsonPayload += (isnan(temperature1) ? "0" : String(temperature1, 1));
          jsonPayload += ",\"humidity1\":";
          jsonPayload += (isnan(humidity1) ? "0" : String(humidity1, 1));
        jsonPayload += ",\"voltage\":";
        jsonPayload += (isnan(voltageSensor) ? "null" : String(voltageSensor, 2));
        jsonPayload += ",\"fanOn\":";
        jsonPayload += (fanKnobOn ? "true" : "false");
        jsonPayload += ",\"motion\":";
        jsonPayload += (motionToSend ? "true" : "false");
        // Always include ld2410cHumanPresent, even if other values are missing
        jsonPayload += ",\"ld2410cHumanPresent\":";
        jsonPayload += (ld2410cHumanPresent ? "true" : "false");
        jsonPayload += ",\"button1\":";
        jsonPayload += (button1State ? "true" : "false");
        jsonPayload += ",\"button2\":";
        jsonPayload += (button2State ? "true" : "false");
        jsonPayload += ",\"button3\":";
        jsonPayload += (button3State ? "true" : "false");
        jsonPayload += ",\"button4\":";
        jsonPayload += (button4State ? "true" : "false");
        jsonPayload += ",\"led1\":";
        jsonPayload += (led1State ? "true" : "false");
        jsonPayload += ",\"led2\":";
        jsonPayload += (led2State ? "true" : "false");
        jsonPayload += ",\"led3\":";
        jsonPayload += (led3State ? "true" : "false");
        jsonPayload += ",\"timestamp\":";
        jsonPayload += String(millis());
        jsonPayload += "}";


        // Only send if state changed or interval elapsed
        bool sendNow = stateChanged || (now - lastBroadcastTime > 2000);
        if (sendNow && jsonPayload != lastBroadcastJson) {
          persistentClient.println("POST /api/sensor-data HTTP/1.1");
          persistentClient.print("Host: ");
          persistentClient.print(webAppHost);
          persistentClient.print(":" );
          persistentClient.println(webAppPort);
          persistentClient.println("Content-Type: application/json");
          persistentClient.print("Content-Length: ");
          persistentClient.println(jsonPayload.length());
          persistentClient.println("Connection: keep-alive");
          persistentClient.println();
          persistentClient.println(jsonPayload);
          persistentClient.flush();
          // Minimal read to clear server response (non-blocking, max 30ms)
          unsigned long readTimeout = millis();
          while (persistentClient.available() && (millis() - readTimeout < 30)) {
            String tmp = persistentClient.readStringUntil('\n');
            (void)tmp;
          }
          lastBroadcastJson = jsonPayload;
          stateChanged = false;
          lastBroadcastTime = now;
          Serial.println(">>> Task: HTTP POST sent");
        }
      }
    } else {
      // Not connected or scan in progress; ensure client closed
      if (persistentClient.connected()) persistentClient.stop();
    }

    // Periodically poll backend for commands (every 2s)
    static unsigned long lastCommandCheck = 0;
    const unsigned long COMMAND_CHECK_INTERVAL = 200;
    unsigned long now2 = millis();
    if ((WiFi.status() == WL_CONNECTED) && (now2 - lastCommandCheck >= COMMAND_CHECK_INTERVAL)) {
      lastCommandCheck = now2;
      WiFiClient cmdClient;
      //Serial.println("[DEBUG] Polling /api/command...");
      if (cmdClient.connect(webAppHost.c_str(), webAppPort)) {
        // Request pending command (server will clear it after returning)
        cmdClient.println("GET /api/command HTTP/1.1");
        cmdClient.print("Host: ");
        cmdClient.print(webAppHost);
        cmdClient.print(":" );
        cmdClient.println(webAppPort);
        cmdClient.println("Connection: close");
        cmdClient.println();

        // Wait briefly for response
        unsigned long startWait = millis();
        while (!cmdClient.available() && (millis() - startWait < 1000)) {
          vTaskDelay(pdMS_TO_TICKS(10));
        }

        // Read response into body string
        String responseBody = "";
        // Skip headers
        while (cmdClient.available()) {
          String line = cmdClient.readStringUntil('\n');
          if (line == "\r" || line == "") break;
        }
        while (cmdClient.available()) {
          char c = cmdClient.read();
          responseBody += c;
        }

        //Serial.print("[DEBUG] /api/command response: ");
        //Serial.println(responseBody);

        if (responseBody.length() > 0) {
          int p = responseBody.indexOf("\"command\"");
          if (p >= 0) {
            int colon = responseBody.indexOf(':', p);
            if (colon >= 0) {
              String val = responseBody.substring(colon + 1);
              // trim whitespace
              val.trim();
              // Remove possible trailing characters
              if (val.endsWith("}")) val = val.substring(0, val.length() - 1);
              val.trim();
              // remove quotes
              if (val.startsWith("\"") && val.endsWith("\"")) {
                val = val.substring(1, val.length() - 1);
              }

              // Support commands like "led1_on", "led2_off", "led3_on"
              if (val.startsWith("led")) {
                int us = val.indexOf('_');
                if (us >= 0) {
                  String ledIdStr = val.substring(3, us);
                  int ledNum = ledIdStr.toInt();
                  String cmd = val.substring(us + 1);
                  int pin = -1;
                  if (ledNum == 1) pin = LED1_PIN;
                  else if (ledNum == 2) pin = LED2_PIN;
                  else if (ledNum == 3) pin = LED3_PIN;

                  // Always set button4State to false when any LED command is received from web app
                  button4State = false;

                  if (pin != -1) {
                    if (cmd == "on") {
                      digitalWrite(pin, HIGH);
                      // Update in-memory state
                      if (ledNum == 1) {
                        led1State = true;
                        button1State = true;
                        led2State = false; button2State = false;
                        led3State = false; button3State = false;
                        digitalWrite(LED2_PIN, LOW);
                        digitalWrite(LED3_PIN, LOW);
                      } else if (ledNum == 2) {
                        led2State = true;
                        button2State = true;
                        led1State = false; button1State = false;
                        led3State = false; button3State = false;
                        digitalWrite(LED1_PIN, LOW);
                        digitalWrite(LED3_PIN, LOW);
                      } else if (ledNum == 3) {
                        led3State = true;
                        button3State = true;
                        led1State = false; button1State = false;
                        led2State = false; button2State = false;
                        digitalWrite(LED1_PIN, LOW);
                        digitalWrite(LED2_PIN, LOW);
                      }
                      // Apply mutual exclusion: turn off others
                      applyLocalMutualExclusion(ledNum);
                      // Immediately sync LED state to backend for instant frontend update
                      postLedStatesSync();
                    } else {
                      digitalWrite(pin, LOW);
                      // Update in-memory state
                      if (ledNum == 1) { led1State = false; button1State = false; }
                      else if (ledNum == 2) { led2State = false; button2State = false; }
                      else if (ledNum == 3) { led3State = false; button3State = false; }
                      // Immediately sync LED state to backend for instant frontend update
                      postLedStatesSync();
                    }
                    Serial.print(">>> Command: LED");
                    Serial.print(ledNum);
                    Serial.print(cmd == "on" ? " ON" : " OFF");
                    Serial.println();
                    // Send acknowledgement back to server with actual applied state
                    {
                      WiFiClient ackClient;
                      if (ackClient.connect(webAppHost.c_str(), webAppPort)) {
                        String ledKey = "led" + String(ledNum);
                        bool appliedState = (cmd == "on");
                        String body = "{";
                        body += "\"led\":\"" + ledKey + "\",";
                        body += "\"state\":" + String(appliedState ? "true" : "false");
                        body += "}";

                        ackClient.println("POST /api/ack HTTP/1.1");
                        ackClient.print("Host: "); ackClient.print(webAppHost); ackClient.print(":" ); ackClient.println(webAppPort);
                        ackClient.println("Content-Type: application/json");
                        ackClient.print("Content-Length: "); ackClient.println(body.length());
                        ackClient.println("Connection: close");
                        ackClient.println();
                        ackClient.println(body);
                        ackClient.flush();

                        unsigned long start = millis();
                        while (!ackClient.available() && (millis() - start < 500)) {
                          vTaskDelay(pdMS_TO_TICKS(10));
                        }
                        // discard response
                        while (ackClient.available()) {
                          ackClient.read();
                        }
                        ackClient.stop();
                      } else {
                        Serial.println("Failed to connect for ack POST");
                      }
                    }
                  }
                }
              }
            }
          }
        }

        cmdClient.stop();
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}


void setup() {
    // Load web app IP from preferences
    Preferences webappPrefs;
    webappPrefs.begin(WEBAPP_PREFS_NAMESPACE, true);
    String storedIp = webappPrefs.getString(WEBAPP_IP_KEY, "");
    if (storedIp.length() > 0) {
      webAppHost = storedIp;
    }
    webappPrefs.end();
  Serial.begin(115200);
  Serial.println("ESP32 Environmental Monitor starting...");

  // Initialize Serial2 for LD2410C (UART2)
  Serial2.begin(256000, SERIAL_8N1, LD2410C_RX_PIN, LD2410C_TX_PIN); // 256000 baud is default for LD2410C
  Serial.println("LD2410C UART initialized on Serial2 (GPIO16=RX, GPIO17=TX)");

  // --- LD2410C configuration: limit range and lower sensitivity ---
  // Enter configuration mode
  Serial2.write(0xFD); Serial2.write(0xFC); Serial2.write(0xFB); Serial2.write(0xFA); // Header
  Serial2.write(0x01); // Command: Enter config
  Serial2.write(0x00); // Data length
  Serial2.write(0x00); // Checksum
  delay(100);

  // Set max detection distance to 3 meters (0x03)
  Serial2.write(0xFD); Serial2.write(0xFC); Serial2.write(0xFB); Serial2.write(0xFA); // Header
  Serial2.write(0x0A); // Command: Set max distance
  Serial2.write(0x01); // Data length
  Serial2.write(0x03); // 3 meters
  Serial2.write(0x0E); // Checksum (0x0A+0x01+0x03=0x0E)
  delay(100);

  // Set sensitivity to 2 (out of 10)
  Serial2.write(0xFD); Serial2.write(0xFC); Serial2.write(0xFB); Serial2.write(0xFA); // Header
  Serial2.write(0x0B); // Command: Set sensitivity
  Serial2.write(0x01); // Data length
  Serial2.write(0x02); // Sensitivity value
  Serial2.write(0x0E); // Checksum (0x0B+0x01+0x02=0x0E)
  delay(100);

  // Exit configuration mode
  Serial2.write(0xFD); Serial2.write(0xFC); Serial2.write(0xFB); Serial2.write(0xFA); // Header
  Serial2.write(0x02); // Command: Exit config
  Serial2.write(0x00); // Data length
  Serial2.write(0x02); // Checksum
  delay(100);

  dht1.begin();
  pinMode(PIR_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIR_PIN), pirISR, RISING);
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  digitalWrite(LED3_PIN, LOW);

  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);
  pinMode(BUTTON3_PIN, INPUT_PULLUP);
  pinMode(BUTTON4_PIN, INPUT_PULLUP);

  // Initialize OLED display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for (;;) ; // Don't proceed, loop forever
  }
  display.clearDisplay();
  display.display();

  delay(2000); // Wait for DHT sensors to stabilize

  // Initialize Preferences
  preferences.begin("wifi", false);

  // Load stored WiFi credentials
  storedSSID = preferences.getString("ssid", "");
  storedPassword = preferences.getString("password", "");
  
  // Validate stored credentials (must be reasonable length)
  if (storedSSID.length() == 0 || storedSSID.length() > 32 || storedPassword.length() > 63) {
    Serial.println("Invalid stored credentials detected. Clearing preferences.");
    preferences.remove("ssid");
    preferences.remove("password");
    storedSSID = "";
    storedPassword = "";
  }

  // Connect to WiFi (use STA mode for connecting)
  if (storedSSID.length() > 0 && storedPassword.length() > 0) {
    WiFi.mode(WIFI_OFF); // Ensure clean state
    delay(200);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);  // Clear any previous connection attempts and turn off STA
    delay(300);
    Serial.print("Attempting to connect to stored SSID: ");
    Serial.println(storedSSID);
    wifiScanInProgress = true;
    WiFi.begin(storedSSID.c_str(), storedPassword.c_str());
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    Serial.println();
    wifiScanInProgress = false;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.print("WiFi connection failed. Status: ");
      Serial.println(WiFi.status());
      Serial.println("WiFi connection failed: wrong credentials, network unavailable, or timeout.");
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected to WiFi");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    // Even if connected, also start AP in AP+STA mode for configuration/fallback
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("ESP32-Config", "password123");
    delay(200);
    Serial.println("AP also started for configuration access (AP+STA mode)");
    Serial.print("AP IP Address: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("Failed to connect to stored WiFi, starting AP+STA mode");
    // Clear bad stored credentials to prevent retry loop
    if (storedSSID.length() > 0 && storedPassword.length() > 0) {
      Serial.println("Clearing invalid stored credentials.");
      preferences.remove("ssid");
      preferences.remove("password");
      storedSSID = "";
      storedPassword = "";
    }
    wifiScanInProgress = false; // Ensure scan flag is reset on failure
    // Start AP in AP+STA mode so the web UI can scan/connect while AP is available
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("ESP32-Config", "password123");
    delay(500); // Increased delay for AP to fully initialize
    Serial.println("AP mode started (AP+STA)");
    Serial.print("AP IP Address: ");
    Serial.println(WiFi.softAPIP());
  }

  Serial.println("[DEBUG] Starting web server...");
  delay(200); // Give WiFi stack a moment before starting server
  server.begin();
  Serial.println("[DEBUG] Web server started.");
  // Sync LED state to server at boot
  if (WiFi.status() == WL_CONNECTED) {
    postLedStatesSync();
  }

  // Create background task for broadcasting sensor data
  xTaskCreatePinnedToCore(
    broadcastTask,   // function
    "broadcastTask",// name
    4096,            // stack size
    NULL,            // parameter
    1,               // priority
    NULL,            // task handle
    1                // run on core 1
  );
}

// --- Web server handlers for web app IP (global scope) ---
void handleSetWebAppIp(WiFiClient& client, String body) {
  int ipStart = body.indexOf("\"ip\"");
  if (ipStart < 0) { client.print("HTTP/1.1 400 Bad Request\r\n\r\n"); return; }
  int colon = body.indexOf(":", ipStart);
  int quote1 = body.indexOf('"', colon);
  int quote2 = body.indexOf('"', quote1+1);
  String ip = body.substring(quote1+1, quote2);
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
// Example: parse UART data from LD2410C and set ld2410cHumanPresent
// This assumes the sensor sends a specific byte for presence (e.g., 0x01 for present, 0x00 for absent)
// LD2410C UART full packet parsing for reliable presence detection
void updateLd2410cPresence() {
  static uint8_t packet[32];
  static int idx = 0;
  static bool lastPresence = false;
  static int debounceCount = 0;
  const int debounceThreshold = 3; // Require 3 consecutive packets for state change
  static unsigned long lastDebugTime = 0;
  const unsigned long debugInterval = 1000; // Print debug output at most once per second

  while (Serial2.available()) {
    uint8_t b = Serial2.read();
    // Buffer bytes until header is found
    if (idx == 0 && b == 0xFD) {
      packet[idx++] = b;
    } else if (idx == 1 && b == 0xFC) {
      packet[idx++] = b;
    } else if (idx == 2 && b == 0xFB) {
      packet[idx++] = b;
    } else if (idx == 3 && b == 0xFA) {
      packet[idx++] = b;
    } else if (idx >= 4) {
      packet[idx++] = b;
      // Typical presence report packet length is 24 bytes
      if (idx == 24) {
        // Validate header
        bool validHeader = (packet[0] == 0xFD && packet[1] == 0xFC && packet[2] == 0xFB && packet[3] == 0xFA);
        if (validHeader) {
          // Print packet debug only once per interval
          unsigned long now = millis();
          if (now - lastDebugTime > debugInterval) {
            Serial.print("LD2410C packet: ");
            for (int i = 0; i < 24; i++) {
              Serial.print("0x");
              Serial.print(packet[i], HEX);
              Serial.print(" ");
            }
            Serial.println();
            lastDebugTime = now;
          }
          // Presence status is usually at byte 20 (index 19)
          uint8_t presence = packet[19];
          Serial.print("Presence byte: 0x");
          Serial.println(presence, HEX);
          // Debounce: only change state after N consecutive packets
          if (presence == 0x01) {
            if (!lastPresence) {
              debounceCount++;
              if (debounceCount >= debounceThreshold) {
                ld2410cHumanPresent = true;
                lastPresence = true;
                debounceCount = 0;
                Serial.println("LD2410C: Human presence detected (debounced)");
              }
            } else {
              debounceCount = 0;
            }
          } else {
            if (lastPresence) {
              debounceCount++;
              if (debounceCount >= debounceThreshold) {
                ld2410cHumanPresent = false;
                lastPresence = false;
                debounceCount = 0;
                Serial.println("LD2410C: No human presence (debounced)");
              }
            } else {
              debounceCount = 0;
            }
          }
        } else {
          Serial.println("LD2410C: Invalid packet header");
        }
        idx = 0; // Reset for next packet
      } else if (idx >= 32) {
        // Prevent overflow, reset if too many bytes
        idx = 0;
      }
    } else {
      // If header sequence breaks, reset
      idx = 0;
    }
  }
}

// Main loop: manual controls and sensor reads are always responsive, regardless of web connection state
void loop() {
    // Update LD2410C presence from UART
    updateLd2410cPresence();
    unsigned long now = millis();
    // --- Button/LED logic: always runs first, regardless of network/server state ---
    // --- LD2410C UART read example ---
    static String lastUartHex = "";
    String uartHex = "";
    if (Serial2.available()) {
        while (Serial2.available()) {
            uint8_t b = Serial2.read();
            if (b < 16) uartHex += "0";
            uartHex += String(b, HEX);
            uartHex += " ";
        }
    }

    bool prevButton1 = button1State;
    bool prevButton2 = button2State;
    bool prevButton3 = button3State;
    bool prevButton4 = button4State;
    if (digitalRead(BUTTON1_PIN) == LOW && now - lastButton1Time > debounceDelay) {
      button1State = true;
      button2State = false;
      button3State = false;
      button4State = false;
      led1State = true;
      led2State = false;
      led3State = false;
      digitalWrite(LED2_PIN, LOW);
      digitalWrite(LED3_PIN, LOW);
      delay(100); // protection delay before turning on LED1 (short, safe)
      digitalWrite(LED1_PIN, HIGH);
      Serial.println("Button1 pressed: LED1 ON, others OFF (with protection delay)");
      lastButton1Time = now;
      // Immediately sync LED state to server
      if (WiFi.status() == WL_CONNECTED) postLedStatesSync();
    }
    if (digitalRead(BUTTON2_PIN) == LOW && now - lastButton2Time > debounceDelay) {
      button1State = false;
      button2State = true;
      button3State = false;
      button4State = false;
      led1State = false;
      led2State = true;
      led3State = false;
      digitalWrite(LED1_PIN, LOW);
      digitalWrite(LED3_PIN, LOW);
      delay(100); // protection delay before turning on LED2 (short, safe)
      digitalWrite(LED2_PIN, HIGH);
      Serial.println("Button2 pressed: LED2 ON, others OFF (with protection delay)");
      lastButton2Time = now;
      if (WiFi.status() == WL_CONNECTED) postLedStatesSync();
    }
    if (digitalRead(BUTTON3_PIN) == LOW && now - lastButton3Time > debounceDelay) {
      button1State = false;
      button2State = false;
      button3State = true;
      button4State = false;
      led1State = false;
      led2State = false;
      led3State = true;
      digitalWrite(LED1_PIN, LOW);
      digitalWrite(LED2_PIN, LOW);
      delay(100); // protection delay before turning on LED3 (short, safe)
      digitalWrite(LED3_PIN, HIGH);
      Serial.println("Button3 pressed: LED3 ON, others OFF (with protection delay)");
      lastButton3Time = now;
      if (WiFi.status() == WL_CONNECTED) postLedStatesSync();
    }
    if (digitalRead(BUTTON4_PIN) == LOW && now - lastButton4Time > debounceDelay) {
      button1State = false;
      button2State = false;
      button3State = false;
      button4State = true;
      led1State = false;
      led2State = false;
      led3State = false;
      digitalWrite(LED1_PIN, LOW);
      digitalWrite(LED2_PIN, LOW);
      digitalWrite(LED3_PIN, LOW);
      Serial.println("Button4 pressed: All LEDs OFF");
      lastButton4Time = now;
      if (WiFi.status() == WL_CONNECTED) postLedStatesSync();
    }
    // Only set stateChanged if any button state changed
    if (prevButton1 != button1State || prevButton2 != button2State || prevButton3 != button3State || prevButton4 != button4State) {
      stateChanged = true;
    }
    yield(); // Prevent WDT reset from long blocking delays

    // (Removed duplicate blocking button/LED logic. Only non-blocking, state-tracking logic remains above.)

    // --- Network/server, sensors, and web requests ---
    static unsigned long lastSensorRead = 0;
    static unsigned long lastAPCheck = 0;
    static unsigned long lastLoopTime = 0;


    // Non-blocking sensor read every 2 seconds
    if (now - lastSensorRead >= 2000) {
      lastSensorRead = now;
      // Read DHT22 sensor data (GPIO 2)
      float newTemp1 = dht1.readTemperature();
      float newHum1 = dht1.readHumidity();
      // Check if DHT22 readings are valid (not NaN)
      if (!isnan(newTemp1) && !isnan(newHum1)) {
        temperature1 = newTemp1;
        humidity1 = newHum1;
        Serial.print("DHT22 - Temperature: ");
        Serial.print(temperature1);
        Serial.print(" °C, Humidity: ");
        Serial.print(humidity1);
        Serial.println(" %");
      } else {
        temperature1 = NAN;
        humidity1 = NAN;
        Serial.println("Failed to read from DHT22 sensor! Setting values to null.");
      }
      // Read ACS712-5A current sensor data (GPIO 32) - powered from ESP32 3.3V
      int numSamples = 10;
      float totalVoltage = 0.0;
      for (int i = 0; i < numSamples; i++) {
        int adcValue = analogRead(CURRENT_PIN);
        totalVoltage += adcValue * 3.3 / 4095.0;
        delay(10);  // Small delay between readings
      }
      float voltage = totalVoltage / numSamples;
      if (voltage < 0.3 || voltage > 3.0) {
        current = NAN;
        Serial.println("ACS712-5A - Disconnected or invalid reading! Setting current to null.");
      } else {
        current = (voltage - 1.65) / 0.185;  // ACS712-5A: ~185 mV/A at 3.3V VCC, 1.65V at 0A
        Serial.print("ACS712-5A - Current: ");
        Serial.print(current, 2);
        Serial.println(" A");
      }
    }

    // Check PIR motion sensor data (GPIO 13) - using ISR for realtime detection
    //Serial.print("PIR Motion Sensor - Motion Detected: ");
    //Serial.println(motionDetected ? "Yes" : "No");
    // Capture motion state before resetting
    motionToSend = motionDetected;
    // Update OLED display with motion status (add voltageSensor if desired)
    updateOLED(temperature1, humidity1, NAN, NAN, NAN, motionDetected);
    // Reset motion flag after reporting
    motionDetected = false;

    // Fallback: if STA is not connected and we're in STA mode, switch back to AP+STA for user access
    if (now - lastAPCheck > 5000) {  // Check every 5 seconds
      lastAPCheck = now;
      if (WiFi.status() != WL_CONNECTED && WiFi.getMode() == WIFI_STA) {
        // STA is disconnected and AP is not running; restart AP+STA mode
        Serial.println("STA disconnected. Switching to AP+STA mode for user access.");
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP("ESP32-Config", "password123");
      }
    }

    // --- Web server: only handle one client per loop, non-blocking ---
    WiFiClient client = server.available();
    if (client) {
      Serial.println("[DEBUG] Client connected.");
      String requestLine = client.readStringUntil('\r');
      client.readStringUntil('\n'); // consume \n
      // Read headers until blank line
      String line = "";
      while ((line = client.readStringUntil('\r')) != "") {
        client.readStringUntil('\n'); // consume \n
      }

      // Now read body if POST
      String body = "";
      if (requestLine.indexOf("POST /wifi-connect") != -1) {
        while (client.available()) {
          body += (char)client.read();
        }
      }

      bool clientStopped = false;  // Flag to prevent double client.stop()


      // --- Web app IP handlers ---
      if (requestLine.indexOf("POST /set-webapp-ip") != -1) {
        while (client.available()) body += (char)client.read();
        handleSetWebAppIp(client, body);
        return;
      }
      if (requestLine.indexOf("GET /get-webapp-ip") != -1) {
        handleGetWebAppIp(client);
        return;
      }

      // --- API endpoint: /data ---
      if (requestLine.indexOf("GET /data") == 0) {
        // Return latest sensor data as JSON
        String json = "{";
        json += "\"temperature1\":" + String(isnan(temperature1) ? "null" : String(temperature1, 2)) + ",";
        json += "\"humidity1\":" + String(isnan(humidity1) ? "null" : String(humidity1, 2)) + ",";
        json += "\"current\":" + String(isnan(current) ? "null" : String(current, 2)) + ",";
        json += "\"motion\":" + String(motionToSend ? "true" : "false") + ",";
        json += "\"ld2410cHumanPresent\":" + String(ld2410cHumanPresent ? "true" : "false");
        json += "}";
        client.print("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n");
        client.print(json);
        client.stop();
        return;
      }

      // --- API endpoint: /wifi-status ---
      if (requestLine.indexOf("GET /wifi-status") == 0) {
        String json = "{";
        json += "\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
        json += "\"ssid\":\"" + WiFi.SSID() + "\",";
        json += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
        json += "}";
        client.print("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n");
        client.print(json);
        client.stop();
        return;
      }

      // --- API endpoint: /wifi-scan ---
      if (requestLine.indexOf("GET /wifi-scan") == 0) {
        int n = WiFi.scanNetworks();
        String json = "[";
        for (int i = 0; i < n; ++i) {
          if (i > 0) json += ",";
          json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"secure\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
        }
        json += "]";
        client.print("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n");
        client.print(json);
        client.stop();
        return;
      }

      // Serve web.h HTML page for root path
      if (requestLine.startsWith("GET / ") || requestLine.startsWith("GET /HTTP") || requestLine.startsWith("GET / HTTP")) {
        client.print("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");
        client.print(htmlPage);
        client.stop();
        return;
      }

      // Default handler for all other requests (prevents ERR_EMPTY_RESPONSE)
      client.print("HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");
      client.print("<html><head><title>404 Not Found</title></head><body><h1>404 Not Found</h1><p>The requested resource was not found on this server.</p></body></html>");
      client.stop();
      return;

      // DHT and current sensor reads every 2 seconds (non-blocking)
      static unsigned long lastSensorRead = 0;
      if (millis() - lastSensorRead >= 2000) {
        lastSensorRead = millis();
        // Read DHT22 sensor data (GPIO 2)
        float newTemp1 = dht1.readTemperature();
        float newHum1 = dht1.readHumidity();
        // Check if DHT22 readings are valid (not NaN)
        if (!isnan(newTemp1) && !isnan(newHum1)) {
          temperature1 = newTemp1;
          humidity1 = newHum1;
          Serial.print("DHT22 - Temperature: ");
          Serial.print(temperature1);
          Serial.print(" °C, Humidity: ");
          Serial.print(humidity1);
          Serial.println(" %");
        } else {
          temperature1 = NAN;
          humidity1 = NAN;
          Serial.println("Failed to read from DHT22 sensor! Setting values to null.");
        }
        // Read ACS712-5A current sensor data (GPIO 32) - powered from ESP32 3.3V
        int numSamples = 10;
        float totalVoltage = 0.0;
        for (int i = 0; i < numSamples; i++) {
          int adcValue = analogRead(CURRENT_PIN);
          totalVoltage += adcValue * 3.3 / 4095.0;
          delay(10);  // Small delay between readings
        }
        float voltage = totalVoltage / numSamples;
        if (voltage < 0.3 || voltage > 3.0) {
          current = NAN;
          Serial.println("ACS712-5A - Disconnected or invalid reading! Setting current to null.");
        } else {
          current = (voltage - 1.65) / 0.185;  // ACS712-5A: ~185 mV/A at 3.3V VCC, 1.65V at 0A
          Serial.print("ACS712-5A - Current: ");
          Serial.print(current, 2);
          Serial.println(" A");
        }
      }

      // Check PIR motion sensor data (GPIO 13) - using ISR for realtime detection
      Serial.print("PIR Motion Sensor - Motion Detected: ");
      Serial.println(motionDetected ? "Yes" : "No");
      // Capture motion state before resetting
      motionToSend = motionDetected;
      // Update OLED display with motion status (add voltageSensor if desired)
      updateOLED(temperature1, humidity1, NAN, NAN, NAN, motionDetected);
      // Reset motion flag after reporting
      motionDetected = false;

    // Broadcast is handled in background task now; just ensure flag state
    // (no-op here)

    // Fallback: if STA is not connected and we're in STA mode, switch back to AP+STA for user access
    static unsigned long lastAPCheck = 0;
    // --- Remove blocking delay at end of loop ---
    // (No delay(500); here)

    // --- Periodic LED state sync (non-blocking) ---
    if (WiFi.status() == WL_CONNECTED) {
      if (now - lastLedStateSyncTime > LED_STATE_SYNC_INTERVAL) {
        postLedStatesSync();
        lastLedStateSyncTime = now;
      }
    }
        } else {
          // If for some reason no response was sent, send a default response
        //  Serial.println("[DEBUG] No response sent, sending default 404.");
          client.println("HTTP/1.1 404 Not Found");
          client.println("Content-Type: text/plain");
          client.println("Connection: close");
          client.println();
          client.println("Not Found");
          client.stop();
        }
     }

// PIR motion sensor ISR
void IRAM_ATTR pirISR() {
  motionDetected = true;
}

// OLED update function (minimal implementation)
void updateOLED(float temp1, float hum1, float temp2, float hum2, float curr, bool motion) {
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
