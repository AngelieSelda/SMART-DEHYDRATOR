#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h> 
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h"

// --- CONFIGURATION ---
#define WIFI_SSID "JAH"
#define WIFI_PASSWORD "12345678"
#define API_KEY "AIzaSyAXz_c7gNjyzKXuPLTU1d-nRU3aSHslGOA"
#define DATABASE_URL "https://smart-dehydrator-final-default-rtdb.asia-southeast1.firebasedatabase.app/"

// --- PINS ---
#define PIN_DHT 4
#define PIN_SELECT 12      
#define PIN_CANCEL 13      
#define PIN_MANGO 14       
#define PIN_APPLE 25       
#define PIN_BANANA 26      
#define PIN_ORANGE 27      
#define PIN_GRAPES 33      
#define PIN_BLOWER 5
#define PIN_HEATER_1 18
#define PIN_HEATER_2 19

DHT dht(PIN_DHT, DHT21); 
LiquidCrystal_I2C lcd(0x27, 20, 4);
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

enum State { WELCOME, LOCKED, MODE_SELECT, PRESET_VIEW, MANUAL_SET, DRYING, STOP_CONFIRM };
State currentState = WELCOME;

float temp = 0, hum = 0;
int targetTemp = 35;        
int targetTimeHrs = 1;
int targetTimeMins = 0;   
unsigned long startTime = 0;
unsigned long totalSeconds = 0;
String fruitName = "IDLE";
bool isRunning = false;
bool adjustMinutes = false; 
unsigned long lastFirebaseUpdate = 0;
unsigned long lastLCDUpdate = 0;
State lastState = WELCOME;

unsigned long lastBlowerToggle = 0;
bool intervalBlowerActive = false;
const unsigned long BLOWER_ON_DURATION = 45000;      
const unsigned long BLOWER_OFF_DURATION = 900000;    

void startDrying();
void stopDrying(String reason);
void updateLCDOperation();
void handleHardware();
void handleAppCommands();
void updateFirebase();
void refreshStaticText();

void setup() {
  Serial.begin(115200);
  
  int inputPins[] = {PIN_SELECT, PIN_CANCEL, PIN_MANGO, PIN_APPLE, PIN_BANANA, PIN_ORANGE, PIN_GRAPES};
  for(int p : inputPins) pinMode(p, INPUT_PULLUP);
  
  pinMode(PIN_BLOWER, OUTPUT);
  pinMode(PIN_HEATER_1, OUTPUT);
  pinMode(PIN_HEATER_2, OUTPUT);
  digitalWrite(PIN_BLOWER, LOW); 
  digitalWrite(PIN_HEATER_1, LOW); 
  digitalWrite(PIN_HEATER_2, LOW);
  
  dht.begin();
  lcd.init();
  lcd.backlight();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  unsigned long startWifiMillis = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startWifiMillis < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected!");
    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
  }
}

void loop() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) temp = t;
  if (!isnan(h)) hum = h;

  handleAppCommands();

  if (currentState != lastState) {
    lcd.clear();
    refreshStaticText();
    lastState = currentState;
  }

  switch (currentState) {
    case WELCOME:
      lcd.setCursor(6, 1); lcd.print("WELCOME!");
      lcd.setCursor(2, 2); lcd.print("Smart Dehydrator");
      delay(2000); 
      currentState = LOCKED;
      break;

    case LOCKED:
      if (digitalRead(PIN_CANCEL) == LOW) { delay(200); currentState = MODE_SELECT; }
      break;

    case MODE_SELECT:
      if (digitalRead(PIN_MANGO) == LOW)  { fruitName="MANGO";  targetTemp=57; targetTimeHrs=12; targetTimeMins=0; currentState=PRESET_VIEW; delay(200); }
      if (digitalRead(PIN_APPLE) == LOW)  { fruitName="APPLE";  targetTemp=57; targetTimeHrs=10; targetTimeMins=0; currentState=PRESET_VIEW; delay(200); }
      if (digitalRead(PIN_BANANA) == LOW) { fruitName="BANANA"; targetTemp=57; targetTimeHrs=14; targetTimeMins=0; currentState=PRESET_VIEW; delay(200); }
      if (digitalRead(PIN_ORANGE) == LOW) { fruitName="ORANGE"; targetTemp=57; targetTimeHrs=15; targetTimeMins=0; currentState=PRESET_VIEW; delay(200); }
      if (digitalRead(PIN_GRAPES) == LOW) { fruitName="GRAPES"; targetTemp=57; targetTimeHrs=30; targetTimeMins=0; currentState=PRESET_VIEW; delay(200); }
      if (digitalRead(PIN_SELECT) == LOW) { fruitName="MANUAL"; targetTemp=35; targetTimeHrs=1; targetTimeMins=0; currentState=MANUAL_SET; delay(200); }
      if (digitalRead(PIN_CANCEL) == LOW) { currentState=LOCKED; delay(200); }
      break;

    case MANUAL_SET:
      lcd.setCursor(6, 1); lcd.print(targetTemp); lcd.print("C    ");
      lcd.setCursor(6, 2); lcd.print(targetTimeHrs); lcd.print("h "); lcd.print(targetTimeMins); lcd.print("m ");
      lcd.setCursor(0, 3); lcd.print(adjustMinutes ? "MOD: MINS " : "MOD: HOURS");

      if (digitalRead(PIN_MANGO) == LOW) { targetTemp -= 5; if(targetTemp < 35) targetTemp = 35; delay(200); }
      if (digitalRead(PIN_APPLE) == LOW) { targetTemp += 5; if(targetTemp > 75) targetTemp = 75; delay(200); }
      if (digitalRead(PIN_GRAPES) == LOW) { adjustMinutes = !adjustMinutes; delay(250); }
      
      if (adjustMinutes) {
        if (digitalRead(PIN_BANANA) == LOW) { targetTimeMins -= 5; if(targetTimeMins < 0) targetTimeMins = 55; delay(200); }
        if (digitalRead(PIN_ORANGE) == LOW) { targetTimeMins += 5; if(targetTimeMins >= 60) targetTimeMins = 0; delay(200); }
      } else {
        if (digitalRead(PIN_BANANA) == LOW) { if(targetTimeHrs > 0) targetTimeHrs -= 1; delay(200); }
        if (digitalRead(PIN_ORANGE) == LOW) { if(targetTimeHrs < 99) targetTimeHrs += 1; delay(200); }
      }
      if (digitalRead(PIN_SELECT) == LOW) { startDrying(); }
      if (digitalRead(PIN_CANCEL) == LOW) { currentState=MODE_SELECT; delay(200); }
      break;

    case PRESET_VIEW:
      if (digitalRead(PIN_SELECT) == LOW) { startDrying(); }
      if (digitalRead(PIN_CANCEL) == LOW) { currentState=MODE_SELECT; delay(200); }
      break;

    case DRYING:
      handleHardware();
      if (millis() - lastLCDUpdate > 1000) {
        updateLCDOperation();
        lastLCDUpdate = millis();
      }
      if (digitalRead(PIN_CANCEL) == LOW) { currentState = STOP_CONFIRM; delay(200); }
      if ((millis() - startTime) / 1000 >= totalSeconds) { stopDrying("FINISHED"); }
      break;

    case STOP_CONFIRM:
      if (digitalRead(PIN_SELECT) == LOW) { stopDrying("IDLE"); }
      if (digitalRead(PIN_CANCEL) == LOW) { currentState=DRYING; delay(200); }
      break;
  }

  if (millis() - lastFirebaseUpdate > 3000) {
    updateFirebase();
    lastFirebaseUpdate = millis();
  }
}

void refreshStaticText() {
  switch (currentState) {
    case LOCKED:
      lcd.setCursor(3, 1); lcd.print("SYSTEM LOCKED");
      lcd.setCursor(2, 2); lcd.print("Tap RED to Unlock");
      break;
    case MODE_SELECT:
      lcd.setCursor(4, 0); lcd.print("SELECT MODE");
      lcd.setCursor(0, 1); lcd.print("BLUE:Man FRUIT:Pre");
      lcd.setCursor(0, 3); lcd.print("RED: Lock System");
      break;
    case MANUAL_SET:
      lcd.setCursor(0, 0); lcd.print("MODE: MANUAL SET");
      lcd.setCursor(0, 1); lcd.print("TEMP: ");
      lcd.setCursor(0, 2); lcd.print("TIME: ");
      break;
    case PRESET_VIEW:
      lcd.setCursor(0, 0); lcd.print("MODE: " + fruitName);
      lcd.setCursor(0, 1); lcd.print("TEMP: "); lcd.print(targetTemp); lcd.print(" C");
      lcd.setCursor(0, 2); lcd.print("TIME: "); lcd.print(targetTimeHrs); lcd.print("h "); lcd.print(targetTimeMins); lcd.print("m");
      lcd.setCursor(0, 3); lcd.print("BLUE:Start RED:Back");
      break;
    case STOP_CONFIRM:
      lcd.setCursor(2, 0); lcd.print("STOP PROCESS?");
      lcd.setCursor(0, 1); lcd.print("BLUE: YES (Stop)");
      lcd.setCursor(0, 2); lcd.print("RED: NO (Resume)");
      break;
  }
}

void startDrying() {
  isRunning = true;
  startTime = millis();
  lastBlowerToggle = millis(); 
  intervalBlowerActive = true; 
  totalSeconds = ((unsigned long)targetTimeHrs * 3600) + ((unsigned long)targetTimeMins * 60);
  currentState = DRYING;
  delay(200);
}

void stopDrying(String reason) {
  isRunning = false;
  digitalWrite(PIN_BLOWER, LOW); 
  digitalWrite(PIN_HEATER_1, LOW); 
  digitalWrite(PIN_HEATER_2, LOW);
  if(reason == "FINISHED") {
    lcd.clear();
    lcd.setCursor(2, 1); lcd.print("DRYING COMPLETE!");
    delay(3000);
  }
  fruitName = "IDLE";
  currentState = LOCKED;
}

void updateLCDOperation() {
  unsigned long elapsedSecs = (millis() - startTime) / 1000;
  unsigned long remainingSecs = (totalSeconds > elapsedSecs) ? totalSeconds - elapsedSecs : 0;
  int h = remainingSecs / 3600;
  int m = (remainingSecs % 3600) / 60;
  int s = remainingSecs % 60;

  lcd.setCursor(0, 0); lcd.print("MODE: "); lcd.print(fruitName); lcd.print("      ");
  lcd.setCursor(0, 1); lcd.print("TEMP: "); lcd.print(temp, 1); 
  lcd.print("/"); lcd.print((float)targetTemp, 1); lcd.print(" C  ");
  lcd.setCursor(0, 2); lcd.print("HUMID: "); lcd.print(hum, 1); lcd.print("%      ");
  char timerBuf[20];
  sprintf(timerBuf, "REM: %02d:%02d:%02d", h, m, s);
  lcd.setCursor(0, 3); lcd.print(timerBuf);
}

void handleHardware() {
  if (!isRunning) return;

  if (temp < (float)targetTemp) {
    digitalWrite(PIN_HEATER_1, HIGH);
    digitalWrite(PIN_HEATER_2, HIGH);
  } else {
    digitalWrite(PIN_HEATER_1, LOW);
    digitalWrite(PIN_HEATER_2, LOW);
  }

  unsigned long currentMillis = millis();
  if (!intervalBlowerActive) {
    if (currentMillis - lastBlowerToggle >= BLOWER_OFF_DURATION) {
      intervalBlowerActive = true;
      lastBlowerToggle = currentMillis;
      digitalWrite(PIN_BLOWER, HIGH);
    } else {
      digitalWrite(PIN_BLOWER, LOW);
    }
  } else {
    if (currentMillis - lastBlowerToggle >= BLOWER_ON_DURATION) {
      intervalBlowerActive = false;
      lastBlowerToggle = currentMillis;
      digitalWrite(PIN_BLOWER, LOW);
    } else {
      digitalWrite(PIN_BLOWER, HIGH);
    }
  }
}

// INAYOS NA COMMAND HANDLER: Ngayon ay hihintayin ang Physical "Go" Button
void handleAppCommands() {
  if (Firebase.ready()) {
    if (Firebase.RTDB.getJSON(&fbdo, "/dehydrator/commands")) {
      if (fbdo.dataType() == "json") {
        FirebaseJson &json = fbdo.jsonObject();
        FirebaseJsonData jsonData;

        json.get(jsonData, "action");
        String action = jsonData.stringValue;

        if (action == "SELECT") {
          json.get(jsonData, "preset"); fruitName = jsonData.stringValue;
          json.get(jsonData, "targetTemp"); targetTemp = jsonData.intValue;
          json.get(jsonData, "targetTimeHrs"); targetTimeHrs = jsonData.intValue;
          json.get(jsonData, "targetTimeMins"); targetTimeMins = jsonData.intValue;

          Firebase.RTDB.deleteNode(&fbdo, "/dehydrator/commands");
          
          // Imbes na mag-start agad, lilipat lang sa PRESET_VIEW state
          currentState = PRESET_VIEW;
          Serial.println("App Select: " + fruitName + ". Waiting for physical button press.");
        } 
        else if (action == "CANCEL") {
          Firebase.RTDB.deleteNode(&fbdo, "/dehydrator/commands");
          stopDrying("IDLE");
        }
        else if (action == "RESET") {
          Firebase.RTDB.deleteNode(&fbdo, "/dehydrator/commands");
          ESP.restart();
        }
      }
    }
  }
}

void updateFirebase() {
  if (Firebase.ready()) {
    Firebase.RTDB.setFloat(&fbdo, "/status/temperature", temp);
    Firebase.RTDB.setFloat(&fbdo, "/status/humidity", hum);
    Firebase.RTDB.setString(&fbdo, "/status/mode", isRunning ? fruitName : "IDLE");
    
    unsigned long elapsed = isRunning ? (millis() - startTime) / 1000 : 0;
    unsigned long rem = (totalSeconds > elapsed) ? totalSeconds - elapsed : 0;
    
    char timerStr[10]; 
    sprintf(timerStr, "%02d:%02d:%02d", (int)(rem/3600), (int)((rem%3600)/60), (int)(rem%60));
    Firebase.RTDB.setString(&fbdo, "/status/remaining_time", timerStr);

    Firebase.RTDB.setBool(&fbdo, "/status/heater1", digitalRead(PIN_HEATER_1));
    Firebase.RTDB.setBool(&fbdo, "/status/heater2", digitalRead(PIN_HEATER_2));
    Firebase.RTDB.setBool(&fbdo, "/status/blower", digitalRead(PIN_BLOWER));

    Firebase.RTDB.setInt(&fbdo, "/dehydrator/status/progress", isRunning ? (int)((float)elapsed / totalSeconds * 100) : 0);
  }
}
