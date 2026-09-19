#ifndef RUNTIME_CONFIG_H
#define RUNTIME_CONFIG_H

#include <Arduino.h>
#include "Config.h"

struct RuntimeConfigValues {
    float temperatureMin;
    float temperatureMax;
    float waterTemperatureMin;
    float waterTemperatureMax;
    float phMin;
    float phMax;
    float ecMin;
    float ecMax;
    float waterFlowMin;
    float waterFlowMax;
    float waterLevelMin;
    float waterLevelMax;
    uint32_t sensorUploadIntervalSeconds;
    uint32_t heartbeatIntervalSeconds;
};

class RuntimeConfig {
public:
    void begin();
    void loadDefaults();
    bool loadFromPreferences();
    bool saveToPreferences() const;
    bool applyPayload(const String& payload, String* error = nullptr);

    const RuntimeConfigValues& values() const { return m_values; }

    float temperatureMin() const { return m_values.temperatureMin; }
    float temperatureMax() const { return m_values.temperatureMax; }
    float waterTemperatureMin() const { return m_values.waterTemperatureMin; }
    float waterTemperatureMax() const { return m_values.waterTemperatureMax; }
    float phMin() const { return m_values.phMin; }
    float phMax() const { return m_values.phMax; }
    float ecMin() const { return m_values.ecMin; }
    float ecMax() const { return m_values.ecMax; }
    float waterFlowMin() const { return m_values.waterFlowMin; }
    float waterFlowMax() const { return m_values.waterFlowMax; }
    float waterLevelMin() const { return m_values.waterLevelMin; }
    float waterLevelMax() const { return m_values.waterLevelMax; }
    uint32_t telemetryIntervalMs() const { return m_values.sensorUploadIntervalSeconds * 1000UL; }
    uint32_t heartbeatIntervalMs() const { return m_values.heartbeatIntervalSeconds * 1000UL; }

private:
    bool parsePayload(const String& payload, RuntimeConfigValues& candidate, String* error) const;
    bool validate(RuntimeConfigValues& candidate, String* error) const;

    RuntimeConfigValues m_values;
};

extern RuntimeConfig g_runtimeConfig;

#endif // RUNTIME_CONFIG_H
