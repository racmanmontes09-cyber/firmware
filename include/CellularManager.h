#ifndef CELLULAR_MANAGER_H
#define CELLULAR_MANAGER_H

#include <Arduino.h>
#define TINY_GSM_MODEM_A7672X
#include <TinyGsmClient.h>
#include "Config.h"

enum class CellularState {
    CELLULAR_DISABLED,
    OFFLINE,
    STARTING,
    REGISTERING,
    CONNECTED,
    ERROR
};

class CellularManager {
public:
    CellularManager();

    void begin();
    void loop();
    bool startDataConnection();
    void stopDataConnection();
    bool isEnabled() const;
    bool isReady() const;
    CellularState state() const;
    int signalQuality() const;
    String ipAddress() const;
    const char* registrationState() const;
    TinyGsmClient& client();

private:
    bool initializeModem();

    HardwareSerial m_serial;
    TinyGsm m_modem;
    TinyGsmClient m_client;
    CellularState m_state;
    uint32_t m_lastAttemptMs;
    int m_signalQuality;
    String m_ipAddress;
};

#endif