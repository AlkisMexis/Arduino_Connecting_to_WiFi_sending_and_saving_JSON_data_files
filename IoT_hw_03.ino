
#include <WiFiNINA.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_BME680.h> // Or use the library you installed for your BME68x

// ---------------------------
// -------- SETTINGS ---------
// ---------------------------

// WiFi credentials (course requirement)
#define SECRET_SSID "ICTlab1"
#define SECRET_PASS "ICT-lab-education"

// Server to send JSON to (course requirement)
const char SERVER_IP[] = "192.168.40.114"; //Not needed for homework 4 
const uint16_t SERVER_PORT = 8080; //Not needed for homework 4 
const char SERVER_PATH[] = "/"; // change if your endpoint is /api/data etc.

// I2C address for BME68x module (common: 0x76 or 0x77)
#define BME_I2C_ADDR 0x76

// Touch / button pins (adjust to match MKR Carrier wiring)
// These are examples: change to the pins your carrier exposes for touch/buttons.
const int TOUCH_PIN_SEND = 4;  // when pressed, send JSON
const int TOUCH_PIN_INFO = 5;  // optional: toggle display or show IP

// OLED display settings (U8g2 constructor — choose one for your driver/model)
// Common choice: SSD1306 128x32 I2C using U8G2_R0, change if needed.
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0);

// ---------------------------
// ---- GLOBAL OBJECTS -------
// ---------------------------

WiFiClient client;
Adafruit_BME680 bme; // BME68x sensor object

// Keep track of last button state to detect presses (simple debouncing)
bool lastSendButtonState = false;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50; // ms

void setup() {
  // Serial for debugging
  Serial.begin(115200);
  while (!Serial);

  // Initialize display
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 10, "MKR Carrier Boot");
  u8g2.sendBuffer();

  // Initialize touch/button pins as inputs (pullup depends on wiring)
  pinMode(TOUCH_PIN_SEND, INPUT_PULLUP); // assume active-low button (pressed = LOW)
  pinMode(TOUCH_PIN_INFO, INPUT_PULLUP);

  // Initialize BME680 sensor
  Wire.begin(); // start I2C
  if (!bme.begin(BME_I2C_ADDR)) {
    Serial.println("Could not find BME680 sensor at address " + String(BME_I2C_ADDR, HEX));
    while (1) delay(1000); // halt here — sensor required for this exercise
  }

  // Recommended settings for BME680 (oversampling, IIR filter, gas heater disabled by default)
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150); // 320*C for 150 ms (example) — adjust or disable if unnecessary

  // Connect to WiFi
  connectWiFi();

  // Show initial measurements and state
  updateDisplay();
}

// ---------------------------
// ---------- LOOP ------------
// ---------------------------

void loop() {
  // Read sensor and update the display periodically
  static unsigned long lastRead = 0;
  const unsigned long readInterval = 2000; // ms
  if (millis() - lastRead >= readInterval) {
    lastRead = millis();
    // perform reading
    if (!bme.performReading()) {
      Serial.println("BME performReading() failed");
    }
    updateDisplay();
  }

  // Check send button and on-press send JSON to server
  bool sendBtn = digitalRead(TOUCH_PIN_SEND) == LOW; // active-low
  if (sendBtn != lastSendButtonState) {
    lastDebounceTime = millis();
    lastSendButtonState = sendBtn;
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (sendBtn) {
      // Button pressed — send JSON message
      Serial.println("Send button pressed: sending JSON...");
      sendJSONToServer();
      // simple visual feedback
      flashDisplay("Sent");
      // wait until released to avoid repeated sends
      while (digitalRead(TOUCH_PIN_SEND) == LOW) delay(10);
    }
  }

  // Small yield to let WiFi and background tasks run
  delay(10);
}

// ---------------------------
// ---- NETWORK / SENDING ----
// ---------------------------

void connectWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(SECRET_SSID);

  // Try to connect
  int status = WL_IDLE_STATUS;
  while (status != WL_CONNECTED) {
    status = WiFi.begin(SECRET_SSID, SECRET_PASS);
    Serial.print('.');
    delay(3000);
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void sendJSONToServer() {
  // Build JSON payload using ArduinoJson
  StaticJsonDocument<256> doc;
  // Device IP as a string
  doc["DeviceIP"] = WiFi.localIP().toString();
  // Sensor values
  doc["Temperature"] = bme.temperature; // Celsius
  doc["Humidity"] = bme.humidity;       // %RH
  doc["Pressure"] = bme.pressure / 100.0; // hPa (Adafruit library returns Pa)

  // Serialize payload to a String
  String payload;
  serializeJson(doc, payload);

  Serial.println("Payload: ");
  Serial.println(payload);

  // Connect to server and send HTTP POST
  if (!client.connect(SERVER_IP, SERVER_PORT)) {
    Serial.println("Connection to server failed");
    flashDisplay("Conn Err");
    return;
  }

  // Build HTTP POST request
  client.print(String("POST ") + SERVER_PATH + " HTTP/1.1\r\n");
  client.print(String("Host: ") + SERVER_IP + ":" + String(SERVER_PORT) + "\r\n");
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(payload.length());
  client.println("Connection: close\r\n");
  client.println(payload);

  // Wait for a short response and print it to Serial
  unsigned long timeout = millis();
  while (client.connected() && millis() - timeout < 3000) {
    while (client.available()) {
      String line = client.readStringUntil('\n');
      Serial.println(line);
      timeout = millis();
    }
  }

  client.stop();
  Serial.println("Connection closed");
}

// ---------------------------
// ---- DISPLAY UTILITIES ----
// ---------------------------

void updateDisplay() {
  // Compose display lines using latest sensor values
  char buf[64];
  u8g2.clearBuffer();

  // Line 1: IP address
  String ip = WiFi.localIP().toString();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 10, ("IP:" + ip).c_str());

  // Line 2: Temperature
  snprintf(buf, sizeof(buf), "T: %.2f C", bme.temperature);
  u8g2.drawStr(0, 22, buf);

  // Line 3: Humidity
  snprintf(buf, sizeof(buf), "H: %.1f %%", bme.humidity);
  u8g2.drawStr(0, 32, buf);

  // Line 4: Pressure (if display big enough)
  // Some 32px-high OLEDs only show 2 lines; if you have 64px device, change positions.
  snprintf(buf, sizeof(buf), "P: %.1f hPa", bme.pressure / 100.0);
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 44, buf);

  u8g2.sendBuffer();
}

void flashDisplay(const char* msg) {
  // Briefly show a message
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 20, msg);
  u8g2.sendBuffer();
  delay(400);
  updateDisplay();
  // Show a fatal error on display and Serial
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 10, "FATAL ERROR");
  u8g2.drawStr(0, 24, msg);
  u8g2.sendBuffer();
  Serial.println(msg);
}


