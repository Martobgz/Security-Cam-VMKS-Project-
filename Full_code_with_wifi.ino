
#include <WiFi.h>
#include <ESP32Servo.h>
#include "MQ2.h"
#include "secrets.h"

// ---------- PINS ----------
const int PIR_PIN    = 15;
const int MQ2_PIN    = 4;
const int PHOTO_PIN  = 5;
const int BUZZER_PIN = 6;
const int SERVO_PIN  = 7;

// ---------- DEVICE ----------
const char* DEVICE_NAME = "ESP32_S3";
const char* DEVICE_TYPE = "sensor";

// ---------- ALERT THRESHOLDS (ppm) ----------
const int LPG_THRESHOLD   = 1000;
const int CO_THRESHOLD    = 50;
const int SMOKE_THRESHOLD = 300;

// ---------- TIMING (ms) ----------
const unsigned long MOTION_COOLDOWN    = 10000;
const unsigned long GAS_COOLDOWN       = 60000;
const unsigned long MQ2_WARMUP         = 60000;
const unsigned long COMMAND_INTERVAL   =   500;
const unsigned long SENSOR_INTERVAL    =  2000;
const unsigned long REGISTER_INTERVAL  = 15000;  // re-register every 15 s

// ---------- BUZZER ----------
const int BUZZER_CHANNEL = 0;

// ---------- OBJECTS ----------
Servo    myservo;
MQ2      mq2(MQ2_PIN);
WiFiServer server(80);

// ---------- STATE ----------
bool registered = false;
int servoPos = 90;

unsigned long lastMotionAlert  = 0;
unsigned long lastGasAlert     = 0;
unsigned long lastCommandPoll  = 0;
unsigned long lastSensorRead   = 0;
unsigned long lastRegisterTime = 0;

bool  pirLastState = LOW;
float lpgPpm   = 0;
float coPpm    = 0;
float smokePpm = 0;
int   photoRaw = 0;

// =====================================================================
//  BUZZER HELPERS
// =====================================================================
void beep(int frequency, int durationMs) {
  ledcWriteTone(BUZZER_CHANNEL, frequency);
  delay(durationMs);
  ledcWriteTone(BUZZER_CHANNEL, 0);
}

void beepNonBlockingStart(int frequency) {
  ledcWriteTone(BUZZER_CHANNEL, frequency);
}

void beepStop() {
  ledcWriteTone(BUZZER_CHANNEL, 0);
}

// =====================================================================
//  HTTP HELPERS
// =====================================================================
bool httpPost(const char* path, const String& payload) {
  WiFiClient c;
  if (!c.connect(FLASK_IP, FLASK_PORT)) {
    Serial.println("[POST] connect failed");
    return false;
  }

  c.println(String("POST ") + path + " HTTP/1.1");
  c.println(String("Host: ") + FLASK_IP + ":" + String(FLASK_PORT));
  c.println("Content-Type: application/json");
  c.println("Connection: close");
  c.print("Content-Length: ");
  c.println(payload.length());
  c.println();
  c.print(payload);

  unsigned long start = millis();
  while (!c.available()) {
    if (millis() - start > 4000) {
      c.stop();
      return false;
    }
    delay(10);
  }

  while (c.available()) {
    c.read();
  }

  c.stop();
  return true;
}

String httpGet(const char* path) {
  WiFiClient c;
  if (!c.connect(FLASK_IP, FLASK_PORT)) {
    return "";
  }

  c.println(String("GET ") + path + " HTTP/1.1");
  c.println(String("Host: ") + FLASK_IP + ":" + String(FLASK_PORT));
  c.println("Connection: close");
  c.println();

  // Wait for any data to arrive
  unsigned long start = millis();
  while (!c.available()) {
    if (millis() - start > 3000) { c.stop(); return ""; }
    delay(10);
  }

  // Read headers line by line; when blank line found, wait for body
  bool inBody = false;
  String body;

  while (c.connected() || c.available()) {
    if (!c.available()) { delay(5); continue; }
    String line = c.readStringUntil('\n');
    if (!inBody) {
      if (line == "\r" || line == "\r\n" || line.length() == 0) {
        inBody = true;
        // Give the body time to arrive in the buffer
        delay(50);
      }
    } else {
      body += line;
    }
  }

  c.stop();
  body.trim();
  return body;
}

// =====================================================================
//  WIFI
// =====================================================================
void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    attempts++;

    if (attempts > 60) {
      Serial.println("\nWiFi timeout — rebooting");
      ESP.restart();
    }
  }

  Serial.print("\nWiFi connected. IP: ");
  Serial.println(WiFi.localIP());
}

// =====================================================================
//  REGISTRATION
// =====================================================================
bool registerDevice() {
  String payload = "{\"ip\":\"" + WiFi.localIP().toString() +
                   "\",\"name\":\"" + String(DEVICE_NAME) +
                   "\",\"type\":\"" + String(DEVICE_TYPE) + "\"}";
  return httpPost("/register", payload);
}

// =====================================================================
//  SENSOR READS
// =====================================================================
void readSensors() {
  lpgPpm   = mq2.readLPG();
  coPpm    = mq2.readCO();
  smokePpm = mq2.readSmoke();
  photoRaw = analogRead(PHOTO_PIN);
}

String readSensorsJSON() {
  return "{\"motion\":"  + String(digitalRead(PIR_PIN)) +
         ",\"lpg\":"     + String((int)lpgPpm)          +
         ",\"co\":"      + String((int)coPpm)           +
         ",\"smoke\":"   + String((int)smokePpm)        +
         ",\"light\":"   + String(photoRaw)             +
         ",\"servo\":"   + String(servoPos)             + "}";
}

// =====================================================================
//  ALERTS
// =====================================================================
void sendMotionAlert() {
  String payload = "{\"type\":\"motion\",\"device\":\"" + String(DEVICE_NAME) + "\"}";
  if (httpPost("/alert", payload)) {
    Serial.println("[ALERT] Motion sent to Flask");
  }
}

void sendGasAlert(const String& gasType, int ppm) {
  String payload = "{\"type\":\"gas\",\"gas\":\"" + gasType +
                   "\",\"ppm\":" + String(ppm) +
                   ",\"device\":\"" + String(DEVICE_NAME) + "\"}";
  if (httpPost("/alert", payload)) {
    Serial.print("[ALERT] " + gasType + ": ");
    Serial.print(ppm);
    Serial.println(" ppm");
  }
}

// =====================================================================
//  SERVO COMMAND POLL
// =====================================================================
void pollCommand() {
  String body = httpGet("/command/ESP32_S3");
  Serial.print("[CMD] body: ");
  Serial.println(body);
  if (body.length() == 0) return;

  int idx = body.indexOf("\"servo\":");
  if (idx < 0) return;

  String val = body.substring(idx + 8);
  val.trim();

  if (val.startsWith("null")) return;

  int angle = val.toInt();
  if (angle >= 0 && angle <= 180) {
    servoPos = angle;
    myservo.write(servoPos);
    Serial.print("[SERVO] -> ");
    Serial.println(servoPos);
  }
}

// =====================================================================
//  HTTP SERVER
// =====================================================================
void handleHTTPServer() {
  WiFiClient client = server.available();
  if (!client) return;

  unsigned long start = millis();
  while (!client.available() && millis() - start < 1000) {
    delay(1);
  }

  if (!client.available()) {
    client.stop();
    return;
  }

  String request = client.readStringUntil('\r');
  client.flush();

  if (request.indexOf("GET /data") >= 0) {
    String body = readSensorsJSON();

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.print(body);
  } else {
    client.println("HTTP/1.1 404 Not Found");
    client.println("Connection: close");
    client.println();
  }

  delay(5);
  client.stop();
}

// =====================================================================
//  SETUP
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(2000);  // let hardware (ADC, radio) fully stabilise after cold power-on

  pinMode(PIR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  analogReadResolution(10); // 0..1023 — matches what the MQ2 library expects

  mq2.begin();
  ledcAttach(BUZZER_PIN, 2000, 8);

  myservo.setPeriodHertz(50);
  myservo.attach(SERVO_PIN, 500, 2400);
  myservo.write(servoPos);

  connectWiFi();
  delay(500);  // brief pause after connect before hitting the network
  server.begin();
  Serial.println("HTTP server started on port 80");

  // Register immediately — don't wait for the first loop iteration
  if (registerDevice()) {
    registered = true;
    lastRegisterTime = millis();
    Serial.println("[REGISTERED]");
  }

  Serial.println("MQ2 warmup started...");
}

// =====================================================================
//  LOOP
// =====================================================================
void loop() {
  // ---------- WiFi watchdog ----------
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    registered = false;
  }

  unsigned long now = millis();

  // ---------- Registration (periodic) ----------
  if (!registered || (now - lastRegisterTime >= REGISTER_INTERVAL)) {
    if (registerDevice()) {
      registered = true;
      lastRegisterTime = now;
      Serial.println("[REGISTERED]");
    } else if (!registered) {
      delay(3000);
      return;
    }
  }

  // ---------- PIR ----------
  bool pirNow = digitalRead(PIR_PIN);
  if (pirNow && !pirLastState && (now - lastMotionAlert > MOTION_COOLDOWN)) {
    lastMotionAlert = now;
    beep(1000, 300);
    sendMotionAlert();
  }
  pirLastState = pirNow;

  // ---------- Sensor reads ----------
  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = now;
    readSensors();

    Serial.printf("[SENSORS] LPG=%d CO=%d Smoke=%d Light=%d\n",
                  (int)lpgPpm, (int)coPpm, (int)smokePpm, photoRaw);

    // Skip MQ2 alerting during warmup
    if (now < MQ2_WARMUP) {
      Serial.println("[MQ2] warming up...");
    } else if (now - lastGasAlert > GAS_COOLDOWN) {
      bool fired = false;
      if (lpgPpm   > 0 && lpgPpm   > LPG_THRESHOLD)   { sendGasAlert("LPG",   (int)lpgPpm);   fired = true; }
      if (coPpm    > 0 && coPpm    > CO_THRESHOLD)     { sendGasAlert("CO",    (int)coPpm);    fired = true; }
      if (smokePpm > 0 && smokePpm > SMOKE_THRESHOLD)  { sendGasAlert("Smoke", (int)smokePpm); fired = true; }
      if (fired) {
        lastGasAlert = now;
        beepNonBlockingStart(2000);
        delay(300);
        beepStop();
      }
    }
  }

  // ---------- Servo command poll ----------
  if (now - lastCommandPoll >= COMMAND_INTERVAL) {
    lastCommandPoll = now;
    pollCommand();
  }

  // ---------- HTTP server ----------
  handleHTTPServer();

  delay(10);
}