#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <Adafruit_SHT31.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "Config.h"

struct SensorState {
    bool enabled = false;
    bool valid = false;
    float value = 0.0f;
    int raw = 0;
    uint32_t lastGoodMs = 0;
    uint8_t consecutiveBadReads = 0;
    uint8_t consecutiveGoodReads = 0;
};

struct SensorReadings {
    SensorState airTemperatureState;
    SensorState humidityState;
    SensorState waterTemperatureState;
    SensorState phState;
    SensorState ecState;
    SensorState waterLevelState;
    SensorState waterFlowState;
    bool airTemperatureValid = false;
    bool humidityValid = false;
    bool waterTemperatureValid = false;
    bool phValid = false;
    bool ecValid = false;
    bool waterFlowValid = false;
    bool waterLevelValid = false;
    float airTemperature = 0.0f;
    float humidity = 0.0f;
    float waterTemperature = 0.0f;
    float ph = 0.0f;
    float ec = 0.0f;
    float waterFlow = 0.0f;
    float flowRate = 0.0f;
    float totalFlow = 0.0f;
    float waterLevel = 0.0f;
    float batteryVoltage = 0.0f;
    int signalStrength = 0;
    uint32_t sequenceNumber = 0;
};

class Sensors {
public:
    void begin();
    SensorReadings read();
    bool loadCalibration();
    bool saveCalibration();
    void setPhOffset(float offset);
    void setEcFactor(float factor);
    void setWaterLevelMapping(float emptyPercent, float fullPercent);
    bool calibratePh(float targetPh, float measuredVoltage);
    bool calibratePhTwoPoint(float target1, float measured1, float target2, float measured2);
    bool interactiveCalibratePh();
    void diagnosePhPotentiometer();
    bool calibrateEc(float referenceMsPerCm);
    bool calibrateEc(float referenceMsPerCm, float measuredMsPerCm);
    bool calibrateWaterLevel(float emptyRaw, float fullRaw);
    // Test helper: simulate flow pulses (for diagnostics)
    void simulateFlowPulses(int n);

private:
    bool readSHT31();
    bool readWaterTemperature();
    bool readWaterLevel();
    bool readPH();
    bool readEC();
    int readEcAdc(uint32_t& averageMv, int& minRaw, int& maxRaw);
    bool calibrateEcFromVoltage(float referenceMsPerCm, float voltageMv, float temperatureC);
    float currentEcTemperatureC() const;
    void updateFlowReading();
    bool isFresh(const SensorState& state) const;

    uint32_t m_sequenceNumber = 0;
    Adafruit_SHT31 m_sht31;
    bool m_sht31Initialized = false;
    OneWire m_oneWire{DS18B20_PIN};
    DallasTemperature m_ds18b20{&m_oneWire};
    bool m_ds18b20Initialized = false;
    bool m_waterLevelInitialized = false;
    bool m_phInitialized = false;
    bool m_ecInitialized = false;
    bool m_flowInitialized = false;
    float m_lastWaterTemperature = 0.0f;
    float m_lastAirTemperature = 0.0f;
    float m_lastHumidity = 0.0f;
    float m_lastWaterLevel = 0.0f;
    float m_lastPhValue = 0.0f;
    float m_phEmaValue = 0.0f;
    bool m_phEmaInitialized = false;
    float m_lastEcValue = 0.0f;
    float m_flowRate = 0.0f;
    float m_totalFlow = 0.0f;
    unsigned long m_lastFlowCalculationMs = 0;
    float m_phOffset = PH_CALIBRATION_OFFSET;
    float m_phSlope = -1000.0f / PH_MV_PER_PH_SLOPE;    // pH/V (negative)
    float m_phIntercept = 7.0f - m_phSlope * PH_MV_REF_VOLTAGE;  // pH at 0V
    float m_ecKValueLow = EC_CALIBRATION_FACTOR;
    float m_ecKValueHigh = EC_CALIBRATION_FACTOR;
    float m_ecActiveKValue = EC_CALIBRATION_FACTOR;
    float m_waterLevelEmptyPercent = 0.0f;
    float m_waterLevelFullPercent = 100.0f;
    int m_waterLevelEmptyRaw = 0;
    int m_waterLevelFullRaw = 4095;
    float m_waterLevelEmaAdc = -1.0f;
    float m_waterLevelHysteresisPct = -1.0f;
    SensorState m_airTemperatureState;
    SensorState m_humidityState;
    SensorState m_waterTemperatureState;
    SensorState m_phState;
    SensorState m_ecState;
    SensorState m_waterLevelState;
    SensorState m_flowState;
    // DS18B20 async conversion state
    bool m_ds18b20RequestPending = false;
    unsigned long m_ds18b20RequestMs = 0;
    bool m_ds18b20LastReadingValid = false;
};

#endif
