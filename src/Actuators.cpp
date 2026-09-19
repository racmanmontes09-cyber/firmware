#include <Arduino.h>
#include "Actuators.h"
#include "FanAutomation.h"
#include "RuntimeConfig.h"
#include "Logging.h"
#include "QuietSerial.h"

#if SERIAL_READINGS_ONLY
#define Serial SerialQuiet
#endif

namespace {
const char* fanStateName(bool enabled) {
    return enabled ? "ON" : "OFF";
}

const char* gpioLevelName(int level) {
    return level == HIGH ? "HIGH" : "LOW";
}

int fanInactiveLevel() {
    return FAN_ACTIVE_LEVEL == HIGH ? LOW : HIGH;
}

const char* fanReason(const char* reason) {
    return reason ? reason : "UNKNOWN";
}

void logFanTraceContext(const char* reason, const SensorReadings* readings,
                        bool thresholdKnown, float threshold,
                        int previousGpio, int desiredGpio, int verifiedGpio,
                        bool verificationOk) {
    Serial.printf("[FAN] reason=%s temperature=", fanReason(reason));
    if (readings && readings->airTemperatureValid) {
        Serial.printf("%.2f", readings->airTemperature);
    } else {
        Serial.print("N/A");
    }

    Serial.print(" valid=");
    if (readings) {
        Serial.print(readings->airTemperatureValid ? "VALID" : "INVALID");
    } else {
        Serial.print("N/A");
    }

    Serial.print(" threshold=");
    if (thresholdKnown) {
        Serial.printf("%.2f", threshold);
    } else {
        Serial.print("N/A");
    }

    Serial.print(" off_threshold=");
    if (thresholdKnown) {
        Serial.printf("%.2f", threshold - FAN_HYSTERESIS_C);
    } else {
        Serial.print("N/A");
    }

    Serial.printf(" hysteresis=%.2f millis=%lu pin=%d desired_gpio=%s previous_gpio=%s verified_gpio=%s verification=%s active_gpio=%s off_gpio=%s\n",
                  FAN_HYSTERESIS_C,
                  static_cast<unsigned long>(millis()),
                  COOLING_FAN_RELAY_PIN,
                  gpioLevelName(desiredGpio),
                  gpioLevelName(previousGpio),
                  gpioLevelName(verifiedGpio),
                  verificationOk ? "OK" : "FAILED",
                  gpioLevelName(FAN_ACTIVE_LEVEL),
                  gpioLevelName(fanInactiveLevel()));

    if (readings) {
        Serial.printf("[FAN] sensor air_enabled=%s air_bad_reads=%u air_good_reads=%u sequence=%lu\n",
                      readings->airTemperatureState.enabled ? "true" : "false",
                      readings->airTemperatureState.consecutiveBadReads,
                      readings->airTemperatureState.consecutiveGoodReads,
                      static_cast<unsigned long>(readings->sequenceNumber));
    }
}
}

void Actuators::writeFanState(bool enabled, const char* reason, const SensorReadings* readings,
                              bool thresholdKnown, float threshold, bool forceWrite) {
    int desiredGpio = enabled ? FAN_ACTIVE_LEVEL : fanInactiveLevel();
    int previousGpio = digitalRead(COOLING_FAN_RELAY_PIN);
    bool logicalAlreadySet = (m_coolingFan == enabled);
    bool gpioAlreadySet = (previousGpio == desiredGpio);

#if LEAF_FAN_DIAG
    // TEMPORARY FAN RELAY-ISOLATION TEST (see docs/fan-relay-isolation-test.md).
    // Automation state advances exactly as normal (m_coolingFan + hysteresis are
    // unaffected) so the thermostat decision is identical to production, but the
    // physical relay pin is NEVER driven. Reading the floating pin back is
    // intentionally non-authoritative in this mode, hence the logical-only key.
    if (m_fanRelayIsolation) {
        if (!forceWrite && logicalAlreadySet) {
            // No change: trace still emitted every tick so the automation cadence
            // (900ms) and any OTHER ~1s writer are visible.
            logFanTraceContext(reason, readings, thresholdKnown, threshold,
                               previousGpio, desiredGpio, previousGpio, true);
        } else {
            bool prevLogical = m_coolingFan;
            m_coolingFan = enabled;
            Serial.printf("[FAN][ISOLATION] logical %s -> %s gpio %s->%s pin=%d PHYSICAL WRITE SUPPRESSED millis=%lu\n",
                          fanStateName(prevLogical), fanStateName(enabled),
                          gpioLevelName(previousGpio), gpioLevelName(desiredGpio),
                          COOLING_FAN_RELAY_PIN, static_cast<unsigned long>(millis()));
            logFanTraceContext(reason, readings, thresholdKnown, threshold,
                               previousGpio, desiredGpio, previousGpio, true);
        }
        return;
    }
#endif

    if (!forceWrite && logicalAlreadySet && gpioAlreadySet) {
        Serial.printf("[FAN] COMMAND IGNORED: already %s\n", fanStateName(enabled));
        logFanTraceContext(reason, readings, thresholdKnown, threshold,
                           previousGpio, desiredGpio, previousGpio, true);
        return;
    }

    if (logicalAlreadySet && !gpioAlreadySet) {
        Serial.printf("[FAN] GPIO CORRECTION: logical %s previous_gpio=%s -> desired_gpio=%s\n",
                      fanStateName(enabled), gpioLevelName(previousGpio), gpioLevelName(desiredGpio));
    } else if (forceWrite && logicalAlreadySet && gpioAlreadySet) {
        Serial.printf("[FAN] GPIO WRITE: already %s (forced)\n", fanStateName(enabled));
    } else {
        Serial.printf("[FAN] GPIO CHANGE: %s -> %s\n", fanStateName(m_coolingFan), fanStateName(enabled));
    }

    digitalWrite(COOLING_FAN_RELAY_PIN, desiredGpio);
    int verifiedGpio = digitalRead(COOLING_FAN_RELAY_PIN);
    m_coolingFan = enabled;
    bool verificationOk = (verifiedGpio == desiredGpio);
    logFanTraceContext(reason, readings, thresholdKnown, threshold,
                       previousGpio, desiredGpio, verifiedGpio, verificationOk);

    if (!verificationOk) {
        Serial.printf("[FAN] GPIO VERIFY FAILED: desired_state=%s desired_gpio=%s verified_gpio=%s\n",
                      fanStateName(enabled), gpioLevelName(desiredGpio), gpioLevelName(verifiedGpio));
    }
}

void Actuators::fanOn(const char* reason, const SensorReadings* readings,
                      bool thresholdKnown, float threshold, bool forceWrite) {
    writeFanState(true, reason, readings, thresholdKnown, threshold, forceWrite);
}

void Actuators::fanOff(const char* reason, const SensorReadings* readings,
                       bool thresholdKnown, float threshold, bool forceWrite) {
    writeFanState(false, reason, readings, thresholdKnown, threshold, forceWrite);
}

void Actuators::waterPumpOn() {
    digitalWrite(WATER_PUMP_RELAY_PIN, PUMP_ACTIVE_LEVEL);
    m_waterPump = true;
    Serial.println("[PUMP] ON");
}

void Actuators::waterPumpOff() {
    digitalWrite(WATER_PUMP_RELAY_PIN, PUMP_ACTIVE_LEVEL == HIGH ? LOW : HIGH);
    m_waterPump = false;
    Serial.println("[PUMP] OFF");
}

void Actuators::nutrientPumpAOn() {
    digitalWrite(NUTRIENT_A_RELAY_PIN, NUTRIENT_ACTIVE_LEVEL);
    m_nutrientPumpA = true;
    Serial.println("[NUTRIENT A] ON");
}

void Actuators::nutrientPumpAOff() {
    digitalWrite(NUTRIENT_A_RELAY_PIN, NUTRIENT_ACTIVE_LEVEL == HIGH ? LOW : HIGH);
    m_nutrientPumpA = false;
    Serial.println("[NUTRIENT A] OFF");
}

void Actuators::nutrientPumpBOn() {
    digitalWrite(NUTRIENT_B_RELAY_PIN, NUTRIENT_ACTIVE_LEVEL);
    m_nutrientPumpB = true;
    Serial.println("[NUTRIENT B] ON");
}

void Actuators::nutrientPumpBOff() {
    digitalWrite(NUTRIENT_B_RELAY_PIN, NUTRIENT_ACTIVE_LEVEL == HIGH ? LOW : HIGH);
    m_nutrientPumpB = false;
    Serial.println("[NUTRIENT B] OFF");
}

void Actuators::phUpPumpOn() {
    digitalWrite(PH_UP_RELAY_PIN, PH_ACTIVE_LEVEL);
    m_phUpPump = true;
    Serial.println("[PH UP] ON");
}

void Actuators::phUpPumpOff() {
    digitalWrite(PH_UP_RELAY_PIN, PH_ACTIVE_LEVEL == HIGH ? LOW : HIGH);
    m_phUpPump = false;
    Serial.println("[PH UP] OFF");
}

void Actuators::phDownPumpOn() {
    digitalWrite(PH_DOWN_RELAY_PIN, PH_ACTIVE_LEVEL);
    m_phDownPump = true;
    Serial.println("[PH DOWN] ON");
}

void Actuators::phDownPumpOff() {
    digitalWrite(PH_DOWN_RELAY_PIN, PH_ACTIVE_LEVEL == HIGH ? LOW : HIGH);
    m_phDownPump = false;
    Serial.println("[PH DOWN] OFF");
}

void Actuators::airPumpOn() {
    digitalWrite(AIR_PUMP_RELAY_PIN, PUMP_ACTIVE_LEVEL);
    m_airPump = true;
    Serial.println("[AIR PUMP] ON");
}

void Actuators::airPumpOff() {
    digitalWrite(AIR_PUMP_RELAY_PIN, PUMP_ACTIVE_LEVEL == HIGH ? LOW : HIGH);
    m_airPump = false;
    Serial.println("[AIR PUMP] OFF");
}

void Actuators::begin() {
    pinMode(COOLING_FAN_RELAY_PIN, OUTPUT);
    Serial.printf("[FAN] Polarity: GPIO%d ON=%s OFF=%s active_level=%s\n",
                  COOLING_FAN_RELAY_PIN,
                  gpioLevelName(FAN_ACTIVE_LEVEL),
                  gpioLevelName(fanInactiveLevel()),
                  gpioLevelName(FAN_ACTIVE_LEVEL));
    fanOff("STARTUP_INIT", nullptr, false, 0.0f, true);
    pinMode(WATER_PUMP_RELAY_PIN, OUTPUT);
    digitalWrite(WATER_PUMP_RELAY_PIN, PUMP_ACTIVE_LEVEL == HIGH ? LOW : HIGH);
    m_waterPump = false;
    pinMode(AIR_PUMP_RELAY_PIN, OUTPUT);
    digitalWrite(AIR_PUMP_RELAY_PIN, PUMP_ACTIVE_LEVEL == HIGH ? LOW : HIGH);
    m_airPump = false;
    pinMode(NUTRIENT_A_RELAY_PIN, OUTPUT);
    digitalWrite(NUTRIENT_A_RELAY_PIN, NUTRIENT_ACTIVE_LEVEL == HIGH ? LOW : HIGH);
    m_nutrientPumpA = false;
    pinMode(NUTRIENT_B_RELAY_PIN, OUTPUT);
    digitalWrite(NUTRIENT_B_RELAY_PIN, NUTRIENT_ACTIVE_LEVEL == HIGH ? LOW : HIGH);
    m_nutrientPumpB = false;
    pinMode(PH_UP_RELAY_PIN, OUTPUT);
    digitalWrite(PH_UP_RELAY_PIN, PH_ACTIVE_LEVEL == HIGH ? LOW : HIGH);
    m_phUpPump = false;
    pinMode(PH_DOWN_RELAY_PIN, OUTPUT);
    digitalWrite(PH_DOWN_RELAY_PIN, PH_ACTIVE_LEVEL == HIGH ? LOW : HIGH);
    m_phDownPump = false;
    pinMode(LED_STATUS_PIN, OUTPUT);
    digitalWrite(LED_STATUS_PIN, LOW);
    Serial.println("[Actuator] Outputs initialized");
}

void Actuators::disableAllOutputs() {
    waterPumpOff();
    airPumpOff();
    nutrientPumpAOff();
    nutrientPumpBOff();
    phUpPumpOff();
    phDownPumpOff();
    m_nutrientDoseActive = false;
    m_nutrientMixActive = false;
    m_phDoseActive = false;
    m_phMixActive = false;
    LOG_INFO("[SAFETY] All fluid actuators disabled");
}

bool Actuators::setState(const String& commandName, bool enabled, const char* reason) {
    if (commandName == "cooling_fan"
        || commandName == "water_pump"
        || commandName == "nutrient_pump_a" || commandName == "nutrient_a"
        || commandName == "nutrient_pump_b" || commandName == "nutrient_b"
        || commandName == "ph_up_pump" || commandName == "ph_up"
        || commandName == "ph_down_pump" || commandName == "ph_down") {
        return applyCommand(commandName, enabled, reason);
    }

    return false;
}

bool Actuators::applyCommand(const String& commandName, bool enabled, const char* reason) {
    LOG_DEBUG("[Command] %s %s reason=%s", commandName.c_str(), enabled ? "ON" : "OFF", fanReason(reason));

    // Per-actuator safety for remote commands
    if (enabled) {
        if (commandName == "water_pump" || commandName == "nutrient_a" || commandName == "nutrient_pump_a"
            || commandName == "nutrient_b" || commandName == "nutrient_pump_b"
            || commandName == "ph_up" || commandName == "ph_up_pump"
            || commandName == "ph_down" || commandName == "ph_down_pump") {
            // All fluid actuators require valid water level
            if (!m_criticalSensorsValid) {
                LOG_WARN("[SAFETY] Command blocked: critical sensor invalid");
                disableAllOutputs();
                return false;
            }
            if (m_lowWater || m_lastWaterLevel < WATER_LEVEL_MIN) {
                LOG_WARN("[SAFETY] Command blocked: low water");
                disableAllOutputs();
                return false;
            }
        }

        // pH pumps additionally require valid pH
        if ((commandName == "ph_up" || commandName == "ph_up_pump"
             || commandName == "ph_down" || commandName == "ph_down_pump")) {
            // pH safety already covered by criticalSensorsValid above
        }

        // Nutrient pumps additionally require valid EC
        if (commandName == "nutrient_a" || commandName == "nutrient_pump_a"
            || commandName == "nutrient_b" || commandName == "nutrient_pump_b") {
            // EC safety already covered by criticalSensorsValid above
        }
    }

    if (commandName == "cooling_fan") {
        if (enabled) {
            fanOn(reason, nullptr, false, 0.0f);
        } else {
            fanOff(reason, nullptr, false, 0.0f);
        }
        return true;
    } else if (commandName == "water_pump") {
        if (enabled) {
            waterPumpOn();
        } else {
            waterPumpOff();
        }
        return true;
    } else if (commandName == "nutrient_a" || commandName == "nutrient_pump_a") {
        if (enabled) {
            nutrientPumpAOn();
            nutrientPumpBOff();
        } else {
            nutrientPumpAOff();
        }
        return true;
    } else if (commandName == "nutrient_b" || commandName == "nutrient_pump_b") {
        if (enabled) {
            nutrientPumpBOn();
            nutrientPumpAOff();
        } else {
            nutrientPumpBOff();
        }
        return true;
    } else if (commandName == "ph_up" || commandName == "ph_up_pump") {
        if (enabled) {
            phUpPumpOn();
            phDownPumpOff();
        } else {
            phUpPumpOff();
        }
        return true;
    } else if (commandName == "ph_down" || commandName == "ph_down_pump") {
        if (enabled) {
            phDownPumpOn();
            phUpPumpOff();
        } else {
            phDownPumpOff();
        }
        return true;
    }

    return false;
}

void Actuators::updateAutoControl(float airTemperature, float waterLevel, float ph, float ec) {
    SensorReadings fallback;
    fallback.airTemperatureValid = true;
    fallback.waterLevelValid = true;
    fallback.phValid = true;
    fallback.ecValid = true;
    fallback.airTemperature = airTemperature;
    fallback.waterLevel = waterLevel;
    fallback.ph = ph;
    fallback.ec = ec;
    fallback.airTemperatureState.valid = true;
    fallback.airTemperatureState.value = airTemperature;
    fallback.waterLevelState.valid = true;
    fallback.waterLevelState.value = waterLevel;
    fallback.phState.valid = true;
    fallback.phState.value = ph;
    fallback.ecState.valid = true;
    fallback.ecState.value = ec;
    updateAutoControl(fallback);
}

void Actuators::updateAutoControl(const SensorReadings& readings) {
#if DEBUG_NO_AUTOMATION
    LOG_WARN("[DEBUG] Automation disabled (DEBUG_NO_AUTOMATION=1)");
    // Still track sensor validity for safety lock
    m_criticalSensorsValid = readings.waterLevelValid && readings.phValid && readings.ecValid;
    m_lastWaterLevel = readings.waterLevel;
    m_lowWater = readings.waterLevelValid ? (readings.waterLevel < WATER_LEVEL_MIN) : true;
    if (!readings.waterLevelValid || m_lowWater) {
        waterPumpOff();
        nutrientPumpAOff();
        nutrientPumpBOff();
        phUpPumpOff();
        phDownPumpOff();
    }
    return;
#endif

    // Fan: only requires valid air temperature (independent of fluid safety)
    float airTemperatureMaximum = g_runtimeConfig.temperatureMax();
    float fanOffThreshold = airTemperatureMaximum - FAN_HYSTERESIS_C;
    if (!readings.airTemperatureValid) {
        bool wasCoolingFanOn = m_coolingFan;
        fanOff("AUTO_TEMP_INVALID", &readings, true, airTemperatureMaximum);
        if (wasCoolingFanOn) {
            LOG_WARN("[SAFETY] Air temp sensor invalid; fan forced OFF");
        }
    } else {
        bool wasCoolingFanOn = m_coolingFan;
        bool shouldRunFan = automation::coolingFanShouldRun(readings.airTemperature, airTemperatureMaximum, m_coolingFan);
        const char* decision = shouldRunFan ? (m_coolingFan ? "KEEP_ON" : "TURN_ON")
                                            : (m_coolingFan ? "TURN_OFF" : "KEEP_OFF");
        LOG_DEBUG("[AUTOMATION] fan decision=%s air_temperature=%.2f valid=%s on_threshold=%.2f off_threshold=%.2f hysteresis=%.2f fan_state=%s",
                  decision,
                  readings.airTemperature,
                  readings.airTemperatureValid ? "VALID" : "INVALID",
                  airTemperatureMaximum,
                  fanOffThreshold,
                  FAN_HYSTERESIS_C,
                  m_coolingFan ? "ON" : "OFF");
        if (shouldRunFan) {
            fanOn("AUTO_TEMP", &readings, true, airTemperatureMaximum);
        } else {
            fanOff("AUTO_TEMP", &readings, true, airTemperatureMaximum);
        }
        if (shouldRunFan && !wasCoolingFanOn) {
            LOG_INFO("[FAN] Auto ON: air_temperature=%.1fC >= air_temperature_max=%.1fC", readings.airTemperature, airTemperatureMaximum);
        } else if (!shouldRunFan && wasCoolingFanOn) {
            LOG_INFO("[FAN] Auto OFF: air_temperature=%.1fC <= fan_off_threshold=%.1fC", readings.airTemperature, fanOffThreshold);
        }
    }

    // Track individual sensor validity for per-actuator safety
    m_criticalSensorsValid = readings.waterLevelValid && readings.phValid && readings.ecValid;
    bool waterLevelValid = readings.waterLevelValid;
    bool phValid = readings.phValid;
    bool ecValid = readings.ecValid;

    m_lastWaterLevel = readings.waterLevel;
    if (waterLevelValid) {
        if (!m_lowWater && readings.waterLevel < WATER_LEVEL_MIN) {
            m_lowWater = true;
            LOG_WARN("[SAFETY] Water level low (%.1f%% < %.1f%%) - fluid actuators locked",
                     readings.waterLevel, WATER_LEVEL_MIN);
        } else if (m_lowWater && readings.waterLevel >= (WATER_LEVEL_MIN + WATER_LEVEL_RECOVERY_MARGIN_PCT)) {
            m_lowWater = false;
            LOG_INFO("[SAFETY] Water level recovered (%.1f%% >= %.1f%%) - fluid actuators unlocked",
                     readings.waterLevel, WATER_LEVEL_MIN + WATER_LEVEL_RECOVERY_MARGIN_PCT);
        }
    } else {
        m_lowWater = true;
    }

    // Water level critically low or unknown: lock ALL fluid actuators
    if (!waterLevelValid || m_lowWater) {
        if (!waterLevelValid) {
            LOG_WARN("[SAFETY] Water level invalid - fluid actuators locked");
        } else {
            LOG_INFO("[SAFETY] Low water - fluid actuators locked");
        }
        disableAllOutputs();
        return;
    }

    // Always service active nutrient dose timer so nutrient pumps turn off cleanly
    if (m_nutrientDoseActive) {
        if (millis() - m_nutrientDoseStartMs >= EC_DOSING_TIME) {
            nutrientPumpAOff();
            nutrientPumpBOff();
            m_nutrientDoseActive = false;
            m_nutrientMixActive = true;
            m_nutrientMixStartMs = millis();
            LOG_DEBUG("[AUTO] Nutrient mixing");
        }
    } else if (m_nutrientMixActive) {
        if (millis() - m_nutrientMixStartMs >= MIXING_DELAY) {
            m_nutrientMixActive = false;
        }
    }

    // pH dosing: requires valid pH and water level
    if (m_phDoseActive) {
        if (millis() - m_phDoseStartMs >= PH_DOSING_TIME) {
            phUpPumpOff();
            phDownPumpOff();
            m_phDoseActive = false;
            m_phMixActive = true;
            m_phMixStartMs = millis();
            LOG_DEBUG("[AUTO] pH mixing");
        }
    } else if (m_phMixActive) {
        if (millis() - m_phMixStartMs >= MIXING_DELAY) {
            m_phMixActive = false;
        }
    } else if (phValid && readings.ph > g_runtimeConfig.phMax()) {
        if (m_phAttempts < MAX_PH_DOSING_ATTEMPTS) {
            LOG_INFO("[AUTO] pH high - dosing pH down (attempt %d/%d)", m_phAttempts + 1, MAX_PH_DOSING_ATTEMPTS);
            phDownPumpOn();
            phUpPumpOff();
            m_phDoseActive = true;
            m_phDoseStartMs = millis();
            m_phAttempts++;
        } else {
            LOG_WARN("[AUTO] pH high - max dosing attempts reached (%d)", MAX_PH_DOSING_ATTEMPTS);
            phUpPumpOff();
            phDownPumpOff();
        }
    } else if (phValid && readings.ph < g_runtimeConfig.phMin()) {
        if (m_phAttempts < MAX_PH_DOSING_ATTEMPTS) {
            LOG_INFO("[AUTO] pH low - dosing pH up (attempt %d/%d)", m_phAttempts + 1, MAX_PH_DOSING_ATTEMPTS);
            phUpPumpOn();
            phDownPumpOff();
            m_phDoseActive = true;
            m_phDoseStartMs = millis();
            m_phAttempts++;
        } else {
            LOG_WARN("[AUTO] pH low - max dosing attempts reached (%d)", MAX_PH_DOSING_ATTEMPTS);
            phUpPumpOff();
            phDownPumpOff();
        }
    } else if (phValid && readings.ph >= g_runtimeConfig.phMin() && readings.ph <= g_runtimeConfig.phMax()) {
        phUpPumpOff();
        phDownPumpOff();
        m_phDoseActive = false;
        m_phMixActive = false;
        m_phAttempts = 0;
    } else {
        phUpPumpOff();
        phDownPumpOff();
        m_phDoseActive = false;
        m_phMixActive = false;
    }

    // Interlock: do not start a new nutrient dose while pH dosing/mixing is active
    if (m_phDoseActive || m_phMixActive) {
        return;
    }

    // Evaluate new nutrient dosing only if not already active or mixing
    if (!m_nutrientDoseActive && !m_nutrientMixActive) {
        if (ecValid && readings.ec < g_runtimeConfig.ecMin()) {
            if (m_nutrientAttempts < MAX_DOSING_ATTEMPTS) {
                if (m_doseNutrientBNext) {
                    LOG_INFO("[AUTO] EC low - dosing nutrient B (attempt %d/%d)", m_nutrientAttempts + 1, MAX_DOSING_ATTEMPTS);
                    nutrientPumpAOff();
                    nutrientPumpBOn();
                    m_doseNutrientBNext = false;
                } else {
                    LOG_INFO("[AUTO] EC low - dosing nutrient A (attempt %d/%d)", m_nutrientAttempts + 1, MAX_DOSING_ATTEMPTS);
                    nutrientPumpBOff();
                    nutrientPumpAOn();
                    m_doseNutrientBNext = true;
                }
                m_nutrientDoseActive = true;
                m_nutrientDoseStartMs = millis();
                m_nutrientAttempts++;
            } else {
                LOG_WARN("[AUTO] EC low - max dosing attempts reached (%d)", MAX_DOSING_ATTEMPTS);
            }
        } else if (ecValid && readings.ec >= g_runtimeConfig.ecMin() && readings.ec <= g_runtimeConfig.ecMax()) {
            nutrientPumpAOff();
            nutrientPumpBOff();
            m_nutrientDoseActive = false;
            m_nutrientMixActive = false;
            m_nutrientAttempts = 0;
        } else {
            nutrientPumpAOff();
            nutrientPumpBOff();
            m_nutrientDoseActive = false;
            m_nutrientMixActive = false;
        }
    }
}
