#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <cstring>
#include <cmath>
#include <freertos/FreeRTOS.h>
#include <esp_task_wdt.h>
#include "Sensors.h"
#include "QuietSerial.h"

#if SERIAL_READINGS_ONLY
#define Serial SerialQuiet
#endif

namespace {
static volatile uint32_t g_flowPulseCount = 0;
static volatile uint32_t g_lastPulseUs = 0;   // timestamp of last accepted pulse
static portMUX_TYPE g_flowPulseMux = portMUX_INITIALIZER_UNLOCKED;

static int readAdcAvgMilliVolts(int pin, int samples, int delayMs,
                                uint32_t* averageMv,
                                int* minRaw = nullptr, int* maxRaw = nullptr) {
    if (samples <= 0) {
        if (averageMv) *averageMv = 0;
        if (minRaw) *minRaw = 0;
        if (maxRaw) *maxRaw = 0;
        return 0;
    }

    int minv = 4096;
    int maxv = 0;
    long rawSum = 0;
    uint64_t mvSum = 0;
    for (int i = 0; i < samples; ++i) {
        int raw = analogRead(pin);
        uint32_t mv = analogReadMilliVolts(pin);
        rawSum += raw;
        mvSum += mv;
        if (raw < minv) minv = raw;
        if (raw > maxv) maxv = raw;
        if (delayMs > 0) delay(delayMs);
    }

    if (averageMv) *averageMv = (uint32_t)(mvSum / samples);
    if (minRaw) *minRaw = minv;
    if (maxRaw) *maxRaw = maxv;
    return (int)(rawSum / samples);
}

// Robust pH acquisition across a burst of ADC reads. Uses analogReadMilliVolts()
// (the ESP32-S3 calibrated ADC) as the voltage representation, matching the EC
// path and the runtime pH conversion. Returns per-sample calibrated millivolts
// along with a median, min/max, spread and stddev so the caller can distinguish
// a stable reading from a floating/noisy (disconnected) input.
struct PhSampleSet {
    int count = 0;
    int medianRaw = 0;
    uint32_t medianMv = 0;
    int minRaw = 4096;
    int maxRaw = 0;
    uint32_t minMv = 0xFFFFFFFFu;
    uint32_t maxMv = 0;
    int spreadRaw = 0;
    uint32_t spreadMv = 0;
    float stddevMv = 0.0f;
};

static PhSampleSet readPhSamples(int pin, int samples, int sampleDelayMs) {
    PhSampleSet out;
    if (samples <= 0) samples = 1;
    if (samples > 63) samples = 63;

    int rawBuf[63];
    uint32_t mvBuf[63];
    uint64_t mvSum = 0;
    uint64_t mvSumSq = 0;

    out.minRaw = 4096; out.maxRaw = 0;
    out.minMv = 0xFFFFFFFFu; out.maxMv = 0;

    for (int i = 0; i < samples; ++i) {
        int raw = analogRead(pin);
        uint32_t mv = analogReadMilliVolts(pin);
        rawBuf[i] = raw;
        mvBuf[i] = mv;
        mvSum += mv;
        mvSumSq += (uint64_t)mv * mv;
        if (raw < out.minRaw) out.minRaw = raw;
        if (raw > out.maxRaw) out.maxRaw = raw;
        if (mv < out.minMv) out.minMv = mv;
        if (mv > out.maxMv) out.maxMv = mv;
        if (sampleDelayMs > 0) delay(sampleDelayMs);
    }

    // Independent insertion sorts for raw and mv to get their medians.
    for (int i = 1; i < samples; ++i) {
        int rk = rawBuf[i];
        int j = i - 1;
        while (j >= 0 && rawBuf[j] > rk) { rawBuf[j + 1] = rawBuf[j]; --j; }
        rawBuf[j + 1] = rk;
        uint32_t mk = mvBuf[i];
        int k = i - 1;
        while (k >= 0 && mvBuf[k] > mk) { mvBuf[k + 1] = mvBuf[k]; --k; }
        mvBuf[k + 1] = mk;
    }

    out.count = samples;
    out.medianRaw = rawBuf[samples / 2];
    out.medianMv = mvBuf[samples / 2];
    out.spreadRaw = out.maxRaw - out.minRaw;
    out.spreadMv = out.maxMv - out.minMv;

    float mean = (float)mvSum / samples;
    float meanSq = (float)mvSumSq / samples;
    float variance = meanSq - mean * mean;
    out.stddevMv = (variance > 0.0f) ? sqrtf(variance) : 0.0f;
    return out;
}

// Water-level burst acquisition for the analog resistive sensor. Returns the
// median raw count (robust to impulse/ripple), the raw min/max and spread, and
// the average calibrated millivolt value. The median/spread/stddev let the
// caller separate a stable connected reading from an erratic floating
// (disconnected) input.
struct WaterLevelSampleSet {
    int medianRaw = 0;
    int minRaw = 4096;
    int maxRaw = 0;
    int spreadRaw = 0;
    uint32_t avgMv = 0;
    float stddevMv = 0.0f;
};

static WaterLevelSampleSet readWaterLevelSamples(int pin, int samples, int sampleDelayUs) {
    WaterLevelSampleSet out;
    if (samples <= 0) samples = 1;
    if (samples > 63) samples = 63;

    int rawBuf[63];
    uint64_t mvSum = 0;
    uint64_t mvSumSq = 0;
    out.minRaw = 4096; out.maxRaw = 0;

    for (int i = 0; i < samples; ++i) {
        int raw = analogRead(pin);
        uint32_t mv = analogReadMilliVolts(pin);
        rawBuf[i] = raw;
        mvSum += mv;
        mvSumSq += (uint64_t)mv * mv;
        if (raw < out.minRaw) out.minRaw = raw;
        if (raw > out.maxRaw) out.maxRaw = raw;
        if (sampleDelayUs > 0) delayMicroseconds(sampleDelayUs);
    }

    for (int i = 1; i < samples; ++i) {
        int key = rawBuf[i];
        int j = i - 1;
        while (j >= 0 && rawBuf[j] > key) { rawBuf[j + 1] = rawBuf[j]; --j; }
        rawBuf[j + 1] = key;
    }

    out.medianRaw = rawBuf[samples / 2];
    out.spreadRaw = out.maxRaw - out.minRaw;
    out.avgMv = (uint32_t)(mvSum / samples);
    float mean = (float)mvSum / samples;
    float meanSq = (float)mvSumSq / samples;
    float variance = meanSq - mean * mean;
    out.stddevMv = (variance > 0.0f) ? sqrtf(variance) : 0.0f;
    return out;
}

static bool isFiniteFloat(float value) {
    return !isnan(value) && !isinf(value);
}

static bool isValidEcKValue(float value) {
    return isFiniteFloat(value) && value >= EC_KVALUE_MIN && value <= EC_KVALUE_MAX;
}

static float ecRawMsPerCmFromMillivolts(float voltageMv) {
    return (1000.0f * voltageMv) / EC_DFROBOT_RES2 / EC_DFROBOT_ECREF;
}

static float ecTemperatureCompensation(float temperatureC) {
    return 1.0f + EC_TEMP_COEFF * (temperatureC - 25.0f);
}

static bool ecUsesLowCalibrationRange(float referenceMsPerCm) {
    return referenceMsPerCm <= EC_RANGE_HIGH_THRESHOLD;
}

static float clampFloat(float value, float minValue, float maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static bool configFlagEnabled(const char* value) {
    String flag(value ? value : "");
    flag.trim();
    flag.toLowerCase();
    if (flag.length() == 0) {
        return false;
    }
    return !(flag == "0" || flag == "false" || flag == "off" ||
             flag == "disabled" || flag == "no");
}

static void markSensorDisabled(SensorState& state) {
    state.enabled = false;
    state.valid = false;
    state.value = 0.0f;
    state.raw = 0;
    state.lastGoodMs = 0;
    state.consecutiveBadReads = 0;
    state.consecutiveGoodReads = 0;
}

static void markSensorInvalid(SensorState& state, int raw) {
    state.enabled = true;
    state.valid = false;
    state.value = 0.0f;
    state.raw = raw;
    state.consecutiveGoodReads = 0;
    if (state.consecutiveBadReads < 255) {
        state.consecutiveBadReads++;
    }
}

static void IRAM_ATTR flowPulseISR() {
    uint32_t now = micros();
    uint32_t prev = g_lastPulseUs;
    g_lastPulseUs = now;
    // Bounce / EMI / contact-noise guard: a real Hall turbine pulse period is
    // ms-scale even at maximum flow, while bounce and glitches arrive within
    // microseconds. Ignore pulses that follow the previous one too closely so
    // a single physical pulse is not counted multiple times.
    if (prev != 0 && (now - prev) < (uint32_t)FLOW_SENSOR_MIN_PULSE_INTERVAL_US) {
        return;
    }
    portENTER_CRITICAL_ISR(&g_flowPulseMux);
    g_flowPulseCount++;
    portEXIT_CRITICAL_ISR(&g_flowPulseMux);
}

static uint32_t consumeFlowPulseCount() {
    portENTER_CRITICAL(&g_flowPulseMux);
    uint32_t pulseCount = g_flowPulseCount;
    g_flowPulseCount = 0;
    portEXIT_CRITICAL(&g_flowPulseMux);
    return pulseCount;
}
}

// Diagnostic helper - allow test code to inject pulses without touching ISR data
void simulateFlowPulses(int n) {
    if (n <= 0) return;
    portENTER_CRITICAL(&g_flowPulseMux);
    for (int i = 0; i < n; ++i) {
        g_flowPulseCount++;
    }
    portEXIT_CRITICAL(&g_flowPulseMux);
}

void Sensors::begin() {
    Serial.println("[Sensor] Initializing sensor inputs");

    Serial.print("[SHT31] I2C SDA GPIO=");
    Serial.print(SHT31_SDA_PIN);
    Serial.print(" SCL GPIO=");
    Serial.print(SHT31_SCL_PIN);
    Serial.println(" address=0x44");
    Wire.begin(SHT31_SDA_PIN, SHT31_SCL_PIN);
    Wire.setClock(100000);
    Wire.setTimeOut(50);
    Wire.beginTransmission(0x44);
    uint8_t sht31ProbeResult = Wire.endTransmission();
    if (sht31ProbeResult != 0) {
        Serial.print("[SHT31] No I2C ACK at 0x44, Wire error=");
        Serial.print(sht31ProbeResult);
        Serial.println(" (check 3.3V, GND, SDA/SCL, pull-ups, and address)");
    }
    if (!m_sht31.begin(0x44)) {
        Serial.println("[SHT31] Initialization Failed");
        m_sht31Initialized = false;
    } else {
        m_sht31Initialized = true;
        Serial.println("[SHT31] Initialized");
    }

    Serial.print("[DS18B20] OneWire GPIO=");
    Serial.print(DS18B20_PIN);
    Serial.println(" (requires 4.7k pull-up to 3.3V and common GND)");
    pinMode(DS18B20_PIN, INPUT_PULLUP);
    m_ds18b20.begin();
    m_ds18b20.setResolution(9);
    int ds18b20DeviceCount = m_ds18b20.getDeviceCount();
    Serial.print("[DS18B20] Device count=");
    Serial.println(ds18b20DeviceCount);
    if (ds18b20DeviceCount == 0) {
        Serial.println("[DS18B20] Initialization Failed");
        m_ds18b20Initialized = false;
    } else {
        m_ds18b20Initialized = true;
        Serial.println("[DS18B20] Initialized");
    }

    if (!configFlagEnabled(WATER_LEVEL_SENSOR_ENABLED)) {
        markSensorDisabled(m_waterLevelState);
        m_waterLevelInitialized = false;
        Serial.println("[LEVEL] Disabled by configuration");
    } else if (strcmp(WATER_LEVEL_SENSOR_TYPE, "analog") != 0 && strcmp(WATER_LEVEL_SENSOR_TYPE, "digital") != 0) {
        Serial.println("[LEVEL] Initialization Failed");
        m_waterLevelInitialized = false;
    } else {
        Serial.print("[LEVEL] Type=");
        Serial.print(WATER_LEVEL_SENSOR_TYPE);
        Serial.print(" GPIO=");
        Serial.println(WATER_LEVEL_PIN);
        if (strcmp(WATER_LEVEL_SENSOR_TYPE, "analog") == 0) {
            // Analog resistive water-level sensor: the signal pin is the midpoint
            // of a resistor divider. Use plain INPUT — an internal pull-down would
            // load that divider and distort the reading. A floating/disconnected
            // input (no pull-down) swings widely and is caught by the stability
            // checks in readWaterLevel(), not by a low-voltage bias.
            pinMode(WATER_LEVEL_PIN, INPUT);
            analogSetPinAttenuation(WATER_LEVEL_PIN, ADC_11db);
            Serial.print("[LEVEL] ADC attenuation=11dB calibrated_mv=analogReadMilliVolts");
            Serial.println();
        } else {
            pinMode(WATER_LEVEL_PIN, INPUT);
        }
        m_waterLevelInitialized = true;
        Serial.println("[LEVEL] Initialized");
    }

    if (!configFlagEnabled(FLOW_SENSOR_ENABLED)) {
        markSensorDisabled(m_flowState);
        m_flowInitialized = false;
        m_flowRate = 0.0f;
        m_totalFlow = 0.0f;
        Serial.println("[FLOW] Disabled by configuration");
    } else {
        // Use internal pull-up so an open/disconnected flow sensor doesn't float
        // and generate spurious pulses. Flow sensors typically pull the line low
        // when outputting a pulse, so keep default HIGH with pull-up.
        pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
        portENTER_CRITICAL(&g_flowPulseMux);
        g_flowPulseCount = 0;
        g_lastPulseUs = 0;
        portEXIT_CRITICAL(&g_flowPulseMux);
        m_flowRate = 0.0f;
        m_totalFlow = 0.0f;
        m_lastFlowCalculationMs = millis();
        attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), flowPulseISR, FALLING);
        m_flowInitialized = true;
        Serial.println("[FLOW] Ready");
    }

    if (!configFlagEnabled(PH_SENSOR_ENABLED)) {
        markSensorDisabled(m_phState);
        m_phInitialized = false;
        Serial.println("[pH] Disabled by configuration");
    } else {
        // pH input is configured as plain INPUT (no pull-down). We must NOT bias
        // it because a DIY pH module drives an active amplified signal on Po;
        // pulling it down would corrupt the reading. A floating/disconnected
        // input (no pull-down) shows up as a large ADC spread/stddev, which the
        // stability checks in readPH() use to flag UNSTABLE/disconnected.
        pinMode(PH_SENSOR_PIN, INPUT);
        analogReadResolution(PH_ADC_RESOLUTION);
        analogSetPinAttenuation(PH_SENSOR_PIN, ADC_11db);
        m_phInitialized = true;
        Serial.print("[pH] ADC GPIO=");
        Serial.print(PH_SENSOR_PIN);
        Serial.print(" (ADC1_CH4) resolution=");
        Serial.print(PH_ADC_RESOLUTION);
        Serial.print(" attenuation=11dB divider=");
        Serial.print(PH_INPUT_DIVIDER_RATIO, 2);
        Serial.println();
        Serial.print("[pH] calibrated_mv=analogReadMilliVolts saturation_raw>=");
        Serial.println(PH_ADC_SATURATION_RAW);
        Serial.println("[pH] Ready");
        Serial.printf("[pH] Formula: pH = %.3f * voltage + %.3f + offset\n",
                      m_phSlope, m_phIntercept);
        Serial.printf("[pH] Plausible range: %.1f-%.1f\n", PH_VALID_MIN, PH_VALID_MAX);
        Serial.printf("[pH] Defaults: Vref=%.3fV (voltage at pH 7), slope=%.2f mV/pH (not the 59.16 Nernst value for this amplified module)\n",
                      PH_MV_REF_VOLTAGE, PH_MV_PER_PH_SLOPE);
        Serial.printf("[pH] Use 'cal ph2 <pH1> <voltage1> <pH2> <voltage2>' for two-point calibration\n");
        Serial.printf("[pH] Use 'cal ph <targetPh> <measuredVoltage>' for single-point offset calibration\n");
        Serial.printf("[pH] Use 'cal ph-interactive' for guided 2-point calibration with pH 7.00 and pH 4.00 buffers\n");
        Serial.printf("[pH] Use 'diag ph-pot' for potentiometer diagnostic (continuous ADC/voltage monitor)\n");
        Serial.printf("[pH] Hardware note: keep module Po within 0-3.3V on GPIO%d (use a voltage divider if from a 5V module).\n",
                      PH_SENSOR_PIN);
    }

    if (!configFlagEnabled(EC_SENSOR_ENABLED)) {
        markSensorDisabled(m_ecState);
        m_ecInitialized = false;
        Serial.println("[EC] Disabled by configuration");
    } else {
        pinMode(EC_SENSOR_PIN, INPUT);
        analogReadResolution(EC_ADC_RESOLUTION);
        analogSetPinAttenuation(EC_SENSOR_PIN, ADC_11db);
        m_ecInitialized = true;
        Serial.print("[EC] ADC GPIO=");
        Serial.print(EC_SENSOR_PIN);
        Serial.print(" resolution=");
        Serial.print(EC_ADC_RESOLUTION);
        Serial.print(" attenuation=11dB calibrated_mV=analogReadMilliVolts saturation_raw>=");
        Serial.println(EC_ADC_SATURATION_RAW);
        Serial.println("[EC] Ready");
    }

    loadCalibration();
}

bool Sensors::isFresh(const SensorState& state) const {
    if (!state.valid) {
        return false;
    }
    return (millis() - state.lastGoodMs) <= SENSOR_STALE_TIMEOUT_MS;
}

bool Sensors::loadCalibration() {
    Preferences preferences;
    if (!preferences.begin("leaf_cal", false)) {
        return false;
    }

    bool ok = true;

    if (preferences.isKey("ph_offset")) {
        m_phOffset = preferences.getFloat("ph_offset", PH_CALIBRATION_OFFSET);
        if (!isFiniteFloat(m_phOffset)) {
            Serial.println("[CAL] ph_offset invalid; using compile-time default");
            m_phOffset = PH_CALIBRATION_OFFSET;
            ok = false;
        } else {
            Serial.print("[CAL] ph_offset loaded: ");
            Serial.println(m_phOffset, 4);
        }
    } else {
        m_phOffset = PH_CALIBRATION_OFFSET;
        Serial.print("[CAL] ph_offset missing; using default ");
        Serial.println(m_phOffset, 4);
    }

    if (preferences.isKey("ph_slope")) {
        m_phSlope = preferences.getFloat("ph_slope", -1000.0f / PH_MV_PER_PH_SLOPE);
        if (!isFiniteFloat(m_phSlope) || m_phSlope == 0.0f) {
            Serial.println("[CAL] ph_slope invalid; using compile-time default");
            m_phSlope = -1000.0f / PH_MV_PER_PH_SLOPE;
            ok = false;
        } else {
            Serial.print("[CAL] ph_slope loaded: ");
            Serial.println(m_phSlope, 4);
        }
    } else {
        m_phSlope = -1000.0f / PH_MV_PER_PH_SLOPE;
        Serial.print("[CAL] ph_slope missing; using default ");
        Serial.println(m_phSlope, 4);
    }

    if (preferences.isKey("ph_intercept")) {
        m_phIntercept = preferences.getFloat("ph_intercept", 7.0f - m_phSlope * PH_MV_REF_VOLTAGE);
        if (!isFiniteFloat(m_phIntercept)) {
            Serial.println("[CAL] ph_intercept invalid; using compile-time default");
            m_phIntercept = 7.0f - m_phSlope * PH_MV_REF_VOLTAGE;
            ok = false;
        } else {
            Serial.print("[CAL] ph_intercept loaded: ");
            Serial.println(m_phIntercept, 4);
        }
    } else {
        m_phIntercept = 7.0f - m_phSlope * PH_MV_REF_VOLTAGE;
        Serial.print("[CAL] ph_intercept missing; using default ");
        Serial.println(m_phIntercept, 4);
    }

    float legacyEcKValue = EC_CALIBRATION_FACTOR;
    if (preferences.isKey("ec_factor")) {
        legacyEcKValue = preferences.getFloat("ec_factor", EC_CALIBRATION_FACTOR);
        if (!isValidEcKValue(legacyEcKValue)) {
            Serial.print("[CAL] legacy ec_factor outside DFRobot K range; ignoring: ");
            Serial.println(legacyEcKValue, 4);
            legacyEcKValue = EC_CALIBRATION_FACTOR;
            ok = false;
        }
    }

    m_ecKValueLow = preferences.isKey("ec_k_low")
        ? preferences.getFloat("ec_k_low", EC_CALIBRATION_FACTOR)
        : legacyEcKValue;
    if (!isValidEcKValue(m_ecKValueLow)) {
        Serial.println("[CAL] ec_k_low invalid; using compile-time default");
        m_ecKValueLow = EC_CALIBRATION_FACTOR;
        ok = false;
    }

    m_ecKValueHigh = preferences.isKey("ec_k_high")
        ? preferences.getFloat("ec_k_high", EC_CALIBRATION_FACTOR)
        : legacyEcKValue;
    if (!isValidEcKValue(m_ecKValueHigh)) {
        Serial.println("[CAL] ec_k_high invalid; using compile-time default");
        m_ecKValueHigh = EC_CALIBRATION_FACTOR;
        ok = false;
    }

    m_ecActiveKValue = m_ecKValueLow;
    Serial.print("[CAL] ec_k_low loaded: ");
    Serial.println(m_ecKValueLow, 4);
    Serial.print("[CAL] ec_k_high loaded: ");
    Serial.println(m_ecKValueHigh, 4);

    if (preferences.isKey("water_empty_raw")) {
        m_waterLevelEmptyRaw = preferences.getInt("water_empty_raw", m_waterLevelEmptyRaw);
        Serial.print("[CAL] water_empty_raw loaded: ");
        Serial.println(m_waterLevelEmptyRaw);
    } else {
        Serial.print("[CAL] water_empty_raw missing; using default ");
        Serial.println(m_waterLevelEmptyRaw);
    }

    if (preferences.isKey("water_full_raw")) {
        m_waterLevelFullRaw = preferences.getInt("water_full_raw", m_waterLevelFullRaw);
        Serial.print("[CAL] water_full_raw loaded: ");
        Serial.println(m_waterLevelFullRaw);
    } else {
        Serial.print("[CAL] water_full_raw missing; using default ");
        Serial.println(m_waterLevelFullRaw);
    }

    if (preferences.isKey("water_empty_pct")) {
        m_waterLevelEmptyPercent = preferences.getFloat("water_empty_pct", 0.0f);
        if (!isFiniteFloat(m_waterLevelEmptyPercent)) {
            Serial.println("[CAL] water_empty_pct invalid; using default 0.0");
            m_waterLevelEmptyPercent = 0.0f;
            ok = false;
        } else {
            Serial.print("[CAL] water_empty_pct loaded: ");
            Serial.println(m_waterLevelEmptyPercent, 2);
        }
    } else {
        m_waterLevelEmptyPercent = 0.0f;
        Serial.println("[CAL] water_empty_pct missing; using default 0.0");
    }

    if (preferences.isKey("water_full_pct")) {
        m_waterLevelFullPercent = preferences.getFloat("water_full_pct", 100.0f);
        if (!isFiniteFloat(m_waterLevelFullPercent)) {
            Serial.println("[CAL] water_full_pct invalid; using default 100.0");
            m_waterLevelFullPercent = 100.0f;
            ok = false;
        } else {
            Serial.print("[CAL] water_full_pct loaded: ");
            Serial.println(m_waterLevelFullPercent, 2);
        }
    } else {
        m_waterLevelFullPercent = 100.0f;
        Serial.println("[CAL] water_full_pct missing; using default 100.0");
    }

    if (m_waterLevelFullRaw <= m_waterLevelEmptyRaw) {
        Serial.println("[CAL] Water raw calibration invalid; using default 0-4095");
        m_waterLevelEmptyRaw = 0;
        m_waterLevelFullRaw = 4095;
        ok = false;
    }

    if (m_waterLevelFullPercent <= m_waterLevelEmptyPercent) {
        Serial.println("[CAL] Water percent calibration invalid; using default 0-100");
        m_waterLevelEmptyPercent = 0.0f;
        m_waterLevelFullPercent = 100.0f;
        ok = false;
    }

    preferences.end();
    return ok;
}

bool Sensors::saveCalibration() {
    Preferences preferences;
    if (!preferences.begin("leaf_cal", false)) {
        return false;
    }

    preferences.putFloat("ph_offset", m_phOffset);
    preferences.putFloat("ph_slope", m_phSlope);
    preferences.putFloat("ph_intercept", m_phIntercept);
    preferences.putFloat("ec_factor", m_ecKValueLow);
    preferences.putFloat("ec_k_low", m_ecKValueLow);
    preferences.putFloat("ec_k_high", m_ecKValueHigh);
    preferences.putInt("water_empty_raw", m_waterLevelEmptyRaw);
    preferences.putInt("water_full_raw", m_waterLevelFullRaw);
    preferences.putFloat("water_empty_pct", m_waterLevelEmptyPercent);
    preferences.putFloat("water_full_pct", m_waterLevelFullPercent);
    preferences.end();
    return true;
}

void Sensors::setPhOffset(float offset) {
    m_phOffset = offset;
}

void Sensors::setEcFactor(float factor) {
    if (!isValidEcKValue(factor)) {
        return;
    }
    m_ecKValueLow = factor;
    m_ecKValueHigh = factor;
    m_ecActiveKValue = factor;
}

void Sensors::setWaterLevelMapping(float emptyPercent, float fullPercent) {
    m_waterLevelEmptyPercent = emptyPercent;
    m_waterLevelFullPercent = fullPercent;
}

bool Sensors::calibratePh(float targetPh, float measuredPhOrVoltage) {
    if (!isFiniteFloat(targetPh) || !isFiniteFloat(measuredPhOrVoltage)) {
        return false;
    }
    // Single-point calibration: calculate offset so formula matches targetPh.
    // If measured argument is voltage (0.0V to 3.3V), convert voltage to expected pH first.
    float expectedPh = measuredPhOrVoltage;
    if (measuredPhOrVoltage >= 0.0f && measuredPhOrVoltage <= 3.3f) {
        expectedPh = m_phSlope * measuredPhOrVoltage + m_phIntercept;
    }
    m_phOffset = targetPh - expectedPh;
    Serial.printf("[CAL] Single-point pH: target=%.2f measured=%.3f offset=%.3f\n",
                  targetPh, measuredPhOrVoltage, m_phOffset);
    return saveCalibration();
}

bool Sensors::calibratePhTwoPoint(float target1, float measured1,
                                   float target2, float measured2) {
    if (!isFiniteFloat(target1) || !isFiniteFloat(measured1) ||
        !isFiniteFloat(target2) || !isFiniteFloat(measured2)) {
        return false;
    }
    if (target1 == target2 || measured1 == measured2) {
        Serial.println("[CAL] Two-point pH cal: targets or measurements identical");
        return false;
    }
    // Solve y = slope * x + intercept where x = voltage, y = pH
    // slope = (target2 - target1) / (measured2 - measured1)
    // intercept = target1 - slope * measured1
    float slope = (target2 - target1) / (measured2 - measured1);
    float intercept = target1 - slope * measured1;

    if (!isFiniteFloat(slope) || !isFiniteFloat(intercept) || slope == 0.0f) {
        Serial.println("[CAL] Two-point pH cal: computed slope invalid");
        return false;
    }

    m_phSlope = slope;
    m_phIntercept = intercept;
    m_phOffset = 0.0f;  // Two-point cal subsumes the old offset

    Serial.printf("[CAL] Two-point pH: slope=%.4f intercept=%.4f\n", slope, intercept);
    Serial.printf("[CAL] Formula now: pH = %.4f * voltage + %.4f\n", slope, intercept);
    return saveCalibration();
}

bool Sensors::interactiveCalibratePh() {
    static constexpr int CAL_SAMPLES = 30;
    static constexpr int CAL_SAMPLE_DELAY_MS = 20;
    static constexpr float VOLTAGE_MIN = 0.1f;
    static constexpr float VOLTAGE_MAX = 3.2f;
    static constexpr float MAX_STDDEV_MV = PH_ADC_NOISE_STDDEV_MV; // mV, matches runtime stability check
    static constexpr float MIN_SLOPE = 5.0f;
    static constexpr float MAX_SLOPE = 50.0f;

    Serial.println();
    Serial.println("pH Interactive Calibration");
    Serial.println("===========================");
    Serial.println("This performs a 2-point calibration using pH 7.00 and pH 4.00 buffer solutions.");
    Serial.println("The probe voltage will be read and averaged for stability.");
    Serial.println("Calibration values are saved to flash and survive reboots.");
    Serial.println();

    // Lambda: read and average calibrated ADC millivolts on PH_SENSOR_PIN.
    // Uses the SAME calibrated-mV representation as runtime readPH() so the
    // two-point fit matches live behavior. Feeds the watchdog periodically
    // since we block the main loop.
    auto readAverageVoltage = [](const char* label, float& outMean, float& outStddev) -> bool {
        Serial.printf("[CAL] Reading %s buffer... (%d samples)\n", label, CAL_SAMPLES);

        double sum = 0.0;
        double sumSq = 0.0;
        int validCount = 0;

        for (int i = 0; i < CAL_SAMPLES; ++i) {
            uint32_t mv = analogReadMilliVolts(PH_SENSOR_PIN);
            sum += mv;
            sumSq += (double)mv * mv;
            validCount++;
            delay(CAL_SAMPLE_DELAY_MS);
            // Feed watchdog every 5 samples (~100ms) to prevent reset during calibration
            if (i % 5 == 0) {
                esp_task_wdt_reset();
            }
        }

        if (validCount < CAL_SAMPLES / 2) {
            Serial.printf("[CAL] ERROR: too few valid samples (%d/%d), probe may be disconnected\n",
                          validCount, CAL_SAMPLES);
            return false;
        }

        double meanMv = sum / validCount;
        double variance = (sumSq / validCount) - (meanMv * meanMv);
        outStddev = (variance > 0.0) ? sqrtf((float)variance) : 0.0f;
        // Recover module-side voltage if a protective divider is installed.
        float divider = (PH_INPUT_DIVIDER_RATIO > 0.0f) ? PH_INPUT_DIVIDER_RATIO : 1.0f;
        outMean = ((float)meanMv / 1000.0f) / divider;

        Serial.printf("[CAL] %s: voltage = %.3f V (± %.3f V, %d samples, %d mV avg)\n",
                      label, outMean, outStddev / 1000.0f, validCount, (int)meanMv);
        return true;
    };

    // --- Step 1: pH 7.00 buffer ---
    Serial.println("Step 1: Place probe in pH 7.00 buffer solution.");
    Serial.println("        Wait for reading to stabilize (30-60 seconds).");
    Serial.println("        Press Enter when ready...");
    while (!Serial.available()) {
        delay(50);
        esp_task_wdt_reset();
    }
    Serial.readStringUntil('\n');

    float v7 = 0.0f, stdDev7 = 0.0f;
    if (!readAverageVoltage("pH 7.00", v7, stdDev7)) {
        Serial.println("[CAL] Calibration aborted: failed to read pH 7.00 buffer");
        return false;
    }
    if (v7 < VOLTAGE_MIN || v7 > VOLTAGE_MAX) {
        Serial.printf("[CAL] Calibration aborted: pH 7.00 voltage %.3fV outside valid range [%.1f-%.1f]V (probe disconnected?)\n",
                      v7, VOLTAGE_MIN, VOLTAGE_MAX);
        return false;
    }
    if (stdDev7 > MAX_STDDEV_MV) {
        Serial.printf("[CAL] Calibration aborted: pH 7.00 reading unstable (stddev=%.1fmV > %.1fmV)\n",
                      stdDev7, MAX_STDDEV_MV);
        return false;
    }

    Serial.println();

    // --- Step 2: pH 4.00 buffer ---
    Serial.println("Step 2: Rinse probe, then place in pH 4.00 buffer solution.");
    Serial.println("        Wait for reading to stabilize (30-60 seconds).");
    Serial.println("        Press Enter when ready...");
    while (!Serial.available()) {
        delay(50);
        esp_task_wdt_reset();
    }
    Serial.readStringUntil('\n');

    float v4 = 0.0f, stdDev4 = 0.0f;
    if (!readAverageVoltage("pH 4.00", v4, stdDev4)) {
        Serial.println("[CAL] Calibration aborted: failed to read pH 4.00 buffer");
        return false;
    }
    if (v4 < VOLTAGE_MIN || v4 > VOLTAGE_MAX) {
        Serial.printf("[CAL] Calibration aborted: pH 4.00 voltage %.3fV outside valid range [%.1f-%.1f]V (probe disconnected?)\n",
                      v4, VOLTAGE_MIN, VOLTAGE_MAX);
        return false;
    }
    if (stdDev4 > MAX_STDDEV_MV) {
        Serial.printf("[CAL] Calibration aborted: pH 4.00 reading unstable (stddev=%.1fmV > %.1fmV)\n",
                      stdDev4, MAX_STDDEV_MV);
        return false;
    }

    // --- Compute calibration ---
    Serial.println("[CAL] Computing calibration...");

    // Guard against division by zero (identical voltages = bad buffers or probe stuck)
    float vDelta = v7 - v4;
    if (fabsf(vDelta) < 0.001f) {
        Serial.printf("[CAL] Calibration aborted: voltages too close (V7=%.3f, V4=%.3f, delta=%.4fV)\n",
                      v7, v4, vDelta);
        Serial.println("[CAL] Check that buffers are correct and probe is responding");
        return false;
    }

    // Linear fit: pH = slope * voltage + intercept
    // slope = (pH7 - pH4) / (V7 - V4) = 3.0 / (V7 - V4)
    // intercept = 7.00 - slope * V7
    float target7 = 7.00f;
    float target4 = 4.00f;
    float slope = (target7 - target4) / vDelta;
    float intercept = target7 - slope * v7;

    if (!isFiniteFloat(slope) || !isFiniteFloat(intercept)) {
        Serial.println("[CAL] Calibration aborted: computed slope or intercept is not a finite number");
        return false;
    }

    // Sanity check: slope magnitude should be in a plausible range.
    // Theoretical Nernst at 25°C is ~16.9 pH/V. Allow 5-50 pH/V to cover
    // real probes, temperature variation, and different board amplification.
    float absSlope = fabsf(slope);
    if (absSlope < MIN_SLOPE || absSlope > MAX_SLOPE) {
        Serial.printf("[CAL] Calibration aborted: slope %.4f pH/V outside plausible range [%.0f-%.0f] pH/V\n",
                      slope, MIN_SLOPE, MAX_SLOPE);
        Serial.println("[CAL] Verify buffer solutions and probe condition");
        return false;
    }

    // Verify: recalibrate points should map back to the target pH values
    float verify7 = slope * v7 + intercept;
    float verify4 = slope * v4 + intercept;
    if (fabsf(verify7 - target7) > 0.01f || fabsf(verify4 - target4) > 0.01f) {
        Serial.printf("[CAL] Calibration verification failed: V7=>pH %.2f (expected 7.00), V4=>pH %.2f (expected 4.00)\n",
                      verify7, verify4);
        return false;
    }

    // Apply calibration (two-point subsumes any previous offset)
    m_phSlope = slope;
    m_phIntercept = intercept;
    m_phOffset = 0.0f;

    Serial.printf("[CAL] Two-point pH: slope = %.4f pH/V, intercept = %.4f\n", slope, intercept);
    Serial.printf("[CAL] Formula: pH = %.4f * voltage + %.4f\n", slope, intercept);
    float slopeMvPerPh = -1000.0f / slope;
    Serial.printf("[CAL] Slope: %.4f pH/V = %.1f mV/pH\n", slope, slopeMvPerPh);
    Serial.printf("[CAL] Verification: V=%.3fV => pH %.2f (expected 7.00), V=%.3fV => pH %.2f (expected 4.00)\n",
                  v7, verify7, v4, verify4);

    if (!saveCalibration()) {
        Serial.println("[CAL] WARNING: calibration computed but failed to save to flash");
        return false;
    }
    Serial.println("[CAL] pH calibration saved to flash");
    return true;
}

void Sensors::diagnosePhPotentiometer() {
    static constexpr int DIAG_SAMPLES = 10;
    static constexpr int DIAG_SAMPLE_DELAY_MS = 5;
    static constexpr unsigned long DIAG_READ_INTERVAL_MS = 500;
    static constexpr float VOLTAGE_CHANGE_THRESHOLD = 0.02f;

    Serial.println();
    Serial.println("pH Potentiometer Diagnostic Mode");
    Serial.println("================================");
    Serial.println("Continuously monitoring pH analog output.");
    Serial.println("Turn potentiometers on the pH board to see voltage changes.");
    Serial.println("Type 'exit' to return to normal operation.");
    Serial.println();

    float prevVoltage = -1.0f;
    bool hasPrev = false;
    unsigned long lastReadMs = 0;

    while (true) {
        unsigned long now = millis();
        if (now - lastReadMs < DIAG_READ_INTERVAL_MS) {
            delay(10);
            esp_task_wdt_reset();
            continue;
        }
        lastReadMs = now;

        // Check for exit command (non-blocking)
        if (Serial.available()) {
            String cmd = Serial.readStringUntil('\n');
            cmd.trim();
            cmd.toLowerCase();
            if (cmd == "exit" || cmd == "quit" || cmd == "q") {
                Serial.println("[DIAG] Exiting pH potentiometer diagnostic mode");
                return;
            }
        }

        // Read and average multiple calibrated-mV ADC samples for noise reduction.
        uint64_t mvSum = 0;
        int minRaw = 4096, maxRaw = 0;
        for (int i = 0; i < DIAG_SAMPLES; ++i) {
            int raw = analogRead(PH_SENSOR_PIN);
            mvSum += analogReadMilliVolts(PH_SENSOR_PIN);
            if (raw < minRaw) minRaw = raw;
            if (raw > maxRaw) maxRaw = raw;
            delay(DIAG_SAMPLE_DELAY_MS);
        }
        float avgMv = (float)(mvSum / DIAG_SAMPLES);
        float divider = (PH_INPUT_DIVIDER_RATIO > 0.0f) ? PH_INPUT_DIVIDER_RATIO : 1.0f;
        float voltage = (avgMv / 1000.0f) / divider;
        float phValue = m_phSlope * voltage + m_phIntercept + m_phOffset;
        int spread = maxRaw - minRaw;

        // Detect significant voltage change from previous stable reading
        if (hasPrev && fabsf(voltage - prevVoltage) >= VOLTAGE_CHANGE_THRESHOLD) {
            Serial.println(">>> ANALOG OUTPUT CHANGED");
        }

        Serial.printf("pH: %.2f | raw_min=%d raw_max=%d | Voltage: %.3f V (GPIO %.3fV)\n",
                      phValue, minRaw, maxRaw, voltage, avgMv / 1000.0f);
        if (spread > 100) {
            Serial.printf("    WARNING: ADC spread=%d (min=%d max=%d) - reading may be noisy/floating\n",
                          spread, minRaw, maxRaw);
        }

        prevVoltage = voltage;
        hasPrev = true;
    }
}

int Sensors::readEcAdc(uint32_t& averageMv, int& minRaw, int& maxRaw) {
    return readAdcAvgMilliVolts(EC_SENSOR_PIN, ADC_SAMPLES, ADC_SAMPLE_DELAY_MS,
                                &averageMv, &minRaw, &maxRaw);
}

float Sensors::currentEcTemperatureC() const {
    return m_waterTemperatureState.valid ? m_waterTemperatureState.value : 25.0f;
}

bool Sensors::calibrateEcFromVoltage(float referenceMsPerCm, float voltageMv, float temperatureC) {
    if (!isFiniteFloat(referenceMsPerCm) || !isFiniteFloat(voltageMv) ||
        !isFiniteFloat(temperatureC) || referenceMsPerCm <= 0.0f || voltageMv <= 0.0f) {
        return false;
    }

    float rawEc = ecRawMsPerCmFromMillivolts(voltageMv);
    float compensation = ecTemperatureCompensation(temperatureC);
    if (!isFiniteFloat(rawEc) || !isFiniteFloat(compensation) ||
        rawEc <= 0.0f || compensation <= 0.0f) {
        return false;
    }

    float kValue = referenceMsPerCm * compensation / rawEc;
    if (!isValidEcKValue(kValue)) {
        Serial.printf("[CAL] EC calibration rejected: computed K=%.4f outside %.2f-%.2f (ref=%.3f mS/cm voltage=%.1f mV temp=%.1fC)\n",
                      kValue, EC_KVALUE_MIN, EC_KVALUE_MAX, referenceMsPerCm, voltageMv, temperatureC);
        return false;
    }

    if (ecUsesLowCalibrationRange(referenceMsPerCm)) {
        m_ecKValueLow = kValue;
        m_ecActiveKValue = m_ecKValueLow;
        Serial.printf("[CAL] EC low-range K saved: %.4f (ref=%.3f mS/cm voltage=%.1f mV temp=%.1fC)\n",
                      kValue, referenceMsPerCm, voltageMv, temperatureC);
    } else {
        m_ecKValueHigh = kValue;
        m_ecActiveKValue = m_ecKValueHigh;
        Serial.printf("[CAL] EC high-range K saved: %.4f (ref=%.3f mS/cm voltage=%.1f mV temp=%.1fC)\n",
                      kValue, referenceMsPerCm, voltageMv, temperatureC);
    }

    return saveCalibration();
}

bool Sensors::calibrateEc(float referenceMsPerCm) {
    if (!m_ecInitialized || !isFiniteFloat(referenceMsPerCm) || referenceMsPerCm <= 0.0f) {
        return false;
    }

    int minRaw = 0;
    int maxRaw = 0;
    uint32_t averageMv = 0;
    int raw = readEcAdc(averageMv, minRaw, maxRaw);
    int spread = maxRaw - minRaw;

    if (spread > ADC_SPREAD_REJECT_THRESHOLD) {
        Serial.printf("[CAL] EC calibration rejected: unstable ADC spread=%d > %d (raw min=%d max=%d)\n",
                      spread, ADC_SPREAD_REJECT_THRESHOLD, minRaw, maxRaw);
        return false;
    }

    if (maxRaw >= EC_ADC_SATURATION_RAW || averageMv >= EC_ADC_SAFE_MAX_MV) {
        Serial.printf("[CAL] EC calibration rejected: ADC over-range raw=%d min=%d max=%d voltage=%u mV; protect GPIO%d from >3.3V\n",
                      raw, minRaw, maxRaw, averageMv, EC_SENSOR_PIN);
        return false;
    }

    return calibrateEcFromVoltage(referenceMsPerCm, (float)averageMv, currentEcTemperatureC());
}

bool Sensors::calibrateEc(float referenceMsPerCm, float measuredMsPerCm) {
    if (!isFiniteFloat(referenceMsPerCm) || !isFiniteFloat(measuredMsPerCm) ||
        referenceMsPerCm <= 0.0f || measuredMsPerCm <= 0.0f) {
        return false;
    }

    float& kValue = ecUsesLowCalibrationRange(referenceMsPerCm) ? m_ecKValueLow : m_ecKValueHigh;
    float calibratedKValue = kValue * (referenceMsPerCm / measuredMsPerCm);
    if (!isValidEcKValue(calibratedKValue)) {
        Serial.printf("[CAL] EC calibration rejected: computed K=%.4f outside %.2f-%.2f (ref=%.3f measured=%.3f)\n",
                      calibratedKValue, EC_KVALUE_MIN, EC_KVALUE_MAX, referenceMsPerCm, measuredMsPerCm);
        return false;
    }

    kValue = calibratedKValue;
    m_ecActiveKValue = kValue;
    Serial.printf("[CAL] EC %s-range K saved: %.4f (ref=%.3f measured=%.3f)\n",
                  ecUsesLowCalibrationRange(referenceMsPerCm) ? "low" : "high",
                  calibratedKValue, referenceMsPerCm, measuredMsPerCm);
    return saveCalibration();
}

bool Sensors::calibrateWaterLevel(float emptyRaw, float fullRaw) {
    if (!isFiniteFloat(emptyRaw) || !isFiniteFloat(fullRaw) || fullRaw <= emptyRaw) {
        return false;
    }
    m_waterLevelEmptyRaw = (int)round(emptyRaw);
    m_waterLevelFullRaw = (int)round(fullRaw);
    return saveCalibration();
}

bool Sensors::readSHT31() {
    bool previousAirValid = m_airTemperatureState.valid;
    bool previousHumidityValid = m_humidityState.valid;

    if (!m_sht31Initialized) {
        Serial.println("[SHT31] DISCONNECTED: not initialized");
        m_airTemperatureState.enabled = false;
        m_airTemperatureState.valid = false;
        m_airTemperatureState.value = 0.0f;
        m_humidityState.enabled = false;
        m_humidityState.valid = false;
        m_humidityState.value = 0.0f;
        if (previousAirValid || previousHumidityValid) {
            Serial.printf("[SHT31] VALIDITY CHANGE: VALID -> INVALID reason=not_initialized millis=%lu\n",
                          static_cast<unsigned long>(millis()));
        }
        return false;
    }

#if EMI_DIAG_LOGGING
    // ── EMI DIAG: probe I²C bus before reading ──
    // Wire.endTransmission() returns: 0=success, 1=data Too Long,
    // 2=NACK on addr, 3=NACK on data, 4=other error, 5=timeout.
    unsigned long diagStartUs = micros();
    Wire.beginTransmission(0x44);
    uint8_t wireError = Wire.endTransmission();
    unsigned long wireProbeUs = micros() - diagStartUs;
#endif

    unsigned long readStartUs = micros();
    float temperature = m_sht31.readTemperature();
    float humidity = m_sht31.readHumidity();
    unsigned long readUs = micros() - readStartUs;

    bool valid = isFiniteFloat(temperature) && isFiniteFloat(humidity);
    valid = valid && (temperature >= -20.0f && temperature <= 80.0f);
    valid = valid && (humidity >= 0.0f && humidity <= 100.0f);

    // ── EMI DIAG: log every I²C read attempt with full context ──
#if EMI_DIAG_LOGGING
    {
        const char* fanState = "?";
        // Read the actual GPIO to correlate with relay state
        int fanGpio = digitalRead(COOLING_FAN_RELAY_PIN);
        if (fanGpio == FAN_ACTIVE_LEVEL) {
            fanState = "ON";
        } else {
            fanState = "OFF";
        }
        Serial.printf(
            "[EMI-DIAG] t=%lu wire_err=%u probe_us=%lu read_us=%lu "
            "raw_temp=%.2f raw_humi=%.2f valid=%s "
            "prev_valid=%s bad=%u good=%u fan_gpio=%s pin=%d\n",
            static_cast<unsigned long>(millis()),
            (unsigned)wireError,
            static_cast<unsigned long>(wireProbeUs),
            static_cast<unsigned long>(readUs),
            temperature, humidity,
            valid ? "YES" : "NO",
            previousAirValid ? "YES" : "NO",
            m_airTemperatureState.consecutiveBadReads,
            m_airTemperatureState.consecutiveGoodReads,
            fanState,
            COOLING_FAN_RELAY_PIN
        );
    }
#endif

    if (!valid) {
        Serial.println("[SHT31] DISCONNECTED: Air/Humidity Invalid");
        m_airTemperatureState.consecutiveGoodReads = 0;
        m_airTemperatureState.consecutiveBadReads++;
        m_humidityState.consecutiveGoodReads = 0;
        m_humidityState.consecutiveBadReads++;

        // Debounce: require N consecutive bad reads before transitioning
        // VALID → INVALID.  A single I2C glitch (e.g. EMI from relay
        // switching) must not flip the sensor invalid and force the fan OFF,
        // which would start the ON/OFF relay chatter cycle.
        if (previousAirValid && m_airTemperatureState.consecutiveBadReads < SHT31_DEBOUNCE_READS) {
            Serial.printf("[SHT31] DEBOUNCE: bad_read=%u/%u keeping previous valid state (last_temp=%.2f) millis=%lu\n",
                          m_airTemperatureState.consecutiveBadReads,
                          (unsigned)SHT31_DEBOUNCE_READS,
                          m_airTemperatureState.value,
                          static_cast<unsigned long>(millis()));
            return false;
        }

        // Confirmed invalid after debounce threshold
        m_airTemperatureState.enabled = true;
        m_airTemperatureState.valid = false;
        m_airTemperatureState.value = 0.0f;
        m_humidityState.enabled = true;
        m_humidityState.valid = false;
        m_humidityState.value = 0.0f;
        if (previousAirValid || previousHumidityValid) {
            Serial.printf("[SHT31] VALIDITY CHANGE: VALID -> INVALID temperature=%.2f humidity=%.2f bad_reads=%u millis=%lu\n",
                          temperature,
                          humidity,
                          m_airTemperatureState.consecutiveBadReads,
                          static_cast<unsigned long>(millis()));
        }
        return false;
    }

    m_lastAirTemperature = temperature;
    m_lastHumidity = humidity;
    m_airTemperatureState.consecutiveBadReads = 0;
    if (m_airTemperatureState.consecutiveGoodReads < 255) {
        m_airTemperatureState.consecutiveGoodReads++;
    }
    m_humidityState.consecutiveBadReads = 0;
    if (m_humidityState.consecutiveGoodReads < 255) {
        m_humidityState.consecutiveGoodReads++;
    }

    // Debounce: require N consecutive good reads before transitioning
    // INVALID → VALID.  A single successful read after a glitch must not
    // immediately re-enable the fan automation path, which would restart
    // the relay chatter cycle.
    if (!previousAirValid && m_airTemperatureState.consecutiveGoodReads < SHT31_DEBOUNCE_READS) {
        Serial.printf("[SHT31] DEBOUNCE: good_read=%u/%u temp=%.2f still INVALID millis=%lu\n",
                      m_airTemperatureState.consecutiveGoodReads,
                      (unsigned)SHT31_DEBOUNCE_READS,
                      temperature,
                      static_cast<unsigned long>(millis()));
        m_airTemperatureState.enabled = true;
        m_airTemperatureState.valid = false;
        m_airTemperatureState.value = temperature;
        m_airTemperatureState.lastGoodMs = millis();
        m_humidityState.enabled = true;
        m_humidityState.valid = false;
        m_humidityState.value = humidity;
        m_humidityState.lastGoodMs = millis();
        return false;
    }

    // Confirmed valid after debounce threshold
    m_airTemperatureState.enabled = true;
    m_airTemperatureState.valid = true;
    m_airTemperatureState.value = temperature;
    m_airTemperatureState.lastGoodMs = millis();
    m_humidityState.enabled = true;
    m_humidityState.valid = true;
    m_humidityState.value = humidity;
    m_humidityState.lastGoodMs = millis();
    if (!previousAirValid || !previousHumidityValid) {
        Serial.printf("[SHT31] VALIDITY CHANGE: INVALID -> VALID temperature=%.2f humidity=%.2f good_reads=%u millis=%lu\n",
                      temperature,
                      humidity,
                      m_airTemperatureState.consecutiveGoodReads,
                      static_cast<unsigned long>(millis()));
    }

#ifndef CFG_PRODUCTION
    Serial.print("[SHT31] Temperature: ");
    Serial.print(temperature); Serial.println("C");
    Serial.print("[SHT31] Humidity: ");
    Serial.print(humidity); Serial.println("%");
#endif
    return true;
}

bool Sensors::readWaterTemperature() {
    if (!m_ds18b20Initialized) {
        Serial.println("[DS18B20] DISCONNECTED: not initialized");
        m_waterTemperatureState.enabled = false;
        return false;
    }

    // Non-blocking DS18B20 read: first call starts conversion, next call reads result.
    // At 9-bit resolution conversion takes ~94ms; sensor reads happen every 500ms
    // so the result is always ready on the next cycle.
    if (!m_ds18b20RequestPending) {
        m_ds18b20.requestTemperatures();
        m_ds18b20RequestPending = true;
        m_ds18b20RequestMs = millis();
        return false; // reading not ready yet
    }

    unsigned long elapsed = millis() - m_ds18b20RequestMs;
    if (elapsed < 100) { // 9-bit needs ~94ms; give 100ms margin
        return false; // still converting
    }

    m_ds18b20RequestPending = false;
    float temperature = m_ds18b20.getTempCByIndex(0);
    bool valid = isFiniteFloat(temperature) && temperature >= 0.0f && temperature <= 50.0f && temperature != -127.0f && temperature != 85.0f;
    if (!valid) {
        Serial.println("[DS18B20] DISCONNECTED: Water Temp Invalid");
        m_waterTemperatureState.enabled = true;
        m_waterTemperatureState.valid = false;
        m_waterTemperatureState.value = 0.0f;
        m_waterTemperatureState.consecutiveBadReads++;
        return false;
    }

    m_lastWaterTemperature = temperature;
    m_waterTemperatureState.enabled = true;
    m_waterTemperatureState.valid = true;
    m_waterTemperatureState.value = temperature;
    m_waterTemperatureState.lastGoodMs = millis();
    m_waterTemperatureState.consecutiveBadReads = 0;
#ifndef CFG_PRODUCTION
    Serial.print("[DS18B20] Water Temperature: ");
    Serial.print(temperature); Serial.println("C");
#endif
    return true;
}

bool Sensors::readWaterLevel() {
    if (!m_waterLevelInitialized) {
        Serial.println("[LEVEL] DISCONNECTED: not initialized");
        m_waterLevelState.enabled = false;
        return false;
    }

    float percentage = 0.0f;
    int rawSample = 0;
    int medianRaw = 0;
    float avgAdc = 0.0f;
    float voltage = 0.0f;

    if (strcmp(WATER_LEVEL_SENSOR_TYPE, "analog") == 0) {
        // Pipeline Stage 1 & 2: burst of samples with median + calibrated mV.
        // median/spread/stddev make the reading robust to water surface ripple
        // while letting us detect a floating (disconnected) input.
        WaterLevelSampleSet s = readWaterLevelSamples(WATER_LEVEL_PIN, WATER_LEVEL_ADC_SAMPLES,
                                                      WATER_LEVEL_ADC_SAMPLE_DELAY_US);
        rawSample = s.medianRaw;
        medianRaw = s.medianRaw;
        int spreadRaw = s.spreadRaw;
        uint32_t avgMv = s.avgMv;

        // ADC / input over-range fault: signal at/near the rail. A hardware
        // problem (sensor driven above 3.3V, bad divider, short), not a level.
        if (s.maxRaw >= WATER_LEVEL_SATURATION_RAW || avgMv >= WATER_LEVEL_SAFE_MAX_MV) {
            Serial.printf("[LEVEL] INVALID: ADC saturation raw=%d min=%d max=%d mv=%u "
                          "(signal outside safe range). Keep GPIO%d within 0-3.3V "
                          "(power the analog sensor at 3.3V).\n",
                          medianRaw, s.minRaw, s.maxRaw, avgMv, WATER_LEVEL_PIN);
            markSensorInvalid(m_waterLevelState, medianRaw);
            return false;
        }

        // Stability / possible-disconnect: with no internal pull-down a
        // disconnected/faulty input floats and swings widely. This is detected
        // by instability, NOT by a low-water reading. A healthy resistive
        // sensor (even in an empty tank with surface ripple) passes this check.
        if (spreadRaw > ADC_SPREAD_REJECT_THRESHOLD || s.stddevMv > WATER_LEVEL_UNSTABLE_STDDEV_MV) {
            Serial.printf("[LEVEL] UNSTABLE/disconnected: spread=%d raw stddev=%.1f mV "
                          "(min=%d max=%d) - check water-level sensor wiring.\n",
                          spreadRaw, s.stddevMv, s.minRaw, s.maxRaw);
            markSensorInvalid(m_waterLevelState, medianRaw);
            return false;
        }

        // Pipeline Stage 2 (cont): EMA low-pass filter across reading cycles
        if (m_waterLevelEmaAdc < 0.0f) {
            m_waterLevelEmaAdc = (float)medianRaw;
        } else {
            m_waterLevelEmaAdc = WATER_LEVEL_EMA_ALPHA * (float)medianRaw + (1.0f - WATER_LEVEL_EMA_ALPHA) * m_waterLevelEmaAdc;
        }
        avgAdc = m_waterLevelEmaAdc;
        // Reported voltage uses calibrated millivolts (consistent with EC/pH).
        // The percentage below intentionally stays raw-count based because the
        // EMPTY/FULL calibration points are stored as raw ADC counts.
        voltage = (float)avgMv / 1000.0f;

        // Pipeline Stage 3 & 4: Calibration & Percentage conversion
        float rawPct = ((avgAdc - m_waterLevelEmptyRaw) / (float)(m_waterLevelFullRaw - m_waterLevelEmptyRaw)) * (m_waterLevelFullPercent - m_waterLevelEmptyPercent);
        rawPct += m_waterLevelEmptyPercent;
        rawPct = clampFloat(rawPct, 0.0f, 100.0f);

        // Pipeline Stage 5: Hysteresis / Deadband Filter
        if (m_waterLevelHysteresisPct < 0.0f) {
            m_waterLevelHysteresisPct = rawPct;
        } else {
            float deltaPct = fabsf(rawPct - m_waterLevelHysteresisPct);
            bool crossesSafety = (rawPct <= WATER_LEVEL_MIN && m_waterLevelHysteresisPct > WATER_LEVEL_MIN) ||
                                  (rawPct > WATER_LEVEL_MIN && m_waterLevelHysteresisPct <= WATER_LEVEL_MIN);
            if (deltaPct >= WATER_LEVEL_DEADBAND_PCT || crossesSafety) {
                m_waterLevelHysteresisPct = rawPct;
            }
        }
        percentage = m_waterLevelHysteresisPct;

        // A low/EMPTY figure is a VALID reading here (it only drives the
        // low-water safety lock via WATER_LEVEL_MIN). It is NOT flagged as a
        // disconnected sensor - that distinction is handled above by instability
        // and saturation, not by a low value alone.
        if (s.stddevMv > WATER_LEVEL_UNSTABLE_STRICT_MV) {
            Serial.printf("[LEVEL] WARN: elevated noise stddev=%.1f mV spread=%d; "
                          "reading may fluctuate (resistive sensor). level=%.1f%%\n",
                          s.stddevMv, spreadRaw, percentage);
        }
    } else {
        rawSample = digitalRead(WATER_LEVEL_PIN);
        medianRaw = rawSample;
        avgAdc = (float)rawSample;
        voltage = rawSample == 1 ? 3.3f : 0.0f;
        percentage = rawSample == 1 ? 100.0f : 0.0f;
    }

    percentage = clampFloat(percentage, 0.0f, 100.0f);

    m_lastWaterLevel = percentage;
    m_waterLevelState.enabled = true;
    m_waterLevelState.raw = medianRaw;
    m_waterLevelState.consecutiveBadReads = 0;
    m_waterLevelState.consecutiveGoodReads++;

    if (m_waterLevelState.consecutiveGoodReads < SENSOR_REQUIRED_GOOD_READS) {
        m_waterLevelState.valid = false;
        m_waterLevelState.value = 0.0f;
#ifndef CFG_PRODUCTION
        Serial.printf("[LEVEL] WARMING UP: %d/%d good reads needed (raw=%d median=%d avg=%.1f %.1f%%)\n",
                      m_waterLevelState.consecutiveGoodReads, SENSOR_REQUIRED_GOOD_READS,
                      rawSample, medianRaw, avgAdc, percentage);
#endif
        return false;
    }

    m_waterLevelState.valid = true;
    m_waterLevelState.value = percentage;
    m_waterLevelState.lastGoodMs = millis();

    bool pumpOn = (digitalRead(WATER_PUMP_RELAY_PIN) == PUMP_ACTIVE_LEVEL);
    const char* pumpStateStr = pumpOn ? "ON" : "OFF";
    uint32_t timestampMs = millis();

#ifndef CFG_PRODUCTION
    Serial.printf("[LEVEL] ts=%u ms raw=%d median=%d avg_adc=%.1f volt=%.3fV level_adc=%.1f pct=%.1f%% pump=%s valid=1\n",
                  timestampMs, rawSample, medianRaw, avgAdc, voltage, avgAdc, percentage, pumpStateStr);
#endif
    return true;
}

bool Sensors::readPH() {
    if (!m_phInitialized) {
        Serial.println("[PH] DISCONNECTED: not initialized");
        m_phState.enabled = false;
        return false;
    }

    // Robust burst read using calibrated millivolts (analogReadMilliVolts).
    // medianMv is robust to impulse noise; stddevMv/spreadMv flag a floating
    // or disconnected input that has no pull-down.
    PhSampleSet s = readPhSamples(PH_SENSOR_PIN, PH_ADC_SAMPLES, PH_ADC_SAMPLE_DELAY_MS);
    int raw = s.medianRaw;
    uint32_t mv = s.medianMv;

    // Always log raw ADC diagnostic before any rejection, so we can observe
    // what GPIO5 is actually measuring even when the reading is discarded.
    Serial.printf("[PH RAW] raw_min=%d raw_max=%d raw_avg=%d mv_min=%u mv_max=%u mv_avg=%u\n",
                  s.minRaw, s.maxRaw, raw, s.minMv, s.maxMv, mv);

    // ADC / input over-range fault: calibrated millivolts at/near the 3.3V rail.
    // Uses the calibrated mV from analogReadMilliVolts() (not raw ADC counts)
    // because ESP32-S3 per-chip ADC calibration can map raw=4095 to less than
    // 3.3V in calibrated millivolts — raw counts and mV are NOT proportional.
    if (mv >= PH_ADC_SAFE_MAX_MV) {
        Serial.printf("[PH] INVALID: ADC saturation mv=%u >= %u (raw=%d min=%d max=%d). "
                      "Keep GPIO%d within 0-3.3V with a voltage divider/protection circuit.\n",
                      mv, (unsigned)PH_ADC_SAFE_MAX_MV, raw, s.minRaw, s.maxRaw, PH_SENSOR_PIN);
        markSensorInvalid(m_phState, raw);
        return false;
    }

    // Stability / noise check. A floating/disconnected input (no pull-down)
    // swings wildly; classify it as UNSTABLE rather than misreporting a pH.
    bool unstable = (s.spreadMv > (uint32_t)PH_ADC_UNSTABLE_SPREAD_MV) ||
                    (s.stddevMv > PH_ADC_NOISE_STDDEV_MV) ||
                    (s.spreadRaw > (int)ADC_VALID_MAX_RAW);
    if (unstable) {
        Serial.printf("[PH] UNSTABLE: spread=%u mV (raw %d-%d) stddev=%.1f mV "
                      "(floating/disconnected input? check probe/module wiring)\n",
                      s.spreadMv, s.minRaw, s.maxRaw, s.stddevMv);
        markSensorInvalid(m_phState, raw);
        return false;
    }

    // Recover module-side voltage if a protective divider is installed.
    float divider = (PH_INPUT_DIVIDER_RATIO > 0.0f) ? PH_INPUT_DIVIDER_RATIO : 1.0f;
    float gpioVoltage = (float)mv / 1000.0f;
    float moduleVoltage = gpioVoltage / divider;
    float voltage = moduleVoltage; // pH conversion always uses the module voltage

    // pH conversion: pH = slope * voltage + intercept + offset.
    // slope is in pH/V (negative: voltage falls as pH rises), intercept is the
    // pH at 0V, offset is for a single-point trim. Two-point calibration sets
    // slope/intercept directly from the pH7.00 + pH4.00 buffers.
    float phValue = m_phSlope * voltage + m_phIntercept + m_phOffset;

    // A stable-but-low voltage (like 0.25V) is suspicious for a PH-4502C-style
    // amplified module. Report it as a diagnostic hint WITHOUT declaring the
    // probe disconnected: low voltage alone is not proof of disconnection. The
    // computed pH is still honored below if it falls within the valid range.
    if (moduleVoltage * 1000.0f < (float)PH_LOW_VOLTAGE_WARN_MV) {
        Serial.printf("[PH] WARN: low module voltage %.3fV (gpio=%.3fV raw=%d mv=%u). "
                      "Check wrong Po wiring, Do/To swapped, missing common GND, "
                      "module 5V power, offset trimmer, probe condition, and the "
                      "divider on GPIO%d.\n",
                      moduleVoltage, gpioVoltage, raw, mv, PH_SENSOR_PIN);
    }

    if (!isFiniteFloat(phValue) ||
        phValue < PH_VALID_MIN || phValue > PH_VALID_MAX) {
        Serial.printf("[PH] INVALID: pH=%.2f outside plausible %.1f-%.1f (raw=%d "
                      "voltage=%.3fV mv=%u; calibrate two-point or check probe/wiring)\n",
                      phValue, PH_VALID_MIN, PH_VALID_MAX, raw, voltage, mv);
        markSensorInvalid(m_phState, raw);
        return false;
    }

    // Smoothing: EMA across reading cycles to suppress residual bounce/noise.
    if (!m_phEmaInitialized) {
        m_phEmaValue = phValue;
        m_phEmaInitialized = true;
    } else {
        m_phEmaValue = PH_EMA_ALPHA * phValue + (1.0f - PH_EMA_ALPHA) * m_phEmaValue;
    }
    float smoothedPh = m_phEmaValue;

#ifndef CFG_PRODUCTION
    Serial.printf("[PH] raw=%d mv=%u voltage=%.3fV pH=%.2f (smoothed=%.2f) slope=%.3f intercept=%.3f offset=%.3f spread=%u mv stddev=%.1f\n",
                  raw, mv, voltage, phValue, smoothedPh, m_phSlope, m_phIntercept,
                  m_phOffset, s.spreadMv, s.stddevMv);
#endif

    m_lastPhValue = smoothedPh;
    m_phState.enabled = true;
    m_phState.raw = raw;
    m_phState.consecutiveBadReads = 0;
    m_phState.consecutiveGoodReads++;
    if (m_phState.consecutiveGoodReads < SENSOR_REQUIRED_GOOD_READS) {
        m_phState.valid = false;
        m_phState.value = 0.0f;
#ifndef CFG_PRODUCTION
        Serial.printf("[PH] WARMING UP: %d/%d good reads needed\n",
                      m_phState.consecutiveGoodReads, SENSOR_REQUIRED_GOOD_READS);
#endif
        return false;
    }
    m_phState.valid = true;
    m_phState.value = smoothedPh;
    m_phState.lastGoodMs = millis();
    return true;
}

bool Sensors::readEC() {
    if (!m_ecInitialized) {
        Serial.println("[EC] DISCONNECTED: not initialized");
        m_ecState.enabled = false;
        return false;
    }

    int minRaw = 0;
    int maxRaw = 0;
    uint32_t averageMv = 0;
    int raw = readEcAdc(averageMv, minRaw, maxRaw);
    int spread = maxRaw - minRaw;
    float voltageMv = (float)averageMv;
    float voltage = voltageMv / 1000.0f;

    Serial.printf("[DIAG] EC  | GPIO=%d | raw_min=%d raw_max=%d raw_avg=%d voltage=%.3fV mv=%u spread=%d\n",
                  EC_SENSOR_PIN, minRaw, maxRaw, raw, voltage, averageMv, spread);

    if (spread > ADC_SPREAD_REJECT_THRESHOLD) {
        Serial.printf("[EC] DISCONNECTED: unstable/floating ADC spread=%d > %d (raw min=%d max=%d)\n",
                      spread, ADC_SPREAD_REJECT_THRESHOLD, minRaw, maxRaw);
        markSensorInvalid(m_ecState, raw);
        return false;
    }

    if (maxRaw >= EC_ADC_SATURATION_RAW || averageMv >= EC_ADC_SAFE_MAX_MV) {
        Serial.printf("[EC] INVALID: ADC over-range raw=%d min=%d max=%d voltage=%u mV; Gravity A output must stay <=3.3V on GPIO%d\n",
                      raw, minRaw, maxRaw, averageMv, EC_SENSOR_PIN);
        markSensorInvalid(m_ecState, raw);
        return false;
    }

    float rawEc = ecRawMsPerCmFromMillivolts(voltageMv);
    float uncompensatedEc = rawEc * m_ecActiveKValue;
    if (uncompensatedEc > EC_RANGE_HIGH_THRESHOLD) {
        m_ecActiveKValue = m_ecKValueHigh;
    } else if (uncompensatedEc < EC_RANGE_LOW_THRESHOLD) {
        m_ecActiveKValue = m_ecKValueLow;
    }

    uncompensatedEc = rawEc * m_ecActiveKValue;
    float tempC = currentEcTemperatureC();
    float compensation = ecTemperatureCompensation(tempC);
    float ecValue = uncompensatedEc / compensation;

    if (!isFiniteFloat(ecValue) || ecValue < 0.0f || ecValue > EC_VALID_MAX) {
        Serial.printf("[EC] INVALID: EC=%.2f outside range 0-%.1f mS/cm (raw=%d voltage=%.3fV rawEC=%.3f K=%.3f temp=%.1fC)\n",
                      ecValue, EC_VALID_MAX, raw, voltage, rawEc, m_ecActiveKValue, tempC);
        markSensorInvalid(m_ecState, raw);
        return false;
    }

#ifndef CFG_PRODUCTION
    Serial.printf("[EC] raw=%d voltage=%.3fV raw_ec=%.3f ec=%.2f mS/cm k=%.3f temp=%.1fC spread=%d\n",
                  raw, voltage, rawEc, ecValue, m_ecActiveKValue, tempC, spread);
#endif

    m_lastEcValue = ecValue;
    m_ecState.enabled = true;
    m_ecState.raw = raw;
    m_ecState.consecutiveBadReads = 0;
    m_ecState.consecutiveGoodReads++;
    if (m_ecState.consecutiveGoodReads < SENSOR_REQUIRED_GOOD_READS) {
        m_ecState.valid = false;
        m_ecState.value = 0.0f;
#ifndef CFG_PRODUCTION
        Serial.printf("[EC] WARMING UP: %d/%d good reads needed\n",
                      m_ecState.consecutiveGoodReads, SENSOR_REQUIRED_GOOD_READS);
#endif
        return false;
    }
    m_ecState.valid = true;
    m_ecState.value = ecValue;
    m_ecState.lastGoodMs = millis();
    return true;
}

void Sensors::updateFlowReading() {
    if (!m_flowInitialized) {
        m_flowRate = 0.0f;
        m_flowState.enabled = false;
        return;
    }

    unsigned long now = millis();
    unsigned long elapsedMs = now - m_lastFlowCalculationMs;
    if (elapsedMs < FLOW_SENSOR_MEASUREMENT_WINDOW_MS) {
        return;
    }

    float elapsedSeconds = elapsedMs / 1000.0f;
    uint32_t pulseCount = consumeFlowPulseCount();
    m_lastFlowCalculationMs = now;

    // Less than 2 accepted pulses in the window is reported as NO_FLOW (valid
    // 0 L/min), not a fault. The ISR already rejects sub-ms bounce/glitches;
    // this second gate drops an isolated residual spurious pulse. Note: a
    // genuinely idle sensor AND a disconnected sensor both sit HIGH on the
    // pulled-up line and yield 0 pulses, so NO_FLOW cannot be told apart from
    // a completely dead sensor by the pulse line alone.
    if (pulseCount < 2) {
        m_flowRate = 0.0f;
        m_flowState.enabled = true;
        m_flowState.valid = true;
        m_flowState.value = 0.0f;
        m_flowState.lastGoodMs = now;
#ifndef CFG_PRODUCTION
        Serial.print("[FLOW] Rate: "); Serial.print(m_flowRate, 2); Serial.println(" L/min");
        Serial.print("[FLOW] Total: "); Serial.print(m_totalFlow, 2); Serial.println(" L");
        Serial.println("[FLOW] Status: VALID NO_FLOW (0 L/min)");
#endif
        return;
    }

    float liters = pulseCount / FLOW_SENSOR_PULSES_PER_LITER;
    float flowRate = (liters / elapsedSeconds) * 60.0f;

    if (!isFiniteFloat(flowRate) || flowRate < 0.0f || flowRate > FLOW_SENSOR_MAX_RATE_LPM) {
        m_flowRate = 0.0f;
        m_flowState.enabled = true;
        m_flowState.valid = false;
#ifndef CFG_PRODUCTION
        Serial.println("[FLOW] Calculated flow out of range or invalid");
        Serial.print("[FLOW] Rate: "); Serial.print(m_flowRate, 2); Serial.println(" L/min");
        Serial.print("[FLOW] Total: "); Serial.print(m_totalFlow, 2); Serial.println(" L");
        Serial.println("[FLOW] Status: INVALID (out of range)");
#endif
        return;
    }

    m_totalFlow += liters;
    m_flowRate = flowRate;
    m_flowState.enabled = true;
    m_flowState.valid = true;
    m_flowState.value = flowRate;
    m_flowState.lastGoodMs = now;
#ifndef CFG_PRODUCTION
    Serial.print("[FLOW] Rate: "); Serial.print(m_flowRate, 2); Serial.println(" L/min");
    Serial.print("[FLOW] Total: "); Serial.print(m_totalFlow, 2); Serial.println(" L");
    Serial.println("[FLOW] Status: VALID");
#endif
}

SensorReadings Sensors::read() {
    SensorReadings readings;
    ++m_sequenceNumber;
    readings.sequenceNumber = m_sequenceNumber;

    readSHT31();
    readWaterTemperature();

    if (configFlagEnabled(PH_SENSOR_ENABLED)) {
        readPH();
    } else {
        markSensorDisabled(m_phState);
    }

    if (configFlagEnabled(EC_SENSOR_ENABLED)) {
        readEC();
    } else {
        markSensorDisabled(m_ecState);
    }

    if (configFlagEnabled(FLOW_SENSOR_ENABLED)) {
        updateFlowReading();
    } else {
        m_flowRate = 0.0f;
        markSensorDisabled(m_flowState);
    }

    if (configFlagEnabled(WATER_LEVEL_SENSOR_ENABLED)) {
        readWaterLevel();
    } else {
        m_waterLevelEmaAdc = -1.0f;
        m_waterLevelHysteresisPct = -1.0f;
        markSensorDisabled(m_waterLevelState);
    }

    readings.airTemperatureValid = m_airTemperatureState.valid;
    readings.humidityValid = m_humidityState.valid;
    readings.waterTemperatureValid = m_waterTemperatureState.valid;
    readings.phValid = m_phState.valid;
    readings.ecValid = m_ecState.valid;
    readings.waterLevelValid = m_waterLevelState.valid;
    readings.waterFlowValid = m_flowState.valid;

    readings.airTemperature = m_airTemperatureState.valid ? m_airTemperatureState.value : 0.0f;
    readings.humidity = m_humidityState.valid ? m_humidityState.value : 0.0f;
    readings.waterTemperature = m_waterTemperatureState.valid ? m_waterTemperatureState.value : 0.0f;
    readings.ph = m_phState.valid ? m_phState.value : 0.0f;
    readings.ec = m_ecState.valid ? m_ecState.value : 0.0f;
    readings.waterLevel = m_waterLevelState.valid ? m_waterLevelState.value : 0.0f;
    readings.waterFlow = m_flowState.valid ? m_flowRate : 0.0f;
    readings.flowRate = m_flowState.valid ? m_flowRate : 0.0f;
    readings.totalFlow = m_totalFlow;
    readings.batteryVoltage = 0.0f;
    readings.signalStrength = 0;

    readings.airTemperatureState = m_airTemperatureState;
    readings.humidityState = m_humidityState;
    readings.waterTemperatureState = m_waterTemperatureState;
    readings.phState = m_phState;
    readings.ecState = m_ecState;
    readings.waterLevelState = m_waterLevelState;
    readings.waterFlowState = m_flowState;

    return readings;
}

// Free function wrapper for Diag.cpp (follows simulateFlowPulses pattern)
void diagnosePhPotentiometer(Sensors& sensors) {
    sensors.diagnosePhPotentiometer();
}
