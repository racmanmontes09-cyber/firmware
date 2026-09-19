#include <Arduino.h>
#include "ApiClient.h"
#include "RuntimeConfig.h"
#include <Preferences.h>
#include <SPIFFS.h>
#include "TelemetryQueue.h"
#include "PerfBenchmark.h"
#include "Logging.h"
#include <ESPmDNS.h>
#include <esp_task_wdt.h>

namespace {
    constexpr char kLeafPreferencesNamespace[] = "leaf";
    constexpr char kDeviceTokenPreferenceKey[] = "device_token";

    static const char* kIsrgRootX1CaPem =
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "-----END CERTIFICATE-----\n";

    ApiClient* s_instance = nullptr;

    void logHeapSnapshot(const char* label) {
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t minHeap = ESP.getMinFreeHeap();
        uint32_t largestBlock = ESP.getMaxAllocHeap();
        Serial.printf("[MEM] %s free=%u min=%u largest=%u\n", label, freeHeap, minHeap, largestBlock);
    }

    const char* mqttStateName(int state) {
        switch (state) {
            case MQTT_CONNECTION_TIMEOUT:   return "CONNECTION_TIMEOUT";
            case MQTT_CONNECTION_LOST:      return "CONNECTION_LOST";
            case MQTT_CONNECT_FAILED:       return "CONNECT_FAILED";
            case MQTT_DISCONNECTED:         return "DISCONNECTED";
            case MQTT_CONNECTED:            return "CONNECTED";
            case MQTT_CONNECT_BAD_PROTOCOL: return "BAD_PROTOCOL";
            case MQTT_CONNECT_BAD_CLIENT_ID: return "BAD_CLIENT_ID";
            case MQTT_CONNECT_UNAVAILABLE:  return "UNAVAILABLE";
            case MQTT_CONNECT_BAD_CREDENTIALS: return "BAD_CREDENTIALS";
            case MQTT_CONNECT_UNAUTHORIZED: return "UNAUTHORIZED";
            default:                        return "UNKNOWN";
        }
    }

    const char* wifiStatusName(wl_status_t status) {
        switch (status) {
            case WL_NO_SHIELD:        return "WL_NO_SHIELD";
            case WL_IDLE_STATUS:      return "WL_IDLE_STATUS";
            case WL_NO_SSID_AVAIL:    return "WL_NO_SSID_AVAIL";
            case WL_SCAN_COMPLETED:   return "WL_SCAN_COMPLETED";
            case WL_CONNECTED:        return "WL_CONNECTED";
            case WL_CONNECT_FAILED:   return "WL_CONNECT_FAILED";
            case WL_CONNECTION_LOST:  return "WL_CONNECTION_LOST";
            case WL_DISCONNECTED:     return "WL_DISCONNECTED";
            default:                  return "WL_UNKNOWN";
        }
    }

    void printMqttState(const char* prefix, int state) {
        Serial.print(prefix);
        Serial.print(state);
        Serial.print(" ");
        Serial.println(mqttStateName(state));
    }

    String currentUtcIsoTimestamp() {
        time_t now = time(nullptr);
        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);
        if ((timeinfo.tm_year + 1900) < 2022) {
            return String("");
        }
        char buffer[25];
        strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
        return String(buffer);
    }

    bool extractStringField(const String& text, const String& fieldName, String& value) {
        String token = "\"" + fieldName + "\"";
        int idx = text.indexOf(token);
        if (idx == -1) return false;
        int colon = text.indexOf(':', idx);
        if (colon == -1) return false;
        int start = colon + 1;
        while (start < (int)text.length() && text[start] == ' ') ++start;
        if (start >= (int)text.length() || text[start] != '"') return false;
        int end = text.indexOf('"', start + 1);
        if (end == -1) return false;
        value = text.substring(start + 1, end);
        return true;
    }

    bool extractBoolField(const String& text, const String& fieldName, bool& value) {
        String token = "\"" + fieldName + "\"";
        int idx = text.indexOf(token);
        if (idx == -1) return false;
        int colon = text.indexOf(':', idx);
        if (colon == -1) return false;
        int start = colon + 1;
        while (start < (int)text.length() && text[start] == ' ') ++start;
        if (text.substring(start, start + 4) == "true") { value = true; return true; }
        if (text.substring(start, start + 5) == "false") { value = false; return true; }
        return false;
    }

    bool configFlagEnabled(const char* value) {
        String flag(value ? value : "");
        flag.trim();
        flag.toLowerCase();
        return !(flag == "0" || flag == "false" || flag == "off" ||
                 flag == "disabled" || flag == "no");
    }

    void replaceJsonFieldWithNull(String& payload, const char* fieldName) {
        String token = "\"" + String(fieldName) + "\"";
        int idx = payload.indexOf(token);
        if (idx < 0) return;

        int colon = payload.indexOf(':', idx + token.length());
        if (colon < 0) return;

        int valueStart = colon + 1;
        while (valueStart < (int)payload.length() && payload[valueStart] == ' ') {
            ++valueStart;
        }
        if (valueStart >= (int)payload.length()) return;

        int valueEnd = -1;
        if (payload[valueStart] == '"') {
            int closingQuote = payload.indexOf('"', valueStart + 1);
            if (closingQuote < 0) return;
            valueEnd = closingQuote + 1;
        } else {
            int comma = payload.indexOf(',', valueStart);
            int brace = payload.indexOf('}', valueStart);
            if (comma < 0) valueEnd = brace;
            else if (brace < 0) valueEnd = comma;
            else valueEnd = min(comma, brace);
        }

        if (valueEnd <= valueStart) return;
        payload = payload.substring(0, valueStart) + "null" + payload.substring(valueEnd);
    }

    String sanitizeQueuedTelemetryPayload(String payload) {
        if (!configFlagEnabled(PH_SENSOR_ENABLED)) {
            replaceJsonFieldWithNull(payload, "ph");
        }
        if (!configFlagEnabled(EC_SENSOR_ENABLED)) {
            replaceJsonFieldWithNull(payload, "ec");
        }
        if (!configFlagEnabled(FLOW_SENSOR_ENABLED)) {
            replaceJsonFieldWithNull(payload, "water_flow");
        }
        if (!configFlagEnabled(WATER_LEVEL_SENSOR_ENABLED)) {
            replaceJsonFieldWithNull(payload, "water_level");
        }
        return payload;
    }

    String sensorProfileFingerprint() {
        String profile;
        profile.reserve(4);
        profile += configFlagEnabled(PH_SENSOR_ENABLED) ? '1' : '0';
        profile += configFlagEnabled(EC_SENSOR_ENABLED) ? '1' : '0';
        profile += configFlagEnabled(WATER_LEVEL_SENSOR_ENABLED) ? '1' : '0';
        profile += configFlagEnabled(FLOW_SENSOR_ENABLED) ? '1' : '0';
        return profile;
    }

    int clearPreferencesTelemetryQueue() {
        Preferences preferences;
        if (!preferences.begin(kLeafPreferencesNamespace, false)) {
            LOG_WARN("[Queue] Preferences queue open failed during clear");
            return 0;
        }

        constexpr int maxQueue = 50;
        int removed = 0;
        for (int i = 0; i < maxQueue; ++i) {
            String key = String("q_") + i;
            if (preferences.isKey(key.c_str())) {
                preferences.remove(key.c_str());
                ++removed;
            }
        }
        preferences.putInt("q_head", 0);
        preferences.putInt("q_count", 0);
        preferences.end();
        return removed;
    }
}

ApiClient::ApiClient()
    : m_mqttClient(m_mqttNetworkClient)
{
    s_instance = this;
    m_mqttNetworkClient.setCACert(kIsrgRootX1CaPem);
    m_mqttClient.setBufferSize(1024);
    m_mqttClient.setKeepAlive(MQTT_KEEPALIVE);
    m_mqttClient.setSocketTimeout(MQTT_SOCKET_TIMEOUT_S);

    m_cachedTelemetryTopic = mqttTelemetryTopic();
    m_cachedStatusTopic = mqttStatusTopic();
    m_cachedCommandTopic = mqttCommandTopic();
    m_cachedSettingsTopic = mqttSettingsTopic();
    m_cachedResultTopic = mqttResultTopic();
}

void ApiClient::discardQueuedTelemetryOnSensorProfileChange() {
    Preferences preferences;
    if (!preferences.begin(kLeafPreferencesNamespace, false)) {
        LOG_WARN("[Queue] Sensor profile check skipped: preferences open failed");
        return;
    }

    String currentProfile = sensorProfileFingerprint();
    String previousProfile = preferences.getString("sensor_profile", String(""));
    bool changed = previousProfile != currentProfile;
    preferences.putString("sensor_profile", currentProfile);
    preferences.end();

    if (!changed) {
        return;
    }

    int spiffsRemoved = TelemetryQueue::clear();
    int preferencesRemoved = clearPreferencesTelemetryQueue();
    Serial.printf("[Queue] Sensor profile changed %s -> %s; cleared queued telemetry spiffs=%d prefs=%d\n",
                  previousProfile.length() ? previousProfile.c_str() : "<unset>",
                  currentProfile.c_str(),
                  spiffsRemoved,
                  preferencesRemoved);
}

bool ApiClient::isMqttConfigured() const {
    return String(MQTT_HOST).length() > 0;
}

String ApiClient::mqttTelemetryTopic() const {
    String topic = String(MQTT_TELEMETRY_TOPIC);
    if (topic.isEmpty()) topic = "leaf/devices/{device_id}/telemetry";
    topic.replace("{device_id}", String(DEVICE_ID));
    return topic;
}

String ApiClient::mqttStatusTopic() const {
    String topic = String(MQTT_STATUS_TOPIC);
    if (topic.isEmpty()) topic = "leaf/devices/{device_id}/status";
    topic.replace("{device_id}", String(DEVICE_ID));
    return topic;
}

String ApiClient::mqttCommandTopic() const {
    String topic = String(MQTT_COMMAND_TOPIC);
    if (topic.isEmpty()) topic = "leaf/devices/{device_id}/commands";
    topic.replace("{device_id}", String(DEVICE_ID));
    return topic;
}

String ApiClient::mqttSettingsTopic() const {
    String topic = String(MQTT_SETTINGS_TOPIC);
    if (topic.isEmpty()) topic = "leaf/devices/{device_id}/settings";
    topic.replace("{device_id}", String(DEVICE_ID));
    return topic;
}

String ApiClient::mqttResultTopic() const {
    String topic = String(MQTT_RESULT_TOPIC);
    if (topic.isEmpty()) topic = "leaf/devices/{device_id}/results";
    topic.replace("{device_id}", String(DEVICE_ID));
    return topic;
}

String ApiClient::mqttClientId() const {
    String configured = String(MQTT_CLIENT_ID);
    if (!configured.isEmpty()) return configured;
    String id = "leaf-";
    id += String(DEVICE_ID);
    id += "-";
    id += String((uint32_t)ESP.getEfuseMac(), HEX);
    return id;
}

void ApiClient::logMqttConfiguration() const {
    Serial.print("[MQTT] Broker: ");
    Serial.println(isMqttConfigured() ? String(MQTT_HOST) : String("(empty)"));
    Serial.print("[MQTT] Port: ");
    Serial.println(MQTT_PORT);
    Serial.print("[MQTT] Telemetry topic: ");
    Serial.println(mqttTelemetryTopic());
    Serial.print("[MQTT] Status topic: ");
    Serial.println(mqttStatusTopic());
    Serial.print("[MQTT] Command topic: ");
    Serial.println(mqttCommandTopic());
    Serial.print("[MQTT] Settings topic: ");
    Serial.println(mqttSettingsTopic());
    Serial.print("[MQTT] Result topic: ");
    Serial.println(mqttResultTopic());
    Serial.print("[MQTT] Client ID: ");
    Serial.println(mqttClientId());
    Serial.println("[MQTT] Transport: plain TCP");
    Serial.print("[MQTT] Auth: ");
    Serial.println(String(MQTT_USERNAME).isEmpty() ? "anonymous" : "username configured");
}

bool ApiClient::ensureMqttConnected() {
    PERF_SCOPE("mqtt_reconnect");
    unsigned long now = millis();

    if (!isMqttConfigured()) {
        LOG_WARN("[MQTT] Not configured (MQTT_HOST empty)");
        return false;
    }

    wl_status_t wifiStatus = WiFi.status();
    if (wifiStatus != WL_CONNECTED) {
        if (shouldLogWifiUnavailable(now, wifiStatus)) {
            LOG_WARN("[MQTT] Waiting for WiFi status=%s (%d)",
                     wifiStatusName(wifiStatus),
                     static_cast<int>(wifiStatus));
        }
        return false;
    }
    m_lastWifiUnavailableStatus = -1;

    // TLS CA validation (setCACert) requires a valid system clock. configTime()
    // is asynchronous and does NOT block until NTP finishes, so gate the first
    // encrypted connect until the clock is synced; otherwise the handshake can
    // fail with an "invalid certificate validity period" on an unsynced clock.
    if (currentUtcIsoTimestamp().isEmpty()) {
        if (now - m_lastNtpWaitLogMs >= 5000UL) {
            m_lastNtpWaitLogMs = now;
            LOG_WARN("[MQTT] NTP time not yet synchronized; deferring TLS connect");
        }
        return false;
    }

    if (m_mqttClient.connected()) {
        if (!m_wasMqttConnected) {
            Serial.println("[MQTT] Connected");
        }
        m_wasMqttConnected = true;
        return true;
    }

    if (m_wasMqttConnected) {
        printMqttState("[MQTT] Disconnected/state=", m_mqttClient.state());
        m_wasMqttConnected = false;
        m_commandsSubscribed = false;
        m_settingsSubscribed = false;
    }

    if (now - m_lastReconnectAttemptMs < m_reconnectDelayMs) {
        return false;
    }
    m_lastReconnectAttemptMs = now;

    // Configure TLS Certificate & Socket Timeout
    m_mqttNetworkClient.setCACert(kIsrgRootX1CaPem);
    m_mqttNetworkClient.setTimeout(10);

    m_mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    m_mqttClient.setCallback([](char* topic, byte* payload, unsigned int length) {
        if (s_instance) s_instance->mqttCallback(topic, payload, length);
    });

    Serial.printf("[MQTTS] Network local=%s gateway=%s target=%s:%u\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.gatewayIP().toString().c_str(),
                  MQTT_HOST,
                  MQTT_PORT);

    String clientId = mqttClientId();
    String username = String(MQTT_USERNAME);
    String password = String(MQTT_PASSWORD);
    String statusTopic = mqttStatusTopic();
    String willPayload = "{\"status\":\"offline\"}";

    Serial.print("[MQTTS] Connecting with TLS to ");
    Serial.print(MQTT_HOST);
    Serial.print(":");
    Serial.print(MQTT_PORT);
    Serial.print(" id=");
    Serial.println(clientId);

    bool connected = false;
    if (!username.isEmpty()) {
        connected = m_mqttClient.connect(
            clientId.c_str(), username.c_str(), password.c_str(),
            statusTopic.c_str(), 1, true, willPayload.c_str()
        );
    } else {
        connected = m_mqttClient.connect(
            clientId.c_str(),
            statusTopic.c_str(), 1, true, willPayload.c_str()
        );
    }

    if (!connected) {
        m_reconnectDelayMs = min(m_reconnectDelayMs * 2, (unsigned long)MQTT_RECONNECT_MAX_MS);
        printMqttState("[MQTTS] Connect failed/state=", m_mqttClient.state());
        LOG_WARN("[MQTTS] TLS Connect failed: host=%s:%d client_id=%s state=%d",
                 MQTT_HOST, MQTT_PORT, clientId.c_str(), m_mqttClient.state());
        return false;
    }

    m_reconnectDelayMs = MQTT_RECONNECT_MIN_MS;
    m_wasMqttConnected = true;
    Serial.println("[MQTTS] TLS Encrypted Connection Established (Port 8883)");
    Serial.println("[BOOT] Milestone: MQTT_READY");

    publishStatus(true);
    subscribeToCommands();
    subscribeToSettings();

    return true;
}

bool ApiClient::shouldLogWifiUnavailable(unsigned long now, wl_status_t status) {
    int statusValue = static_cast<int>(status);
    bool statusChanged = m_lastWifiUnavailableStatus != statusValue;
    bool intervalElapsed = now - m_lastWifiUnavailableLogMs >= 5000UL;

    if (!statusChanged && !intervalElapsed) {
        return false;
    }

    m_lastWifiUnavailableLogMs = now;
    m_lastWifiUnavailableStatus = statusValue;
    return true;
}

bool ApiClient::subscribeToCommands() {
    String topic = mqttCommandTopic();
    if (m_mqttClient.subscribe(topic.c_str(), 1)) {
        m_commandsSubscribed = true;
        Serial.print("[MQTT] Subscribed: ");
        Serial.println(topic);
        return true;
    }
    Serial.println("[MQTT] Command subscribe failed");
    return false;
}

bool ApiClient::subscribeToSettings() {
    String topic = mqttSettingsTopic();
    if (m_mqttClient.subscribe(topic.c_str(), 1)) {
        m_settingsSubscribed = true;
        Serial.print("[MQTT] Subscribed: ");
        Serial.println(topic);
        return true;
    }
    Serial.println("[MQTT] Settings subscribe failed");
    return false;
}

void ApiClient::mqttCallback(char* topic, byte* payload, unsigned int length) {
    String message;
    message.reserve(length);
    for (unsigned int i = 0; i < length; ++i) {
        message += (char)payload[i];
    }

    Serial.println("[MQTT] Command received");
    Serial.print("[MQTT] Topic: ");
    Serial.println(topic);
    Serial.print("[MQTT] Payload: ");
    Serial.println(message);

    if (String(topic) == m_cachedSettingsTopic) {
        Serial.println("[SETTINGS] Received settings message on topic: " + String(topic));
        Serial.printf("[SETTINGS] Payload length=%u bytes\n", length);
        String error;
        float previousTemperatureMax = g_runtimeConfig.temperatureMax();
        if (g_runtimeConfig.applyPayload(message, &error)) {
            Serial.println("[SETTINGS] Runtime configuration applied successfully");
            Serial.printf("[SETTINGS] fan_enabled=%s fan_state_reset=%s automation_mode=%s max_temp=%.2f previous_max_temp=%.2f hysteresis=%.2f millis=%lu\n",
                          DEBUG_NO_AUTOMATION ? "false" : "true",
                          "no",
                          DEBUG_NO_AUTOMATION ? "disabled" : "auto_temp",
                          g_runtimeConfig.temperatureMax(),
                          previousTemperatureMax,
                          FAN_HYSTERESIS_C,
                          static_cast<unsigned long>(millis()));
            Serial.printf("[SETTINGS] air_temperature_max=%.1fC ph=%.1f-%.1f ec=%.1f-%.1f\n",
                          g_runtimeConfig.temperatureMax(),
                          g_runtimeConfig.phMin(), g_runtimeConfig.phMax(),
                          g_runtimeConfig.ecMin(), g_runtimeConfig.ecMax());
        } else {
            Serial.print("[SETTINGS] Runtime configuration REJECTED: ");
            Serial.println(error);
            Serial.println("[SETTINGS] Raw payload: " + message);
        }
        return;
    }

    String commandName;
    bool enabled = true;
    if (!extractStringField(message, "command", commandName)) {
        Serial.println("[MQTT] Command parse failed: missing 'command'");
        return;
    }
    extractBoolField(message, "enabled", enabled);

    commandName.toLowerCase();

    uint8_t nextHead = (m_commandQueueHead + 1) % MQTT_COMMAND_QUEUE_SIZE;
    if (nextHead == m_commandQueueTail) {
        Serial.println("[MQTT] Command queue full, dropping command");
        return;
    }

    m_commandQueue[m_commandQueueHead].command = commandName;
    m_commandQueue[m_commandQueueHead].enabled = enabled;
    m_commandQueueHead = nextHead;
}

void ApiClient::processQueuedCommands(Actuators& actuators) {
    while (m_commandQueueTail != m_commandQueueHead) {
        const PendingCommand& cmd = m_commandQueue[m_commandQueueTail];
        m_commandQueueTail = (m_commandQueueTail + 1) % MQTT_COMMAND_QUEUE_SIZE;

        Serial.print("[MQTT] Executing command: ");
        Serial.print(cmd.command);
        Serial.print(" enabled=");
        Serial.println(cmd.enabled ? "true" : "false");

        bool ok = actuators.setState(cmd.command, cmd.enabled, "MQTT_COMMAND");
        if (ok) {
            Serial.print("[MQTT] Command applied: ");
            Serial.println(cmd.command);
        } else {
            Serial.print("[MQTT] Command failed: ");
            Serial.println(cmd.command);
        }
        publishCommandResult(cmd.command, ok, ok ? "Command applied" : "Command not supported or blocked by safety");
    }
}

bool ApiClient::publishStatus(bool online, const Actuators* actuators) {
    if (!m_mqttClient.connected()) return false;

    String payload;
    if (!online) {
        payload = "{\"status\":\"offline\"}";
    } else if (actuators != nullptr) {
        char buf[384];
        snprintf(buf, sizeof(buf),
            "{\"status\":\"online\",\"actuators\":{\"cooling_fan\":%s,\"nutrient_pump_a\":%s,\"nutrient_pump_b\":%s,\"ph_up_pump\":%s,\"ph_down_pump\":%s}}",
            actuators->isCoolingFanOn() ? "true" : "false",
            actuators->isNutrientPumpAOn() ? "true" : "false",
            actuators->isNutrientPumpBOn() ? "true" : "false",
            actuators->isPhUpPumpOn() ? "true" : "false",
            actuators->isPhDownPumpOn() ? "true" : "false"
        );
        payload = String(buf);
    } else {
        payload = "{\"status\":\"online\"}";
    }

    bool ok = m_mqttClient.publish(m_cachedStatusTopic.c_str(), payload.c_str(), true);
    if (ok && actuators != nullptr) {
        LOG_INFO("[STATUS] Actuator state published: fan=%d nutA=%d nutB=%d phUp=%d phDn=%d",
                 actuators->isCoolingFanOn(), actuators->isNutrientPumpAOn(),
                 actuators->isNutrientPumpBOn(), actuators->isPhUpPumpOn(),
                 actuators->isPhDownPumpOn());
    }
    return ok;
}

bool ApiClient::publishCommandResult(const String& command, bool success, const String& message) {
    if (!m_mqttClient.connected()) return false;

    String payload = "{";
    payload += "\"command\":\"" + command + "\",";
    payload += "\"success\":" + String(success ? "true" : "false") + ",";
    payload += "\"message\":\"" + message + "\"";
    payload += "}";

    bool ok = m_mqttClient.publish(m_cachedResultTopic.c_str(), payload.c_str(), false);
    return ok;
}

void ApiClient::publishStatusHeartbeat(const Actuators& actuators) {
    if (!isMqttConfigured() || !m_mqttClient.connected()) return;

    unsigned long now = millis();
    uint32_t intervalMs = g_runtimeConfig.heartbeatIntervalMs();
    if (now - m_lastStatusPublishMs < intervalMs) return;
    m_lastStatusPublishMs = now;

    PERF_SCOPE("heartbeat_publish");
    if (publishStatus(true, &actuators)) {
        if (m_lastSuccessfulHeartbeatMs != 0) {
            Serial.printf("[TIMING] Heartbeat interval=%lums\n", now - m_lastSuccessfulHeartbeatMs);
        }
        m_lastSuccessfulHeartbeatMs = now;
    }
}

void ApiClient::loopMqtt() {
    if (!isMqttConfigured()) return;

    if (!m_mqttClient.connected()) {
        ensureMqttConnected();
    }

    m_mqttClient.loop();

    if (!m_mqttClient.connected()) {
        m_wasMqttConnected = false;
        m_commandsSubscribed = false;
        m_settingsSubscribed = false;
    }
}

String ApiClient::buildTelemetryPayload(const SensorReadings& readings) const {
    char buf[768];
    int pos = 0;
    size_t cap = sizeof(buf);

    String measuredAt = currentUtcIsoTimestamp();
    pos += snprintf(buf + pos, cap - pos, "{\"device_id\":\"%s\"", DEVICE_ID);
    if (!measuredAt.isEmpty()) {
        pos += snprintf(buf + pos, cap - pos, ",\"measured_at\":\"%s\"", measuredAt.c_str());
    }
    if (readings.airTemperatureValid)
        pos += snprintf(buf + pos, cap - pos, ",\"air_temperature\":%.2f", readings.airTemperature);
    else
        pos += snprintf(buf + pos, cap - pos, ",\"air_temperature\":null");
    if (readings.humidityValid)
        pos += snprintf(buf + pos, cap - pos, ",\"humidity\":%.2f", readings.humidity);
    else
        pos += snprintf(buf + pos, cap - pos, ",\"humidity\":null");
    if (readings.waterTemperatureValid)
        pos += snprintf(buf + pos, cap - pos, ",\"water_temperature\":%.2f", readings.waterTemperature);
    else
        pos += snprintf(buf + pos, cap - pos, ",\"water_temperature\":null");
    if (readings.phValid)
        pos += snprintf(buf + pos, cap - pos, ",\"ph\":%.2f", readings.ph);
    else
        pos += snprintf(buf + pos, cap - pos, ",\"ph\":null");
    if (readings.ecValid)
        pos += snprintf(buf + pos, cap - pos, ",\"ec\":%.2f", readings.ec);
    else
        pos += snprintf(buf + pos, cap - pos, ",\"ec\":null");
    if (readings.waterFlowValid)
        pos += snprintf(buf + pos, cap - pos, ",\"water_flow\":%.2f", readings.waterFlow);
    else
        pos += snprintf(buf + pos, cap - pos, ",\"water_flow\":null");
    if (readings.waterLevelValid)
        pos += snprintf(buf + pos, cap - pos, ",\"water_level\":%.2f", readings.waterLevel);
    else
        pos += snprintf(buf + pos, cap - pos, ",\"water_level\":null");
    pos += snprintf(buf + pos, cap - pos, ",\"sequence_number\":%u", readings.sequenceNumber);
    pos += snprintf(buf + pos, cap - pos, ",\"firmware_version\":\"%s\"", FIRMWARE_VERSION);
    pos += snprintf(buf + pos, cap - pos, ",\"payload_version\":1}");
    return String(buf);
}

bool ApiClient::sendTelemetry(const SensorReadings& readings) {
    String payload = buildTelemetryPayload(readings);
    if (payload.length() == 0 || payload == "{}") {
        return false;
    }

    bool ok = publishTelemetryMqtt(payload);
    if (!ok) {
        enqueueTelemetryPayload(payload);
    }
    return ok;
}

bool ApiClient::publishTelemetryMqtt(const String& payload) {
    PERF_SCOPE("mqtt_publish");
    // Reconnects belong to loopMqtt(); telemetry only attempts a publish when
    // the maintenance state machine has already established the connection.
    if (!m_mqttClient.connected()) {
        return false;
    }

    bool ok = m_mqttClient.publish(m_cachedTelemetryTopic.c_str(), payload.c_str(), false);

    if (!ok) {
        LOG_WARN("[MQTT] Publish failed: WiFi=%d mqtt_connected=%d state=%d",
                 WiFi.status(), m_mqttClient.connected(), m_mqttClient.state());
    }

    if (ok) {
        unsigned long now = millis();
        if (m_lastSuccessfulTelemetryMs != 0) {
            Serial.printf("[TIMING] Telemetry interval=%lums\n", now - m_lastSuccessfulTelemetryMs);
        }
        m_lastSuccessfulTelemetryMs = now;
    }

    m_mqttClient.loop();
    return ok;
}

bool ApiClient::enqueueTelemetry(const SensorReadings& readings) {
    String entry = buildTelemetryPayload(readings);
    return enqueueTelemetryPayload(entry);
}

bool ApiClient::enqueueTelemetryPayload(const String& entry) {
    if (TelemetryQueue::begin()) {
        if (TelemetryQueue::enqueue(entry.c_str())) {
            LOG_INFO("Telemetry enqueued to SPIFFS");
            return true;
        }
    }

    Preferences preferences;
    if (!preferences.begin(kLeafPreferencesNamespace, false)) {
        LOG_ERROR("Failed to open preferences for queue");
        return false;
    }

    constexpr int maxQueue = 50;
    int head = preferences.getInt("q_head", 0);
    int count = preferences.getInt("q_count", 0);

    String key = String("q_") + (head % maxQueue);
    preferences.putString(key.c_str(), entry);
    preferences.putInt("q_head", (head + 1) % maxQueue);
    if (count < maxQueue) {
        preferences.putInt("q_count", count + 1);
    }

    preferences.end();
    LOG_INFO("Telemetry enqueued to Preferences (circular)");
    return true;
}

bool ApiClient::flushQueuedTelemetry(size_t maxEntries) {
    if (maxEntries == 0) return true;
    if (!m_mqttClient.connected()) return false;

    size_t flushed = 0;

    if (TelemetryQueue::begin()) {
        File dir = SPIFFS.open("/telemetry");
        if (dir) {
            File f = dir.openNextFile();
            while (f && flushed < maxEntries) {
                String name = f.path();
                if (name.isEmpty()) name = f.name();
                if (!name.startsWith("/")) name = String("/telemetry/") + name;
                String payload = f.readString();
                f.close();
                if (payload.length() == 0) {
                    LOG_WARN("SPIFFS queue entry empty for %s; removing", name.c_str());
                    if (!SPIFFS.remove(name)) {
                        LOG_WARN("SPIFFS queue remove failed for %s", name.c_str());
                    }
                    ++flushed;
                    f = dir.openNextFile();
                    yield();
                    continue;
                }

                String sanitizedPayload = sanitizeQueuedTelemetryPayload(payload);
                if (!publishTelemetryMqtt(sanitizedPayload)) {
                    LOG_WARN("SPIFFS queue flush failed, stopping");
                    return false;
                }
                if (!SPIFFS.exists(name) || !SPIFFS.remove(name)) {
                    LOG_WARN("SPIFFS queue remove failed for %s", name.c_str());
                }
                ++flushed;
                f = dir.openNextFile();
                yield();
            }
            if (flushed > 0) {
                Serial.print("[Queue] Flushed SPIFFS entries: ");
                Serial.println(flushed);
            }
            if (flushed >= maxEntries) return true;
            LOG_INFO("SPIFFS queue flushed");
        }
    }

    Preferences preferences;
    if (!preferences.begin(kLeafPreferencesNamespace, false)) {
        LOG_WARN("Preferences queue open failed");
        return true;
    }

    constexpr int maxQueue = 50;
    int head = preferences.getInt("q_head", 0);
    int count = preferences.getInt("q_count", 0);
    if (count <= 0) {
        preferences.end();
        return true;
    }

    // Circular buffer: oldest is at (head - count + maxQueue) % maxQueue
    int tail = (head - count + maxQueue) % maxQueue;
    LOG_INFO("Flushing %d preferences queued entries (tail=%d head=%d)", count, tail, head);

    int remaining = count;
    int readIdx = tail;
    while (remaining > 0 && flushed < maxEntries) {
        String key = String("q_") + (readIdx % maxQueue);
        String entry = preferences.getString(key.c_str(), String(""));
        if (entry.length() == 0) {
            // Skip empty slots
            readIdx = (readIdx + 1) % maxQueue;
            remaining--;
            continue;
        }
        String sanitizedEntry = sanitizeQueuedTelemetryPayload(entry);
        if (!publishTelemetryMqtt(sanitizedEntry)) {
            LOG_WARN("Preferences queue flush failed, stopping");
            preferences.putInt("q_count", remaining - 1);
            preferences.end();
            return false;
        }
        preferences.remove(key.c_str());
        ++flushed;
        readIdx = (readIdx + 1) % maxQueue;
        remaining--;
        yield();
    }

    if (flushed > 0) {
        Serial.print("[Queue] Flushed preferences entries: ");
        Serial.println(flushed);
    }

    int newCount = count - (count - remaining);
    if (remaining <= 0) {
        preferences.putInt("q_count", 0);
    } else {
        preferences.putInt("q_count", remaining);
    }

    preferences.end();
    LOG_INFO("Preferences queue flushed");
    return true;
}
