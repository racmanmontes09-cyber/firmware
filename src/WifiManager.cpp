#include <Arduino.h>
#include "WifiManager.h"
#include "Logging.h"
#include <ArduinoOTA.h>
#include <time.h>
#include "PerfBenchmark.h"

WifiManager::WifiManager()
    : m_state(WifiState::DISCONNECTED),
      m_lastReconnectAttempt(0),
      m_lastHeartbeatTime(0),
      m_connectStartedAt(0),
      m_lastStatusLogAt(0),
      m_reconnectBackoffMs(RECONNECT_INITIAL_BACKOFF_MS),
      m_attemptNumber(0) {}

void WifiManager::begin() {
    configureRadio();

    m_state = WifiState::DISCONNECTED;
    m_reconnectBackoffMs = RECONNECT_INITIAL_BACKOFF_MS;
    m_lastReconnectAttempt = 0;
    m_connectStartedAt = 0;
    m_lastStatusLogAt = 0;

    if (String(WIFI_SSID).isEmpty()) {
        LOG_ERROR("[WiFi] WIFI_SSID not configured");
        return;
    }

    connect();
}

void WifiManager::configureRadio() {
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.setHostname(DEVICE_ID);
    // Cap TX power: at the default 19.5-21 dBm the radio calibration draws a
    // large current spike right after boot, which browns out supply-limited
    // boards (classic "won't boot once WiFi is enabled" symptom). 11 dBm has
    // ample link margin for indoor deployments.
    WiFi.setTxPower(WIFI_POWER_11dBm);
    WiFi.disconnect(false, false);
}

void WifiManager::connect() {
    startConnectionAttempt(millis());
}

void WifiManager::startConnectionAttempt(unsigned long now) {
    PERF_SCOPE("wifi_reconnect");
    ++m_attemptNumber;

    Serial.printf("[WiFi] Attempt #%lu starting (ssid_len=%u)\n",
                  m_attemptNumber,
                  static_cast<unsigned>(String(WIFI_SSID).length()));

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    m_state = WifiState::CONNECTING;
    m_lastReconnectAttempt = now;
    m_connectStartedAt = now;
    m_lastStatusLogAt = now;
}

void WifiManager::loop() {
    wl_status_t status = WiFi.status();
    unsigned long now = millis();

    switch (m_state) {
        case WifiState::CONNECTING:
            handleConnecting(now, status);
            break;

        case WifiState::CONNECTED:
            if (status != WL_CONNECTED) {
                scheduleReconnect(now, status, "connection lost");
            }
            break;

        case WifiState::DISCONNECTED:
            if (now - m_lastReconnectAttempt >= m_reconnectBackoffMs) {
                startConnectionAttempt(now);
            }
            break;
    }
}

void WifiManager::handleConnecting(unsigned long now, wl_status_t status) {
    if (status == WL_CONNECTED) {
        handleConnected(now);
        return;
    }

    if (isFastFailure(status)) {
        scheduleReconnect(now, status, "connection failed");
        return;
    }

    if (now - m_connectStartedAt >= CONNECT_ATTEMPT_TIMEOUT_MS) {
        scheduleReconnect(now, status, "connection timeout");
        return;
    }

    logConnectionProgress(now, status);
}

void WifiManager::handleConnected(unsigned long now) {
    unsigned long elapsed = now - m_connectStartedAt;

    m_state = WifiState::CONNECTED;
    m_lastHeartbeatTime = now;
    m_reconnectBackoffMs = RECONNECT_INITIAL_BACKOFF_MS;

    Serial.printf("[WiFi] Connected IP=%s RSSI=%d attempt=%lu elapsed=%lums\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.RSSI(),
                  m_attemptNumber,
                  elapsed);
    Serial.println("[BOOT] Milestone: NETWORK_READY");

    ArduinoOTA.begin();
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

void WifiManager::scheduleReconnect(unsigned long now, wl_status_t status, const char* reason) {
    Serial.printf("[WARN] [WiFi] %s status=%s retry_in=%lums\n",
                  reason,
                  statusName(status),
                  m_reconnectBackoffMs);

    if (status == WL_NO_SSID_AVAIL) {
        Serial.println("[WiFi] SSID not visible to ESP32; check 2.4GHz band, hidden SSID, or range");
    }

    WiFi.disconnect(false, false);

    m_state = WifiState::DISCONNECTED;
    m_lastReconnectAttempt = now;
    m_connectStartedAt = 0;
    m_lastStatusLogAt = now;
    m_reconnectBackoffMs = min(m_reconnectBackoffMs * 2, RECONNECT_MAX_BACKOFF_MS);
}

void WifiManager::logConnectionProgress(unsigned long now, wl_status_t status) {
    if (now - m_lastStatusLogAt < CONNECT_STATUS_LOG_MS) {
        return;
    }

    Serial.printf("[WiFi] Attempt #%lu status=%s elapsed=%lums\n",
                  m_attemptNumber,
                  statusName(status),
                  now - m_connectStartedAt);
    m_lastStatusLogAt = now;
}

const char* WifiManager::statusName(wl_status_t status) {
    switch (status) {
        case WL_IDLE_STATUS: return "IDLE";
        case WL_NO_SSID_AVAIL: return "NO_SSID_AVAIL";
        case WL_SCAN_COMPLETED: return "SCAN_COMPLETED";
        case WL_CONNECTED: return "CONNECTED";
        case WL_CONNECT_FAILED: return "CONNECT_FAILED";
        case WL_CONNECTION_LOST: return "CONNECTION_LOST";
        case WL_DISCONNECTED: return "DISCONNECTED";
        default: return "UNKNOWN";
    }
}

bool WifiManager::isFastFailure(wl_status_t status) {
    return status == WL_CONNECT_FAILED;
}

bool WifiManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

String WifiManager::getIPAddress() const {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.localIP().toString();
    }
    return String("0.0.0.0");
}

int32_t WifiManager::getRSSI() const {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.RSSI();
    }
    return 0;
}
