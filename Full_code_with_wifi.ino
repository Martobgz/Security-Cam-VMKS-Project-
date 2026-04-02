/*
  Arduino UNO R4 WiFi — Security Camera Node
  Sensors : PIR, MQ2 (LPG/CO/Smoke), Photoresistor
  Actuators: Servo, Buzzer
  - Pushes motion/gas ALERTS to Flask immediately (no polling lag)
  - Polls Flask every 500 ms for servo commands from the app
  - Serves GET /data for Flask scraper
*/

#include <WiFiS3.h>
#include <Servo.h>
#include "MQ2.h"
#include "secrets.h"

// ---------- PINS ----------
const int PIR_PIN    = 5;
const int MQ2_PIN    = A0;
const int PHOTO_PIN  = A1;
const int BUZZER_PIN = 10;
const int SERVO_PIN  = 9;

// ---------- ALERT THRESHOLDS ----------
const int LPG_THRESHOLD   = 1000;  // ppm
const int CO_THRESHOLD    = 50;    // ppm
const int SMOKE_THRESHOLD = 300;   // ppm

// ---------- TIMING (ms) ----------
const unsigned long MOTION_COOLDOWN   =  10000;
const unsigned long GAS_COOLDOWN      =  60000;  // 1 minute between repeat alerts
const unsigned long MQ2_WARMUP        =  60000;  // 1 minute calibration on boot
const unsigned long COMMAND_INTERVAL  =    500;
const unsigned long SENSOR_INTERVAL   =   2000;

// ---------- OBJECTS ----------
MQ2   mq2(MQ2_PIN);
Servo myservo;

// ---------- STATE ----------
bool          registered    = false;
int           servoPos      = 90;
unsigned long lastMotionAlert  = 0;
unsigned long lastGasAlert     = 0;
unsigned long lastCommandPoll  = 0;
unsigned long lastSensorRead   = 0;
bool          pirLastState     = LOW;

// ---------- HTTP SERVER ----------
WiFiServer server(80);

// =====================================================================
//  HTTP HELPERS
// =====================================================================

// Returns true on HTTP 2xx; drains response body.
bool httpPost(const char* path, const String& payload) {
  WiFiClient c;
  if (!c.connect(FLASK_IP, FLASK_PORT)) {
    Serial.println("[POST] connect failed");
    return false;
  }

  c.println(String("POST ") + path + " HTTP/1.1");
  c.println(String("Host: ") + FLASK_IP);
  c.println("Content-Type: application/json");
  c.println("Connection: close");
  c.print("Content-Length: ");
  c.println(payload.length());
  c.println();
  c.print(payload);

  unsigned long t = millis();
  while (!c.available()) {
    if (millis() - t > 4000) { c.stop(); return false; }
    delay(10);
  }
  while (c.available()) c.read();
  c.stop();
  return true;
}

// Returns response body (after headers); empty string on failure.
String httpGet(const char* path) {
  WiFiClient c;
  if (!c.connect(FLASK_IP, FLASK_PORT)) return "";

  c.println(String("GET ") + path + " HTTP/1.1");
  c.println(String("Host: ") + FLASK_IP);
  c.println("Connection: close");
  c.println();

  unsigned long t = millis();
  while (!c.available()) {
    if (millis() - t > 3000) { c.stop(); return ""; }
    delay(10);
  }

  // Skip headers — body starts after blank line (\r\n\r\n)
  bool inBody = false;
  String body = "";
  while (c.available()) {
    String line = c.readStringUntil('\n');
    if (!inBody) {
      if (line == "\r") inBody = true;
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
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (++attempts > 60) {
      Serial.println("\nWiFi timeout — rebooting");
      NVIC_SystemReset();
    }
  }
  Serial.print("\nIP: ");
  Serial.println(WiFi.localIP());
}

// =====================================================================
//  REGISTRATION
// =====================================================================
bool registerDevice() {
  String payload = "{\"ip\":\"" + WiFi.localIP().toString() +
                   "\",\"name\":\"UNO_R4\",\"type\":\"sensor\"}";
  return httpPost("/register", payload);
}

// =====================================================================
//  SENSORS
// =====================================================================
String readSensorsJSON() {
  return "{\"motion\":"  + String(digitalRead(PIR_PIN)) +
         ",\"lpg\":"     + String(mq2.readLPG())        +
         ",\"co\":"      + String(mq2.readCO())         +
         ",\"smoke\":"   + String(mq2.readSmoke())      +
         ",\"light\":"   + String(analogRead(PHOTO_PIN))+
         ",\"servo\":"   + String(servoPos)             + "}";
}

// =====================================================================
//  ALERTS
// =====================================================================
void sendMotionAlert() {
  String p = "{\"type\":\"motion\",\"device\":\"UNO_R4\"}";
  if (httpPost("/alert", p))
    Serial.println("[ALERT] Motion sent to Flask");
}

void sendGasAlert(const String& gasType, int ppm) {
  String p = "{\"type\":\"gas\",\"gas\":\"" + gasType +
             "\",\"ppm\":" + String(ppm) + ",\"device\":\"UNO_R4\"}";
  if (httpPost("/alert", p)) {
    Serial.print("[ALERT] Gas: ");
    Serial.print(gasType);
    Serial.print(" ");
    Serial.print(ppm);
    Serial.println(" ppm");
  }
}

// =====================================================================
//  SERVO COMMAND POLL
// =====================================================================
void pollCommand() {
  String body = httpGet("/command/UNO_R4");
  if (body.length() == 0) return;

  // Parse {"servo":90} or {"servo":null}
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
//  HTTP SERVER  (serves /data for Flask scraper)
// =====================================================================
void handleHTTPServer() {
  WiFiClient clientTCP = server.available();
  if (!clientTCP) return;

  String request = clientTCP.readStringUntil('\r');
  clientTCP.flush();

  if (request.indexOf("GET /data") >= 0) {
    String body = readSensorsJSON();
    clientTCP.println("HTTP/1.1 200 OK");
    clientTCP.println("Content-Type: application/json");
    clientTCP.println("Connection: close");
    clientTCP.println();
    clientTCP.print(body);
  }

  delay(5);
  clientTCP.stop();
}

// =====================================================================
//  SETUP
// =====================================================================
void setup() {
  Serial.begin(9600);
  delay(1000);

  pinMode(PIR_PIN,    INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  mq2.begin();
  myservo.attach(SERVO_PIN);
  myservo.write(servoPos);   // start centered at 90°

  connectWiFi();
  server.begin();
  Serial.println("HTTP server on port 80");
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

  // ---------- Registration ----------
  if (!registered) {
    registered = registerDevice();
    if (!registered) { delay(3000); return; }
    Serial.println("[REGISTERED]");
  }

  unsigned long now = millis();

  // ---------- PIR — edge-triggered ----------
  bool pirNow = digitalRead(PIR_PIN);
  if (pirNow && !pirLastState && (now - lastMotionAlert > MOTION_COOLDOWN)) {
    lastMotionAlert = now;
    tone(BUZZER_PIN, 1000, 300);
    sendMotionAlert();
  }
  pirLastState = pirNow;

  // ---------- Gas reading ----------
  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = now;

    int lpg   = mq2.readLPG();
    int co    = mq2.readCO();
    int smoke = mq2.readSmoke();

    // Skip alerting during the 1-minute warm-up (sensor calibration)
    if (now < MQ2_WARMUP) {
      Serial.println("[MQ2] warming up...");
    } else if (now - lastGasAlert > GAS_COOLDOWN) {
      bool fired = false;
      if (lpg   > 0 && lpg   > LPG_THRESHOLD)   { sendGasAlert("LPG",   lpg);   fired = true; }
      if (co    > 0 && co    > CO_THRESHOLD)     { sendGasAlert("CO",    co);    fired = true; }
      if (smoke > 0 && smoke > SMOKE_THRESHOLD)  { sendGasAlert("Smoke", smoke); fired = true; }
      if (fired) {
        lastGasAlert = now;
        tone(BUZZER_PIN, 2000, 2000);
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
