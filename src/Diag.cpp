#include "Diag.h"
#include <Preferences.h>
#include <SPIFFS.h>
#include "Config.h"
#include "Actuators.h"
#include <FS.h>
#include "Sensors.h"
#include "PerfBenchmark.h"
#include "QuietSerial.h"
#include "TelemetryQueue.h"

#if SERIAL_READINGS_ONLY
#define Serial SerialQuiet
#endif

extern void simulateFlowPulses(int n); // declared in Sensors.h/impl
extern void diagnosePhPotentiometer(Sensors& sensors);
namespace hw { extern Sensors g_sensors; }
namespace hw { extern Actuators g_actuators; }

// Cached slope/intercept from last loadCalibration() — Diag uses these
// for one-shot pH display so the output matches the main loop formula.
// Defaults match the Sensors class: slope in pH/V, intercept = pH at 0V.
static float s_diagPhSlope = -1000.0f / PH_MV_PER_PH_SLOPE;
static float s_diagPhIntercept = 7.0f - s_diagPhSlope * PH_MV_REF_VOLTAGE;
static float s_diagPhOffset = PH_CALIBRATION_OFFSET;

void diagBegin(){
    if(!TelemetryQueue::begin()){
        Serial.println("[DIAG] SPIFFS mount failed");
    } else {
        Serial.println("[DIAG] SPIFFS mounted");
    }
    // Load calibration so Diag pH matches main formula
    Preferences p;
    if (p.begin("leaf_cal", true)) {
        if (p.isKey("ph_offset")) s_diagPhOffset = p.getFloat("ph_offset", PH_CALIBRATION_OFFSET);
        if (p.isKey("ph_slope")) s_diagPhSlope = p.getFloat("ph_slope", -1000.0f / PH_MV_PER_PH_SLOPE);
        if (p.isKey("ph_intercept")) s_diagPhIntercept = p.getFloat("ph_intercept", 7.0f - s_diagPhSlope * PH_MV_REF_VOLTAGE);
        p.end();
    }
    Serial.printf("[DIAG] pH formula: pH = %.3f * voltage + %.3f + offset %.3f\n",
                  s_diagPhSlope, s_diagPhIntercept, s_diagPhOffset);
    Serial.println("[DIAG] Commands: diag prefs | diag queue | diag pulse N | diag actuators | diag sensors | diag level [N] | diag ph-raw | diag ph-pot | diag perf | diag help");
#if LEAF_FAN_DIAG
    Serial.println("[DIAG] TEMPORARY fan relay isolation test active: diag fan-isolation on | off | status");
#endif
}

static void printPrefs(){
    Preferences p;
    if(!p.begin("leaf_cal", true)){
        Serial.println("[DIAG] Preferences begin failed");
        return;
    }
    Serial.print("[DIAG] ph_offset=");
    Serial.println(p.isKey("ph_offset") ? p.getFloat("ph_offset", 0.0f) : 0.0f);
    Serial.print("[DIAG] ph_slope=");
    Serial.println(p.isKey("ph_slope") ? p.getFloat("ph_slope", -1000.0f / PH_MV_PER_PH_SLOPE) : -1000.0f / PH_MV_PER_PH_SLOPE);
    Serial.print("[DIAG] ph_intercept=");
    float slope = p.isKey("ph_slope") ? p.getFloat("ph_slope", -1000.0f / PH_MV_PER_PH_SLOPE) : -1000.0f / PH_MV_PER_PH_SLOPE;
    Serial.println(p.isKey("ph_intercept") ? p.getFloat("ph_intercept", 7.0f - slope * PH_MV_REF_VOLTAGE) : 7.0f - slope * PH_MV_REF_VOLTAGE);
    Serial.print("[DIAG] ec_factor_legacy=");
    Serial.println(p.isKey("ec_factor") ? p.getFloat("ec_factor", EC_CALIBRATION_FACTOR) : EC_CALIBRATION_FACTOR);
    Serial.print("[DIAG] ec_k_low=");
    Serial.println(p.isKey("ec_k_low") ? p.getFloat("ec_k_low", EC_CALIBRATION_FACTOR) : EC_CALIBRATION_FACTOR);
    Serial.print("[DIAG] ec_k_high=");
    Serial.println(p.isKey("ec_k_high") ? p.getFloat("ec_k_high", EC_CALIBRATION_FACTOR) : EC_CALIBRATION_FACTOR);
    Serial.print("[DIAG] water_empty_raw=");
    Serial.println(p.isKey("water_empty_raw") ? p.getInt("water_empty_raw", 0) : 0);
    Serial.print("[DIAG] water_full_raw=");
    Serial.println(p.isKey("water_full_raw") ? p.getInt("water_full_raw", 4095) : 4095);
    Serial.print("[DIAG] water_empty_pct=");
    Serial.println(p.isKey("water_empty_pct") ? p.getFloat("water_empty_pct", 0.0f) : 0.0f);
    Serial.print("[DIAG] water_full_pct=");
    Serial.println(p.isKey("water_full_pct") ? p.getFloat("water_full_pct", 100.0f) : 100.0f);
    p.end();
}

static void printQueue(){
    Serial.println("[DIAG] Telemetry queue listing:");
    File root = SPIFFS.open("/");
    if(!root){
        Serial.println("[DIAG] SPIFFS open root failed");
        return;
    }
    File file = root.openNextFile();
    while(file){
        String name = file.name();
        if(name.indexOf("telemetry") >= 0){
            Serial.print(" - "); Serial.println(name);
        }
        file = root.openNextFile();
    }
}

static void actuatorsStatus(){
    Serial.println("[DIAG] Actuators status:");
    Serial.print("COOLING_FAN_RELAY_PIN: "); Serial.println(digitalRead(COOLING_FAN_RELAY_PIN));
    Serial.print("WATER_PUMP_RELAY_PIN: "); Serial.println(digitalRead(WATER_PUMP_RELAY_PIN));
    Serial.print("NUTRIENT_A_RELAY_PIN: "); Serial.println(digitalRead(NUTRIENT_A_RELAY_PIN));
    Serial.print("NUTRIENT_B_RELAY_PIN: "); Serial.println(digitalRead(NUTRIENT_B_RELAY_PIN));
    Serial.print("PH_UP_RELAY_PIN: "); Serial.println(digitalRead(PH_UP_RELAY_PIN));
    Serial.print("PH_DOWN_RELAY_PIN: "); Serial.println(digitalRead(PH_DOWN_RELAY_PIN));
}

static void diagSensors(){
    Serial.println("[DIAG] One-shot ADC readings (10 samples each, 5ms delay):");
    const int samples = 10;
    const int dly = 5;
    int vals[10];

    // pH (uses calibrated millivolts to match the runtime readPH() conversion)
    uint64_t pMvSum = 0;
    for (int i = 0; i < samples; ++i) { vals[i] = analogRead(PH_SENSOR_PIN); pMvSum += analogReadMilliVolts(PH_SENSOR_PIN); delay(dly); }
    int pMin = vals[0], pMax = vals[0]; long pSum = 0;
    for (int i = 0; i < samples; ++i) { pSum += vals[i]; if (vals[i] < pMin) pMin = vals[i]; if (vals[i] > pMax) pMax = vals[i]; }
    int pAvg = pSum / samples;
    float pDivider = (PH_INPUT_DIVIDER_RATIO > 0.0f) ? PH_INPUT_DIVIDER_RATIO : 1.0f;
    float pVoltage = ((float)(pMvSum / samples) / 1000.0f) / pDivider;
    float pPh = s_diagPhSlope * pVoltage + s_diagPhIntercept + s_diagPhOffset;
    Serial.printf("[DIAG pH ] raw_avg=%d min=%d max=%d spread=%d voltage=%.3fV mv=%u pH=%.2f\n",
                  pAvg, pMin, pMax, pMax - pMin, pVoltage, (unsigned)(pMvSum / samples), pPh);

    // EC
    uint64_t eMvSum = 0;
    for (int i = 0; i < samples; ++i) { vals[i] = analogRead(EC_SENSOR_PIN); eMvSum += analogReadMilliVolts(EC_SENSOR_PIN); delay(dly); }
    int eMin = vals[0], eMax = vals[0]; long eSum = 0;
    for (int i = 0; i < samples; ++i) { eSum += vals[i]; if (vals[i] < eMin) eMin = vals[i]; if (vals[i] > eMax) eMax = vals[i]; }
    int eAvg = eSum / samples;
    uint32_t eMv = (uint32_t)(eMvSum / samples);
    float eVoltage = eMv / 1000.0f;
    Serial.printf("[DIAG EC  ] raw_avg=%d min=%d max=%d spread=%d voltage=%.3fV mv=%u\n",
                  eAvg, eMin, eMax, eMax - eMin, eVoltage, eMv);

    // Water level
    for (int i = 0; i < samples; ++i) { vals[i] = analogRead(WATER_LEVEL_PIN); delay(dly); }
    int wMin = vals[0], wMax = vals[0]; long wSum = 0;
    for (int i = 0; i < samples; ++i) { wSum += vals[i]; if (vals[i] < wMin) wMin = vals[i]; if (vals[i] > wMax) wMax = vals[i]; }
    int wAvg = wSum / samples;
    Serial.printf("[DIAG LVL] raw_avg=%d min=%d max=%d spread=%d\n",
                  wAvg, wMin, wMax, wMax - wMin);
}

static void diagPhRaw() {
    const int samples = 20;
    const int dlyMs = 5;
    uint32_t mvSum = 0;
    uint32_t mvMin = 0xFFFFFFFF;
    uint32_t mvMax = 0;
    long rawSum = 0;
    int rawMin = 4096;
    int rawMax = 0;

    for (int i = 0; i < samples; ++i) {
        int raw = analogRead(PH_SENSOR_PIN);
        uint32_t mv = analogReadMilliVolts(PH_SENSOR_PIN);
        rawSum += raw;
        mvSum += mv;
        if ((int)raw < rawMin) rawMin = raw;
        if ((int)raw > rawMax) rawMax = raw;
        if (mv < mvMin) mvMin = mv;
        if (mv > mvMax) mvMax = mv;
        if (dlyMs > 0) delay(dlyMs);
    }

    uint32_t mvAvg = mvSum / samples;
    int rawAvg = rawSum / samples;
    Serial.printf("[PH RAW] samples=%d raw_min=%d raw_max=%d raw_avg=%d raw_spread=%d\n",
                  samples, rawMin, rawMax, rawAvg, rawMax - rawMin);
    Serial.printf("[PH RAW] mv_min=%u mv_max=%u mv_avg=%u mv_spread=%u\n",
                  mvMin, mvMax, mvAvg, mvMax - mvMin);
    Serial.printf("[PH RAW] pin=%d attenuation=11db divider=%.2f\n",
                  PH_SENSOR_PIN, PH_INPUT_DIVIDER_RATIO);
}

static void diagLevelContinuous(int count){
    Serial.printf("[DIAG] Continuous level ADC: %d readings (200ms interval)\n", count);
    Serial.println("[DIAG] Watch for floating (0/4095) vs stable readings");
    for (int i = 0; i < count; ++i) {
        int raw = analogRead(WATER_LEVEL_PIN);
        delay(200);
        Serial.printf("[DIAG LVL] %3d/%d raw=%4d\n", i + 1, count, raw);
    }
    Serial.println("[DIAG] Done");
}

bool diagHandleLine(const String &line){
    String s = line;
    s.trim();
    s.toLowerCase();
    if(!s.startsWith("diag")) return false;
    if(s == "diag" || s == "diag help"){
        Serial.println("diag prefs | diag queue | diag pulse N | diag actuators | diag sensors | diag level [N] | diag ph-raw | diag ph-pot | diag perf | diag help");
#if LEAF_FAN_DIAG
        Serial.println("diag fan-isolation on | off | status  (TEMPORARY fan relay isolation test)");
#endif
        return true;
    }
    if(s == "diag prefs"){
        printPrefs();
        return true;
    }
    if(s == "diag queue"){
        printQueue();
        return true;
    }
    if(s.startsWith("diag pulse")){
        int sp = s.indexOf(' ');
        int n = 1;
        if(sp > 0){
            String rest = s.substring(sp+1);
            rest.trim();
            if(rest.length() > 0) n = rest.toInt();
        }
        Serial.printf("[DIAG] Simulating %d pulses\n", n);
        simulateFlowPulses(n);
        return true;
    }
    if(s == "diag actuators"){
        actuatorsStatus();
        return true;
    }
    if(s == "diag sensors"){
        diagSensors();
        return true;
    }
    if(s.startsWith("diag level")){
        int sp = s.indexOf(' ');
        int n = 20;
        if(sp > 0){
            String rest = s.substring(sp+1);
            rest.trim();
            if(rest.length() > 0) n = rest.toInt();
        }
        diagLevelContinuous(n);
        return true;
    }
    if(s == "diag perf"){
        perfPrintFullReport();
        return true;
    }
    if(s == "diag ph-raw"){
        diagPhRaw();
        return true;
    }
    if(s == "diag ph-pot"){
        diagnosePhPotentiometer(hw::g_sensors);
        return true;
    }
#if LEAF_FAN_DIAG
    // TEMPORARY fan relay-isolation toggle (see docs/fan-relay-isolation-test.md).
    if (s.startsWith("diag fan-isolation")) {
        String rest = s.substring(18); // len("diag fan-isolation") == 18
        rest.trim();
        if (rest.length() == 0 || rest == "status" || rest == "?") {
            Serial.printf("[DIAG] Fan relay isolation: %s\n",
                          hw::g_actuators.fanRelayIsolation()
                              ? "ENABLED (physical relay writes SUPPRESSED)"
                              : "DISABLED (relay writes LIVE)");
            return true;
        }
        if (rest == "on") { hw::g_actuators.setFanRelayIsolation(true); return true; }
        if (rest == "off") { hw::g_actuators.setFanRelayIsolation(false); return true; }
        Serial.println("[DIAG] Usage: diag fan-isolation on | off | status");
        return true;
    }
#endif
    Serial.println("[DIAG] Unknown diag command");
    return true;
}
