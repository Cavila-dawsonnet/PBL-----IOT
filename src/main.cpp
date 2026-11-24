// libraries
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <MFRC522.h>
#include <SD.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h> 

// ========== global variables ==========

//Wifi credentials
const char* ssid = "Password";
const char* password = "IoT_PBL";

unsigned long timeSeconds = millis() /1000; // time in seconds
#define RFID_SS 5
#define RFID_RST 22
String authorizedUIDs[] = {"02 43 4F 05"};     // authorized UIDs (add more UIDs as needed)
int numAuthorized = 1; // number of authorized UIDs
MFRC522 rfid(RFID_SS, RFID_RST);

uint8_t statusByte = 0;
#define BIT_OCCUPIED (1 << 0)
#define GREEN_LED 25
#define RED_LED 26
unsigned long indicatorStart = 0;
unsigned long indicatorTime = 0;
bool indicatorON = false;
String lastUID = "";
unsigned long lastScanTime = 0;

const int chipSelect = 2;
File dataFile;

Adafruit_BME280 bme;
const int NUM_SAMPLES = 10;
float tempSamples[NUM_SAMPLES];
int sampleIndex = 0;
int sampleCount = 0;
float avgTemp = 0.0;
unsigned long lastTempTime = 0;
const unsigned long TEMP_INTERVAL = 2000; // 2 seconds
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_MOSI 11
#define OLED_CLK 13
#define OLED_DC 9
#define OLED_CS 5
#define OLED_RST 8
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RST, OLED_CS);

//webserver
WebServer server(80);

const char webpage[] PROGMEM = R"rawliteral(

<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<meta http-equiv="refresh" content="5">
<title>ESP32 Environmental Monitor</title>
<style>
body{
    font-family:Arial,sans-serif;
    background:linear-gradient(135deg, #1E3C72, #2A5298);
    color:white;
    text-align:center;
    margin:0;
    padding:20px;
}
.container{
    max-width:800px;
    margin:auto;
}
.card{
    background:rgba(255, 255, 255, 0.15);
    padding:20px;
    border-radius:15px;
    margin-bottom: 20px;
    box-shadow: 0px 4px 12px rgba(0, 0, 0, 0.3);
}
.value{
    font-size:36px;
    font-weight:bold;
    color:#FFD54F;
}
.grid {
    display: flex;
    justify-content: space-around;
    flex-wrap: wrap;
}
.sub-card {
    background: rgba(0, 0, 0, 0.2);
    padding: 15px;
    border-radius: 10px;
    width: 40%;
    margin-bottom: 15px;
}
button{
    width:180px;
    height:60px;
    margin:10px;
    font-size:20px;
    border:none;
    border-radius:10px;
    cursor:pointer;
}
.on{
    background:#4CAF50;
    color:white;
}
.off{
    background:#F44336;
    color:white;
}
.info{
    margin-top:20px;
}
</style>
</head>
<body>
<div class="container">
<h1>🌎 ESP32 Environmental Monitor</h1>

<div class="card">
    <h2>Average Temperature</h2>
    <p class="value">TEMP_PLACEHOLDER &deg;C</p>
</div>

<div class="grid">
    <div class="sub-card">
        <h3>Room Status</h3>
        <p class="value" style="font-size: 24px;">ROOM_STATUS_PLACEHOLDER</p>
    </div>
    <div class="sub-card">
        <h3>Fan Level</h3>
        <p class="value">FAN_LEVEL_PLACEHOLDER</p>
    </div>
</div>

<div class="card">
    <h3>System Status Byte (Hex/Bin)</h3>
    <p style="font-family: monospace; font-size: 18px; color: #81D4FA;">STATUS_BYTE_PLACEHOLDER</p>
</div>

<button class="on" onclick="location.href='/on'">LED ON</button>
<button class="off" onclick="location.href='/off'">LED OFF</button>

<div class="info">
<p>SSID: ESP32_Lab7</p>
<p>IP Address: 192.168.4.1</p>
</div>
</div>
</body>
</html>
)rawliteral";

// ========== functions ==========
    // check-in = OnOccupiedRoom
    // check-out = OnFreeRoom

String getUID(){    // returns the UID of the scanned card as a string
  String uid = "";

  for (int i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) {
      uid = uid + "0";
    }
    uid = uid + String(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) {
      uid = uid + " ";
    }
  }
  uid.toUpperCase();
  return uid;
}

bool isAuthorized(String uid) {
  for (int i = 0; i < numAuthorized; i++){
    if (uid == authorizedUIDs[i]) {
      return true;
    }
  }
  return false;
}

void startIndicatorTimer(unsigned long duration) {
  indicatorStart = millis();
  indicatorTime = duration;
  indicatorON = true;
}

void printStatusByte() {
  Serial.print("Status byte: ");
  Serial.println(statusByte, BIN);
}

// ============ CHECK-OUT ============
void onFreeRoom() {
  Serial.println("ROOM IS FREE, YOU MAY ENTER");
  digitalWrite(GREEN_LED, LOW);
}

// ============ CHECK-IN ============
void onOccupiedRoom(){
  Serial.println("ROOM IS OCCUPIED, DO NOT ENTER");
  digitalWrite(GREEN_LED, HIGH);
}

void onUnknownCard() {
  Serial.println("ACCESS DENIED, UNKNOWN CARD");
  digitalWrite(RED_LED, HIGH);
  startIndicatorTimer(1000);
}

void OnTimeExpired() {
  digitalWrite(RED_LED, LOW);
  indicatorON = false;
  Serial.println("READY");
}

void onCardScanned(String uid) {    // called when a card is scanned
  if (uid == lastUID && millis() - lastScanTime <2000) {
    Serial.println("Repeat ignored");
    return;
  }

  lastUID = uid;
  lastScanTime = millis();

  Serial.print("Card UID: ");
  Serial.println(uid);

  if (isAuthorized(uid) == true) {    // check if the card is authorized
    if ((statusByte & BIT_OCCUPIED) == 0) {
      statusByte |= BIT_OCCUPIED;
      onOccupiedRoom();
    } else {
      statusByte &= ~BIT_OCCUPIED;
      onFreeRoom();
    }
  } else {
    onUnknownCard();
  }
}

void setup() {
    Serial.begin(115200);
    Serial.println("Program started");
    Wire.begin();
    Serial.println("I2C started");

  // ========== check if sensor is connected ==========
    if (bme.begin(0x76))  {
      Serial.println("BME280 Found at 0x76");
    } else if (bme.begin(0x77)) {
      Serial.println("BME280 Found at 0x77");
    } else {
      Serial.println("BME280 Not Found");
    }

  //========== OLED Display setup ==========
    display.begin(SSD1306_SWITCHCAPVCC);
    display.clearDisplay(); // CLEAR DISPLAY
    display.setTextSize(1); // SET TEXT SIZE
    display.setTextColor(SSD1306_WHITE);  // SET TEXT COLOR
    display.display();  // UPDATE DISPLAY
    
  //=========== RFID code ===========
    SPI.begin();
    rfid.PCD_Init();
    delay(10);
    rfid.PCD_DumpVersionToSerial();     // prints the firmware version
    Serial.println("Tap your student library card");

  //=========== LED code ===========
    pinMode(GREEN_LED, OUTPUT);
    pinMode(RED_LED, OUTPUT);
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(RED_LED, LOW);

  // ========== data logging code ===========
    Serial.println("Looking for SD card...");

    if(SD.begin(chipSelect)) {
      Serial.println("SD card found");
    } else {
      Serial.println("File not found");
    }
}



void loop() {
  // ========== RFID code ===========
  if (rfid.PICC_IsNewCardPresent() == true && rfid.PICC_ReadCardSerial() == true){
    String uid = getUID();
    onCardScanned(uid);
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  } 

  // ========== timer code ===========
  if (indicatorON == true && millis() - indicatorStart >= indicatorTime) {
    OnTimeExpired();
  }

  // ========== TEMP READING ===========
    temperature = bme.readTemperature();
    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.println(" °C");
    delay(2000); // wait 2 seconds before next reading

  // ========== OLED Display code ===========
    display.clearDisplay();
    display.setCursor(0, 0);

  // ========== Display temperature ===========
    display.print("Temp: ");
    display.print(temperature);
    display.println(" °C");
  
  // ========== Display room status ===========
    display.println("Room Status: ");
    if (statusByte & BIT_OCCUPIED) {
      display.println("Occupied");
    } else {
      display.println("Free");
    }
    display.display();


  // ========== data logging code ===========
    if(onOccupiedRoom || onFreeRoom)
    {
      dataFile = SD.open("data.csv", FILE_WRITE);

      if(dataFile)
      {
        if(onOccupiedRoom)
        {
          dataFile.print("Check in at, ");
          dataFile.println(timeSeconds);
        }
        if(onFreeRoom)
        {
          dataFile.print("Checked out at, ");
          dataFile.println(timeSeconds);
        }
        dataFile.close();
      }
      else
      {
        Serial.println("Error! Could not find data.csv");
      }
    }
    //needs help requests
}
