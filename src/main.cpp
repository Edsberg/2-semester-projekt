#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <U8g2lib.h>
#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif

#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif
#include <Wire.h>
#include "DFRobot_OxygenSensor.h"

#define COLLECT_NUMBER 10
#define Oxygen_IICAddress 0x73

DFRobot_OxygenSensor oxygen;

#define MQ135_PIN 34
#define BUZZER_PIN 27

U8G2_SH1106_128X32_VISIONOX_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// WiFi credentials
const char* SSID = "iPhone";
const char* PASSWORD = "bamse123";

// MQTT broker settings
const char* MQTT_BROKER = "172.20.10.3";
const int MQTT_PORT = 1883;
const char* MQTT_TOPIC = "edge_device";

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastPublishTime = 0;
unsigned long startTime = 0;
const unsigned long PUBLISH_INTERVAL = 2000;

// Starting date/time
int startYear = 2026, startMonth = 5, startDay = 27;
int startHour = 12, startMinute = 30, startSecond = 45;

void printAvailableNetworks() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);

  int n = WiFi.scanNetworks();
  if (n == WIFI_SCAN_FAILED) {
    Serial.println("WiFi scan failed. Trying async scan...");
    WiFi.scanNetworks(true);
    while (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
      delay(100);
    }
    n = WiFi.scanComplete();
  }

  if (n <= 0) {
    if (n == 0) {
      Serial.println("No networks found.");
    } else {
      Serial.print("Scan error: ");
      Serial.println(n);
    }
  } else {
    Serial.print(n);
    Serial.println(" networks found:");
    for (int i = 0; i < n; i++) {
      Serial.print(i + 1);
      Serial.print(". ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (RSSI ");
      Serial.print(WiFi.RSSI(i));
      Serial.print(" dBm, ");
      Serial.print(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "Open" : "Encrypted");
      Serial.println(")");
    }
  }
  WiFi.scanDelete();
}

void setupWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.print("WiFi connection failed. Status: ");
    Serial.println(WiFi.status());
    Serial.println("Scanning available networks...");
    printAvailableNetworks();
    // Status codes: 0=IDLE, 1=NO_SSID_AVAIL, 2=SCAN_COMPLETED, 3=CONNECTED,
    // 4=CONNECT_FAILED, 5=CONNECTION_LOST, 6=WRONG_PASSWORD, 7=DISCONNECTED
  }
}

void reconnectMQTT() {
  while (!client.connected()) {
    if (client.connect("ESP32_Sensor")) {
      Serial.println("MQTT connected");
    } else {
      delay(5000);
    }
  }
}

String getFormattedTime() {
  unsigned long elapsedSeconds = (millis() - startTime) / 1000;
  
  int seconds = startSecond + (elapsedSeconds % 60);
  int minutes = startMinute + ((elapsedSeconds / 60) % 60);
  int hours = startHour + ((elapsedSeconds / 3600) % 24);
  int days = startDay + (elapsedSeconds / 86400);
  
  int month = startMonth;
  int year = startYear;
  
  // Simple day overflow handling
  int daysInMonth[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  
  if (seconds >= 60) {
    seconds -= 60;
    minutes++;
  }
  if (minutes >= 60) {
    minutes -= 60;
    hours++;
  }
  if (hours >= 24) {
    hours -= 24;
    days++;
  }
  if (days > daysInMonth[month]) {
    days = 1;
    month++;
    if (month > 12) {
      month = 1;
      year++;
    }
  }
  
  char buffer[20];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d", 
           year, month, days, hours, minutes, seconds);
  
  return String(buffer);
}

void publishSensorData(float oxygenData, float co2Data) {
  // Format: yyyy-mm-dd hh:mm:ss sensor1_value sensor2_value
  String timestamp = getFormattedTime();
  char message[128];
  snprintf(message, sizeof(message), "%s %.1f %.1f", timestamp.c_str(), oxygenData, co2Data);
  
  if (client.publish(MQTT_TOPIC, message)) {
    Serial.println(message);
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  
  //mqtt
  Serial.println("\nESP32 MQTT Sensor Publisher");
  startTime = millis();
  setupWiFi();
  client.setServer(MQTT_BROKER, MQTT_PORT);

  // oxygen sensor
  while(!oxygen.begin(Oxygen_IICAddress)){
    Serial.println("I2c device number error !");
    delay(1000);
  }
  Serial.println("I2c connect success !");

  // OLED display
  u8g2.begin();
  Wire.begin();
  pinMode(BUZZER_PIN, OUTPUT);
  delay(2000);
}

void loop() {
  //display og sensors
  //clear display
  u8g2.clearBuffer(); 
  u8g2.setFont(u8g2_font_t0_11_tf);

  float oxygenData = oxygen.getOxygenData(COLLECT_NUMBER);
  Serial.print("oxygen concentration is ");
  Serial.print(oxygenData);
  Serial.println(" %vol");
  u8g2.drawStr(10, 10, "O2: ");
  u8g2.drawStr(40, 10, String(oxygenData, 1).c_str());
  u8g2.drawStr(70, 10, "procent");

  // Læs sensor
  int rawValue = analogRead(MQ135_PIN);

  // Spænding
  float voltage = rawValue * (3.3 / 4095.0);

  float ppm = map(rawValue, 3000, 4095, 1000, 5000);

  // Begræns værdierne
  if (ppm < 400) ppm = 400;
  if (ppm > 5000) ppm = 5000;

  Serial.print("Estimeret CO2: ");
  Serial.print(ppm);
  Serial.println(" ppm");

  // Luftkvalitet vurdering
  if (ppm > 1333 || oxygenData < 18.5) {
    Serial.println("Luftkvalitet: DÅRLIG");
    u8g2.drawStr(10, 30, "DÅRLIG LUFT!!!");
    digitalWrite(BUZZER_PIN, HIGH);
  }
  else {
    Serial.println("Luftkvalitet: GOD");
    u8g2.drawStr(10, 30, "GOD LUFT");
    digitalWrite(BUZZER_PIN, LOW);
  }

  u8g2.drawStr(10, 20, "CO2: ");
  u8g2.drawStr(40, 20, String(ppm, 1).c_str());
  u8g2.drawStr(80, 20, "ppm");

  u8g2.sendBuffer();

  //mqtt og wifi
  if (WiFi.status() != WL_CONNECTED) {
    setupWiFi();
  }
  
  if (!client.connected()) {
    reconnectMQTT();
  }
  
  client.loop();
  
  if (millis() - lastPublishTime >= PUBLISH_INTERVAL) {
    publishSensorData(oxygenData, ppm);
    lastPublishTime = millis();
  }


  delay(2000);
    
}