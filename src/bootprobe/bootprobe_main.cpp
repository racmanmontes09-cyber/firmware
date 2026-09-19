// ============================================================================
// Project L.E.A.F. — Boot Isolation Probe (TEST 0..8)
//
// Controlled binary-search firmware. Each LEAF_BOOT_TEST level enables
// exactly ONE additional production boot stage on top of the previous one.
// Production module code is called unmodified wherever a stage exercises it.
//
//   TEST 0: minimal Arduino startup (Serial + delay only)
//   TEST 1: + raw GPIO initialization (mirrors production pinMode/digitalWrite,
//           no buses, no ISR, no peripherals)
//   TEST 2: + Sensors::begin()            (I2C, OneWire scan, ADC cfg, flow ISR)
//   TEST 3: + Actuators::begin()          (production output init sequence)
//   TEST 4: + storage (SPIFFS mount + NVS read/write smoke test)
//   TEST 5: + task watchdog               (production 25 s panic watchdog)
//   TEST 6: + WiFi                        (WifiManager::begin/loop until
//                                          NETWORK_READY or bounded timeout)
//   TEST 7: + MQTT/API                    (ApiClient construction effects +
//                                          logMqttConfiguration + loopMqtt
//                                          until MQTT_READY or bounded window)
//   TEST 8: + remaining services          (OTA rollback check/mark, diagBegin,
//                                          perfBegin, ArduinoOTA wiring,
//                                          production-like loop cadence)
//
// Build via envs diag0..diag8 in platformio.ini. Identical build flags in all
// envs; the only variable is -DLEAF_BOOT_TEST=n and executed statements.
//
// Serial contract (machine-parseable):
//   [PROBE] BANNER ...            first line after every reset
//   [PROBE] MILESTONE <NAME> t=<ms>
//   [PROBE] GPIO <pin> ...        instrumented GPIO table rows
//   [PROBE] DIAG ...              heap / psram / reset-reason snapshots
// ============================================================================

#include <Arduino.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include "Config.h"

// The COM-C port is the external UART bridge. With ARDUINO_USB_CDC_ON_BOOT=0
// (set in platformio.ini) `Serial` already resolves to UART0, so no remap is
// needed here (and `Serial0` is not declared in that configuration).
#ifndef LEAF_BOOT_TEST
#define LEAF_BOOT_TEST 0
#endif

#if LEAF_BOOT_TEST >= 2
#include "Sensors.h"
#endif
#if LEAF_BOOT_TEST >= 3
#include "Actuators.h"
#endif
#if LEAF_BOOT_TEST >= 4
#include <Preferences.h>
#include <SPIFFS.h>
#include "TelemetryQueue.h"
#endif
#if LEAF_BOOT_TEST >= 6
#include "WifiManager.h"
#endif
#if LEAF_BOOT_TEST >= 7
#include <ESPmDNS.h>
#include "ApiClient.h"
#endif
#if LEAF_BOOT_TEST >= 8
#include <ArduinoOTA.h>
#include "OtaRollback.h"
#include "Diag.h"
#include "PerfBenchmark.h"
#endif

// ---------------------------------------------------------------------------
// Stage gating
// ---------------------------------------------------------------------------
#define STAGE_GPIO        (LEAF_BOOT_TEST >= 1)
#define STAGE_SENSORS     (LEAF_BOOT_TEST >= 2)
#define STAGE_ACTUATORS   (LEAF_BOOT_TEST >= 3)
#define STAGE_STORAGE     (LEAF_BOOT_TEST >= 4)
#define STAGE_WATCHDOG    (LEAF_BOOT_TEST >= 5)
#define STAGE_WIFI        (LEAF_BOOT_TEST >= 6)
#define STAGE_MQTT        (LEAF_BOOT_TEST >= 7)
#define STAGE_FULL        (LEAF_BOOT_TEST >= 8)

#ifndef PROBE_WIFI_WAIT_MS
#define PROBE_WIFI_WAIT_MS 20000UL
#endif
#ifndef PROBE_MQTT_WAIT_MS
#define PROBE_MQTT_WAIT_MS 30000UL
#endif

// ---------------------------------------------------------------------------
// RTC-persistent counters (no flash/NVS writes: zero persistence side-effects).
// Survives warm resets; cleared by cold power cycle.
// ---------------------------------------------------------------------------
RTC_NOINIT_ATTR static uint32_t s_rtcMagic = 0;
RTC_NOINIT_ATTR static uint32_t s_warmBootCount = 0;
RTC_NOINIT_ATTR static uint32_t s_lastResetReason = 0xFFFFFFFF;
static const uint32_t kRtcMagic = 0x4C454146UL; // "LEAF"

// ---------------------------------------------------------------------------
// Milestones
// ---------------------------------------------------------------------------
struct Milestone { const char* name; uint32_t tMs; bool seen; };
static Milestone s_milestones[16];
static int s_milestoneCount = 0;

static void milestone(const char* name) {
    uint32_t now = millis();
    if (s_milestoneCount < 16) {
        s_milestones[s_milestoneCount++] = {name, now, true};
    }
    Serial.printf("[PROBE] MILESTONE %s t=%lums\n", name, (unsigned long)now);
}

// ---------------------------------------------------------------------------
// Reset reason decoding
// ---------------------------------------------------------------------------
static const char* resetReasonName(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_UNKNOWN:  return "UNKNOWN";
        case ESP_RST_POWERON:  return "POWERON";
        case ESP_RST_SW:       return "SW_RESET";
        case ESP_RST_PANIC:    return "PANIC";
        case ESP_RST_INT_WDT:  return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT:      return "OTHER_WDT";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO:     return "SDIO";
#ifdef ESP_RST_USB
        case ESP_RST_USB:      return "USB";
#endif
#ifdef ESP_RST_JTAG
        case ESP_RST_JTAG:     return "JTAG";
#endif
#ifdef ESP_RST_EFUSE
        case ESP_RST_EFUSE:    return "EFUSE";
#endif
#ifdef ESP_RST_PWR_GLITCH
        case ESP_RST_PWR_GLITCH:return "PWR_GLITCH";
#endif
#ifdef ESP_RST_CPU_LOCKUP
        case ESP_RST_CPU_LOCKUP: return "CPU_LOCKUP";
#endif
        default:               return "UNDEFINED";
    }
}

static void printDiag(const char* label) {
    esp_reset_reason_t r = esp_reset_reason();
    Serial.printf("[PROBE] DIAG %s reset=%s(%d) freeHeap=%u minFreeHeap=%u "
                  "psramSize=%u freePsram=%u\n",
                  label, resetReasonName(r), (int)r,
                  ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                  ESP.getPsramSize(), ESP.getFreePsram());
}

// ---------------------------------------------------------------------------
// Instrumented GPIO registry.
// One row per GPIO the production boot path configures or writes before
// APPLICATION_READY. 'live' is re-read at dump time so drift is visible.
// ---------------------------------------------------------------------------
struct GpioRec {
    int pin;
    const char* peripheral;
    const char* mode;
    const char* initialState;   // state right after pinMode()/first write
    const char* firstWrite;
    const char* firstRead;
    bool bootSensitive;
    const char* conflict;
};
static GpioRec s_gpioTable[] = {
#if STAGE_GPIO || STAGE_ACTUATORS
    {COOLING_FAN_RELAY_PIN,  "relay cooling_fan", "OUTPUT", "LOW(inactive)", "fanOff()->LOW",  "-", false, "JTAG MTDO on S3 if external debugger attached"},
    {WATER_PUMP_RELAY_PIN,   "relay water_pump",  "OUTPUT", "LOW(inactive)", "waterPumpOff()->LOW", "-", false, ""},
    {NUTRIENT_A_RELAY_PIN,   "relay nutrient_A",  "OUTPUT", "LOW(inactive)", "nutrientAOff()->LOW", "-", false, ""},
    {NUTRIENT_B_RELAY_PIN,   "relay nutrient_B",  "OUTPUT", "LOW(inactive)", "nutrientBOff()->LOW", "-", false, "JTAG MTMS on S3 if external debugger attached"},
    {PH_UP_RELAY_PIN,        "relay ph_up",       "OUTPUT", "LOW(inactive)", "phUpOff()->LOW", "-", false, "XTAL_32K_P if 32k crystal fitted"},
    {PH_DOWN_RELAY_PIN,      "relay ph_down",     "OUTPUT", "LOW(inactive)", "phDownOff()->LOW","-", false, "XTAL_32K_N if 32k crystal fitted"},
    {LED_STATUS_PIN,         "status LED",        "OUTPUT", "LOW",           "LOW then blink", "-", false, ""},
    {AIR_PUMP_RELAY_PIN,     "relay air_pump",    "NEVER-CONFIGURED", "floating(Hi-Z during reset)", "NONE", "NONE", true, "DEFINED IN Config.h BUT NEVER pinMode'd ANYWHERE: floats at boot; relay input may chatter"},
#endif
#if STAGE_SENSORS
    {DS18B20_PIN,            "1-Wire DS18B20",    "INPUT_PULLUP", "PU HIGH",   "-", "m_ds18b20.begin() bus scan", false, ""},
    {WATER_LEVEL_PIN,        "ADC water_level",   "INPUT(analog)", "no internal pull", "-", "analogRead/analogReadMilliVolts", false, "resistive divider; keep <=3.3V"},
    {FLOW_SENSOR_PIN,        "flow pulse INT",    "INPUT_PULLUP+FALLING-ISR", "PU HIGH", "-", "ISR flowPulseISR", false, "safe GPIO on S3 (JTAG is GPIO39-42; not a strap/USB pin)"},
    {PH_SENSOR_PIN,          "ADC pH",            "INPUT_PULLDOWN(analog)", "PD LOW-biased", "-", "analogRead", false, ""},
    {EC_SENSOR_PIN,          "ADC EC",            "INPUT(analog)", "no internal pull", "-", "analogReadMilliVolts", false, "DFRobot DFR0300 analog output"},
    {SHT31_SDA_PIN,          "I2C SDA SHT31",     "Wire open-drain", "pull-up HIGH", "-", "SHT31 probe @0x44", false, ""},
    {SHT31_SCL_PIN,          "I2C SCL SHT31",     "Wire open-drain", "pull-up HIGH", "-", "SHT31 probe @0x44", false, ""},
#endif
};
static const int kGpioCount = sizeof(s_gpioTable) / sizeof(s_gpioTable[0]);

static void printStrapState() {
    // Pure reads of strap pins as inputs. No pinMode changes: strap pins are
    // already inputs at this point; reading them does not alter latching
    // (straps are sampled by the ROM before the app runs).
    Serial.printf("[PROBE] STRAP gpio0=%d gpio3=%d gpio45=%d gpio46=%d\n",
                  digitalRead(0), digitalRead(3), digitalRead(45), digitalRead(46));
}

static void dumpGpioTable() {
    Serial.println("[PROBE] GPIOTABLE BEGIN");
    Serial.println("[PROBE] GPIO | peripheral | pinMode | initial | first_write | first_read | live_now | boot_sensitive | conflict");
    for (int i = 0; i < kGpioCount; ++i) {
        const GpioRec& g = s_gpioTable[i];
        String live("n/a");
        if (g.pin >= 0) {
            live = String(digitalRead(g.pin));
        }
        Serial.printf("[PROBE] GPIO %d | %s | %s | %s | %s | %s | %s | %s | %s\n",
                      g.pin, g.peripheral, g.mode, g.initialState,
                      g.firstWrite, g.firstRead, live.c_str(),
                      g.bootSensitive ? "YES" : "no",
                      strlen(g.conflict) ? g.conflict : "-");
    }
    Serial.println("[PROBE] GPIOTABLE END");
}

static void dumpMilestoneChain() {
    Serial.println("[PROBE] CHAIN BEGIN");
    for (int i = 1; i < s_milestoneCount; ++i) {
        Serial.printf("[PROBE] CHAIN %-18s t=%6lums delta=%+ldms\n",
                      s_milestones[i].name,
                      (unsigned long)s_milestones[i].tMs,
                      (long)(s_milestones[i].tMs - s_milestones[i - 1].tMs));
    }
    Serial.println("[PROBE] CHAIN END");
}

// ---------------------------------------------------------------------------
// TEST 1: raw GPIO initialization mirroring production order.
// Outputs follow Actuators::begin() (Actuators.cpp:82-98) exactly.
// Inputs follow Sensors::begin() pin setup (Sensors.cpp:159,186,209,229,256)
// but WITHOUT Wire/OneWire/analogSetPinAttenuation/attachInterrupt.
// ---------------------------------------------------------------------------
static void rawGpioInit() {
    // --- outputs: identical to Actuators::begin() ---
    pinMode(COOLING_FAN_RELAY_PIN, OUTPUT);
    digitalWrite(COOLING_FAN_RELAY_PIN, FAN_ACTIVE_LEVEL == HIGH ? LOW : HIGH);
    pinMode(WATER_PUMP_RELAY_PIN, OUTPUT);
    digitalWrite(WATER_PUMP_RELAY_PIN, PUMP_ACTIVE_LEVEL == HIGH ? LOW : HIGH);
    pinMode(AIR_PUMP_RELAY_PIN, OUTPUT);
    digitalWrite(AIR_PUMP_RELAY_PIN, PUMP_ACTIVE_LEVEL == HIGH ? LOW : HIGH);
    pinMode(NUTRIENT_A_RELAY_PIN, OUTPUT);
    digitalWrite(NUTRIENT_A_RELAY_PIN, NUTRIENT_ACTIVE_LEVEL == HIGH ? LOW : HIGH);
    pinMode(NUTRIENT_B_RELAY_PIN, OUTPUT);
    digitalWrite(NUTRIENT_B_RELAY_PIN, NUTRIENT_ACTIVE_LEVEL == HIGH ? LOW : HIGH);
    pinMode(PH_UP_RELAY_PIN, OUTPUT);
    digitalWrite(PH_UP_RELAY_PIN, PH_ACTIVE_LEVEL == HIGH ? LOW : HIGH);
    pinMode(PH_DOWN_RELAY_PIN, OUTPUT);
    digitalWrite(PH_DOWN_RELAY_PIN, PH_ACTIVE_LEVEL == HIGH ? LOW : HIGH);
    pinMode(LED_STATUS_PIN, OUTPUT);
    digitalWrite(LED_STATUS_PIN, LOW);

#if STAGE_SENSORS
    // From TEST 2 on, real modules own pin config; nothing extra here.
#else
    // --- inputs: same pinMode calls as Sensors::begin(), no protocols ---
    pinMode(DS18B20_PIN, INPUT_PULLUP);
    pinMode(WATER_LEVEL_PIN, INPUT);
    pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
    pinMode(PH_SENSOR_PIN, INPUT_PULLDOWN);
    pinMode(EC_SENSOR_PIN, INPUT);
#endif
    Serial.println("[PROBE] rawGpioInit done");
}

// ---------------------------------------------------------------------------
// Global module objects (constructed in every variant, like production main).
// Their constructors run before setup(); keeping them unconditional preserves
// that side of the comparison across variants.
// ---------------------------------------------------------------------------
#if LEAF_BOOT_TEST >= 2
static Sensors g_sensors;
#endif
#if LEAF_BOOT_TEST >= 3
static Actuators g_actuators;
#endif
#if LEAF_BOOT_TEST >= 6
static WifiManager g_wifiManager;
#endif
#if LEAF_BOOT_TEST >= 7
static ApiClient g_apiClient;
#endif

static bool s_watchdogOn = false;

static void watchdogFeed() {
    if (s_watchdogOn) esp_task_wdt_reset();
}

// ---------------------------------------------------------------------------

void setup() {
    // ---- TEST 0 baseline: serial + delay only -----------------------------
    Serial.begin(115200);
    Serial.setTimeout(0);

    // RTC warm-reset accounting (RAM only, survives sw reset, not power cycle)
    if (s_rtcMagic != kRtcMagic) {
        s_rtcMagic = kRtcMagic;
        s_warmBootCount = 0;
    } else {
        s_warmBootCount++;
    }
    esp_reset_reason_t rr = esp_reset_reason();
    bool reasonChanged = (s_lastResetReason != (uint32_t)rr);
    s_lastResetReason = (uint32_t)rr;

    Serial.println();
    Serial.printf("[PROBE] BANNER test=TEST%d build=" __DATE__ " " __TIME__
                  " warmBootCount=%lu resetReasonChanged=%d\n",
                  LEAF_BOOT_TEST, (unsigned long)s_warmBootCount,
                  reasonChanged ? 1 : 0);
    printDiag("BOOT");
    printStrapState();
    delay(100); // let host attach before milestone flood

    milestone("BOOT_START");

#if STAGE_GPIO
    // ---- TEST 1 ------------------------------------------------------------
    milestone("GPIO_INIT");
    rawGpioInit();
#endif

#if STAGE_SENSORS
    // ---- TEST 2 ------------------------------------------------------------
    milestone("SENSORS_BEGIN");
    g_sensors.begin();
    milestone("SENSORS_READY");
#endif

#if STAGE_ACTUATORS
    // ---- TEST 3 ------------------------------------------------------------
    milestone("ACTUATORS_BEGIN");
    g_actuators.begin();
    milestone("ACTUATORS_READY");
#endif

#if STAGE_STORAGE
    // ---- TEST 4 ------------------------------------------------------------
    milestone("STORAGE_BEGIN");
    if (!TelemetryQueue::begin()) {
        Serial.println("[PROBE] SPIFFS mount FAILED");
    }
    {
        Preferences cal;
        if (cal.begin("leaf_cal", true)) {
            float off = cal.getFloat("ph_offset", PH_CALIBRATION_OFFSET);
            Serial.printf("[PROBE] NVS read ok ph_offset=%.4f\n", off);
            cal.end();
        } else {
            Serial.println("[PROBE] NVS begin leaf_cal FAILED");
        }
    }
    milestone("STORAGE_READY");
#endif

#if STAGE_WATCHDOG
    // ---- TEST 5 ------------------------------------------------------------
    esp_task_wdt_init(WATCHDOG_TIMEOUT_MS / 1000, true);
    esp_task_wdt_add(NULL);
    s_watchdogOn = true;
    milestone("WATCHDOG_BEGIN");
#endif

#if STAGE_WIFI
    // ---- TEST 6 ------------------------------------------------------------
    milestone("WIFI_BEGIN");
    g_wifiManager.begin();
    unsigned long wifiDeadline = millis() + PROBE_WIFI_WAIT_MS;
    while (!g_wifiManager.isConnected() && millis() < wifiDeadline) {
        g_wifiManager.loop();
        watchdogFeed();
        delay(10);
    }
    // NETWORK_READY is printed by WifiManager itself on connect
    // (WifiManager.cpp:117). If it did not appear within the window we record
    // the timeout explicitly so logs stay unambiguous.
    if (!g_wifiManager.isConnected()) {
        Serial.printf("[PROBE] WIFI_TIMEOUT after %lums\n",
                      (unsigned long)PROBE_WIFI_WAIT_MS);
    }
#endif

#if STAGE_MQTT
    // ---- TEST 7 ------------------------------------------------------------
    // Production runs discardQueuedTelemetryOnSensorProfileChange() right
    // after TelemetryQueue::begin() (main.cpp:125). Reproduce that placement:
    // it needs storage, so guard on the stage that provides it.
    Serial.println("[BOOT] Milestone: MQTT_BEGIN");
    g_apiClient.logMqttConfiguration();
    unsigned long mqttDeadline = millis() + PROBE_MQTT_WAIT_MS;
    while (millis() < mqttDeadline) {
        if (STAGE_WIFI) g_wifiManager.loop();
        g_apiClient.loopMqtt();
        watchdogFeed();
        delay(10);
    }
    // MQTT_READY is printed inside ensureMqttConnected() on success
    // (ApiClient.cpp:400). Absence after the window = offline broker path.
#endif

#if STAGE_FULL
    // ---- TEST 8 ------------------------------------------------------------
    otaRollback_checkBoot();
    otaRollback_markSuccess();
    ArduinoOTA.setHostname(DEVICE_ID);
    ArduinoOTA.onStart([]() {
        Serial.println("[OTA] Begin");
        g_actuators.disableAllOutputs();
    });
    ArduinoOTA.onEnd([]() {
        g_actuators.disableAllOutputs();
        Serial.println("[OTA] End");
    });
    ArduinoOTA.onError([](ota_error_t e) {
        g_actuators.disableAllOutputs();
        Serial.printf("[OTA] Error: %d\n", e);
    });
    if (g_wifiManager.isConnected()) ArduinoOTA.begin();
    diagBegin();
    perfBegin();
#endif

    dumpGpioTable();
    dumpMilestoneChain();
    printDiag("APPLICATION");
    milestone("APPLICATION_READY");
}

// ---------------------------------------------------------------------------
// Post-ready steady-state loop. Feeds WDT when enabled, blinks status LED only
// if LED was configured, prints an alive line with diagnostics every 10 s so
// classification can also detect late resets after a clean boot.
// ---------------------------------------------------------------------------
void loop() {
    watchdogFeed();
    uint32_t now = millis();
    static uint32_t lastMinimalHeartbeat = 0;

    if (now - lastMinimalHeartbeat >= 500) {
        lastMinimalHeartbeat = now;
        Serial.printf("[PROBE] ALIVE t=%lums\n", (unsigned long)now);
    }

#if STAGE_ACTUATORS
    static uint32_t lastBlink = 0;
    if (now - lastBlink >= 1000) {
        lastBlink = now;
        digitalWrite(LED_STATUS_PIN, !digitalRead(LED_STATUS_PIN));
    }
#endif

#if STAGE_FULL
    // Production-equivalent cadence for the services introduced at TEST 8.
    static uint32_t nextOta = 0, nextDiag = 0;
    if (now - nextOta >= 100) {
        nextOta = now;
        ArduinoOTA.handle();
        if (STAGE_WIFI) g_wifiManager.loop();
    }
    if (now - nextDiag >= 60000) {
        nextDiag = now;
        printDiag("ALIVE60S");
    }
#elif STAGE_MQTT
    static uint32_t nextMaint = 0;
    static uint32_t nextAlive = 0;
    if (now - nextMaint >= 50) {
        nextMaint = now;
        if (STAGE_WIFI) g_wifiManager.loop();
        g_apiClient.loopMqtt();
        watchdogFeed();
    }
    if (now - nextAlive >= 10000) {
        nextAlive = now;
        printDiag("ALIVE10S");
    }
#else
    static uint32_t nextAlive = 0;
    if (now - nextAlive >= 10000) {
        nextAlive = now;
        printDiag("ALIVE10S");
    }
#endif

    yield();
}
