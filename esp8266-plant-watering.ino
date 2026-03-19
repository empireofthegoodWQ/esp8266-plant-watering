#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Пины
#define BTN_LEFT     D4
#define BTN_RIGHT    D5
#define RELAY_PIN    D7
#define SOIL_PIN     A0

// LCD
#define LCD_ADDR 0x27
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

// Параметры
const int DRY_VAL = 30;        // порог сухости (%)
const int PUMP_TIME = 3000;    // работа помпы (мс)
const int PAUSE = 10000;       // пауза между поливами (мс)
const int SHOW_TIME = 2000;    // показ надписи перед поливом (мс)

// Переменные
bool autoMode = true;
bool pumpActive = false;
unsigned long pumpTimer = 0;
unsigned long lastPump = 0;
int moisture = 0;

// Для кнопок
bool lastRight = HIGH;
bool lastLeft = HIGH;

// ===== Чтение кнопок =====
bool btnPressed(int pin, bool &lastState) {
  bool reading = digitalRead(pin);
  if (reading == LOW && lastState == HIGH) {
    lastState = reading;
    return true;
  }
  lastState = reading;
  return false;
}

// ===== Датчик влажности =====
void readSoil() {
  int raw = analogRead(SOIL_PIN);
  moisture = map(raw, 1024, 340, 0, 100);
  if (moisture < 0) moisture = 0;
  if (moisture > 100) moisture = 100;
  
  lcd.setCursor(0, 0);
  lcd.print("Soil: ");
  if (moisture < 10) lcd.print(" ");
  lcd.print(moisture);
  lcd.print("%  ");
}

// ===== Обновление статуса =====
void setStatus(const char* text) {
  lcd.setCursor(0, 1);
  lcd.print(text);
  for (int i = strlen(text); i < 16; i++) lcd.print(" ");
}

// ===== Управление реле =====
void relay(bool on) {
  if (on) {
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
  } else {
    pinMode(RELAY_PIN, INPUT);
  }
}

// ===== Запуск полива =====
void startPump(const char* msg) {
  // Показываем сообщение
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(msg);
  lcd.setCursor(0, 1);
  lcd.print("Wait...");
  delay(SHOW_TIME);
  
  // Гасим дисплей
  lcd.clear();
  lcd.noBacklight();
  
  // Включаем помпу
  relay(true);
  pumpActive = true;
  pumpTimer = millis();
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(RELAY_PIN, INPUT);
  
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("System OK");
  delay(1000);
  lcd.clear();
  readSoil();
  setStatus("Auto: ON");
}

// ===== LOOP =====
void loop() {
  // Чтение датчика раз в 1.5 секунды
  static unsigned long lastRead = 0;
  if (millis() - lastRead > 1500) {
    readSoil();
    lastRead = millis();
  }
  
  // Правая кнопка - авторежим
  if (btnPressed(BTN_RIGHT, lastRight)) {
    autoMode = !autoMode;
    setStatus(autoMode ? "Auto: ON" : "Auto: OFF");
  }
  
  // Левая кнопка - ручной полив
  if (btnPressed(BTN_LEFT, lastLeft)) {
    if (!pumpActive && millis() - lastPump > PAUSE) {
      startPump("NOW WATERING");
    }
  }
  
  // Автополив
  if (autoMode && !pumpActive && millis() - lastPump > PAUSE) {
    if (moisture < DRY_VAL) {
      startPump("AUTO WATERING");
    }
  }
  
  // Контроль помпы
  if (pumpActive && millis() - pumpTimer >= PUMP_TIME) {
    relay(false);
    pumpActive = false;
    lastPump = millis();
    
    // Включаем дисплей
    lcd.init();
    lcd.backlight();
    readSoil();
    setStatus(autoMode ? "Auto: ON" : "Auto: OFF");
  }
}