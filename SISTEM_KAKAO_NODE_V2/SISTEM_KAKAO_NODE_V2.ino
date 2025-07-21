#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HardwareSerial.h>
#include <LoRa.h>
#include <SPI.h>
#include <Wire.h>

// Id
String id = "006";

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

// Communication Serial
#define RX_PIN 16
#define TX_PIN 17
#define RE_DE_PIN 32
HardwareSerial modbusSerial(2);

// Soil Sensor Setup
uint8_t request[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x07, 0x04, 0x08};
uint8_t response[32];

// Values
String hum, temp, cond, ph, n, p, k;

void preTransmission() {
  digitalWrite(RE_DE_PIN, HIGH);
}

void postTransmission() {
  delayMicroseconds(500);
  digitalWrite(RE_DE_PIN, LOW);
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
    Serial.println("LoRa failed!");
    display.println("LoRa failed!");
    display.display();
    while (true); // Dead loop
  } else {
    Serial.println("LoRa succeeded!");
    display.println("LoRa succeeded!");
    display.display();
  }
  delay(1000);

  // Communication Init
  pinMode(RE_DE_PIN, OUTPUT);
  digitalWrite(RE_DE_PIN, LOW);
  modbusSerial.begin(4800, SERIAL_8N1, RX_PIN, TX_PIN);
  Serial.println("Soil sensor ready!");
  display.println("Soil sensor ready!");
  display.display();
  delay(1000);
}

void loop() {
  requestSoilSensor();
  uploadData();
  delay(1000);
}

void requestSoilSensor() {
  while (modbusSerial.available()) modbusSerial.read();

  // Sending Request
  display.clearDisplay();
  display.setCursor(0, 0);
  Serial.println("Sending Request...");
  display.println("Sending Request...");
  display.display();
  preTransmission();
  modbusSerial.write(request, sizeof(request));
  modbusSerial.flush();
  postTransmission();
  delay(300);

  // Set Values
  hum     = "0";
  temp    = "0";
  cond    = "0";
  ph      = "0";
  n       = "0";
  p       = "0";
  k       = "0";

  // Receiving Response
  int len = 0;
  unsigned long start = millis();
  while ((millis() - start) < 2000 && len < sizeof(response)) {
    if (modbusSerial.available()) {
      response[len++] = modbusSerial.read();
    }
  }
  Serial.print("Bytes response: ");
  Serial.println(len);

  if (len >= 17 && response[0] == 0x01 && response[1] == 0x03 && response[2] == 0x0E) {
    Serial.println("Response valid!");
    Serial.println("HEX:");
    for (int i = 0; i < len; i++) {
      if (response[i] < 0x10) Serial.print("0");
      Serial.print(response[i], HEX);
      Serial.print(" ");
    }
    Serial.println();

    // Parsing Values
    hum     = String(((response[3] << 8) | response[4]) / 10);
    temp    = String(((response[5] << 8) | response[6]) / 10);
    cond    = String(((response[7] << 8) | response[8]) / 10);
    ph      = String(((response[9] << 8) | response[10]) / 10);
    n       = String(((response[11] << 8) | response[12]) / 10);
    p       = String(((response[13] << 8) | response[14]) / 10);
    k       = String(((response[15] << 8) | response[16]) / 10);

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
  } else {
    Serial.println("Response not valid!");
    display.println("Response not valid!");
    display.display();
  }
}

void uploadData() {
  uint32_t period = 1 * 60000L;
  String payload = id + "," + hum + "," + temp + "," + cond + "," + ph + "," + n + "," + p + "," + k;
  for (uint32_t tStart = millis(); (millis()-tStart) < period;) {
    LoRa.beginPacket();
    LoRa.print(payload);
    LoRa.endPacket();
  }
}