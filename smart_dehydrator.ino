#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h"

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

enum State { WELCOME, LOCKED, MODE_SELECT, PRESET_VIEW, MANUAL_SET, DRYING, STOP_CONFIRM, FINISHED_CONFIRM };
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
unsigned long lastLCDUpdate = 0;
State lastState = WELCOME;

// --- BLOWER LOGIC ---
unsigned long lastBlowerToggle = 0;
bool intervalBlowerActive = false;
const unsigned long BLOWER_OFF_DURATION = 2700000; // 45 mins
const unsigned long BLOWER_ON_DURATION = 10000;    // 10 secs

// --- SAFETY & DEBOUNCE ---
const unsigned long DEBOUNCE_DELAY = 50; 
const float MAX_SAFETY_TEMP = 75.0; // Hard limit

void startDrying();
void stopDrying(String reason);
void updateLCDOperation();
void handleHardware();
void refreshStaticText();

bool isButtonPressed(int pin) {
  if (digitalRead(pin) == LOW) {
    delay(DEBOUNCE_DELAY); 
    if (digitalRead(pin) == LOW) return true;
  }
  return false;
}

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
}

void loop() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) temp = t;
  if (!isnan(h)) hum = h;

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
      if (isButtonPressed(PIN_CANCEL)) { currentState = MODE_SELECT; }
      break;

    case MODE_SELECT:
      if (isButtonPressed(PIN_MANGO))  { fruitName="MANGO";  targetTemp=57; targetTimeHrs=12; targetTimeMins=0; currentState=PRESET_VIEW; }
      if (isButtonPressed(PIN_APPLE))  { fruitName="APPLE";  targetTemp=57; targetTimeHrs=10; targetTimeMins=0; currentState=PRESET_VIEW; }
      if (isButtonPressed(PIN_BANANA)) { fruitName="BANANA"; targetTemp=57; targetTimeHrs=14; targetTimeMins=0; currentState=PRESET_VIEW; }
      if (isButtonPressed(PIN_ORANGE)) { fruitName="ORANGE"; targetTemp=57; targetTimeHrs=15; targetTimeMins=0; currentState=PRESET_VIEW; }
      if (isButtonPressed(PIN_GRAPES)) { fruitName="GRAPES"; targetTemp=57; targetTimeHrs=30; targetTimeMins=0; currentState=PRESET_VIEW; }
      if (isButtonPressed(PIN_SELECT)) { fruitName="MANUAL"; targetTemp=35; targetTimeHrs=1; targetTimeMins=0; currentState=MANUAL_SET; }
      if (isButtonPressed(PIN_CANCEL)) { currentState=LOCKED; }
      break;

    case MANUAL_SET:
      lcd.setCursor(6, 1); lcd.print(targetTemp); lcd.print("C    ");
      lcd.setCursor(6, 2); lcd.print(targetTimeHrs); lcd.print("h "); lcd.print(targetTimeMins); lcd.print("m ");
      lcd.setCursor(0, 3); lcd.print(adjustMinutes ? "MOD: MINS " : "MOD: HOURS");

      // --- TEMPERATURE LOOP LOGIC (35 to 75) ---
      if (isButtonPressed(PIN_MANGO)) { 
        targetTemp -= 5; 
        if(targetTemp < 35) targetTemp = 75; // Loop back to 75
        delay(150); 
      }
      if (isButtonPressed(PIN_APPLE)) { 
        targetTemp += 5; 
        if(targetTemp > 75) targetTemp = 35; // Loop back to 35
        delay(150); 
      }
      
      if (isButtonPressed(PIN_GRAPES)) { adjustMinutes = !adjustMinutes; delay(250); }
      
      if (adjustMinutes) {
        if (isButtonPressed(PIN_BANANA)) { targetTimeMins -= 5; if(targetTimeMins < 0) targetTimeMins = 55; delay(150); }
        if (isButtonPressed(PIN_ORANGE)) { targetTimeMins += 5; if(targetTimeMins >= 60) targetTimeMins = 0; delay(150); }
      } else {
        if (isButtonPressed(PIN_BANANA)) { if(targetTimeHrs > 0) targetTimeHrs -= 1; delay(150); }
        if (isButtonPressed(PIN_ORANGE)) { if(targetTimeHrs < 99) targetTimeHrs += 1; delay(150); }
      }
      if (isButtonPressed(PIN_SELECT)) { startDrying(); }
      if (isButtonPressed(PIN_CANCEL)) { currentState=MODE_SELECT; }
      break;

    case PRESET_VIEW:
      if (isButtonPressed(PIN_SELECT)) { startDrying(); }
      if (isButtonPressed(PIN_CANCEL)) { currentState=MODE_SELECT; }
      break;

    case DRYING:
      handleHardware();
      if (millis() - lastLCDUpdate > 1000) {
        updateLCDOperation();
        lastLCDUpdate = millis();
      }
      if (isButtonPressed(PIN_CANCEL)) { currentState = STOP_CONFIRM; }
      if ((millis() - startTime) / 1000 >= totalSeconds) { stopDrying("FINISHED"); }
      break;

    case STOP_CONFIRM:
      if (isButtonPressed(PIN_SELECT)) { stopDrying("IDLE"); }
      if (isButtonPressed(PIN_CANCEL)) { currentState=DRYING; }
      break;

    case FINISHED_CONFIRM:
      if (isButtonPressed(PIN_SELECT)) { currentState = MODE_SELECT; }
      break;
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
    case FINISHED_CONFIRM:
      lcd.setCursor(2, 0); lcd.print("DRYING COMPLETE!");
      lcd.setCursor(0, 2); lcd.print("Tap BLUE Button");
      lcd.setCursor(0, 3); lcd.print("to Confirm & Reset");
      break;
  }
}

void startDrying() {
  isRunning = true;
  startTime = millis();
  intervalBlowerActive = false; 
  lastBlowerToggle = millis(); 
  totalSeconds = ((unsigned long)targetTimeHrs * 3600) + ((unsigned long)targetTimeMins * 60);
  currentState = DRYING;
}

void stopDrying(String reason) {
  isRunning = false;
  digitalWrite(PIN_BLOWER, LOW); 
  digitalWrite(PIN_HEATER_1, LOW); 
  digitalWrite(PIN_HEATER_2, LOW);
  
  if(reason == "FINISHED") {
    currentState = FINISHED_CONFIRM;
  } else {
    fruitName = "IDLE";
    currentState = LOCKED;
  }
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

  if (temp >= MAX_SAFETY_TEMP) {
    digitalWrite(PIN_HEATER_1, LOW);
    digitalWrite(PIN_HEATER_2, LOW);
  } 
  else if (temp < (float)targetTemp) {
    digitalWrite(PIN_HEATER_1, HIGH);
    digitalWrite(PIN_HEATER_2, HIGH);
  } 
  else {
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
