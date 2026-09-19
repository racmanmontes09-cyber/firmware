#include "CellularManager.h"

CellularManager::CellularManager()
    : m_serial(1),
      m_modem(m_serial),
      m_client(m_modem),
    m_state(CELLULAR_ENABLED ? CellularState::OFFLINE : CellularState::CELLULAR_DISABLED),
      m_lastAttemptMs(0),
      m_signalQuality(0),
      m_ipAddress("0.0.0.0") {}

void CellularManager::begin() {
    if (!CELLULAR_ENABLED) {
        Serial.println("[A7670C] Disabled by configuration");
        return;
    }

    if (CELLULAR_UART_RX_PIN < 0 || CELLULAR_UART_TX_PIN < 0) {
        m_state = CellularState::ERROR;
        Serial.println("[A7670C] UART pins are not configured; refusing to start");
        return;
    }

    m_serial.begin(CELLULAR_UART_BAUD, SERIAL_8N1, CELLULAR_UART_RX_PIN, CELLULAR_UART_TX_PIN);
    m_state = CellularState::STARTING;
    m_lastAttemptMs = 0;
    Serial.printf("[A7670C] UART ready RX=%d TX=%d baud=%lu\n",
                  CELLULAR_UART_RX_PIN, CELLULAR_UART_TX_PIN, CELLULAR_UART_BAUD);
}

bool CellularManager::initializeModem() {
    if (!m_modem.testAT(1000)) {
        return false;
    }
    if (m_modem.getSimStatus(2000) != SIM_READY) {
        Serial.println("[A7670C] SIM not ready");
        return false;
    }
    m_signalQuality = m_modem.getSignalQuality();
    if (!m_modem.waitForNetwork(1000, true)) {
        return false;
    }
    Serial.printf("[A7670C] Network registered signal=%d operator=%s\n",
                  m_signalQuality, m_modem.getOperator().c_str());
    m_state = CellularState::REGISTERING;
    return true;
}

void CellularManager::loop() {
    if (!isEnabled() || m_state == CellularState::CONNECTED) return;
    uint32_t now = millis();
    if (m_lastAttemptMs != 0 && now - m_lastAttemptMs < CELLULAR_INIT_RETRY_MS) return;
    m_lastAttemptMs = now;
    if (initializeModem()) {
        Serial.println("[A7670C] Ready for data connection");
    } else {
        m_state = CellularState::ERROR;
        Serial.println("[A7670C] Initialization failed; retry scheduled");
    }
}

bool CellularManager::startDataConnection() {
    if (!isReady() || String(CELLULAR_APN).isEmpty()) {
        Serial.println("[A7670C] APN missing or modem not registered");
        return false;
    }
    if (!m_modem.gprsConnect(CELLULAR_APN, CELLULAR_APN_USER, CELLULAR_APN_PASSWORD)) {
        m_state = CellularState::ERROR;
        return false;
    }
    m_ipAddress = m_modem.localIP().toString();
    m_state = CellularState::CONNECTED;
    Serial.printf("[A7670C] Data connection established IP=%s\n", m_ipAddress.c_str());
    return true;
}

void CellularManager::stopDataConnection() {
    if (!isEnabled()) return;
    m_modem.gprsDisconnect();
    m_ipAddress = "0.0.0.0";
    m_state = CellularState::REGISTERING;
}

bool CellularManager::isEnabled() const { return CELLULAR_ENABLED != 0; }
bool CellularManager::isReady() const {
    return m_state == CellularState::REGISTERING || m_state == CellularState::CONNECTED;
}
CellularState CellularManager::state() const { return m_state; }
int CellularManager::signalQuality() const { return m_signalQuality; }
String CellularManager::ipAddress() const { return m_ipAddress; }
const char* CellularManager::registrationState() const {
    return isReady() ? "REGISTERED" : "NOT_REGISTERED";
}
TinyGsmClient& CellularManager::client() { return m_client; }