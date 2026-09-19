#ifndef ACTUATORS_H
#define ACTUATORS_H

#include <Arduino.h>
#include "Config.h"
#include "Sensors.h"

class Actuators {
public:
    void begin();
    void disableAllOutputs();
    bool setState(const String& commandName, bool enabled, const char* reason = "COMMAND");
    bool applyCommand(const String& commandName, bool enabled, const char* reason = "COMMAND");
    void updateAutoControl(float airTemperature, float waterLevel, float ph, float ec);
    void updateAutoControl(const SensorReadings& readings);
    bool isCoolingFanOn() const { return m_coolingFan; }
    bool isWaterPumpOn() const { return m_waterPump; }
    bool isNutrientPumpAOn() const { return m_nutrientPumpA; }
    bool isNutrientPumpBOn() const { return m_nutrientPumpB; }
    bool isPhUpPumpOn() const { return m_phUpPump; }
    bool isPhDownPumpOn() const { return m_phDownPump; }

#if LEAF_FAN_DIAG
    // TEMPORARY fan relay-isolation test (see docs/fan-relay-isolation-test.md).
    void setFanRelayIsolation(bool enabled) { m_fanRelayIsolation = enabled; }
    bool fanRelayIsolation() const { return m_fanRelayIsolation; }
#endif

private:
    void writeFanState(bool enabled, const char* reason, const SensorReadings* readings,
                       bool thresholdKnown, float threshold, bool forceWrite = false);
    void fanOn(const char* reason = "DIRECT", const SensorReadings* readings = nullptr,
               bool thresholdKnown = false, float threshold = 0.0f, bool forceWrite = false);
    void fanOff(const char* reason = "DIRECT", const SensorReadings* readings = nullptr,
                bool thresholdKnown = false, float threshold = 0.0f, bool forceWrite = false);
    void waterPumpOn();
    void waterPumpOff();
    void nutrientPumpAOn();
    void nutrientPumpAOff();
    void nutrientPumpBOn();
    void nutrientPumpBOff();
    void phUpPumpOn();
    void phUpPumpOff();
    void phDownPumpOn();
    void phDownPumpOff();
    void airPumpOn();
    void airPumpOff();

    bool m_coolingFan = false;
    bool m_waterPump = false;
    bool m_airPump = false;
    float m_lastWaterLevel = 0.0f;
    bool m_criticalSensorsValid = false;
    bool m_lowWater = true;
    bool m_nutrientPumpA = false;
    bool m_nutrientPumpB = false;
    bool m_phUpPump = false;
    bool m_phDownPump = false;
    unsigned long m_nutrientDoseStartMs = 0;
    unsigned long m_nutrientMixStartMs = 0;
    unsigned long m_phDoseStartMs = 0;
    unsigned long m_phMixStartMs = 0;
    bool m_nutrientDoseActive = false;
    bool m_nutrientMixActive = false;
    bool m_phDoseActive = false;
    bool m_phMixActive = false;
    uint8_t m_nutrientAttempts = 0;
    uint8_t m_phAttempts = 0;
    bool m_doseNutrientBNext = false;

#if LEAF_FAN_DIAG
    bool m_fanRelayIsolation = true;
#endif
};

#endif
