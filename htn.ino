/*
 * ============================================================
 *  SMART GARDEN - ESP32 + FreeRTOS + Blynk + LCD I2C
 */

#define BLYNK_TEMPLATE_ID   "TMPL6Dpx2jTxG"
#define BLYNK_TEMPLATE_NAME "HTN"
#define BLYNK_AUTH_TOKEN    "Iy_XxenwY57iYvZIA0Z4sjZlzlQbrN2f"

/*
 * ============================================================
 *  HỆ THỐNG VƯỜN THÔNG MINH - NHÓM 11
 *  ESP32 + FreeRTOS + Blynk + LCD
 * ============================================================
 *  Thành viên:
 *    - Võ Quang Thiềm     106220112
 *    - Trần Văn Đức       106220009
 *    - Lê Văn Minh Hoàng  106220134
 *    - Trần Minh Thông    106220154
 * ============================================================
 */

// ============================================================
// thông tin Blynk & WiFi 
// ============================================================

#define WIFI_SSID           "PHUHANG"
#define WIFI_PASS           "02122016"

#include <Arduino.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// ============================================================
//  GPIO CONFIG
// ============================================================
#define PIN_DHT           4
#define PIN_SOIL_MOISTURE 34
#define PIN_LIGHT_SENSOR  35
#define PIN_PIR           27
#define PIN_PUMP          26
#define PIN_ROOF          25
#define PIN_SDA           21
#define PIN_SCL           22

#define LCD_ADDR          0x27
#define LCD_COLS          16
#define LCD_ROWS          2

#define RELAY_ON_LEVEL    HIGH
#define RELAY_OFF_LEVEL   LOW

// ============================================================
//  SENSOR CALIBRATION
// ============================================================
// Cam bien do am dat: chinh lai theo gia tri do thuc te cua ban
// ADC khi dat rat kho va rat uot
#define SOIL_ADC_DRY      4095
#define SOIL_ADC_WET      1200

// Dat kho thi bom duoc phep bat
#define SOIL_DRY_THRESHOLD       40   // duoi 40% xem nhu kho
#define SOIL_RESTART_DELAY_MS 30000UL // sau mot chu ky bom, doi 30s moi cho phep bat lai trong AUTO

// ============================================================
//  LIGHT CALIBRATION
//  LIGHT_BRIGHT_IS_HIGH = 1: sang hon -> ADC lon hon
//  LIGHT_BRIGHT_IS_HIGH = 0: sang hon -> ADC nho hon
//  Hay mo Serial Monitor de xem ADC thuc te roi sua 3 gia tri nay
// ============================================================
#define LIGHT_BRIGHT_IS_HIGH  1
#define LIGHT_CLOSE_THRESHOLD 2500  // du sang -> dong mai
#define LIGHT_OPEN_THRESHOLD  1800  // toi hon -> mo mai

// ============================================================
//  CONTROL / PROTECTION
// ============================================================
#define PUMP_ON_TIME_MS      10000UL
#define MOTION_BLOCK_MS      10000UL
#define TEMP_HIGH_THRESHOLD  35.0f
#define TEMP_ALERT_COOLDOWN_MS 300000UL
#define PIR_WARMUP_MS            45000UL
#define PIR_RETRIGGER_GUARD_MS    2000UL

// ============================================================
//  BLYNK VIRTUAL PINS
// ============================================================
#define VPIN_TEMPERATURE  V0
#define VPIN_HUMIDITY     V1
#define VPIN_SOIL         V2
#define VPIN_LIGHT        V3
#define VPIN_PUMP         V4
#define VPIN_ROOF         V5
#define VPIN_MODE         V6
#define VPIN_MOTION       V7
#define VPIN_PUMP_BTN     V8

// ============================================================
//  TASK PERIODS (ms)
// ============================================================
#define PERIOD_DHT_MS       2500
#define PERIOD_ANALOG_MS     500
#define PERIOD_PUMP_MS       200
#define PERIOD_ROOF_MS       300
#define PERIOD_LCD_MS       1500
#define PERIOD_BLYNK_SYNC_MS 1000
#define PERIOD_DEBUG_MS     3000
#define PERIOD_BLYNK_RUN_MS   20

// ============================================================
//  CORE / PRIORITY
// ============================================================
#define CORE_0  0
#define CORE_1  1
#define PRIO_LOW     1
#define PRIO_MEDIUM  2
#define PRIO_HIGH    3

// ============================================================
//  SHARED DATA
// ============================================================
typedef struct {
  float    temperature;
  float    humidity;
  int      soilMoisturePct;
  int      soilAdc;
  int      lightValue;
  bool     pumpOn;
  bool     roofClosed;
  bool     autoMode;
  bool     manualPumpCmd;
  bool     motionBlocked;
  uint32_t motionCount;
  uint32_t motionBlockRemainingMs;
} SharedData_t;

static SharedData_t      g_data;
static SemaphoreHandle_t g_mutex;

#define SHARED_LOCK()   xSemaphoreTake(g_mutex, portMAX_DELAY)
#define SHARED_UNLOCK() xSemaphoreGive(g_mutex)

// ============================================================
//  OBJECTS
// ============================================================
DHT dht(PIN_DHT, DHT11);
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

// ============================================================
//  ISR DATA
// ============================================================
volatile bool     g_motionEventISR = false;
volatile uint32_t g_motionCountISR = 0;
portMUX_TYPE      g_isrMux = portMUX_INITIALIZER_UNLOCKED;

// ============================================================
//  HELPERS
// ============================================================
static inline void setPumpRelay(bool on) {
  digitalWrite(PIN_PUMP, on ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
}

static inline void setRoofRelay(bool on) {
  digitalWrite(PIN_ROOF, on ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
}

static int soilAdcToPercent(int adc) {
  long pct = map(adc, SOIL_ADC_DRY, SOIL_ADC_WET, 0, 100);
  pct = constrain(pct, 0, 100);
  return (int)pct;
}

static bool shouldCloseRoofByLight(int lightVal, bool currentRoofState) {
#if LIGHT_BRIGHT_IS_HIGH
  if (!currentRoofState && lightVal >= LIGHT_CLOSE_THRESHOLD) return true;
  if ( currentRoofState && lightVal <= LIGHT_OPEN_THRESHOLD ) return false;
#else
  if (!currentRoofState && lightVal <= LIGHT_CLOSE_THRESHOLD) return true;
  if ( currentRoofState && lightVal >= LIGHT_OPEN_THRESHOLD ) return false;
#endif
  return currentRoofState;
}

static void lcdPrintLine(uint8_t row, const char* text) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16.16s", text);
  lcd.setCursor(0, row);
  lcd.print(buf);
}

static SharedData_t getSnapshot() {
  SharedData_t snap;
  SHARED_LOCK();
  snap = g_data;
  SHARED_UNLOCK();
  return snap;
}

static bool consumeMotionEvent(uint32_t& totalCount) {
  bool event = false;
  portENTER_CRITICAL(&g_isrMux);
  event = g_motionEventISR;
  if (event) {
    g_motionEventISR = false;
  }
  totalCount = g_motionCountISR;
  portEXIT_CRITICAL(&g_isrMux);
  return event;
}

// ============================================================
//  PIR ISR
// ============================================================
void IRAM_ATTR motionISR() {
  portENTER_CRITICAL_ISR(&g_isrMux);
  g_motionCountISR++;
  g_motionEventISR = true;
  portEXIT_CRITICAL_ISR(&g_isrMux);
}

// ============================================================
//  BLYNK CALLBACKS
// ============================================================
BLYNK_WRITE(VPIN_MODE) {
  bool mode = param.asInt();
  SHARED_LOCK();
  g_data.autoMode = mode;
  if (mode) {
    g_data.manualPumpCmd = false;
  }
  SHARED_UNLOCK();
  Serial.printf("[Blynk] Pump mode = %s\n", mode ? "AUTO" : "MANUAL");
}

BLYNK_WRITE(VPIN_PUMP_BTN) {
  bool cmd = param.asInt();
  SHARED_LOCK();
  g_data.manualPumpCmd = cmd;
  SHARED_UNLOCK();
  Serial.printf("[Blynk] Manual pump cmd = %s\n", cmd ? "ON" : "OFF");
}

// ============================================================
//  TASK: DHT
// ============================================================
void dhtTask(void* pvParameters) {
  Serial.println("[DHTTask] Started");
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (!isnan(t) && !isnan(h)) {
      SHARED_LOCK();
      g_data.temperature = t;
      g_data.humidity    = h;
      SHARED_UNLOCK();
    } else {
      Serial.println("[DHTTask] Read failed, keep old value");
    }

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(PERIOD_DHT_MS));
  }
}

// ============================================================
//  TASK: ANALOG SENSORS
// ============================================================
void analogTask(void* pvParameters) {
  Serial.println("[AnalogTask] Started");
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    const int N = 8;
    uint32_t soilSum = 0;
    uint32_t lightSum = 0;

    for (int i = 0; i < N; ++i) {
      soilSum  += analogRead(PIN_SOIL_MOISTURE);
      lightSum += analogRead(PIN_LIGHT_SENSOR);
      vTaskDelay(pdMS_TO_TICKS(5));
    }

    int soilAdc = soilSum / N;
    int light   = lightSum / N;
    int soilPct = soilAdcToPercent(soilAdc);

    SHARED_LOCK();
    g_data.soilAdc         = soilAdc;
    g_data.soilMoisturePct = soilPct;
    g_data.lightValue      = light;
    SHARED_UNLOCK();

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(PERIOD_ANALOG_MS));
  }
}

// ============================================================
//  TASK: PUMP CONTROL
//  - AUTO: dat kho -> bom 10s -> tat -> doi 30s moi cho bat lai
//  - MANUAL: nhan nut -> bom 10s -> tat
//  - PIR: dung ngay va khoa bom trong 10s
// ============================================================
void pumpControlTask(void* pvParameters) {
  Serial.println("[PumpTask] Started");
  TickType_t lastWake = xTaskGetTickCount();

  bool pumpState = false;
  bool lastManualCmd = false;
  unsigned long pumpStartedAt = 0;
  unsigned long autoNextAllowedAt = 0;
  unsigned long motionBlockUntil = 0;
  unsigned long pirReadyAt = millis() + PIR_WARMUP_MS;
  unsigned long lastAcceptedMotionAt = 0;

  for (;;) {
    uint32_t motionCount = 0;
    bool motionEventRaw = consumeMotionEvent(motionCount);
    unsigned long now = millis();

    SharedData_t snap = getSnapshot();
    bool motionBlocked = (now < motionBlockUntil);

    // Loc PIR de giam spam log va giam block vo han:
    // 1) Bo qua 45s dau sau khi khoi dong
    // 2) Chi nhan 1 lan trong moi PIR_RETRIGGER_GUARD_MS
    // 3) Neu dang block roi thi khong in log lap lai nua
    if (motionEventRaw) {
      bool pirReady = (now >= pirReadyAt);
      bool retriggerOk = (now - lastAcceptedMotionAt >= PIR_RETRIGGER_GUARD_MS);

      if (pirReady && retriggerOk && !motionBlocked) {
        lastAcceptedMotionAt = now;
        motionBlockUntil = now + MOTION_BLOCK_MS;
        motionBlocked = true;

        if (pumpState) {
          pumpState = false;
          setPumpRelay(false);
          Serial.println("[PumpTask] Motion detected -> Pump STOP");
        } else {
          Serial.println("[PumpTask] Motion detected -> Block watering");
        }
      }
    }

    if (pumpState && (now - pumpStartedAt >= PUMP_ON_TIME_MS)) {
      pumpState = false;
      setPumpRelay(false);
      Serial.println("[PumpTask] Pump OFF after 10s");

      if (snap.autoMode) {
        autoNextAllowedAt = now + SOIL_RESTART_DELAY_MS;
      }
    }

    if (!pumpState && !motionBlocked) {
      if (snap.autoMode) {
        bool dry = (snap.soilMoisturePct < SOIL_DRY_THRESHOLD);
        if (dry && now >= autoNextAllowedAt) {
          pumpState = true;
          pumpStartedAt = now;
          setPumpRelay(true);
          Serial.printf("[PumpTask] AUTO Pump ON for %lu ms\n", PUMP_ON_TIME_MS);
        }
      } else {
        bool manualRisingEdge = (snap.manualPumpCmd && !lastManualCmd);
        if (manualRisingEdge) {
          pumpState = true;
          pumpStartedAt = now;
          setPumpRelay(true);
          Serial.printf("[PumpTask] MANUAL Pump ON for %lu ms\n", PUMP_ON_TIME_MS);
        }
      }
    }

    SHARED_LOCK();
    g_data.pumpOn = pumpState;
    g_data.motionBlocked = motionBlocked;
    g_data.motionCount = motionCount;
    g_data.motionBlockRemainingMs = motionBlocked ? (motionBlockUntil - now) : 0;
    SHARED_UNLOCK();

    lastManualCmd = snap.manualPumpCmd;
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(PERIOD_PUMP_MS));
  }
}

// ============================================================
//  TASK: ROOF CONTROL
//  - Mai che LUON tu dong theo anh sang
//  - Khong phu thuoc V6 Pump Auto Mode
//  - Dung hysteresis de tranh dong/mo lien tuc
// ============================================================
void roofControlTask(void* pvParameters) {
  Serial.println("[RoofTask] Started");
  TickType_t lastWake = xTaskGetTickCount();
  bool roofState = false;

  for (;;) {
    SharedData_t snap = getSnapshot();

    // V6 chi dieu khien che do AUTO/MANUAL cua bom.
    // Mai che van tu dong dong/mo theo anh sang de bao ve cay.
    bool newRoofState = shouldCloseRoofByLight(snap.lightValue, roofState);

    if (newRoofState != roofState) {
      roofState = newRoofState;
      setRoofRelay(roofState);
      Serial.printf("[RoofTask] Roof %s (light=%d)\n",
                    roofState ? "CLOSED" : "OPEN",
                    snap.lightValue);
    }

    SHARED_LOCK();
    g_data.roofClosed = roofState;
    SHARED_UNLOCK();

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(PERIOD_ROOF_MS));
  }
}

// ============================================================
//  TASK: LCD
// ============================================================
void displayTask(void* pvParameters) {
  Serial.println("[DisplayTask] Started");
  TickType_t lastWake = xTaskGetTickCount();
  uint8_t page = 0;

  for (;;) {
    SharedData_t snap = getSnapshot();
    char line0[17];
    char line1[17];

    // Uu tien hien thi canh bao khi PIR phat hien co nguoi.
    // Trong thoi gian nay bom bi khoa, LCD se hien canh bao thay vi cac trang thong so.
    if (snap.motionBlocked) {
      snprintf(line0, sizeof(line0), "CANH BAO NGUOI");
      snprintf(line1, sizeof(line1), "Khoa bom:%lus", snap.motionBlockRemainingMs / 1000UL);

      lcdPrintLine(0, line0);
      lcdPrintLine(1, line1);

      vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(PERIOD_LCD_MS));
      continue;
    }

    switch (page) {
      case 0:
        snprintf(line0, sizeof(line0), "T:%.1fC H:%.0f%%", snap.temperature, snap.humidity);
        snprintf(line1, sizeof(line1), "Soil:%d%%", snap.soilMoisturePct);
        break;

      case 1:
        snprintf(line0, sizeof(line0), "Light:%d", snap.lightValue);
        snprintf(line1, sizeof(line1), "Pump:%s", snap.autoMode ? "AUTO" : "MANUAL");
        break;

      default:
        snprintf(line0, sizeof(line0), "P:%s R:%s", snap.pumpOn ? "ON" : "OFF", snap.roofClosed ? "CLS" : "OPN");
        snprintf(line1, sizeof(line1), "Cnt:%lu", (unsigned long)snap.motionCount);
        break;
    }

    lcdPrintLine(0, line0);
    lcdPrintLine(1, line1);

    page = (page + 1) % 3;
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(PERIOD_LCD_MS));
  }
}

//  TASK: BLYNK
//  - Blynk.run() chay nhanh
//  - Moi 1s moi gui du lieu 1 lan
// ============================================================
void blynkTask(void* pvParameters) {
  Serial.println("[BlynkTask] Started");
  unsigned long lastSync = 0;
  unsigned long lastReconnectTry = 0;
  unsigned long lastTempAlert = 0;

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      if (millis() - lastReconnectTry > 5000UL) {
        lastReconnectTry = millis();
        Serial.println("[BlynkTask] WiFi reconnect...");
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASS);
      }
    } else {
      if (!Blynk.connected() && millis() - lastReconnectTry > 5000UL) {
        lastReconnectTry = millis();
        Serial.println("[BlynkTask] Blynk reconnect...");
        Blynk.connect(2000);
      }

      Blynk.run();

      if (millis() - lastSync >= PERIOD_BLYNK_SYNC_MS) {
        lastSync = millis();

        SharedData_t snap = getSnapshot();
        if (Blynk.connected()) {
          Blynk.virtualWrite(VPIN_TEMPERATURE, snap.temperature);
          Blynk.virtualWrite(VPIN_HUMIDITY,    snap.humidity);
          Blynk.virtualWrite(VPIN_SOIL,        snap.soilMoisturePct);
          Blynk.virtualWrite(VPIN_LIGHT,       snap.lightValue);
          Blynk.virtualWrite(VPIN_PUMP,        snap.pumpOn ? 1 : 0);
          Blynk.virtualWrite(VPIN_ROOF,        snap.roofClosed ? 1 : 0);
          Blynk.virtualWrite(VPIN_MODE,        snap.autoMode ? 1 : 0);
          Blynk.virtualWrite(VPIN_MOTION,      snap.motionCount);

          if (snap.temperature > TEMP_HIGH_THRESHOLD && millis() - lastTempAlert > TEMP_ALERT_COOLDOWN_MS) {
            lastTempAlert = millis();
            Blynk.logEvent("high_temp", String("Nhiet do cao: ") + String(snap.temperature, 1) + "C");
          }
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(PERIOD_BLYNK_RUN_MS));
  }
}

// ============================================================
//  TASK: SERIAL DEBUG
// ============================================================
void serialDebugTask(void* pvParameters) {
  Serial.println("[DebugTask] Started");
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    SharedData_t s = getSnapshot();

    Serial.println("\n================ SMART GARDEN ================");
    Serial.printf("Temp      : %.1f C\n", s.temperature);
    Serial.printf("Humidity  : %.1f %%\n", s.humidity);
    Serial.printf("Soil ADC  : %d\n", s.soilAdc);
    Serial.printf("Soil %%    : %d %%\n", s.soilMoisturePct);
    Serial.printf("Light ADC : %d\n", s.lightValue);
    Serial.printf("PumpMode  : %s\n", s.autoMode ? "AUTO" : "MANUAL");
    Serial.printf("Pump      : %s\n", s.pumpOn ? "ON" : "OFF");
    Serial.printf("Roof      : %s\n", s.roofClosed ? "CLOSED" : "OPEN");
    Serial.printf("MotionCnt : %lu\n", (unsigned long)s.motionCount);
    Serial.printf("MotionBlk : %s", s.motionBlocked ? "YES" : "NO");
    if (s.motionBlocked) {
      Serial.printf(" (%lu ms)", (unsigned long)s.motionBlockRemainingMs);
    }
    Serial.println();
    Serial.printf("Soil rule : <%d%% => watering\n", SOIL_DRY_THRESHOLD);
#if LIGHT_BRIGHT_IS_HIGH
    Serial.printf("Light rule: close if >= %d, open if <= %d\n", LIGHT_CLOSE_THRESHOLD, LIGHT_OPEN_THRESHOLD);
#else
    Serial.printf("Light rule: close if <= %d, open if >= %d\n", LIGHT_CLOSE_THRESHOLD, LIGHT_OPEN_THRESHOLD);
#endif
    Serial.println("==============================================\n");

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(PERIOD_DEBUG_MS));
  }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("=== SMART GARDEN REFACTOR START ===");

  // GPIO
  pinMode(PIN_SOIL_MOISTURE, INPUT);
  pinMode(PIN_LIGHT_SENSOR,  INPUT);
  pinMode(PIN_PIR, INPUT_PULLDOWN);
  pinMode(PIN_PUMP, OUTPUT);
  pinMode(PIN_ROOF, OUTPUT);
  setPumpRelay(false);
  setRoofRelay(false);

  // ADC
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_SOIL_MOISTURE, ADC_11db);
  analogSetPinAttenuation(PIN_LIGHT_SENSOR,  ADC_11db);

  // DHT
  dht.begin();

  // LCD
  Wire.begin(PIN_SDA, PIN_SCL);
  lcd.init();
  lcd.backlight();
  lcdPrintLine(0, "Smart Garden");
  lcdPrintLine(1, "Booting...");

  // Shared data
  g_mutex = xSemaphoreCreateMutex();
  memset(&g_data, 0, sizeof(g_data));
  g_data.autoMode = true;
  g_data.soilMoisturePct = 100;  // tranh bom ngay khi vua khoi dong, cho analogTask cap nhat

  // WiFi + Blynk
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // giam tre/mat ket noi Blynk tren ESP32
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; ++i) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected. IP = ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi not connected yet. System still runs offline.");
  }

  Blynk.config(BLYNK_AUTH_TOKEN);
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.connect(2000);
  }

  // PIR interrupt
  attachInterrupt(digitalPinToInterrupt(PIN_PIR), motionISR, RISING);
  Serial.println("PIR interrupt attached");

  // Tasks
  xTaskCreatePinnedToCore(dhtTask,          "DHTTask",       4096, NULL, PRIO_MEDIUM, NULL, CORE_0);
  xTaskCreatePinnedToCore(analogTask,       "AnalogTask",    4096, NULL, PRIO_MEDIUM, NULL, CORE_0);
  xTaskCreatePinnedToCore(pumpControlTask,  "PumpTask",      4096, NULL, PRIO_HIGH,   NULL, CORE_1);
  xTaskCreatePinnedToCore(roofControlTask,  "RoofTask",      4096, NULL, PRIO_HIGH,   NULL, CORE_1);
  xTaskCreatePinnedToCore(displayTask,      "DisplayTask",   4096, NULL, PRIO_LOW,    NULL, CORE_0);
  xTaskCreatePinnedToCore(blynkTask,        "BlynkTask",     8192, NULL, PRIO_LOW,    NULL, CORE_0);
  xTaskCreatePinnedToCore(serialDebugTask,  "DebugTask",     4096, NULL, PRIO_LOW,    NULL, CORE_0);

  lcdPrintLine(0, "System ready");
  lcdPrintLine(1, "Pump AUTO mode");
  Serial.println("All tasks created");
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  vTaskDelay(portMAX_DELAY);
}
