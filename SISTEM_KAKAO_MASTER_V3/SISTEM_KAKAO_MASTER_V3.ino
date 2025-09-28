#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LoRa.h>
#include <SPI.h>
#include <Wire.h>
#include <vector>
#include <set>

// Nodes setup
std::vector<String> registeredNodes;
std::vector<String> activeValves;
std::set<String> receivedNodes;

// OLED config
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// LORA config
#define SS 5
#define RST 14
#define DIO0 2

// Relay config
#define RELAY_PIN 4

// System values
String farm_id = "Farm_001";
String node_id, temperature, moisture, conductivity, ph, nitrogen, phosphorus, potassium;

// Wi-Fi credentials
#define WIFI_SSID "kakaofarm"
#define WIFI_PASSWORD "Kakaofarm123"

// Firebase credentials
#define API_KEY "AIzaSyDxwRrvDjLqTMjjrbgwjS5FIhY49XcgHqw"
#define PROJECT_ID "roadeye-284ac"
#define DATABASE_URL "https://roadeye-284ac-default-rtdb.asia-southeast1.firebasedatabase.app/"

// Firebase Authentication credentials
#define USER_EMAIL "kakaofarmbali@gmail.com"
#define USER_PASS "Kakaofarm123"
String user_uid = "null";

// Firebase functions
void processData(AsyncResult &aResult);
bool verifyUser(const String &apiKey, const String &email, const String &password);

// Firebase config
FirebaseApp app;
WiFiClientSecure ssl_client, stream_ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client), streamClient(stream_ssl_client);

// Firebase authentication
UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASS);

// Firebase RTDB config
RealtimeDatabase Database;

// Firebase firestore config
Firestore::Documents Docs;
AsyncResult databaseResult;

// Pre-initialize Functions
String getTimestampString(uint64_t sec, uint32_t nano);
String getReadableTimestamp(uint64_t sec);

// Main setup
void setup() {
  Serial.begin(115200);

  // Pinmode Relay
  pinMode(RELAY_PIN, OUTPUT);

  // OLED setup
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found.");
    Serial.println("Program Stoped.");
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

  // LORA setup
  LoRa.setPins(SS, RST, DIO0);
  if (!LoRa.begin(915E6)) {
    Serial.println("LoRa failed!...");
    display.println("LoRa failed!...");
    Serial.println("Program Stoped.");
    display.println("Program Stoped.");
    display.display();
    while (true); // Dead loop
  } else {
    Serial.println("LoRa initialized...");
    display.println("LoRa initialized...");
    display.display();
  }
  delay(1000);

  // WiFi Setup
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
  display.println("Wi-Fi connected.");
  display.display();
  delay(1000);
  
  // SSL client setup
  ssl_client.setInsecure();
  ssl_client.setConnectionTimeout(1000);
  ssl_client.setHandshakeTimeout(5);
  stream_ssl_client.setInsecure();
  stream_ssl_client.setConnectionTimeout(1000);
  stream_ssl_client.setHandshakeTimeout(5);
  
  // Firebase setup
  Serial.println("Connecting to Firebase...");
  display.println("Connecting to Firebase...");
  display.display();
  initializeApp(aClient, app, getAuth(user_auth), 120 * 1000, processData);
  Serial.println("Firebase connected.");
  display.println("Firebase connected.");
  display.display();

  // Verify firebase authentication
  Serial.print("Verifying user...");
  display.println("Verifying user...");
  display.display();
  bool ret = verifyUser(API_KEY, USER_EMAIL, USER_PASS);
  user_uid = (ret ? app.getUid().c_str() : "null");
  Serial.println(ret ? "verified." : "failed.");
  display.println(ret ? "verified." : "failed.");
  display.display();

  // Firebase RTDB setup
  app.getApp<RealtimeDatabase>(Database);
  Database.url(DATABASE_URL);

  // Firebase RTDB stream setup
  streamClient.setSSEFilters("get");
  Database.get(streamClient, user_uid + "/dataFarms/" + farm_id + "/statusValve", processData, true, "streamTask");

  // Firebase firestore setup
  app.getApp<Firestore::Documents>(Docs);

  // Get registered nodes
  getNodes();
}

// Main
void loop() {
  receiveData(true);

  display.clearDisplay();
  display.setCursor(0, 0);
  Serial.println("Reloading...");
  display.println("Reloading...");
  display.display();
  delay(3000);
}

// Get timestamp
String getTimestampString(uint64_t sec, uint32_t nano) {
  if (sec > 0x3afff4417f)
      sec = 0x3afff4417f;

  if (nano > 0x3b9ac9ff)
      nano = 0x3b9ac9ff;

  time_t now;
  struct tm ts;
  char buf[80];
  now = sec;
  ts = *localtime(&now);

  String format = "%Y-%m-%dT%H:%M:%S";

  if (nano > 0)
  {
    String fraction = String(double(nano) / 1000000000.0f, 9);
    fraction.remove(0, 1);
    format += fraction;
  }
  format += "Z";

  strftime(buf, sizeof(buf), format.c_str(), &ts);
  return buf;
}

// Get readable timestamp
String getReadableTimestamp(uint64_t sec) {
  time_t now = sec;
  struct tm ts = *localtime(&now);
  char buf[30];
  
  // Format: YYYY-MM-DD_HH-MM-SS
  strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &ts);
  
  return String(buf);
}

// Lora receive data
void receiveData(bool upload) {
  receivedNodes.clear();

  display.clearDisplay();
  display.setCursor(0, 0);
  Serial.println("Scanning Nodes...");
  display.println("Scanning Nodes...");
  display.display();

  uint32_t period = 60 * 60000L;
  uint32_t tStart = millis();
  while (receivedNodes.size() < registeredNodes.size() && (millis()-tStart) < period) {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
      String payload = "";
      while (LoRa.available()) {
        payload += (char)LoRa.read();
      }

      // Parsing Values
      node_id           = payload.substring(0, payload.indexOf(','));
      payload      = payload.substring(payload.indexOf(',') + 1);
      temperature  = payloadData(payload);
      moisture     = payloadData(payload);
      conductivity = payloadData(payload);
      ph           = payloadData(payload);
      nitrogen     = payloadData(payload);
      phosphorus   = payloadData(payload);
      potassium    = payload;

      // Checking for Node
      if (std::find(registeredNodes.begin(), registeredNodes.end(), node_id) != registeredNodes.end() &&
          receivedNodes.find(node_id) == receivedNodes.end()) {

        // Displaying
        display.clearDisplay();
        display.setCursor(0, 0);
        Serial.println("Soil sensor: Node " + node_id);
        display.println("Soil sensor: Node " + node_id);
        display.display();

        Serial.println("Temp : " + temperature + " C");
        display.println("Temp : " + temperature + " C");
        display.display();

        Serial.println("Mois : " + moisture + " %");
        display.println("Mois : " + moisture + " %");
        display.display();

        Serial.println("Cond : " + conductivity + " uS/cm");
        display.println("Cond : " + conductivity + " uS/cm");
        display.display();

        Serial.println("pH   : " + ph);
        display.println("pH   : " + ph);
        display.display();

        Serial.println("NPK  : " + nitrogen + " / " + phosphorus + " / " + potassium);
        display.println("NPK  : " + nitrogen + " / " + phosphorus + " / " + potassium);
        display.display();

        if (upload) {
          uint64_t nowSec = time(nullptr);
          RTDBUpload(nowSec);
          firestoreUpload(nowSec);
          Serial.println("Database updated.");
          display.println("");
          display.println("Database updated.");
          display.display();
        }
        
        receivedNodes.insert(node_id);
      } else {
        Serial.println("Node " + node_id + ", already checked or not registered.");
      }
    }
    delay(300);
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
    // Idling while timer runsout
  }
  delay(3000);
}

// Lora payload helper
String payloadData(String &payload) {
  String d = payload.substring(0, payload.indexOf(','));
  payload = payload.substring(payload.indexOf(',') + 1);
  return d;
}

// Get registered nodes
void getNodes() {
  String values = Database.get<String>(aClient, user_uid + "/dataFarms/" + farm_id + "/registeredNodes");
  Serial.println("Registered Nodes: " + values);

  values.replace("[", "");
  values.replace("]", "");
  values.replace("\"", "");

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

// Get valve's status
void getStatusValve(bool status) {
  if (status) {
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

// Firebase RTDB upload
void RTDBUpload(uint64_t nowSec) {
  Database.set<String>(aClient, user_uid + "/dataFarms/" + farm_id + "dataNodes/" + node_id + "/temperature", temperature);
  Database.set<String>(aClient, user_uid + "/dataFarms/" + farm_id + "dataNodes/" + node_id + "/moisture", moisture);
  Database.set<String>(aClient, user_uid + "/dataFarms/" + farm_id + "dataNodes/" + node_id + "/conductivity", conductivity);
  Database.set<String>(aClient, user_uid + "/dataFarms/" + farm_id + "dataNodes/" + node_id + "/ph", ph);
  Database.set<String>(aClient, user_uid + "/dataFarms/" + farm_id + "dataNodes/" + node_id + "/nitrogen", nitrogen);
  Database.set<String>(aClient, user_uid + "/dataFarms/" + farm_id + "dataNodes/" + node_id + "/phosphorus", phosphorus);
  Database.set<String>(aClient, user_uid + "/dataFarms/" + farm_id + "dataNodes/" + node_id + "/potassium", potassium);
  Database.set<String>(aClient, user_uid + "/dataFarms/" + farm_id + "dataNodes/" + node_id + "/timestamp", getReadableTimestamp(nowSec));
}

// Firebase firestore upload
void firestoreUpload(uint64_t nowSec) {
  String readableTime = getReadableTimestamp(nowSec);
  
  String history_path = user_uid + farm_id + node_id + readableTime;

  Document<Values::Value> doc("temperature", Values::Value(Values::StringValue(temperature)));
  doc.add("moisture", Values::Value(Values::StringValue(moisture)));
  doc.add("conductivity", Values::Value(Values::StringValue(conductivity)));
  doc.add("ph", Values::Value(Values::StringValue(ph)));
  doc.add("nitrogen", Values::Value(Values::StringValue(nitrogen)));
  doc.add("phosphorus", Values::Value(Values::StringValue(phosphorus)));
  doc.add("potassium", Values::Value(Values::StringValue(potassium)));
  doc.add("timestamp", Values::Value(Values::StringValue(getReadableTimestamp(nowSec))));

  Docs.createDocument(aClient, Firestore::Parent(PROJECT_ID), history_path, DocumentMask(), doc);
}

// ...Firebase functions
void processData(AsyncResult &aResult) {
  // Exits when no result available when calling from the loop.
  if (!aResult.isResult())
    return;

  if (aResult.isEvent())
  {
    Firebase.printf("Event task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.eventLog().message().c_str(), aResult.eventLog().code());
  }

  if (aResult.isDebug())
  {
    Firebase.printf("Debug task: %s, msg: %s\n", aResult.uid().c_str(), aResult.debug().c_str());
  }

  if (aResult.isError())
  {
    Firebase.printf("Error task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());
  }

  if (aResult.available())
  {
    Firebase.printf("task: %s, payload: %s\n", aResult.uid().c_str(), aResult.c_str());

    RealtimeDatabaseResult &RTDB = aResult.to<RealtimeDatabaseResult>();
    if (RTDB.isStream())
    {
      Serial.println("----------------------------");
      Firebase.printf("task: %s\n", aResult.uid().c_str());
      Firebase.printf("event: %s\n", RTDB.event().c_str());
      Firebase.printf("path: %s\n", RTDB.dataPath().c_str());
      Firebase.printf("data: %s\n", RTDB.to<const char *>());
      Firebase.printf("type: %d\n", RTDB.type());

      bool valve_status = RTDB.to<bool>();
      getStatusValve(valve_status);
    }
    else
    {
      Serial.println("----------------------------");
      Firebase.printf("task: %s, payload: %s\n", aResult.uid().c_str(), aResult.c_str());
    }
    Firebase.printf("Free Heap: %d\n", ESP.getFreeHeap());
  }
}

bool verifyUser(const String &apiKey, const String &email, const String &password) {
  if (ssl_client.connected())
    ssl_client.stop();

  String host = "www.googleapis.com";
  bool ret = false;

  if (ssl_client.connect(host.c_str(), 443) > 0)
  {
    String payload = "{\"email\":\"";
    payload += email;
    payload += "\",\"password\":\"";
    payload += password;
    payload += "\",\"returnSecureToken\":true}";

    String header = "POST /identitytoolkit/v3/relyingparty/verifyPassword?key=";
    header += apiKey;
    header += " HTTP/1.1\r\n";
    header += "Host: ";
    header += host;
    header += "\r\n";
    header += "Content-Type: application/json\r\n";
    header += "Content-Length: ";
    header += payload.length();
    header += "\r\n\r\n";

    if (ssl_client.print(header) == header.length())
    {
      if (ssl_client.print(payload) == payload.length())
      {
        unsigned long ms = millis();
        while (ssl_client.connected() && ssl_client.available() == 0 && millis() - ms < 5000)
        {
          delay(1);
        }

        ms = millis();
        while (ssl_client.connected() && ssl_client.available() && millis() - ms < 5000)
        {
          String line = ssl_client.readStringUntil('\n');
          if (line.length())
          {
            ret = line.indexOf("HTTP/1.1 200 OK") > -1;
            break;
          }
        }
        ssl_client.stop();
      }
    }
  }

  return ret;
}