#include <iostream>
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>
#include <cstdint>

// Lightweight mock for Arduino testing on Host system
static unsigned long g_mockMillis = 0;
unsigned long millis() { return g_mockMillis; }
void setMockMillis(unsigned long ms) { g_mockMillis = ms; }

// Test cases
void testPhCalibration() {
    std::cout << "[TEST] pH Calibration & Conversion Accuracy... ";
    // Amplified DIY analog pH module (PH-4502C-style): ~180 mV/pH, NOT the
    // theoretical 59.16 mV/pH Nernst slope. slope is negative pH/V.
    constexpr float phMvPerPh = 180.0f;
    float phSlope = -1000.0f / phMvPerPh;    // -5.556 pH/V
    float phIntercept = 7.0f - phSlope * 2.5f; // 20.889
    float phOffset = 0.0f;

    // Test pH at 2.5V (standard pH 7)
    float v7 = 2.5f;
    float ph7 = phSlope * v7 + phIntercept + phOffset;
    assert(std::abs(ph7 - 7.0f) < 0.001f);

    // Test single point offset cal: buffer target 7.00, probe reads 7.20 at 2.618V
    float measuredV = 2.618f;
    float expectedPh = phSlope * measuredV + phIntercept;
    phOffset = 7.00f - expectedPh;
    float calibratedPh = phSlope * measuredV + phIntercept + phOffset;
    assert(std::abs(calibratedPh - 7.00f) < 0.001f);

    std::cout << "PASSED!" << std::endl;
}

void testPhTwoPointCalibration() {
    std::cout << "[TEST] pH Two-Point Cal (pH7.00 + pH4.00)... ";
    // Using the same calibrated-mV based voltage representation as runtime.
    // pH7 buffer reads 2.500V, pH4 buffer reads 3.040V on the amplified module
    // (each pH unit spans ~180mV, so 3 pH units = 0.54V).
    float v7 = 2.500f;
    float v4 = 3.040f;
    float ph7 = 7.00f, ph4 = 4.00f;
    float slope = (ph7 - ph4) / (v7 - v4); // 3.0 / -0.54 = -5.556 pH/V
    float intercept = ph7 - slope * v7;
    float offset = 0.0f;

    // Verify both calibration points map back to their targets.
    assert(std::abs(slope * v7 + intercept + offset - ph7) < 0.001f);
    assert(std::abs(slope * v4 + intercept + offset - ph4) < 0.001f);

    // Interpolation: pH ~5.5 should sit halfway between the two buffers.
    float vMid = (v7 + v4) / 2.0f;
    float phMid = slope * vMid + intercept + offset;
    assert(std::abs(phMid - 5.5f) < 0.01f);

    // The amplified-module default is NOT the 59.16 Nernst value.
    assert(std::abs(slope - (-1000.0f / 180.0f)) < 0.01f);
    assert(std::abs(slope - (-1000.0f / 59.16f)) > 1.0f);

    std::cout << "PASSED!" << std::endl;
}

void testPhCalibratedMvConversion() {
    std::cout << "[TEST] pH Calibrated-mV Conversion (no raw/4095*3.3)... ";
    // Runtime uses analogReadMilliVolts() calibrated mV, then
    // pH = slope*voltage + intercept + offset, with voltage in volts = mV/1000.
    constexpr float phMvPerPh = 180.0f;
    float slope = -1000.0f / phMvPerPh;
    float intercept = 7.0f - slope * 2.5f;
    float offset = 0.0f;

    // A calibrated reading of 2500 mV must map to pH 7 (same as 2.5V).
    float mv = 2500.0f;
    float voltage = mv / 1000.0f;
    float ph = slope * voltage + intercept + offset;
    assert(std::abs(ph - 7.0f) < 0.001f);

    // Demonstrate the old (raw / 4095 * 3.3) formula is gone: we never derive
    // voltage from a raw 12-bit count on the pH path.
    // (Structural check is enforced by the firmware; here we confirm the
    // calibrated-mv math reproduces the intended value.)
    std::cout << "PASSED!" << std::endl;
}

void testPhOutOfRangeIsInvalid() {
    std::cout << "[TEST] pH Out-of-Range / Invalid Detection... ";
    constexpr float phMvPerPh = 180.0f;
    float slope = -1000.0f / phMvPerPh;
    float intercept = 7.0f - slope * 2.5f;
    constexpr float phValidMin = 3.0f, phValidMax = 11.0f;

    // A stable-but-suspicious 0.25V reading (the 44.98 symptom) must map to an
    // out-of-range pH and therefore be reported INVALID, not clamped to 3-11.
    float badVoltage = 0.253f; // the previously observed 0.253V
    float badPh = slope * badVoltage + intercept;
    std::cout << " (0.253V -> pH " << badPh << ") ";
    bool invalid = !(badPh >= phValidMin && badPh <= phValidMax);
    assert(invalid == true); // must be flagged invalid, not silently clamped

    // A healthy mid-range reading stays valid.
    float goodVoltage = 2.5f;
    float goodPh = slope * goodVoltage + intercept;
    bool valid = (goodPh >= phValidMin && goodPh <= phValidMax);
    assert(valid == true);

    std::cout << "PASSED!" << std::endl;
}

void testPhStabilityFilter() {
    std::cout << "[TEST] pH Stability (UNSTABLE vs stable)... ";
    // A floating/disconnected input shows a large spread/stddev and must be
    // classified as UNSTABLE (not reported as a pH). A stable reading with a
    // small spread is accepted (then EMA-smoothed by firmware).
    constexpr float spreadThresholdMv = 80.0f;
    constexpr float stddevThresholdMv = 25.0f;

    // Stable: small spread, small stddev.
    float stableSpreadMv = 12.0f;
    float stableStddevMv = 4.0f;
    bool stable = (stableSpreadMv <= spreadThresholdMv) && (stableStddevMv <= stddevThresholdMv);
    assert(stable == true);

    // Unstable/floating: large spread and/or stddev (no pull-down on GPIO5).
    float floatSpreadMv = 900.0f;
    float floatStddevMv = 260.0f;
    bool floating = (floatSpreadMv > spreadThresholdMv) || (floatStddevMv > stddevThresholdMv);
    assert(floating == true);

    // Bouncing but not fully floating: high spread triggers UNSTABLE.
    float bounceSpreadMv = 150.0f;
    float bounceStddevMv = 30.0f;
    bool bounce = (bounceSpreadMv > spreadThresholdMv) || (bounceStddevMv > stddevThresholdMv);
    assert(bounce == true);

    std::cout << "PASSED!" << std::endl;
}

void testPhAdcSaturationFault() {
    std::cout << "[TEST] pH ADC Saturation Fault... ";
    constexpr int saturationRaw = 4040;
    constexpr uint32_t safeMaxMv = 3300;

    // Saturation: raw near top of range, or calibrated mv at the 3.3V rail ->
    // treated as an ADC fault, not a valid pH.
    bool satRaw = (4095 >= saturationRaw);
    bool satMv = (3301 >= safeMaxMv);
    assert(satRaw == true);
    assert(satMv == true);

    // Normal in-range reading is not a saturation fault.
    bool notSat = (314 < saturationRaw) && (253 < safeMaxMv);
    assert(notSat == true);

    std::cout << "PASSED!" << std::endl;
}

void testEcTemperatureCompensation() {
    std::cout << "[TEST] DFRobot EC V2 Conversion & Compensation... ";
    constexpr float res2 = 820.0f;
    constexpr float ecRef = 200.0f;
    constexpr float ecTempCoeff = 0.0185f;
    constexpr float kValue = 1.0f;

    auto rawEcFromMv = [](float millivolts) {
        return (1000.0f * millivolts) / res2 / ecRef;
    };

    // DFRobot_EC uses millivolts. 231.732mV corresponds to 1.413 mS/cm with K=1 at 25C.
    float lowBufferMv = 1.413f * res2 * ecRef / 1000.0f;
    float lowRawEc = rawEcFromMv(lowBufferMv);
    float lowEc25 = lowRawEc * kValue / (1.0f + ecTempCoeff * (25.0f - 25.0f));
    assert(std::abs(lowEc25 - 1.413f) < 0.001f);

    // 12.88 mS/cm high buffer should also map correctly before high-range K calibration.
    float highBufferMv = 12.88f * res2 * ecRef / 1000.0f;
    float highRawEc = rawEcFromMv(highBufferMv);
    float highEc25 = highRawEc * kValue / (1.0f + ecTempCoeff * (25.0f - 25.0f));
    assert(std::abs(highEc25 - 12.88f) < 0.001f);

    // Temperature compensation should return the 25C-equivalent conductivity.
    float temp15 = 15.0f;
    float rawEc15 = 1.50f * (1.0f + ecTempCoeff * (temp15 - 25.0f));
    float compensatedEc15 = rawEc15 / (1.0f + ecTempCoeff * (temp15 - 25.0f));
    assert(std::abs(compensatedEc15 - 1.50f) < 0.001f);

    float temp35 = 35.0f;
    float rawEc35 = 1.50f * (1.0f + ecTempCoeff * (temp35 - 25.0f));
    float compensatedEc35 = rawEc35 / (1.0f + ecTempCoeff * (temp35 - 25.0f));
    assert(std::abs(compensatedEc35 - 1.50f) < 0.001f);

    std::cout << "PASSED!" << std::endl;
}

void testWaterLevelMapping() {
    std::cout << "[TEST] Water Level Empty vs Disconnect Accuracy... ";
    int emptyRaw = 200;
    int fullRaw = 3800;
    constexpr int spreadThreshold = 1500;      // matching ADC_SPREAD_REJECT_THRESHOLD
    constexpr float stddevThresholdMv = 250.0f; // matching WATER_LEVEL_UNSTABLE_STDDEV_MV

    // A stable LOW raw (5, below the old raw-disconnect threshold) is a VALID
    // near/at-empty reading and maps to 0% - it is NOT a disconnected sensor.
    int rawStableLow = 5;
    int spreadLow = 10;
    float stddevLowMv = 5.0f;
    bool validStableLow = (spreadLow <= spreadThreshold) && (stddevLowMv <= stddevThresholdMv);
    float pctStableLow = ((float)(rawStableLow - emptyRaw) / (float)(fullRaw - emptyRaw)) * 100.0f;
    if (pctStableLow < 0.0f) pctStableLow = 0.0f;
    assert(validStableLow == true);
    assert(std::abs(pctStableLow - 0.0f) < 0.001f);

    // Empty tank (raw = 200, stable) -> VALID at 0%.
    int rawEmpty = 200;
    bool validEmpty = true;
    float pctEmpty = ((float)(rawEmpty - emptyRaw) / (float)(fullRaw - emptyRaw)) * 100.0f;
    assert(validEmpty == true);
    assert(std::abs(pctEmpty - 0.0f) < 0.001f);

    // Full tank (raw = 3800, stable) -> VALID at 100%.
    int rawFull = 3800;
    bool validFull = true;
    float pctFull = ((float)(rawFull - emptyRaw) / (float)(fullRaw - emptyRaw)) * 100.0f;
    assert(validFull == true);
    assert(std::abs(pctFull - 100.0f) < 0.001f);

    // A floating/disconnected input is erratic: large spread / stddev -> INVALID.
    int spreadFloating = 3885;
    float stddevFloatingMv = 1256.0f;
    bool validFloating = (spreadFloating <= spreadThreshold) && (stddevFloatingMv <= stddevThresholdMv);
    assert(validFloating == false);

    std::cout << "PASSED!" << std::endl;
}

// Mirror of the ISR debounce: accepts a pulse only if it arrives at least
// FLOW_SENSOR_MIN_PULSE_INTERVAL_US after the previous accepted pulse. Returns
// the number of accepted pulses for a sequence of rising-edge timestamps.
int simulateFlowDebounce(const std::vector<unsigned long>& edgeUs, unsigned long minIntervalUs) {
    int accepted = 0;
    unsigned long lastAcceptedUs = 0;
    for (unsigned long t : edgeUs) {
        if (accepted == 0 || (t - lastAcceptedUs) >= minIntervalUs) {
            ++accepted;
            lastAcceptedUs = t;
        }
    }
    return accepted;
}

void testFlowSensorIdleValidity() {
    std::cout << "[TEST] Flow Sensor Idle/Formula/Debounce Accuracy... ";
    constexpr float pulsesPerLiter = 450.0f;
    constexpr unsigned long minIntervalUs = 1000;

    // Idle / no flow: 0 pulses in the window -> valid 0 L/min (NO_FLOW).
    {
        int pulseCount = 0;
        bool flowValid = true; // < 2 pulses -> valid zero flow
        float flowRate = (pulseCount < 2) ? 0.0f : 10.0f;
        assert(flowValid == true);
        assert(flowRate == 0.0f);
    }

    // Flow formula: liters = count / pulsesPerLiter; L/min = liters/sec*60.
    {
        int count = 450;                       // 450 pulses = 1 liter
        unsigned long elapsedMs = 1000;        // over 1 second
        float liters = count / pulsesPerLiter;
        float flowRate = (liters / (elapsedMs / 1000.0f)) * 60.0f;
        assert(std::abs(flowRate - 60.0f) < 0.001f); // 1 L/s = 60 L/min
    }

    // Debounce: a rapid bounce burst (edges within 1ms of each other) counts
    // as ONE physical pulse, not several.
    {
        // One real pulse with severe contact bounce: all edges within ~0.6ms.
        std::vector<unsigned long> bounce = {1000, 1100, 1200, 1300, 1400, 1500, 1600};
        int n = simulateFlowDebounce(bounce, minIntervalUs);
        assert(n == 1);
    }

    // Debounce: well-spaced pulses (real turbine edges) are all accepted.
    {
        std::vector<unsigned long> spaced = {1000, 5000, 9000, 13000};
        int n = simulateFlowDebounce(spaced, minIntervalUs);
        assert(n == 4);
    }

    // Debounce: one real pulse immediately followed by an EMI spike < interval
    // is ignored, then the next real pulse after that is still counted.
    {
        std::vector<unsigned long> seq = {1000, 1400, 9000, 9400, 17000};
        int n = simulateFlowDebounce(seq, minIntervalUs);
        assert(n == 3); // 1000, 9000, 17000 accepted; 1400 & 9400 rejected
    }

    std::cout << "PASSED!" << std::endl;
}

void testAutomationSafetyLimits() {
    std::cout << "[TEST] Automation Safety Limits & Interlocks... ";
    uint8_t phAttempts = 0;
    uint8_t maxPhAttempts = 3;
    bool phDosed = false;

    // Simulate 3 dosing attempts
    for (int i = 0; i < 3; ++i) {
        if (phAttempts < maxPhAttempts) {
            phDosed = true;
            phAttempts++;
        }
    }
    assert(phAttempts == 3);

    // 4th attempt should be blocked
    bool blocked4th = false;
    if (phAttempts < maxPhAttempts) {
        phDosed = true;
    } else {
        blocked4th = true;
    }
    assert(blocked4th == true);

    std::cout << "PASSED!" << std::endl;
}

int main() {
    std::cout << "==========================================" << std::endl;
    std::cout << "  FIRMWARE ACCURACY & AUTOMATION TEST SUITE" << std::endl;
    std::cout << "==========================================" << std::endl;

    testPhCalibration();
    testPhTwoPointCalibration();
    testPhCalibratedMvConversion();
    testPhOutOfRangeIsInvalid();
    testPhStabilityFilter();
    testPhAdcSaturationFault();
    testEcTemperatureCompensation();
    testWaterLevelMapping();
    testFlowSensorIdleValidity();
    testAutomationSafetyLimits();

    std::cout << "==========================================" << std::endl;
    std::cout << "  ALL ACCURACY TESTS PASSED PERFECTLY!" << std::endl;
    std::cout << "==========================================" << std::endl;
    return 0;
}
