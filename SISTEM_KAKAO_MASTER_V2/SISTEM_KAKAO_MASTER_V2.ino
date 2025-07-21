#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FirebaseClient.h>
#include <LoRa.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <vector>
#include <set>
#include "time.h"

// Debug
bool debug = true;

// Nodes
std::vector<String> registeredNodes;
std::vector<String> activeValves;
std::set<String> receivedNodes;

// OLED Dimension
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// OLED Instance
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// LORA Setup
#define SS 5
#define RST 14
#define DIO0 2

// Relay
#define RELAY_PIN 34

// Values
String id, hum, temp, cond, ph, n, p, k;

// Wi-Fi credentials
#define WIFI_SSID "kakao_farm"
#define WIFI_PASSWORD "kakao951farm753"

// Firebase credentials
#define Web_API_KEY "AIzaSyDxwRrvDjLqTMjjrbgwjS5FIhY49XcgHqw"
#define DATABASE_URL "https://roadeye-284ac-default-rtdb.asia-southeast1.firebasedatabase.app/"

// Firebase Authentication credentials
#define USER_EMAIL "roadeye.esp32@gmail.com"
#define USER_PASS "road3y3@esp32"

// User function
void processData(AsyncResult &aResult);

// Authentication
UserAuth user_auth(Web_API_KEY, USER_EMAIL, USER_PASS);

// Firebase components
FirebaseApp app;
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);
RealtimeDatabase Database;

void setupTime() {
  configTime(28800, 0, "pool.ntp.org", "time.nist.gov"); // UTC+8 = 28800 detik
  while (time(nullptr) < 100000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nTime synced.");
}

String getTimestamp() {
  time_t now = time(nullptr);
  struct tm* timeinfo;
  char buf[30];
  timeinfo = localtime(&now);
  strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", timeinfo);
  return String(buf);
}

void setup() {
  Serial.begin(115200);

  // OLED Init
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found.");
    while (true); // Dead loop
  } else {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.clearDisplay();
    display.setCursor(0, 0);
    Serial.println("Program Starting...");
    display.println("Program Starting...");
    display.display();
  }
  delay(1000);

  // LORA Init
  LoRa.setPins(SS, RST, DIO0);
  if (!LoRa.begin(915E6)) {
    Serial.println("LoRa failed!...");
    display.println("LoRa failed!...");
    display.display();
    while (true); // Dead loop
  } else {
    Serial.println("LoRa succeeded...");
    display.println("LoRa succeeded...");
    display.display();
  }
  delay(1000);

  // Init WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Connecting Wi-Fi...");
  display.println("Connecting Wi-Fi...");
  display.display();
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();
  delay(1000);
  
  // Configure SSL client
  ssl_client.setInsecure();
  ssl_client.setConnectionTimeout(1000);
  ssl_client.setHandshakeTimeout(5);
  
  // Init Firebase
  Serial.println("Initializing Firebase...");
  display.println("Initializing Firebase...");
  display.display();
  initializeApp(aClient, app, getAuth(user_auth), 120 * 1000, processData);
  app.getApp<RealtimeDatabase>(Database);
  Database.url(DATABASE_URL);
  getIds();
  
  // Init Times
  setupTime();
}

void loop() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  int hourNow = timeinfo->tm_hour;

  // static bool sentMorning = false;
  // static bool sentEvening = false;

  getStatusValve();
  receiveData(true);

  // if (hourNow == 6 && !sentMorning) {
  //   receiveData(true);
  //   sentMorning = true;
  //   sentEvening = false;
  // }
  // if (hourNow == 18 && !sentEvening) {
  //   receiveData(true);
  //   sentEvening = true;
  //   sentMorning = false;
  // } else {
  //   receiveData(false);
  // }

  display.clearDisplay();
  display.setCursor(0, 0);
  Serial.println("Reloading...");
  display.println("Reloading...");
  display.display();
  delay(5000);
}

void receiveData(bool upload) {
  receivedNodes.clear();

  display.clearDisplay();
  display.setCursor(0, 0);
  Serial.println("Scanning Nodes...");
  display.println("Scanning Nodes...");
  display.display();

  uint32_t period = 15 * 60000L;
  uint32_t tStart = millis();
  while (receivedNodes.size() < registeredNodes.size() && (millis()-tStart) < period) {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
      String payload = "";
      while (LoRa.available()) {
        payload += (char)LoRa.read();
      }

      // Parsing Values
      id = payload.substring(0, payload.indexOf(','));
      payload = payload.substring(payload.indexOf(',') + 1);
      hum = payloadData(payload);
      temp = payloadData(payload);
      cond = payloadData(payload);
      ph = payloadData(payload);
      n = payloadData(payload);
      p = payloadData(payload);
      k = payload;

      // Checking for Node
      if (std::find(registeredNodes.begin(), registeredNodes.end(), id) != registeredNodes.end() &&
          receivedNodes.find(id) == receivedNodes.end()) {

        // Displaying
        display.clearDisplay();
        display.setCursor(0, 0);
        Serial.println("Soil sensor: Node " + id);
        display.println("Soil sensor: Node " + id);
        display.display();

        Serial.println("Humi : " + hum + " %");
        display.println("Humi : " + hum + " %");
        display.display();

        Serial.println("Temp : " + temp + " C");
        display.println("Temp : " + temp + " C");
        display.display();

        Serial.println("EC   : " + cond + " uS/cm");
        display.println("EC   : " + cond + " uS/cm");
        display.display();

        Serial.println("pH   : " + ph);
        display.println("pH   : " + ph);
        display.display();

        Serial.println("NPK  : " + n + " / " + p + " / " + k);
        display.println("NPK  : " + n + " / " + p + " / " + k);
        display.display();

        if (upload) {
          uploadData();
        }
        
        receivedNodes.insert(id);
      } else {
        Serial.println("Node " + id + ", already checked or not registered.");
      }
    }
    delay(1000);
  }

  if (receivedNodes.size() < registeredNodes.size()) {
    display.clearDisplay();
    display.setCursor(0, 0);
    Serial.println("Timeout.");
    display.println("Timeout.");
    display.display();
  } else {
    display.clearDisplay();
    display.setCursor(0, 0);
    Serial.println("All nodes checked.");
    display.println("All nodes checked.");
    display.display();
  }
  delay(5000);
}

void uploadData() {
  bool status;
  status = Database.set<String>(aClient, "dataNodes/" + id + "/hum", hum);
  status &= Database.set<String>(aClient, "dataNodes/" + id + "/temp", temp);
  status &= Database.set<String>(aClient, "dataNodes/" + id + "/cond", cond);
  status &= Database.set<String>(aClient, "dataNodes/" + id + "/ph", ph);
  status &= Database.set<String>(aClient, "dataNodes/" + id + "/n", n);
  status &= Database.set<String>(aClient, "dataNodes/" + id + "/p", p);
  status &= Database.set<String>(aClient, "dataNodes/" + id + "/k", k);
  status &= Database.set<String>(aClient, "dataNodes/" + id + "/datetime", getTimestamp());

  Serial.println(status ? "Success to update." : "Fail to upload.");
  display.println(status ? "Success to update." : "Fail to upload.");
  display.display();
}

String payloadData(String &payload) {
  String d = payload.substring(0, payload.indexOf(','));
  payload = payload.substring(payload.indexOf(',') + 1);
  return d;
}

void getIds() {
  String values = Database.get<String>(aClient, "/registeredNodes");
  Serial.println("Registered Nodes: " + values);

  // Hapus karakter [ dan ]
  values.replace("[", "");
  values.replace("]", "");
  values.replace("\"", "");  // hilangkan tanda petik

  // Split berdasarkan koma
  int startIdx = 0;
  while (true) {
    int commaIdx = values.indexOf(',', startIdx);
    if (commaIdx == -1) {
      registeredNodes.push_back(values.substring(startIdx));
      break;
    }
    registeredNodes.push_back(values.substring(startIdx, commaIdx));
    startIdx = commaIdx + 1;
  }
}

void getStatusValve() {
  bool value = Database.get<bool>(aClient, "/statusValve");

  if (value) {
    display.clearDisplay();
    display.setCursor(0, 0);
    Serial.println("Valve Activated.");
    display.println("Valve Activated.");
    display.display();
    digitalWrite(RELAY_PIN, HIGH);
  } else {
    display.clearDisplay();
    display.setCursor(0, 0);
    Serial.println("Valve Deactivated.");
    display.println("Valve Deactivated.");
    display.display();
    digitalWrite(RELAY_PIN, LOW);
  }

  delay(5000);
}

void processData(AsyncResult &aResult) {
  if (!aResult.isResult())
    return;

  if (aResult.isEvent())
    Firebase.printf("Event task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.eventLog().message().c_str(), aResult.eventLog().code());

  if (aResult.isDebug())
    Firebase.printf("Debug task: %s, msg: %s\n", aResult.uid().c_str(), aResult.debug().c_str());

  if (aResult.isError())
    Firebase.printf("Error task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());

  if (aResult.available())
    Firebase.printf("task: %s, payload: %s\n", aResult.uid().c_str(), aResult.c_str());
}