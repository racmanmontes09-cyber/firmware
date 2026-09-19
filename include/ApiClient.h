#ifndef API_CLIENT_H
#define API_CLIENT_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "Config.h"
#include "Sensors.h"
#include "Actuators.h"

struct PendingCommand {
    String command;
    bool enabled;
};

class ApiClient {
public:
    ApiClient();

    bool sendTelemetry(const SensorReadings& readings);
    bool enqueueTelemetry(const SensorReadings& readings);
    bool flushQueuedTelemetry(size_t maxEntries = 1);

    void loopMqtt();
    void logMqttConfiguration() const;
    void discardQueuedTelemetryOnSensorProfileChange();

    void processQueuedCommands(Actuators& actuators);
    void publishStatusHeartbeat(const Actuators& actuators);

private:
    void mqttCallback(char* topic, byte* payload, unsigned int length);
    bool ensureMqttConnected();
    bool isMqttConfigured() const;
    bool subscribeToCommands();
    bool subscribeToSettings();
    bool publishStatus(bool online, const Actuators* actuators = nullptr);
    bool publishCommandResult(const String& command, bool success, const String& message);

    String mqttTelemetryTopic() const;
    String mqttStatusTopic() const;
    String mqttCommandTopic() const;
    String mqttSettingsTopic() const;
    String mqttResultTopic() const;
    String mqttClientId() const;
    String buildTelemetryPayload(const SensorReadings& readings) const;
    bool enqueueTelemetryPayload(const String& payload);
    bool publishTelemetryMqtt(const String& payload);
    bool shouldLogWifiUnavailable(unsigned long now, wl_status_t status);

    bool m_wasMqttConnected = false;
    bool m_commandsSubscribed = false;
    bool m_settingsSubscribed = false;
    WiFiClientSecure m_mqttNetworkClient;
    PubSubClient m_mqttClient;

    String m_cachedTelemetryTopic;
    String m_cachedStatusTopic;
    String m_cachedCommandTopic;
    String m_cachedSettingsTopic;
    String m_cachedResultTopic;

    PendingCommand m_commandQueue[MQTT_COMMAND_QUEUE_SIZE];
    volatile uint8_t m_commandQueueHead = 0;
    volatile uint8_t m_commandQueueTail = 0;

    unsigned long m_lastStatusPublishMs = 0;
    unsigned long m_lastSuccessfulHeartbeatMs = 0;
    unsigned long m_lastSuccessfulTelemetryMs = 0;
    unsigned long m_lastReconnectAttemptMs = 0;
    unsigned long m_lastWifiUnavailableLogMs = 0;
    unsigned long m_lastNtpWaitLogMs = 0;
    int m_lastWifiUnavailableStatus = -1;
    unsigned long m_reconnectDelayMs = MQTT_RECONNECT_MIN_MS;
};

#endif
