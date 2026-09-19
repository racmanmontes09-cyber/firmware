#include "RuntimeConfig.h"

#include <Preferences.h>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "Logging.h"

namespace {
constexpr char kPreferencesNamespace[] = "leaf_runtime";
constexpr uint32_t kSchemaVersion = 1;
constexpr uint32_t kMinIntervalSeconds = 1;
constexpr uint32_t kMaxIntervalSeconds = 3600;
constexpr float kAirTemperatureMin = -20.0f;
constexpr float kAirTemperatureMax = 80.0f;
constexpr float kWaterTemperatureMin = 0.0f;
constexpr float kWaterTemperatureMax = 50.0f;
constexpr float kEcMin = 0.0f;
constexpr float kEcMax = 20.0f;
constexpr float kWaterPercentMin = 0.0f;
constexpr float kWaterPercentMax = 100.0f;
constexpr float kFlowMin = 0.0f;
constexpr float kFlowMax = 100.0f;

void setError(String* error, const char* message) {
    if (error) *error = String(message);
}

void setFieldError(String* error, const char* prefix, const char* field) {
    if (error) {
        *error = String(prefix) + String(field);
    }
}

bool finiteNumber(double value) {
    return !isnan(value) && !isinf(value);
}

bool extractStringField(const String& text, const char* fieldName, String& value) {
    char token[64];
    snprintf(token, sizeof(token), "\"%s\"", fieldName);
    const char* start = strstr(text.c_str(), token);
    if (!start) return false;

    const char* colon = strchr(start + strlen(token), ':');
    if (!colon) return false;

    const char* cursor = colon + 1;
    while (*cursor && isspace(static_cast<unsigned char>(*cursor))) ++cursor;
    if (*cursor != '"') return false;
    ++cursor;

    const char* end = strchr(cursor, '"');
    if (!end) return false;

    value = String(cursor).substring(0, end - cursor);
    return true;
}

bool extractNumberField(const String& text, const char* fieldName, double& value) {
    char token[64];
    snprintf(token, sizeof(token), "\"%s\"", fieldName);
    const char* start = strstr(text.c_str(), token);
    if (!start) return false;

    const char* colon = strchr(start + strlen(token), ':');
    if (!colon) return false;

    const char* cursor = colon + 1;
    while (*cursor && isspace(static_cast<unsigned char>(*cursor))) ++cursor;
    if (*cursor == '\0' || *cursor == '"' || *cursor == '{' || *cursor == '[') return false;
    if (strncmp(cursor, "null", 4) == 0 || strncmp(cursor, "true", 4) == 0 || strncmp(cursor, "false", 5) == 0) return false;

    errno = 0;
    char* end = nullptr;
    double parsed = strtod(cursor, &end);
    if (end == cursor || errno == ERANGE || !finiteNumber(parsed)) return false;

    while (*end && isspace(static_cast<unsigned char>(*end))) ++end;
    if (*end != ',' && *end != '}' && *end != '\0') return false;

    value = parsed;
    return true;
}

bool parseFloatField(const String& payload, const char* fieldName, float& target, String* error) {
    double parsed = 0.0;
    if (!extractNumberField(payload, fieldName, parsed)) {
        setFieldError(error, "missing or invalid numeric field: ", fieldName);
        return false;
    }

    target = static_cast<float>(parsed);
    return true;
}

bool parseUIntField(const String& payload, const char* fieldName, uint32_t& target, String* error) {
    double parsed = 0.0;
    if (!extractNumberField(payload, fieldName, parsed)) {
        setFieldError(error, "missing or invalid integer field: ", fieldName);
        return false;
    }

    if (parsed < 0.0 || floor(parsed) != parsed || parsed > 4294967295.0) {
        setFieldError(error, "non-integer or negative field: ", fieldName);
        return false;
    }

    target = static_cast<uint32_t>(parsed);
    return true;
}

bool requireRange(const char* fieldName, float value, float minValue, float maxValue, String* error) {
    if (isnan(value) || isinf(value) || value < minValue || value > maxValue) {
        if (error) {
            *error = String(fieldName) + String(" out of range");
        }
        return false;
    }
    return true;
}

bool requireOrdered(const char* minName, float minValue, const char* maxName, float maxValue, String* error) {
    if (minValue >= maxValue) {
        if (error) {
            *error = String(minName) + String(" must be less than ") + String(maxName);
        }
        return false;
    }
    return true;
}

uint32_t compiledIntervalSeconds(uint32_t intervalMs) {
    uint32_t seconds = intervalMs / 1000UL;
    return seconds < kMinIntervalSeconds ? kMinIntervalSeconds : seconds;
}
}

RuntimeConfig g_runtimeConfig;

void RuntimeConfig::begin() {
    loadDefaults();
    if (loadFromPreferences()) {
        LOG_INFO("[CFG] Loaded runtime configuration from Preferences");
    } else {
        LOG_INFO("[CFG] Using compiled runtime configuration defaults");
    }
    LOG_INFO("[CFG] Active air_temperature_max=%.1f°C", temperatureMax());
}

void RuntimeConfig::loadDefaults() {
    m_values.temperatureMin = 18.0f;
    m_values.temperatureMax = 25.0f;
    m_values.waterTemperatureMin = 18.0f;
    m_values.waterTemperatureMax = 24.0f;
    m_values.phMin = TARGET_PH_MIN;
    m_values.phMax = TARGET_PH_MAX;
    m_values.ecMin = TARGET_EC_MIN;
    m_values.ecMax = TARGET_EC_MAX;
    m_values.waterFlowMin = 0.5f;
    m_values.waterFlowMax = 2.0f;
    m_values.waterLevelMin = WATER_LEVEL_MIN;
    m_values.waterLevelMax = 80.0f;
    m_values.sensorUploadIntervalSeconds = compiledIntervalSeconds(TELEMETRY_INTERVAL_MS);
    m_values.heartbeatIntervalSeconds = compiledIntervalSeconds(MQTT_STATUS_INTERVAL_MS);
}

bool RuntimeConfig::loadFromPreferences() {
    Preferences preferences;
    if (!preferences.begin(kPreferencesNamespace, true)) {
        LOG_WARN("[CFG] Preferences open failed for runtime config load");
        return false;
    }

    const char* requiredKeys[] = {
        "schema", "air_min", "air_max", "wtemp_min", "wtemp_max", "ph_min", "ph_max",
        "ec_min", "ec_max", "flow_min", "flow_max", "level_min", "level_max",
        "upload_s", "heart_s"
    };

    for (const char* key : requiredKeys) {
        if (!preferences.isKey(key)) {
            preferences.end();
            return false;
        }
    }

    if (preferences.getUInt("schema", 0) != kSchemaVersion) {
        preferences.end();
        return false;
    }

    RuntimeConfigValues candidate;
    candidate.temperatureMin = preferences.getFloat("air_min", m_values.temperatureMin);
    candidate.temperatureMax = preferences.getFloat("air_max", m_values.temperatureMax);
    candidate.waterTemperatureMin = preferences.getFloat("wtemp_min", m_values.waterTemperatureMin);
    candidate.waterTemperatureMax = preferences.getFloat("wtemp_max", m_values.waterTemperatureMax);
    candidate.phMin = preferences.getFloat("ph_min", m_values.phMin);
    candidate.phMax = preferences.getFloat("ph_max", m_values.phMax);
    candidate.ecMin = preferences.getFloat("ec_min", m_values.ecMin);
    candidate.ecMax = preferences.getFloat("ec_max", m_values.ecMax);
    candidate.waterFlowMin = preferences.getFloat("flow_min", m_values.waterFlowMin);
    candidate.waterFlowMax = preferences.getFloat("flow_max", m_values.waterFlowMax);
    candidate.waterLevelMin = preferences.getFloat("level_min", m_values.waterLevelMin);
    candidate.waterLevelMax = preferences.getFloat("level_max", m_values.waterLevelMax);
    candidate.sensorUploadIntervalSeconds = preferences.getUInt("upload_s", m_values.sensorUploadIntervalSeconds);
    candidate.heartbeatIntervalSeconds = preferences.getUInt("heart_s", m_values.heartbeatIntervalSeconds);
    preferences.end();

    String error;
    if (!validate(candidate, &error)) {
        LOG_WARN("[CFG] Stored runtime config rejected: %s", error.c_str());
        return false;
    }

    m_values = candidate;
    return true;
}

bool RuntimeConfig::saveToPreferences() const {
    Preferences preferences;
    if (!preferences.begin(kPreferencesNamespace, false)) {
        LOG_WARN("[CFG] Preferences open failed for runtime config save");
        return false;
    }

    bool ok = true;
    ok = preferences.putUInt("schema", kSchemaVersion) > 0 && ok;
    ok = preferences.putFloat("air_min", m_values.temperatureMin) > 0 && ok;
    ok = preferences.putFloat("air_max", m_values.temperatureMax) > 0 && ok;
    ok = preferences.putFloat("wtemp_min", m_values.waterTemperatureMin) > 0 && ok;
    ok = preferences.putFloat("wtemp_max", m_values.waterTemperatureMax) > 0 && ok;
    ok = preferences.putFloat("ph_min", m_values.phMin) > 0 && ok;
    ok = preferences.putFloat("ph_max", m_values.phMax) > 0 && ok;
    ok = preferences.putFloat("ec_min", m_values.ecMin) > 0 && ok;
    ok = preferences.putFloat("ec_max", m_values.ecMax) > 0 && ok;
    ok = preferences.putFloat("flow_min", m_values.waterFlowMin) > 0 && ok;
    ok = preferences.putFloat("flow_max", m_values.waterFlowMax) > 0 && ok;
    ok = preferences.putFloat("level_min", m_values.waterLevelMin) > 0 && ok;
    ok = preferences.putFloat("level_max", m_values.waterLevelMax) > 0 && ok;
    ok = preferences.putUInt("upload_s", m_values.sensorUploadIntervalSeconds) > 0 && ok;
    ok = preferences.putUInt("heart_s", m_values.heartbeatIntervalSeconds) > 0 && ok;
    preferences.end();

    return ok;
}

bool RuntimeConfig::applyPayload(const String& payload, String* error) {
    RuntimeConfigValues candidate;
    if (!parsePayload(payload, candidate, error)) {
        return false;
    }

    if (!validate(candidate, error)) {
        return false;
    }

    m_values = candidate;
    bool persisted = saveToPreferences();
    if (!persisted) {
        LOG_WARN("[CFG] Runtime configuration applied but Preferences save failed");
    }

    LOG_INFO("[CFG] Runtime configuration applied: air_temperature_max=%.2f ph=%.2f-%.2f ec=%.2f-%.2f level_min=%.2f telemetry=%lus heartbeat=%lus",
             temperatureMax(), phMin(), phMax(), ecMin(), ecMax(), waterLevelMin(),
             static_cast<unsigned long>(m_values.sensorUploadIntervalSeconds),
             static_cast<unsigned long>(m_values.heartbeatIntervalSeconds));
    return true;
}

bool RuntimeConfig::parsePayload(const String& payload, RuntimeConfigValues& candidate, String* error) const {
    if (payload.indexOf("\"settings\"") < 0) {
        setError(error, "missing settings object");
        return false;
    }

    uint32_t schemaVersion = 0;
    if (!parseUIntField(payload, "schema_version", schemaVersion, error)) return false;
    if (schemaVersion != kSchemaVersion) {
        setError(error, "unsupported schema_version");
        return false;
    }

    String deviceId;
    if (!extractStringField(payload, "device_id", deviceId)) {
        setError(error, "missing device_id");
        return false;
    }
    if (deviceId != String(DEVICE_ID)) {
        setError(error, "device_id mismatch");
        return false;
    }

    if (!parseFloatField(payload, "temperature_min", candidate.temperatureMin, error)) return false;
    if (!parseFloatField(payload, "temperature_max", candidate.temperatureMax, error)) return false;
    if (!parseFloatField(payload, "water_temperature_min", candidate.waterTemperatureMin, error)) return false;
    if (!parseFloatField(payload, "water_temperature_max", candidate.waterTemperatureMax, error)) return false;
    if (!parseFloatField(payload, "ph_min", candidate.phMin, error)) return false;
    if (!parseFloatField(payload, "ph_max", candidate.phMax, error)) return false;
    if (!parseFloatField(payload, "ec_min", candidate.ecMin, error)) return false;
    if (!parseFloatField(payload, "ec_max", candidate.ecMax, error)) return false;
    if (!parseFloatField(payload, "water_flow_min", candidate.waterFlowMin, error)) return false;
    if (!parseFloatField(payload, "water_flow_max", candidate.waterFlowMax, error)) return false;
    if (!parseFloatField(payload, "water_level_min", candidate.waterLevelMin, error)) return false;
    if (!parseFloatField(payload, "water_level_max", candidate.waterLevelMax, error)) return false;
    if (!parseUIntField(payload, "sensor_upload_interval", candidate.sensorUploadIntervalSeconds, error)) return false;
    if (!parseUIntField(payload, "heartbeat_interval", candidate.heartbeatIntervalSeconds, error)) return false;

    return true;
}

bool RuntimeConfig::validate(RuntimeConfigValues& candidate, String* error) const {
    if (!requireRange("temperature_min", candidate.temperatureMin, kAirTemperatureMin, kAirTemperatureMax, error)) return false;
    if (!requireRange("temperature_max", candidate.temperatureMax, kAirTemperatureMin, kAirTemperatureMax, error)) return false;
    if (!requireOrdered("temperature_min", candidate.temperatureMin, "temperature_max", candidate.temperatureMax, error)) return false;

    if (!requireRange("water_temperature_min", candidate.waterTemperatureMin, kWaterTemperatureMin, kWaterTemperatureMax, error)) return false;
    if (!requireRange("water_temperature_max", candidate.waterTemperatureMax, kWaterTemperatureMin, kWaterTemperatureMax, error)) return false;
    if (!requireOrdered("water_temperature_min", candidate.waterTemperatureMin, "water_temperature_max", candidate.waterTemperatureMax, error)) return false;

    if (!requireRange("ph_min", candidate.phMin, PH_VALID_MIN, PH_VALID_MAX, error)) return false;
    if (!requireRange("ph_max", candidate.phMax, PH_VALID_MIN, PH_VALID_MAX, error)) return false;
    if (!requireOrdered("ph_min", candidate.phMin, "ph_max", candidate.phMax, error)) return false;

    if (!requireRange("ec_min", candidate.ecMin, kEcMin, kEcMax, error)) return false;
    if (!requireRange("ec_max", candidate.ecMax, kEcMin, kEcMax, error)) return false;
    if (!requireOrdered("ec_min", candidate.ecMin, "ec_max", candidate.ecMax, error)) return false;

    if (!requireRange("water_flow_min", candidate.waterFlowMin, kFlowMin, kFlowMax, error)) return false;
    if (!requireRange("water_flow_max", candidate.waterFlowMax, kFlowMin, kFlowMax, error)) return false;
    if (!requireOrdered("water_flow_min", candidate.waterFlowMin, "water_flow_max", candidate.waterFlowMax, error)) return false;

    if (!requireRange("water_level_min", candidate.waterLevelMin, kWaterPercentMin, kWaterPercentMax, error)) return false;
    if (!requireRange("water_level_max", candidate.waterLevelMax, kWaterPercentMin, kWaterPercentMax, error)) return false;
    if (candidate.waterLevelMin < WATER_LEVEL_MIN) {
        LOG_WARN("[CFG] Clamping water_level_min %.2f to hard safety floor %.2f", candidate.waterLevelMin, WATER_LEVEL_MIN);
        candidate.waterLevelMin = WATER_LEVEL_MIN;
    }
    if (!requireOrdered("water_level_min", candidate.waterLevelMin, "water_level_max", candidate.waterLevelMax, error)) return false;

    if (candidate.sensorUploadIntervalSeconds < kMinIntervalSeconds || candidate.sensorUploadIntervalSeconds > kMaxIntervalSeconds) {
        setError(error, "sensor_upload_interval out of range");
        return false;
    }
    if (candidate.heartbeatIntervalSeconds < kMinIntervalSeconds || candidate.heartbeatIntervalSeconds > kMaxIntervalSeconds) {
        setError(error, "heartbeat_interval out of range");
        return false;
    }

    return true;
}
