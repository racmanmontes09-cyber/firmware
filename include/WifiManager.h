#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include "Config.h"

enum class WifiState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED
};

class WifiManager {
public:
    WifiManager();

    void begin();
    void loop();
    bool isConnected() const;
    String getIPAddress() const;
    int32_t getRSSI() const;
    String getSSID() const;

private:
    void configureRadio();
    void connect();
    void startConnectionAttempt(unsigned long now);
    void handleConnecting(unsigned long now, wl_status_t status);
    void handleConnected(unsigned long now);
    void scheduleReconnect(unsigned long now, wl_status_t status, const char* reason);
    void logConnectionProgress(unsigned long now, wl_status_t status);
    void resetRadio();
    void loadCredentials();

    static const char* statusName(wl_status_t status);
    static bool isFastFailure(wl_status_t status);

    WifiState m_state;
    unsigned long m_lastReconnectAttempt;
    unsigned long m_lastHeartbeatTime;
    unsigned long m_connectStartedAt;
    unsigned long m_lastStatusLogAt;
    unsigned long m_reconnectBackoffMs;
    unsigned long m_attemptNumber;
    String m_ssid;
    String m_password;

    static constexpr unsigned long CONNECT_ATTEMPT_TIMEOUT_MS = WIFI_CONNECT_ATTEMPT_TIMEOUT_MS;
    static constexpr unsigned long CONNECT_STATUS_LOG_MS = WIFI_CONNECT_STATUS_LOG_MS;
    static constexpr unsigned long RECONNECT_INITIAL_BACKOFF_MS = WIFI_RECONNECT_INITIAL_BACKOFF_MS;
    static constexpr unsigned long RECONNECT_MAX_BACKOFF_MS = WIFI_RECONNECT_MAX_BACKOFF_MS;
};

#endif // WIFI_MANAGER_H
