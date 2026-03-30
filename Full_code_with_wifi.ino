#include <WiFiS3.h>
#include <Servo.h>
#include "MQ2.h"
#include "secrets.h"

// ---------- PINS ----------
const int pirPin = 5;
const int mqPin = A0;
const int photoresPin = A1;
const int buzzer = 10;
const int servoPin = 9;

// ---------- SENSORS ----------
MQ2 mq2(mqPin);
Servo myservo;

// ---------- VARIABLES ----------
int sensorValue;
int threshold = 650;
int servoPos = 0;

// ---------- WIFI ----------
bool registered = false;
WiFiServer server(80);
WiFiClient client;

// ---------- FUNCTIONS ----------

// Connect to WiFi
void connectWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    attempts++;
    if (attempts > 60) {
      Serial.println("\nFailed to connect WiFi, restarting...");
      NVIC_SystemReset(); // reboot UNO R4
    }
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

// Register device to Flask
bool registerDevice() {
  if (WiFi.status() != WL_CONNECTED) return false;

  String payload = "{\"ip\":\"" + WiFi.localIP().toString() + "\",\"name\":\"UNO_R4\"}";
  String host = String(FLASK_IP);
  int port = FLASK_PORT;

  if (!client.connect(host.c_str(), port)) {
    Serial.println("Connection failed");
    return false;
  }

  // Manual HTTP POST
  client.println("POST /register HTTP/1.1");
  client.println("Host: " + host);
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(payload.length());
  client.println();
  client.println(payload);

  // Wait for response
  long timeout = millis();
  while (!client.available()) {
    if (millis() - timeout > 5000) {
      Serial.println("Server response timeout");
      client.stop();
      return false;
    }
  }

  while (client.available()) {
    String line = client.readStringUntil('\r');
    Serial.print(line);
  }

  client.stop();
  return true;
}

// Read sensors and return JSON
String readSensorsJSON() {
  int pirState = digitalRead(pirPin);
  int lpg = mq2.readLPG();
  int co = mq2.readCO();
  int smoke = mq2.readSmoke();
  int light = analogRead(photoresPin);

  String json = "{";
  json += "\"motion\":" + String(pirState) + ",";
  json += "\"lpg\":" + String(lpg) + ",";
  json += "\"co\":" + String(co) + ",";
  json += "\"smoke\":" + String(smoke) + ",";
  json += "\"light\":" + String(light);
  json += "}";
  return json;
}

// ---------- SETUP ----------
void setup() {
  Serial.begin(9600);
  delay(2000);

  // Sensors
  pinMode(pirPin, INPUT);
  pinMode(buzzer, OUTPUT);
  mq2.begin();
  myservo.attach(servoPin);

  // Connect WiFi
  connectWiFi();

  // Start local TCP server for /data endpoint
  server.begin();
  Serial.println("Server started, listening on port 80");
}

// ---------- LOOP ----------
void loop() {
  // Try register until success
  if (!registered) {
    registered = registerDevice();
    delay(3000);
  }

  // Read sensors and print JSON to serial
  String json = readSensorsJSON();
  Serial.println(json);

  // Serve /data endpoint
  WiFiClient clientTCP = server.available();
  if (clientTCP) {
    String request = clientTCP.readStringUntil('\r');
    clientTCP.flush();

    if (request.indexOf("GET /data") >= 0) {
      String response = readSensorsJSON();
      clientTCP.println("HTTP/1.1 200 OK");
      clientTCP.println("Content-Type: application/json");
      clientTCP.println("Connection: close");
      clientTCP.println();
      clientTCP.println(response);
    }

    clientTCP.stop();
  }

  // Buzzer test
  tone(buzzer, 1000);
  delay(1000);
  noTone(buzzer);
  delay(1000);

  // Servo sweep
  for (servoPos = 0; servoPos <= 180; servoPos += 1) {
    myservo.write(servoPos);
    delay(15);
  }
  for (servoPos = 180; servoPos >= 0; servoPos -= 1) {
    myservo.write(servoPos);
    delay(15);
  }

  delay(2000); // main loop pacing
}