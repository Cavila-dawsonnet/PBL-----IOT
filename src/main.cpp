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
#include <HTTPServer.h>

// ========== global variables ==========

//Wifi credentials
const char* ssid = "Password";
const char* password = "IoT_PBL"; 

Adafruit_BME280 bme;
const int NUM_SAMPLES = 10;
float tempSamples[NUM_SAMPLES];
int sampleIndex = 0;
int sampleCount = 0;
float avgTemp = 0.0;
unsigned long lastTempTime = 0;
const unsigned long TEMP_INTERVAL = 2000; // 2 seconds

#define RFID_SS 5
#define RFID_RST 27
String authorizedUIDs[] = {"02 43 4F 05"};     // authorized UIDs (add more UIDs as needed)
int numAuthorized = 1; // number of authorized UIDs
MFRC522 rfid(RFID_SS, RFID_RST);
String lastUID = "";
unsigned long lastScanTime = 0;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_DC 2
#define OLED_CS 15
#define OLED_RST 4
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &SPI, OLED_DC, OLED_RST, OLED_CS);

uint8_t statusByte = 0;
#define BIT_OCCUPIED (1 << 0)
#define GREEN_LED 25
#define RED_LED 26
unsigned long indicatorStart = 0;
unsigned long indicatorTime = 0;
bool indicatorON = false;

unsigned long timeSeconds = millis() / 1000; // time in seconds since program start
const int chipSelect = 10;

//webserver 
WebServer server(80);

const char webpage[] PROGMEM = R"rawliteral(

<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Room Status</title>
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin-top: 30px; }
    .status { font-size: 2em; margin: 20px 0; font-weight: bold; }
    .occupied { color: red; }
    .free { color: green; }
    .metrics { display: inline-block; text-align: left; font-size: 1.2em; line-height: 1.8; }
  </style>
</head>
<body>
  <h1>Room Monitor</h1>
  <div id="status" class="status">Loading...</div>
  <div class="metrics">
    <div><strong>Avg Temperature:</strong> <span id="temp">--</span> &deg;C</div>
    <div><strong>Fan Level:</strong> <span id="fan">--</span></div>
    <div><strong>Status Byte:</strong> <span id="byte">--</span></div>
  </div>

  <script>
    async function fetchData() {
      try {
        const response = await fetch('/data');
        const data = await response.json();

        const statusEl = document.getElementById('status');
        if (data.occupied) {
          statusEl.textContent = 'OCCUPIED';
          statusEl.className = 'status occupied';
        } else {
          statusEl.textContent = 'FREE';
          statusEl.className = 'status free';
        }

        document.getElementById('temp').textContent = data.avg_temperature.toFixed(1);
        document.getElementById('fan').textContent = data.fan_level;
        // Display status byte in both hex and binary format
        document.getElementById('byte').textContent = 
          '0x' + data.status_byte.toString(16).toUpperCase().padStart(2, '0') + 
          ' (0b' + data.status_byte.toString(2).padStart(8, '0') + ')';
      } catch (err) {
        console.error('Error fetching /data:', err);
      }
    }

    // Fetch immediately on load, then every 2 seconds
    fetchData();
    setInterval(fetchData, 2000);
  </script>
</body>
</html>
)rawliteral";

// ====== sensor and display functions
float getAverage() {
  if(sampleCount == 0) {
    return 0.0;
  }float sum = 0.0;
  for (int i = 0; i < sampleCount; i++) {
    sum = sum + tempSamples[i];
  }
  return sum / sampleCount;
}

void updateDisplay() {
  display.clearDisplay();
  display.setCursor(0, 0);

  // DISPLAY ROOM STATUS
  display.println("Room Status: ");
  if (statusByte & BIT_OCCUPIED) {
    display.println("Occupied");
  } else {
    display.println("Free");
  }

  // show alert if unknown card is scanned
  if (indicatorON) {
    display.println("ALERT: Access Denied");
    display.println("UNKNOWN CARD!!!!");
  }

  // DISPLAY AVERAGE TEMP
  display.println();
  display.print("Avg Temp: ");
  display.print(avgTemp, 1);
  display.println(" C");

  display.display();
}

void readTemp() {
  float currentTemp = bme.readTemperature();

  // store readings in array
  tempSamples[sampleIndex] = currentTemp;
  sampleIndex = (sampleIndex + 1) % NUM_SAMPLES;
  if (sampleCount < NUM_SAMPLES) {
    sampleCount++;
  }

  avgTemp = getAverage();

  Serial.print("Temperature: ");
  Serial.print(currentTemp);
  Serial.print(" C | Avg (");
  Serial.print(sampleCount);
  Serial.print("): ");
  Serial.print(avgTemp, 1);
  Serial.println(" C");

  updateDisplay();
}

// ====== RFID AND LED FUNCTIONS ======
String getUID() {    // returns the UID of the scanned card as a string
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
  updateDisplay();
}

// ============ CHECK-IN ============
void onOccupiedRoom(){
  Serial.println("ROOM IS OCCUPIED, DO NOT ENTER");
  digitalWrite(GREEN_LED, HIGH);
  updateDisplay();
}

void onUnknownCard() {
  Serial.println("ACCESS DENIED, UNKNOWN CARD");
  digitalWrite(RED_LED, HIGH);
  startIndicatorTimer(1000);
  updateDisplay();
}

void OnTimeExpired() {
  digitalWrite(RED_LED, LOW);
  indicatorON = false;
  Serial.println("READY");
  updateDisplay();
}

void onCardScanned(String uid) {    // called when a card is scanned
  if (uid == lastUID && millis() - lastScanTime < 2000) {
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
    printStatusByte();
  } else {
    onUnknownCard();
  }
}

void setup() {
    Serial.begin(115200);
    Serial.println("Program started");

    Wire.begin();
    Serial.println("I2C started");
    SPI.begin();

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

  // first temp reading
    readTemp();
    lastTempTime = millis();
}

void loop() {
  // ========== RFID code ===========
    if (rfid.PICC_IsNewCardPresent() == true  && rfid.PICC_ReadCardSerial() == true) {
      String uid = getUID();
      onCardScanned(uid);
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
    }

  // ========== red LED timer ===========
    if (indicatorON && millis() - indicatorStart >= indicatorTime) {
      OnTimeExpired();
    }

  // ========== temperature and OLED timer ===========
    if (millis() - lastTempTime >= TEMP_INTERVAL) {
      lastTempTime = millis();
      readTemp();
    }

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
