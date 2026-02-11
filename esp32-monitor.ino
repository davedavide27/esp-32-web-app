#include <Arduino.h>
#include <WiFi.h>
#include <DHT.h>
#include "web.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>

// Local web app server details (change these to match your web app)
const char* webAppHost = "192.168.1.10";  // IP address of your Windows machine on main network
const int webAppPort = 5000;               // Port your web app is running on

WiFiServer server(80);

Preferences preferences;
String storedSSID = "";
String storedPassword = "";

#define DHTPIN1 2
#define DHTTYPE1 DHT22
DHT dht1(DHTPIN1, DHTTYPE1);

#define DHTPIN2 4
#define DHTTYPE2 DHT11
DHT dht2(DHTPIN2, DHTTYPE2);

#define CURRENT_PIN 32
#define PIR_PIN 5

// OLED display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

float temperature1 = 0.0;
float humidity1 = 0.0;
float temperature2 = 0.0;
float humidity2 = 0.0;
float current = 0.0;
volatile bool motionDetected = false;
bool motionToSend = false;
bool wifiScanInProgress = false;  // Flag to prevent sensor broadcast during WiFi operations



// ISR for PIR motion sensor
void IRAM_ATTR pirISR() {
  motionDetected = true;
}

// Function to update OLED display
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

// Global persistent connection to web app
WiFiClient persistentClient;
unsigned long lastBroadcastTime = 0;
const unsigned long BROADCAST_INTERVAL = 500;  // Send every 500ms
unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 10000;  // Wait 10 seconds between reconnect attempts
unsigned long connectionEstablishedTime = 0;
const unsigned long POST_CONNECT_DELAY = 500;  // Wait 500ms after connecting before sending

// Background FreeRTOS task: sends sensor data to web app so HTTP server stays responsive
void broadcastTask(void *pvParameters) {
  (void) pvParameters;
  const TickType_t delayTicks = pdMS_TO_TICKS(BROADCAST_INTERVAL);
  for (;;) {
    // Only operate when not scanning and WiFi connected
    if (WiFi.status() == WL_CONNECTED && !wifiScanInProgress) {
      unsigned long now = millis();

      // Reconnect logic (throttled)
      if (!persistentClient.connected()) {
        if (now - lastReconnectAttempt > RECONNECT_INTERVAL) {
          Serial.println(">>> Reconnecting to web app server from task...");
          persistentClient.setTimeout(5000);
          if (!persistentClient.connect(webAppHost, webAppPort)) {
            Serial.println(">>> Task: failed to connect to web app server");
            lastReconnectAttempt = now;
            persistentClient.stop();
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
          }
          Serial.println(">>> Task: connected to web app server");
          connectionEstablishedTime = now;
          lastReconnectAttempt = now;
        }
      }

      // Wait briefly after connect
      if (persistentClient.connected() && (millis() - connectionEstablishedTime) >= POST_CONNECT_DELAY) {
        // Build JSON payload from latest sensor values
        String jsonPayload = "{";
        jsonPayload += "\"temperature1\":" + String(temperature1, 1) + ",";
        jsonPayload += "\"humidity1\":" + String(humidity1, 1) + ",";
        jsonPayload += "\"temperature2\":" + String(temperature2, 1) + ",";
        jsonPayload += "\"humidity2\":" + String(humidity2, 1) + ",";
        jsonPayload += "\"current\":" + String(current, 2) + ",";
        jsonPayload += "\"motion\":" + String(motionToSend ? "true" : "false") + ",";
        jsonPayload += "\"timestamp\":" + String(millis());
        jsonPayload += "}";

        // Send as HTTP POST to keep server compatibility
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

        // Minimal read to clear server response (non-blocking)
        unsigned long readTimeout = millis();
        while (persistentClient.available() && (millis() - readTimeout < 100)) {
          String tmp = persistentClient.readStringUntil('\n');
          (void)tmp;
        }

        // Periodic log
        if (now - lastBroadcastTime > 2000) {
          Serial.println(">>> Task: HTTP POST sent");
          lastBroadcastTime = now;
        }
      }
    } else {
      // Not connected or scan in progress; ensure client closed
      if (persistentClient.connected()) persistentClient.stop();
    }

    vTaskDelay(delayTicks);
  }
}






void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 Environmental Monitor starting...");

  dht1.begin();
  dht2.begin();
  pinMode(PIR_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIR_PIN), pirISR, RISING);

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
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();  // Clear any previous connection attempts
    delay(100);
    Serial.print("Attempting to connect to stored SSID: ");
    Serial.println(storedSSID);
    WiFi.begin(storedSSID.c_str(), storedPassword.c_str());
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    Serial.println();
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
    // Start AP in AP+STA mode so the web UI can scan/connect while AP is available
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("ESP32-Config", "password123");
    delay(200);
    Serial.println("AP mode started (AP+STA)");
    Serial.print("AP IP Address: ");
    Serial.println(WiFi.softAPIP());
  }

  server.begin();
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

void loop() {
  // Read DHT22 sensor data (GPIO 2)
  float newTemp1 = dht1.readTemperature();

  float newHum1 = dht1.readHumidity();

  // Read DHT11 sensor data (GPIO 4)
  float newTemp2 = dht2.readTemperature();
  float newHum2 = dht2.readHumidity();

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
    temperature1 = 0.0;
    humidity1 = 0.0;
    Serial.println("Failed to read from DHT22 sensor! Setting values to 0.");
  }

  // Check if DHT11 readings are valid (not NaN)
  if (!isnan(newTemp2) && !isnan(newHum2)) {
    temperature2 = newTemp2;
    humidity2 = newHum2;
    Serial.print("DHT11 - Temperature: ");
    Serial.print(temperature2);
    Serial.print(" °C, Humidity: ");
    Serial.print(humidity2);
    Serial.println(" %");
  } else {
    temperature2 = 0.0;
    humidity2 = 0.0;
    Serial.println("Failed to read from DHT11 sensor! Setting values to 0.");
  }

  // Read ACS712-5A current sensor data (GPIO 32) - powered from ESP32 3.3V
  // Take multiple readings and average to reduce noise
  int numSamples = 10;
  float totalVoltage = 0.0;
  for (int i = 0; i < numSamples; i++) {
    int adcValue = analogRead(CURRENT_PIN);
    totalVoltage += adcValue * 3.3 / 4095.0;
    delay(10);  // Small delay between readings
  }
  float voltage = totalVoltage / numSamples;

  if (voltage < 0.3 || voltage > 3.0) {
    current = 0.0;
    Serial.println("ACS712-5A - Disconnected or invalid reading! Setting current to 0.");
  } else {
    current = (voltage - 1.65) / 0.185;  // ACS712-5A: ~185 mV/A at 3.3V VCC, 1.65V at 0A
    Serial.print("ACS712-5A - Current: ");
    Serial.print(current, 2);
    Serial.println(" A");
  }

  // Check PIR motion sensor data (GPIO 13) - using ISR for realtime detection
  Serial.print("PIR Motion Sensor - Motion Detected: ");
  Serial.println(motionDetected ? "Yes" : "No");
  
  // Capture motion state before resetting
  motionToSend = motionDetected;
  
  // Update OLED display with motion status
  updateOLED(temperature1, humidity1, temperature2, humidity2, current, motionDetected);

  // Reset motion flag after reporting
  motionDetected = false;

  // Broadcast is handled in background task now; just ensure flag state
  // (no-op here)

  // Fallback: if STA is not connected and we're in STA mode, switch back to AP+STA for user access
  static unsigned long lastAPCheck = 0;
  unsigned long now = millis();
  if (now - lastAPCheck > 5000) {  // Check every 5 seconds
    lastAPCheck = now;
    if (WiFi.status() != WL_CONNECTED && WiFi.getMode() == WIFI_STA) {
      // STA is disconnected and AP is not running; restart AP+STA mode
      Serial.println("STA disconnected. Switching to AP+STA mode for user access.");
      WiFi.mode(WIFI_AP_STA);
      WiFi.softAP("ESP32-Config", "password123");
    }
  }

  // Only read sensor every 0.5 seconds for more responsive updates
  delay(500);

  WiFiClient client = server.available();
  if (client) {
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

    // Process requests

    if (requestLine.indexOf("GET /data") != -1) {
      // Send JSON response for data endpoint
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: application/json");
      client.println("Access-Control-Allow-Origin: *");
      client.println("");

      String jsonResponse = "{";
      jsonResponse += "\"temperature1\":" + String(temperature1, 1) + ",";
      jsonResponse += "\"humidity1\":" + String(humidity1, 1) + ",";
      jsonResponse += "\"temperature2\":" + String(temperature2, 1) + ",";
      jsonResponse += "\"humidity2\":" + String(humidity2, 1) + ",";
      jsonResponse += "\"current\":" + String(current, 2) + ",";
      jsonResponse += "\"motion\":" + String(motionToSend ? "true" : "false");
      jsonResponse += "}";

      client.println(jsonResponse);
    } else if (requestLine.indexOf("GET /wifi-scan") != -1) {
      // WiFi scan endpoint - prevents broadcasts during scan to avoid socket errors
      wifiScanInProgress = true;
      Serial.println("Starting WiFi scan (broadcasts paused)...");
      
      // Ensure we're in AP+STA mode so AP stays active during scan
      if (WiFi.getMode() != WIFI_AP_STA) {
        Serial.println("Switching to AP+STA mode for scan");
        WiFi.mode(WIFI_AP_STA);
        delay(100);
        WiFi.softAP("ESP32-Config", "password123");
        delay(100);
      }
      
      // Perform blocking scan instead of async to avoid interference with STA connection
      // This is more reliable when already connected to WiFi
      // show_hidden=true, passive=false
      Serial.println("Scanning networks...");
      int n = WiFi.scanNetworks(false, true, false);  // Blocking scan
      
      Serial.print("Scan completed. Found ");
      Serial.print(n);
      Serial.println(" networks");

      // Handle case where scan failed
      if (n == -1) {
        Serial.println("Scan failed - returning empty results");
        n = 0;  // Will return empty array
      }

      // Build JSON response while client is still connected
      String jsonResponse = "[";
      if (n > 0) {
        for (int i = 0; i < n; ++i) {
          if (i > 0) jsonResponse += ",";
          // Escape quotes in SSID to prevent JSON breakage
          String ssid = WiFi.SSID(i);
          ssid.replace("\"", "\\\"");
          jsonResponse += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"encryption\":" + String(WiFi.encryptionType(i)) + "}";
          Serial.print("Network: ");
          Serial.print(WiFi.SSID(i));
          Serial.print(" (");
          Serial.print(WiFi.RSSI(i));
          Serial.println(" dBm)");
        }
      }
      jsonResponse += "]";
      
      // Send complete response with headers and body
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: application/json");
      client.println("Content-Length: " + String(jsonResponse.length()));
      client.println("Access-Control-Allow-Origin: *");
      client.println("Connection: close");
      client.println("");
      client.print(jsonResponse);
      client.flush();
      delay(10);
      
      // Delete scan results to free memory
      WiFi.scanDelete();
      
      // Ensure AP is still active after scan
      if (WiFi.getMode() != WIFI_AP_STA) {
        Serial.println("Restoring AP+STA mode after scan");
        WiFi.mode(WIFI_AP_STA);
        delay(100);
        WiFi.softAP("ESP32-Config", "password123");
        delay(100);
      }
      
      wifiScanInProgress = false;
      Serial.println("Scan completed and response sent. Broadcasts resumed.");




    } else if (requestLine.indexOf("GET /wifi-status") != -1) {


      // Return actual WiFi connection status
      bool isConnected = (WiFi.status() == WL_CONNECTED);
      String jsonResponse = "{";
      jsonResponse += "\"connected\":" + String(isConnected ? "true" : "false") + ",";
      jsonResponse += "\"ssid\":\"" + (isConnected ? WiFi.SSID() : "") + "\",";
      jsonResponse += "\"ip\":\"" + (isConnected ? WiFi.localIP().toString() : "Not connected") + "\",";
      jsonResponse += "\"storedSsid\":\"" + storedSSID + "\",";
      jsonResponse += "\"mode\":\"" + String(WiFi.getMode() == WIFI_AP ? "AP" : (WiFi.getMode() == WIFI_STA ? "STA" : "AP_STA")) + "\"";
      jsonResponse += "}";

      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: application/json");
      client.println("Access-Control-Allow-Origin: *");
      client.println("");
      client.println(jsonResponse);
    } else if (requestLine.indexOf("POST /wifi-disconnect") != -1) {
      // Disconnect from current WiFi and switch back to AP+STA mode
      Serial.println("Disconnect requested...");
      WiFi.disconnect(true);  // disconnect(true) also turns off STA
      delay(300);  // Give WiFi stack time to clean up
      
      // Switch to AP+STA mode so AP becomes available for reconnection
      WiFi.mode(WIFI_AP_STA);
      delay(100);  // Wait for mode switch
      WiFi.softAP("ESP32-Config", "password123");
      delay(300);  // Wait for AP to fully initialize
      Serial.println("Disconnected from WiFi. AP restarted in AP+STA mode.");
      
      String response = "Disconnected";
      client.print("HTTP/1.1 200 OK\r\n");
      client.print("Content-Type: text/plain\r\n");
      client.print("Access-Control-Allow-Origin: *\r\n");
      client.print("Content-Length: ");
      client.print(response.length());
      client.print("\r\n");
      client.print("Connection: close\r\n");
      client.print("\r\n");
      client.print(response);
      client.flush();
    } else if (requestLine.indexOf("POST /wifi-connect") != -1) {

      // Parse JSON body (simple parsing for ssid and password)
      int ssidStart = body.indexOf("\"ssid\":\"") + 8;
      int ssidEnd = body.indexOf("\"", ssidStart);
      String newSSID = body.substring(ssidStart, ssidEnd);

      int passStart = body.indexOf("\"password\":\"") + 12;
      int passEnd = body.indexOf("\"", passStart);
      String newPassword = body.substring(passStart, passEnd);

      // Validate credentials before attempting connection
      if (newSSID.length() == 0 || newSSID.length() > 32) {
        Serial.println("Invalid SSID length. Aborting connection.");
        String response = "Failed";
        client.print("HTTP/1.1 200 OK\r\n");
        client.print("Content-Type: text/plain\r\n");
        client.print("Access-Control-Allow-Origin: *\r\n");
        client.print("Content-Length: ");
        client.print(response.length());
        client.print("\r\n");
        client.print("Connection: close\r\n");
        client.print("\r\n");
        client.print(response);
        client.flush();
      } else if (newPassword.length() > 63) {
        Serial.println("Invalid password length. Aborting connection.");
        String response = "Failed";
        client.print("HTTP/1.1 200 OK\r\n");
        client.print("Content-Type: text/plain\r\n");
        client.print("Access-Control-Allow-Origin: *\r\n");
        client.print("Content-Length: ");
        client.print(response.length());
        client.print("\r\n");
        client.print("Connection: close\r\n");
        client.print("\r\n");
        client.print(response);
        client.flush();
      } else {
        // Store credentials persistently and update in-memory vars
        preferences.putString("ssid", newSSID);
        storedSSID = newSSID;
        // Only overwrite stored password if a non-empty password was provided
        if (newPassword.length() > 0) {
          preferences.putString("password", newPassword);
          storedPassword = newPassword;
        }

        // Try to connect using STA mode. Stop AP if active so STA can connect cleanly.
        WiFi.mode(WIFI_STA);
        WiFi.softAPdisconnect(true);
        delay(300);  // Increased delay for proper AP shutdown

        WiFi.disconnect();
        delay(100);
        // Use the stored password if an empty password was provided in the request
        const char* pwToUse = (newPassword.length() > 0) ? newPassword.c_str() : storedPassword.c_str();
        Serial.print("Attempting to connect to SSID: ");
        Serial.print(newSSID);
        Serial.println(" with provided credentials");
        WiFi.begin(newSSID.c_str(), pwToUse);
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 10) {
          delay(500);
          Serial.print(".");
          attempts++;
        }
        Serial.println();

        bool connected = (WiFi.status() == WL_CONNECTED);
        if (connected) {
          Serial.print("Successfully connected! IP: ");
          Serial.println(WiFi.localIP());
          // Switch to AP+STA mode to keep configuration interface available
          WiFi.mode(WIFI_AP_STA);
          delay(100);
          WiFi.softAP("ESP32-Config", "password123");
          delay(300);
          Serial.println("AP+STA mode activated for continued configuration access");
        } else {
          Serial.println("Failed to connect. Clearing invalid credentials.");
          // Connection failed, clear the bad credentials
          preferences.remove("ssid");
          preferences.remove("password");
          storedSSID = "";
          storedPassword = "";
          // Switch back to AP+STA mode so user can retry
          WiFi.mode(WIFI_AP_STA);
          WiFi.softAP("ESP32-Config", "password123");
          delay(300);
        }
        
        String response = connected ? "Connected" : "Failed";
        client.print("HTTP/1.1 200 OK\r\n");
        client.print("Content-Type: text/plain\r\n");
        client.print("Access-Control-Allow-Origin: *\r\n");
        client.print("Content-Length: ");
        client.print(response.length());
        client.print("\r\n");
        client.print("Connection: close\r\n");
        client.print("\r\n");
        client.print(response);
        client.flush();
      }
    } else if (requestLine.indexOf("POST /wifi-forget") != -1) {
      // Clear stored WiFi credentials
      preferences.remove("ssid");
      preferences.remove("password");
      storedSSID = "";
      storedPassword = "";
      
      // Disconnect from current WiFi and switch back to AP+STA mode
      Serial.println("Forget WiFi credentials requested...");
      WiFi.disconnect(true);  // disconnect(true) also turns off STA
      delay(300);  // Give WiFi stack time to clean up
      
      WiFi.mode(WIFI_AP_STA);
      delay(100);  // Wait for mode switch
      WiFi.softAP("ESP32-Config", "password123");
      delay(300);  // Wait for AP to fully initialize
      Serial.println("WiFi credentials forgotten. AP restarted in AP+STA mode.");
      
      String response = "Forgotten";
      client.print("HTTP/1.1 200 OK\r\n");
      client.print("Content-Type: text/plain\r\n");
      client.print("Access-Control-Allow-Origin: *\r\n");
      client.print("Content-Length: ");
      client.print(response.length());
      client.print("\r\n");
      client.print("Connection: close\r\n");
      client.print("\r\n");
      client.print(response);
      client.flush();
    } else {

      // Send HTML response for main page
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/html");
      client.println("");

      String htmlResponse = String(htmlPage);
      htmlResponse.replace("TEMPERATURE1", String(temperature1, 1));
      htmlResponse.replace("HUMIDITY1", String(humidity1, 1));
      htmlResponse.replace("TEMPERATURE2", String(temperature2, 1));
      htmlResponse.replace("HUMIDITY2", String(humidity2, 1));
      htmlResponse.replace("CURRENT", String(current, 2));
      htmlResponse.replace("MOTION", motionToSend ? "Motion Detected" : "No Motion");

      client.println(htmlResponse);
    }

    if (!clientStopped) {
      delay(1);
      client.stop();
    }
  }
}
