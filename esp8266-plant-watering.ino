#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <time.h>

// ======================= ПИНЫ ==========================
#define BTN_LEFT     D4
#define BTN_RIGHT    D5
#define RELAY_PIN    D7
#define SOIL_PIN     A0

// ======================= LCD ===========================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ======================= ВРЕМЯ =========================
time_t currentEpoch = 0;
unsigned long epochBaseMillis = 0;
bool timeSynced = false;

void setupTimeZone() {
  setenv("TZ", "MSK-3", 1);
  tzset();
}

void setDefaultTime() {
  currentEpoch = 1742936400;
  epochBaseMillis = millis();
  timeSynced = true;
}

void setTimeFromUnix(time_t t) {
  currentEpoch = t;
  epochBaseMillis = millis();
  timeSynced = true;
}

time_t getCurrentUnixTime() {
  if (!timeSynced) return 0;
  return currentEpoch + (millis() - epochBaseMillis) / 1000;
}

int getHour() {
  time_t t = getCurrentUnixTime();
  if (t == 0) return 0;
  struct tm *tm = localtime(&t);
  return tm->tm_hour;
}
int getMinute() {
  time_t t = getCurrentUnixTime();
  if (t == 0) return 0;
  struct tm *tm = localtime(&t);
  return tm->tm_min;
}
int getDay() {
  time_t t = getCurrentUnixTime();
  if (t == 0) return 0;
  struct tm *tm = localtime(&t);
  return tm->tm_mday;
}
int getMonth() {
  time_t t = getCurrentUnixTime();
  if (t == 0) return 0;
  struct tm *tm = localtime(&t);
  return tm->tm_mon + 1;
}
const char* getWeekDay() {
  static const char* days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  time_t t = getCurrentUnixTime();
  if (t == 0) return "---";
  struct tm *tm = localtime(&t);
  return days[tm->tm_wday];
}

// ======================= МЕТЕОДАННЫЕ ====================
const int WEATHER_TEMP = 5;
const int WEATHER_FEELS = 2;
const int WEATHER_HUMIDITY = 78;
const int WEATHER_WIND = 3;

// ======================= ГЛОБАЛЬНЫЕ ====================
bool displayActive = false;
unsigned long lastActivity = 0;
bool autoMode = true;
int noWaterCounter = 0;
bool noWaterFlag = false;
int currentSlide = 0;  // 0 - главный, 1 - погода

int lastHour = -1, lastMinute = -1, lastDay = -1, lastMonth = -1;
int lastDisplayedHumidity = -1;
bool lastAutoMode = true;

int autoHours[3] = {12, 17, 20};
int autoMinutes[3] = {0, 0, 0};
const int AUTO_TIMES_COUNT = 3;
int lastAutoWaterDay = -1;

// ======================= ВЕБ-СЕРВЕР ====================
const char* apSSID = "ESP_Watering";
const char* apPass = "12345678";
ESP8266WebServer server(80);

float getSoilPercent();
void requestWatering(bool isAuto = false);
void updateDisplay();
void resetNoWater();

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1, user-scalable=yes'>
    <title>Умный полив</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: linear-gradient(135deg, #1a2a3a, #0f1a24);
            min-height: 100vh;
            padding: 20px;
            color: #e0e0e0;
        }
        .container { max-width: 480px; margin: 0 auto; }
        .card {
            background: rgba(30, 40, 50, 0.7);
            backdrop-filter: blur(12px);
            border-radius: 32px;
            padding: 24px;
            margin-bottom: 16px;
            border: 1px solid rgba(100, 180, 100, 0.2);
        }
        h1 { text-align: center; font-size: 24px; margin-bottom: 4px; color: #8bc34a; }
        .subtitle { text-align: center; font-size: 12px; color: #8a9aa8; margin-bottom: 20px; }
        .sensor-value { font-size: 64px; font-weight: bold; text-align: center; margin: 16px 0; color: #8bc34a; }
        .time-display { text-align: center; font-size: 22px; margin: 8px 0; color: #b8c7d0; font-family: monospace; }
        .mode-row {
            display: flex; justify-content: space-between; align-items: center;
            background: rgba(0,0,0,0.3); border-radius: 40px; padding: 8px 16px; margin: 16px 0;
        }
        .mode-label { font-size: 14px; color: #9aaeb9; }
        .mode-switch { display: flex; gap: 8px; background: rgba(0,0,0,0.4); border-radius: 30px; padding: 4px; }
        .mode-option {
            padding: 6px 20px; border-radius: 25px; font-size: 14px; font-weight: 500;
            cursor: pointer; background: transparent; color: #9aaeb9;
        }
        .mode-option.active { background: #8bc34a; color: #1a2a3a; }
        button {
            background: #4c7a34; border: none; color: white; padding: 12px 20px;
            border-radius: 40px; font-size: 15px; font-weight: 500; cursor: pointer;
            margin: 4px 0; width: 100%;
        }
        button:hover { background: #5c8c44; transform: scale(0.98); }
        button.secondary { background: #3a5a6e; }
        .time-input-group { display: flex; gap: 12px; align-items: center; margin: 16px 0; }
        .time-input-group input {
            flex: 2; background: rgba(0,0,0,0.5); border: 1px solid #5a7a4a;
            color: white; padding: 12px; border-radius: 40px; font-size: 16px; text-align: center;
        }
        .time-input-group button { flex: 1; margin: 0; }
        .auto-settings { transition: all 0.3s; }
        .hidden { display: none; }
        .time-selector { display: flex; gap: 8px; margin: 12px 0; flex-wrap: wrap; }
        .time-input {
            flex: 1; background: rgba(0,0,0,0.3); border-radius: 24px; padding: 8px; text-align: center;
        }
        .time-input label { display: block; font-size: 11px; color: #8a9aa8; margin-bottom: 4px; }
        .time-input input {
            background: #1e2a32; border: 1px solid #5a7a4a; color: white;
            padding: 6px; border-radius: 20px; width: 100%; text-align: center; font-size: 14px;
        }
        .error-block { background: rgba(184,76,76,0.15); border: 1px solid #b84c4c; }
        .small-text { font-size: 11px; color: #6a7a84; text-align: center; margin-top: 16px; }
        .status-badge { font-size: 12px; color: #8bc34a; text-align: center; margin-top: 8px; }
    </style>
</head>
<body>
<div class="container">
    <h1>🌱 УМНЫЙ ПОЛИВ</h1>
    <div class="subtitle">автоматическая система орошения</div>
    
    <div class="card">
        <div class="time-display" id="datetime">--:-- --/-- ---</div>
        <div class="sensor-value" id="humidity">--%</div>
        <div class="status-badge" id="status-badge"></div>
        
        <div class="mode-row">
            <span class="mode-label">Режим работы</span>
            <div class="mode-switch">
                <div class="mode-option" id="mode-auto" onclick="setMode(true)">АВТО</div>
                <div class="mode-option" id="mode-manual" onclick="setMode(false)">РУЧН</div>
            </div>
        </div>
        
        <button onclick="manualWater()" id="manual-btn">💧 ПОЛИВ СЕЙЧАС</button>
        
        <div class="time-input-group">
            <input type="datetime-local" id="customDateTime">
            <button onclick="setCustomTime()" class="secondary">📅 УСТАНОВИТЬ ДАТУ И ВРЕМЯ</button>
        </div>
        
        <button onclick="syncTime()" class="secondary">📱 СИНХРОНИЗИРОВАТЬ С ТЕЛЕФОНОМ</button>
    </div>
    
    <div class="card auto-settings" id="auto-settings">
        <div style="font-size: 14px; margin-bottom: 12px; color: #8bc34a;">⏰ РАСПИСАНИЕ АВТОПОЛИВА</div>
        <div class="time-selector">
            <div class="time-input"><label>1 полив</label><input type="time" id="time0" step="60"></div>
            <div class="time-input"><label>2 полив</label><input type="time" id="time1" step="60"></div>
            <div class="time-input"><label>3 полив</label><input type="time" id="time2" step="60"></div>
        </div>
        <button onclick="saveTimes()" class="secondary">💾 СОХРАНИТЬ</button>
    </div>
    
    <div id="error-block" class="card hidden error-block">
        <h3 style="font-size: 15px; margin-bottom: 8px;">⚠️ НЕТ ВОДЫ!</h3>
        <div id="error-msg" style="font-size: 13px; margin-bottom: 12px;"></div>
        <button onclick="resetError()" class="warning">🔄 СБРОСИТЬ БЛОКИРОВКУ</button>
    </div>
    
    <div class="small-text">
        🌧️ Полив при влажности ниже 75%<br>
        💧 При 3 неудачных поливах блокировка
    </div>
</div>

<script>
    function setMode(isAuto) {
        fetch('/setMode?auto=' + (isAuto ? '1' : '0'))
            .then(() => fetchData());
    }
    
    function fetchData() {
        fetch('/api/data')
            .then(response => response.json())
            .then(data => {
                document.getElementById('humidity').innerHTML = data.humidity + '%';
                document.getElementById('datetime').innerHTML = data.datetime;
                
                const autoBtn = document.getElementById('mode-auto');
                const manualBtn = document.getElementById('mode-manual');
                if (data.autoMode) {
                    autoBtn.classList.add('active');
                    manualBtn.classList.remove('active');
                    document.getElementById('auto-settings').classList.remove('hidden');
                } else {
                    manualBtn.classList.add('active');
                    autoBtn.classList.remove('active');
                    document.getElementById('auto-settings').classList.add('hidden');
                }
                
                if (data.noWater) {
                    document.getElementById('status-badge').innerHTML = '⚠️ НЕТ ВОДЫ • СИСТЕМА ЗАБЛОКИРОВАНА';
                    document.getElementById('status-badge').style.color = '#b84c4c';
                    document.getElementById('error-block').classList.remove('hidden');
                    document.getElementById('error-msg').innerHTML = 'Вода не поступает. Заполните бак и нажмите "Сбросить блокировку".';
                    document.getElementById('manual-btn').disabled = true;
                    document.getElementById('manual-btn').style.opacity = '0.5';
                } else {
                    document.getElementById('status-badge').innerHTML = '';
                    document.getElementById('error-block').classList.add('hidden');
                    document.getElementById('manual-btn').disabled = false;
                    document.getElementById('manual-btn').style.opacity = '1';
                }
                
                if (data.humidity > 75 && !data.noWater) {
                    document.getElementById('manual-btn').style.opacity = '0.5';
                    document.getElementById('manual-btn').disabled = true;
                    document.getElementById('status-badge').innerHTML = '🌊 ВЛАЖНОСТЬ > 75% • ПОЛИВ НЕДОСТУПЕН';
                    document.getElementById('status-badge').style.color = '#b8c7d0';
                } else if (!data.noWater && data.humidity <= 75) {
                    document.getElementById('manual-btn').disabled = false;
                    document.getElementById('manual-btn').style.opacity = '1';
                }
            });
    }
    
    function manualWater() {
        fetch('/water')
            .then(() => fetchData());
    }
    
    function syncTime() {
        const now = Math.floor(Date.now() / 1000);
        fetch('/settime?t=' + now)
            .then(() => fetchData());
    }
    
    function setCustomTime() {
        const datetimeStr = document.getElementById('customDateTime').value;
        if (datetimeStr) {
            const date = new Date(datetimeStr);
            const timestamp = Math.floor(date.getTime() / 1000);
            fetch('/settime?t=' + timestamp)
                .then(() => fetchData());
        } else {
            alert('Выберите дату и время');
        }
    }
    
    function saveTimes() {
        const times = [];
        for (let i = 0; i < 3; i++) {
            const val = document.getElementById('time' + i).value;
            if (val) {
                const [h, m] = val.split(':');
                times.push({h: parseInt(h), m: parseInt(m)});
            }
        }
        fetch('/settimes', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify(times)
        }).then(() => fetchData());
    }
    
    function resetError() {
        fetch('/resetError')
            .then(() => fetchData());
    }
    
    function loadTimes() {
        fetch('/api/times')
            .then(response => response.json())
            .then(times => {
                for (let i = 0; i < times.length; i++) {
                    const h = times[i].h.toString().padStart(2, '0');
                    const m = times[i].m.toString().padStart(2, '0');
                    document.getElementById('time' + i).value = h + ':' + m;
                }
            });
    }
    
    setInterval(fetchData, 2000);
    fetchData();
    loadTimes();
</script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleApiData() {
  String json = "{";
  json += "\"humidity\":" + String((int)getSoilPercent()) + ",";
  json += "\"autoMode\":" + String(autoMode ? "true" : "false") + ",";
  json += "\"noWater\":" + String(noWaterFlag ? "true" : "false") + ",";
  char buf[30];
  time_t t = getCurrentUnixTime();
  if (t > 0) {
    struct tm *tm = localtime(&t);
    sprintf(buf, "%02d:%02d %02d/%02d %s", tm->tm_hour, tm->tm_min, tm->tm_mday, tm->tm_mon+1, getWeekDay());
  } else {
    sprintf(buf, "--:-- --/-- ---");
  }
  json += "\"datetime\":\"" + String(buf) + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleApiTimes() {
  String json = "[";
  for (int i = 0; i < AUTO_TIMES_COUNT; i++) {
    if (i > 0) json += ",";
    json += "{\"h\":" + String(autoHours[i]) + ",\"m\":" + String(autoMinutes[i]) + "}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleSetTimes() {
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    int idx = 0;
    for (int i = 0; i < AUTO_TIMES_COUNT && idx < body.length(); i++) {
      int posH = body.indexOf("\"h\":", idx);
      if (posH == -1) break;
      int posM = body.indexOf("\"m\":", posH);
      if (posM == -1) break;
      int h = body.substring(posH+4, body.indexOf(",", posH)).toInt();
      int m = body.substring(posM+4, body.indexOf("}", posM)).toInt();
      autoHours[i] = h;
      autoMinutes[i] = m;
      idx = posM + 5;
    }
  }
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleWater() {
  float humidity = getSoilPercent();
  if (humidity > 75) {
    server.send(200, "application/json", "{\"status\":\"blocked\"}");
    return;
  }
  requestWatering(false);
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleSetMode() {
  if (server.hasArg("auto")) {
    autoMode = (server.arg("auto") == "1");
  }
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleSetTime() {
  if (server.hasArg("t")) {
    time_t t = atol(server.arg("t").c_str());
    if (t > 100000) {
      setTimeFromUnix(t);
    }
  }
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleResetError() {
  noWaterCounter = 0;
  noWaterFlag = false;
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void setupWebServer() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID, apPass);
  server.on("/", handleRoot);
  server.on("/api/data", handleApiData);
  server.on("/api/times", handleApiTimes);
  server.on("/settimes", HTTP_POST, handleSetTimes);
  server.on("/water", HTTP_GET, handleWater);
  server.on("/setMode", HTTP_GET, handleSetMode);
  server.on("/settime", HTTP_GET, handleSetTime);
  server.on("/resetError", HTTP_GET, handleResetError);
  server.begin();
}

// ======================= ДАТЧИК ВЛАЖНОСТИ ==============
float getSoilPercent() {
  int raw = analogRead(SOIL_PIN);
  if (raw > 1024) raw = 1024;
  if (raw < 340) raw = 340;
  return (1024 - raw) * 100.0 / (1024 - 340);
}

// ======================= УПРАВЛЕНИЕ РЕЛЕ ===============
void relayOn() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
}

void relayOff() {
  pinMode(RELAY_PIN, INPUT);
}

void doWatering() {
  relayOn();
  delay(5000);
  relayOff();
  delay(400);
}

// ======================= ПОЛИВ =========================
bool watering = false;
unsigned long wateringTimer = 0;
int wateringState = 0;
const unsigned long WARNING_TIME = 2000;
const unsigned long PUMP_TIME = 5000;

void requestWatering(bool isAuto) {
  if (watering) return;
  if (noWaterFlag) return;
  float humidity = getSoilPercent();
  if (humidity > 75) return;
  
  watering = true;
  wateringState = 0;
  wateringTimer = millis();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WATERING...");
  lcd.setCursor(0, 1);
  lcd.print("Please wait");
}

void wateringMachine() {
  if (!watering) return;
  unsigned long now = millis();
  
  if (wateringState == 0) {
    if (now - wateringTimer > WARNING_TIME) {
      lcd.noBacklight();
      lcd.clear();
      delay(100);
      relayOn();
      wateringState = 1;
      wateringTimer = now;
    }
  } else if (wateringState == 1) {
    if (now - wateringTimer > PUMP_TIME) {
      relayOff();
      delay(200);
      lcd.backlight();
      delay(100);
      watering = false;
      
      float humidityAfter = getSoilPercent();
      if (humidityAfter < 25) {
        noWaterCounter++;
        if (noWaterCounter >= 3) {
          noWaterFlag = true;
        }
      } else {
        noWaterCounter = 0;
      }
      
      lastDisplayedHumidity = -1;
      lastAutoMode = !autoMode;
      lastHour = -1; lastMinute = -1; lastDay = -1; lastMonth = -1;
      updateDisplay();
      lastActivity = millis();
    }
  }
}

// ======================= КНОПКИ ========================
int getButtonEvent() {
  static bool lastLeft = HIGH, lastRight = HIGH;
  static unsigned long leftPressTime = 0, rightPressTime = 0;
  const unsigned long LONG_PRESS = 800;
  bool left = (digitalRead(BTN_LEFT) == LOW);
  bool right = (digitalRead(BTN_RIGHT) == LOW);
  int event = 0;

  if (left && !lastLeft) leftPressTime = millis();
  if (!left && lastLeft && (millis() - leftPressTime) < LONG_PRESS) event = 1;
  if (right && !lastRight) rightPressTime = millis();
  if (!right && lastRight && (millis() - rightPressTime) < LONG_PRESS) event = 2;

  if (left && !lastLeft) leftPressTime = millis();
  if (!left && lastLeft && (millis() - leftPressTime) >= LONG_PRESS) event = 4;
  if (right && !lastRight) rightPressTime = millis();
  if (!right && lastRight && (millis() - rightPressTime) >= LONG_PRESS) event = 5;

  lastLeft = left;
  lastRight = right;
  return event;
}

// ======================= АВТОПОЛИВ =====================
void checkAutoWatering() {
  if (!autoMode) return;
  if (watering) return;
  if (noWaterFlag) return;
  
  int h = getHour();
  int m = getMinute();
  int d = getDay();
  int mon = getMonth();
  int dayId = d + mon * 100;
  
  if (dayId == lastAutoWaterDay) return;
  
  for (int i = 0; i < AUTO_TIMES_COUNT; i++) {
    if (h == autoHours[i] && m == autoMinutes[i]) {
      float humidity = getSoilPercent();
      if (humidity < 80 && humidity <= 75) {
        requestWatering(true);
        lastAutoWaterDay = dayId;
        break;
      }
    }
  }
}

// ======================= ЭКРАН =========================
const unsigned long SLEEP_TIMEOUT = 3 * 60 * 1000;
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_UPDATE_INTERVAL = 2000;

void showMainSlide() {
  int humidity = (int)getSoilPercent();
  int h = getHour();
  int m = getMinute();
  int d = getDay();
  int mon = getMonth();
  const char* wd = getWeekDay();
  
  char buf[17];
  sprintf(buf, "%02d:%02d %02d/%02d %s", h, m, d, mon, wd);
  lcd.setCursor(0, 0);
  lcd.print(buf);
  
  lcd.setCursor(0, 1);
  lcd.print("Soil: ");
  lcd.print(humidity);
  lcd.print("%");
  lcd.print("   ");
  
  lcd.setCursor(13, 1);
  if (autoMode) {
    lcd.print(" ON");
  } else {
    lcd.print("OFF");
  }
  
  lastHour = h; lastMinute = m; lastDay = d; lastMonth = mon;
  lastDisplayedHumidity = humidity;
  lastAutoMode = autoMode;
}

void showWeatherSlide() {
  lcd.setCursor(0, 0);
  lcd.print("Temp:");
  lcd.print(WEATHER_TEMP);
  lcd.print("C F:");
  lcd.print(WEATHER_FEELS);
  lcd.print("C");
  
  lcd.setCursor(0, 1);
  lcd.print("Hum:");
  lcd.print(WEATHER_HUMIDITY);
  lcd.print("% W:");
  lcd.print(WEATHER_WIND);
  lcd.print("m/s");
}

void updateDisplay() {
  if (!displayActive) return;
  
  lcd.clear();  // Полная очистка перед выводом
  
  if (currentSlide == 0) {
    showMainSlide();
  } else {
    showWeatherSlide();
  }
}

void wakeDisplay() {
  if (!displayActive) {
    lcd.backlight();
    displayActive = true;
    updateDisplay();
  }
  lastActivity = millis();
}

void sleepDisplay() {
  if (displayActive) {
    lcd.noBacklight();
    displayActive = false;
  }
}

// ======================= SETUP =========================
void setup() {
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(RELAY_PIN, INPUT);
  
  lcd.init();
  lcd.noBacklight();  // Дисплей выключен при старте
  lcd.clear();
  
  setupTimeZone();
  setDefaultTime();
  setupWebServer();
  lastActivity = millis();
}

// ======================= LOOP ==========================
void loop() {
  server.handleClient();
  
  int btn = getButtonEvent();
  
  // Пробуждение только левой кнопкой
  if (!displayActive && btn == 1) {
    wakeDisplay();
    currentSlide = 0;
    updateDisplay();
  }
  
  if (displayActive) {
    // Короткое нажатие правой кнопки — переключение слайда
    if (btn == 2) {
      currentSlide = (currentSlide == 0) ? 1 : 0;
      updateDisplay();
      lastActivity = millis();
    }
    
    // Длинное нажатие левой — полив
    if (btn == 4) {
      if (!noWaterFlag && getSoilPercent() <= 75) {
        requestWatering(false);
      }
      lastActivity = millis();
    }
    
    // Длинное нажатие правой — переключение режима AUTO/MANUAL
    if (btn == 5) {
      autoMode = !autoMode;
      updateDisplay();
      lastActivity = millis();
    }
    
    // Сброс таймера активности при любом нажатии
    if (btn != 0) {
      lastActivity = millis();
    }
  }
  
  // Таймаут сна
  if (displayActive && !watering && (millis() - lastActivity > SLEEP_TIMEOUT)) {
    sleepDisplay();
  }
  
  wateringMachine();
  checkAutoWatering();
  
  // Обновление главного слайда (только если активен и не полив)
  if (displayActive && !watering && currentSlide == 0 && (millis() - lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL)) {
    lastDisplayUpdate = millis();
    // Обновляем только данные на главном слайде без полной перерисовки
    int humidity = (int)getSoilPercent();
    int h = getHour();
    int m = getMinute();
    int d = getDay();
    int mon = getMonth();
    
    if (h != lastHour || m != lastMinute || d != lastDay || mon != lastMonth) {
      char buf[17];
      sprintf(buf, "%02d:%02d %02d/%02d %s", h, m, d, mon, getWeekDay());
      lcd.setCursor(0, 0);
      lcd.print(buf);
      lastHour = h; lastMinute = m; lastDay = d; lastMonth = mon;
    }
    
    if (humidity != lastDisplayedHumidity) {
      lcd.setCursor(0, 1);
      lcd.print("Soil: ");
      lcd.print(humidity);
      lcd.print("%");
      lcd.print("   ");
      lastDisplayedHumidity = humidity;
    }
    
    if (autoMode != lastAutoMode) {
      lcd.setCursor(13, 1);
      if (autoMode) {
        lcd.print(" ON");
      } else {111
        lcd.print("OFF");
      }
      lastAutoMode = autoMode;
    }
  }
}