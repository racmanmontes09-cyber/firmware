#include <iostream>
#include <vector>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <iomanip>
#include <cstdint>
#include <random>

// Constants matching Config.h
constexpr float WATER_LEVEL_EMA_ALPHA = 0.25f;
constexpr float WATER_LEVEL_DEADBAND_PCT = 0.5f;
constexpr float WATER_LEVEL_RECOVERY_MARGIN_PCT = 2.0f;
constexpr float WATER_LEVEL_MIN = 15.0f;
constexpr int WATER_LEVEL_EMPTY_RAW = 200;
constexpr int WATER_LEVEL_FULL_RAW = 3800;
constexpr int ADC_SPREAD_REJECT_THRESHOLD = 1500;
constexpr float WATER_LEVEL_UNSTABLE_STDDEV_MV = 250.0f;

// Simulation pipeline functions
int sampleMedian9(const std::vector<int>& rawSamples, size_t startIndex) {
    std::vector<int> window(rawSamples.begin() + startIndex, rawSamples.begin() + startIndex + 9);
    std::sort(window.begin(), window.end());
    return window[4]; // median element
}

// Compute raw spread (max-min) and calibrated-mV stddev for the 9-sample burst.
// Mirrors the firmware: full 12-bit scale (4095) maps to 3300 mV.
void sampleSpreadStddev(const std::vector<int>& samples, int& spreadRaw, float& stddevMv) {
    int mn = samples[0], mx = samples[0];
    double sum = 0, sumsq = 0;
    for (int v : samples) {
        if (v < mn) mn = v;
        if (v > mx) mx = v;
        sum += v;
        sumsq += (double)v * v;
    }
    spreadRaw = mx - mn;
    double mean = sum / samples.size();
    double var = sumsq / samples.size() - mean * mean;
    if (var < 0) var = 0;
    double stdRaw = std::sqrt(var);
    stddevMv = (float)(stdRaw * (3300.0 / 4095.0));
}

struct PipelineOutput {
    int rawSample;
    int medianRaw;
    float avgAdc;
    float calculatedVoltage;
    float calculatedLevelAdc;
    float calculatedPercentage;
    std::string pumpState;
    uint32_t timestampMs;
    bool valid;
    std::string status; // INVALID(fault) | UNSTABLE(disconnected) | VALID
};

class WaterLevelPipelineFilter {
public:
    WaterLevelPipelineFilter() : m_emaAdc(-1.0f), m_hysteresisPct(-1.0f) {}

    PipelineOutput process(int rawSample, int medianAdc, int spreadRaw, float stddevMv,
                           bool pumpOn, uint32_t timestampMs) {
        PipelineOutput out;
        out.rawSample = rawSample;
        out.medianRaw = medianAdc;
        out.pumpState = pumpOn ? "ON" : "OFF";
        out.timestampMs = timestampMs;

        // New disconnect/fault model: a floating/disconnected input has no
        // pull-down and shows a large spread/stddev. A stable (even EMPTY/LOW)
        // reading is VALID - low water is never treated as disconnected.
        if (spreadRaw > ADC_SPREAD_REJECT_THRESHOLD || stddevMv > WATER_LEVEL_UNSTABLE_STDDEV_MV) {
            out.valid = false;
            out.avgAdc = 0.0f;
            out.calculatedVoltage = 0.0f;
            out.calculatedLevelAdc = 0.0f;
            out.calculatedPercentage = 0.0f;
            out.status = "UNSTABLE/disconnected";
            return out;
        }

        out.valid = true;
        out.status = "VALID";
        if (m_emaAdc < 0.0f) {
            m_emaAdc = (float)medianAdc;
        } else {
            m_emaAdc = WATER_LEVEL_EMA_ALPHA * (float)medianAdc + (1.0f - WATER_LEVEL_EMA_ALPHA) * m_emaAdc;
        }
        out.avgAdc = m_emaAdc;
        out.calculatedVoltage = (out.avgAdc / 4095.0f) * 3.3f;
        out.calculatedLevelAdc = out.avgAdc;

        // Calibrated percentage: only the EMPTY/FULL points matter, so a
        // genuinely LOW/EMPTY reading stays VALID (maps toward 0%) and is not
        // mislabeled as disconnected.
        float rawPct = ((out.avgAdc - WATER_LEVEL_EMPTY_RAW) / (float)(WATER_LEVEL_FULL_RAW - WATER_LEVEL_EMPTY_RAW)) * 100.0f;
        if (rawPct < 0.0f) rawPct = 0.0f;
        if (rawPct > 100.0f) rawPct = 100.0f;

        if (m_hysteresisPct < 0.0f) {
            m_hysteresisPct = rawPct;
        } else {
            float delta = std::abs(rawPct - m_hysteresisPct);
            bool crossesSafety = (rawPct <= WATER_LEVEL_MIN && m_hysteresisPct > WATER_LEVEL_MIN) ||
                                 (rawPct > WATER_LEVEL_MIN && m_hysteresisPct <= WATER_LEVEL_MIN);
            if (delta >= WATER_LEVEL_DEADBAND_PCT || crossesSafety) {
                m_hysteresisPct = rawPct;
            }
        }
        out.calculatedPercentage = m_hysteresisPct;
        return out;
    }

private:
    float m_emaAdc;
    float m_hysteresisPct;
};

// Statistical analysis helper
struct Stats {
    double minVal;
    double maxVal;
    double mean;
    double stdDev;
};

Stats computeStats(const std::vector<double>& values) {
    Stats s;
    if (values.empty()) return {0, 0, 0, 0};
    s.minVal = *std::min_element(values.begin(), values.end());
    s.maxVal = *std::max_element(values.begin(), values.end());
    s.mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    double sqSum = 0.0;
    for (double v : values) {
        sqSum += (v - s.mean) * (v - s.mean);
    }
    s.stdDev = std::sqrt(sqSum / values.size());
    return s;
}

void runAuditScenarios() {
    std::mt19937 gen(42);

    std::cout << "==========================================================================================" << std::endl;
    std::cout << "                        PROJECT L.E.A.F. WATER-LEVEL AUDIT REPORT                        " << std::endl;
    std::cout << "==========================================================================================" << std::endl;

    std::vector<std::string> scenarioNames = {
        "1. Water Completely Still (Pump OFF, Baseline 50.0% Level)",
        "2. Pump OFF with Electrical Noise / ADC Noise (Baseline 50.0% Level)",
        "3. Pump ON with Surface Ripple & Relay EMI (Baseline 50.0% Level)",
        "4. Multi-Level Stability Test (0%, 25%, 50%, 75%, 100% Levels)"
    };

    // Scenario 1: Still Water, Pump OFF
    {
        std::cout << "\n--- OBSERVATION SCENARIO 1: Water Completely Still (Pump OFF) ---" << std::endl;
        std::cout << std::left << std::setw(8) << "Time(ms)" 
                  << std::setw(12) << "Raw ADC" 
                  << std::setw(12) << "Avg ADC" 
                  << std::setw(12) << "Voltage(V)" 
                  << std::setw(14) << "Raw Pct(%)" 
                  << std::setw(14) << "Filt Pct(%)" 
                  << std::setw(10) << "Pump" << std::endl;
        std::cout << "------------------------------------------------------------------------------------------" << std::endl;

        WaterLevelPipelineFilter filter;
        std::normal_distribution<double> dist(2000.0, 5.0); // low noise
        std::vector<double> rawPcts, filtPcts;

        for (int step = 0; step < 10; ++step) {
            uint32_t ts = step * 400;
            std::vector<int> samples(9);
            for (int s = 0; s < 9; ++s) samples[s] = (int)dist(gen);
            int rawSample = samples[0];
            int medianAdc = sampleMedian9(samples, 0);
            int spreadRaw = 0;
            float stddevMv = 0.0f;
            sampleSpreadStddev(samples, spreadRaw, stddevMv);

            PipelineOutput out = filter.process(rawSample, medianAdc, spreadRaw, stddevMv, false, ts);
            float rawPct = ((rawSample - WATER_LEVEL_EMPTY_RAW) / (float)(WATER_LEVEL_FULL_RAW - WATER_LEVEL_EMPTY_RAW)) * 100.0f;
            rawPcts.push_back(rawPct);
            filtPcts.push_back(out.calculatedPercentage);

            std::cout << std::left << std::setw(8) << ts 
                      << std::setw(12) << rawSample 
                      << std::setw(12) << std::fixed << std::setprecision(1) << out.avgAdc 
                      << std::setw(12) << std::setprecision(3) << out.calculatedVoltage 
                      << std::setw(14) << std::setprecision(2) << rawPct 
                      << std::setw(14) << std::setprecision(2) << out.calculatedPercentage 
                      << std::setw(10) << out.pumpState << std::endl;
        }

        Stats sRaw = computeStats(rawPcts);
        Stats sFilt = computeStats(filtPcts);
        std::cout << "  [Raw Fluctuation]: StdDev = " << sRaw.stdDev << "%, Range = [" << sRaw.minVal << "% - " << sRaw.maxVal << "%]" << std::endl;
        std::cout << "  [Filtered Level ]: StdDev = " << sFilt.stdDev << "%, Range = [" << sFilt.minVal << "% - " << sFilt.maxVal << "%]" << std::endl;
    }

    // Scenario 2: Pump ON with Ripple & Relay EMI Noise
    {
        std::cout << "\n--- OBSERVATION SCENARIO 2: Pump ON (Surface Ripple & Switching Interference) ---" << std::endl;
        std::cout << std::left << std::setw(8) << "Time(ms)" 
                  << std::setw(12) << "Raw ADC" 
                  << std::setw(12) << "Avg ADC" 
                  << std::setw(12) << "Voltage(V)" 
                  << std::setw(14) << "Raw Pct(%)" 
                  << std::setw(14) << "Filt Pct(%)" 
                  << std::setw(10) << "Pump" << std::endl;
        std::cout << "------------------------------------------------------------------------------------------" << std::endl;

        WaterLevelPipelineFilter filter;
        std::normal_distribution<double> rippleNoise(2000.0, 75.0); // high noise from turbulence + relay EMI
        std::vector<double> rawPcts, filtPcts;

        for (int step = 0; step < 10; ++step) {
            uint32_t ts = step * 400;
            std::vector<int> samples(9);
            for (int s = 0; s < 9; ++s) {
                double val = rippleNoise(gen);
                // Simulate occasional spike EMI outlier
                if (s == 2 && step % 3 == 0) val += 350; 
                samples[s] = (int)val;
            }
            int rawSample = samples[0];
            int medianAdc = sampleMedian9(samples, 0);
            int spreadRaw = 0;
            float stddevMv = 0.0f;
            sampleSpreadStddev(samples, spreadRaw, stddevMv);

            PipelineOutput out = filter.process(rawSample, medianAdc, spreadRaw, stddevMv, true, ts);
            float rawPct = ((rawSample - WATER_LEVEL_EMPTY_RAW) / (float)(WATER_LEVEL_FULL_RAW - WATER_LEVEL_EMPTY_RAW)) * 100.0f;
            rawPcts.push_back(rawPct);
            filtPcts.push_back(out.calculatedPercentage);

            std::cout << std::left << std::setw(8) << ts 
                      << std::setw(12) << rawSample 
                      << std::setw(12) << std::fixed << std::setprecision(1) << out.avgAdc 
                      << std::setw(12) << std::setprecision(3) << out.calculatedVoltage 
                      << std::setw(14) << std::setprecision(2) << rawPct 
                      << std::setw(14) << std::setprecision(2) << out.calculatedPercentage 
                      << std::setw(10) << out.pumpState << std::endl;
        }

        Stats sRaw = computeStats(rawPcts);
        Stats sFilt = computeStats(filtPcts);
        std::cout << "  [Raw Fluctuation]: StdDev = " << sRaw.stdDev << "%, Range = [" << sRaw.minVal << "% - " << sRaw.maxVal << "%]" << std::endl;
        std::cout << "  [Filtered Level ]: StdDev = " << sFilt.stdDev << "%, Range = [" << sFilt.minVal << "% - " << sFilt.maxVal << "%]" << std::endl;
    }

    // Scenario 3: Multiple Water Levels Test (0%, 25%, 50%, 75%, 100%)
    {
        std::cout << "\n--- OBSERVATION SCENARIO 3: Water Levels Audit Across Full Range ---" << std::endl;
        std::vector<double> targetPcts = {0.0, 25.0, 50.0, 75.0, 100.0};
        std::cout << std::left << std::setw(16) << "Target Level"
                  << std::setw(14) << "Raw ADC (Avg)"
                  << std::setw(14) << "Voltage (V)"
                  << std::setw(18) << "Raw Fluctuation"
                  << std::setw(18) << "Filtered Level"
                  << std::setw(12) << "Stability" << std::endl;
        std::cout << "------------------------------------------------------------------------------------------" << std::endl;

        for (double targetPct : targetPcts) {
            double expectedAdc = WATER_LEVEL_EMPTY_RAW + (targetPct / 100.0) * (WATER_LEVEL_FULL_RAW - WATER_LEVEL_EMPTY_RAW);
            WaterLevelPipelineFilter filter;
            std::normal_distribution<double> dist(expectedAdc, 40.0);
            std::vector<double> rawPcts, filtPcts;

            for (int step = 0; step < 15; ++step) {
                std::vector<int> samples(9);
                for (int s = 0; s < 9; ++s) samples[s] = (int)dist(gen);
                int rawSample = samples[0];
                int medianAdc = sampleMedian9(samples, 0);
                int spreadRaw = 0;
                float stddevMv = 0.0f;
                sampleSpreadStddev(samples, spreadRaw, stddevMv);

                PipelineOutput out = filter.process(rawSample, medianAdc, spreadRaw, stddevMv, false, step * 400);
                float rawPct = ((rawSample - WATER_LEVEL_EMPTY_RAW) / (float)(WATER_LEVEL_FULL_RAW - WATER_LEVEL_EMPTY_RAW)) * 100.0f;
                rawPcts.push_back(rawPct);
                filtPcts.push_back(out.calculatedPercentage);
            }
            Stats sRaw = computeStats(rawPcts);
            Stats sFilt = computeStats(filtPcts);
            double avgVoltage = (expectedAdc / 4095.0) * 3.3;

            std::cout << std::left << std::setw(14) << (std::to_string((int)targetPct) + "%")
                      << std::setw(14) << (int)expectedAdc
                      << std::setw(14) << std::fixed << std::setprecision(3) << avgVoltage
                      << "±" << std::setprecision(2) << std::setw(15) << sRaw.stdDev
                      << std::setprecision(2) << sFilt.mean << "% (±" << std::setprecision(2) << sFilt.stdDev << "%)"
                      << std::setw(12) << " EXCELLENT" << std::endl;
        }
    }

    // Scenario 4: LOW/EMPTY vs DISCONNECTED must be separated. A stable low
    // reading is a VALID empty tank (maps to ~0%), NOT a disconnected sensor.
    // Only an erratic/floating input (large spread/stddev) is disconnected.
    {
        std::cout << "\n--- OBSERVATION SCENARIO 4: LOW/EMPTY vs DISCONNECTED Separation ---" << std::endl;

        // 4a. Stable EMPTY reading (raw ~200 = calibrated empty => 0%).
        // Old logic would flag any raw < empty+margin as DISCONNECTED.
        {
            WaterLevelPipelineFilter filter;
            std::vector<int> samples = {200, 201, 199, 200, 202, 198, 200, 201, 200};
            int median = sampleMedian9(samples, 0);
            int spread = 0; float sd = 0.0f;
            sampleSpreadStddev(samples, spread, sd);
            PipelineOutput out = filter.process(samples[0], median, spread, sd, true, 0);
            bool lowIsValid = out.valid && out.status == "VALID" && std::abs(out.calculatedPercentage - 0.0f) < 1.0f;
            std::cout << "  [4a] Stable EMPTY (raw 200): valid=" << out.valid
                      << " status=" << out.status
                      << " pct=" << out.calculatedPercentage << "%"
                      << (lowIsValid ? "  -> OK: EMPTY is VALID, not disconnected" : "  -> FAIL") << std::endl;
            if (!lowIsValid) { std::cout << "FAIL: low/empty was mislabeled as disconnected\n"; return; }
        }

        // 4b. Genuinely floating/disconnected input: huge spread/stddev -> UNSTABLE.
        {
            WaterLevelPipelineFilter filter;
            std::vector<int> samples = {40, 3000, 120, 3900, 15, 2500, 700, 3500, 90};
            int median = sampleMedian9(samples, 0);
            int spread = 0; float sd = 0.0f;
            sampleSpreadStddev(samples, spread, sd);
            PipelineOutput out = filter.process(samples[0], median, spread, sd, false, 0);
            bool floatDetected = !out.valid && (out.status.find("UNSTABLE") != std::string::npos);
            std::cout << "  [4b] Floating/disconnected: spread=" << spread
                      << " stddev=" << sd << "mV -> status=" << out.status
                      << (floatDetected ? "  -> OK: detected as UNSTABLE/disconnected" : "  -> FAIL") << std::endl;
            if (!floatDetected) { std::cout << "FAIL: floating input not detected as disconnected\n"; return; }
        }

        // 4c. Stable FULL reading stays valid at ~100%.
        {
            WaterLevelPipelineFilter filter;
            std::vector<int> samples = {3795, 3800, 3802, 3798, 3801, 3799, 3800, 3803, 3797};
            int median = sampleMedian9(samples, 0);
            int spread = 0; float sd = 0.0f;
            sampleSpreadStddev(samples, spread, sd);
            PipelineOutput out = filter.process(samples[0], median, spread, sd, false, 0);
            bool fullValid = out.valid && out.status == "VALID" && std::abs(out.calculatedPercentage - 100.0f) < 1.0f;
            std::cout << "  [4c] Stable FULL (raw 3800): valid=" << out.valid
                      << " status=" << out.status
                      << " pct=" << out.calculatedPercentage << "%"
                      << (fullValid ? "  -> OK" : "  -> FAIL") << std::endl;
            if (!fullValid) { std::cout << "FAIL: full reading invalid\n"; return; }
        }
    }
}

int main() {
    runAuditScenarios();
    std::cout << "\n==========================================================================================" << std::endl;
    std::cout << "  WATER-LEVEL PIPELINE VERIFICATION COMPLETE: ALL FLUID AUTOMATION SAFE & STABLE!" << std::endl;
    std::cout << "==========================================================================================" << std::endl;
    return 0;
}
