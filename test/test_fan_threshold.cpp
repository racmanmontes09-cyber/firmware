#include <cassert>
#include <iostream>
#include <cmath>

#include "FanAutomation.h"

static int tests_run = 0;
static int tests_passed = 0;

void assertFanThreshold(float maximum, float airTemperature, bool expected, bool currentState = false) {
    tests_run++;
    bool actual = automation::coolingFanShouldRun(airTemperature, maximum, currentState);
    if (actual != expected) {
        std::cerr << "FAIL: coolingFanShouldRun(air=" << airTemperature
                  << ", max=" << maximum
                  << ", state=" << (currentState ? "ON" : "OFF")
                  << ") expected=" << (expected ? "ON" : "OFF")
                  << " actual=" << (actual ? "ON" : "OFF") << std::endl;
    } else {
        tests_passed++;
    }
    assert(actual == expected);
}

int main() {
    // ── Original tests (fan OFF → turn-on threshold) ──
    std::cout << "[TEST] Fan automation: turn-on threshold (fan OFF)... ";

    assertFanThreshold(25.0f, 24.9f, false);
    assertFanThreshold(25.0f, 25.0f, true);
    assertFanThreshold(25.0f, 25.1f, true);

    assertFanThreshold(30.0f, 29.9f, false);
    assertFanThreshold(30.0f, 30.0f, true);
    assertFanThreshold(30.0f, 30.1f, true);

    std::cout << "PASSED! (" << tests_passed << "/" << tests_run << ")" << std::endl;

    // ── Hysteresis tests (fan already ON → stay-on band) ──
    std::cout << "[TEST] Fan automation: hysteresis band (fan ON)... ";

    // With FAN_HYSTERESIS_C = 1.0 (default from Config.h):
    // Fan stays ON while temperature > (max - 1.0)
    // Fan turns OFF when temperature <= (max - 1.0)

    // max=25.0, hysteresis=1.0 → off threshold = 24.0
    assertFanThreshold(25.0f, 24.9f, true, true);   // 24.9 > 24.0 → stay ON
    assertFanThreshold(25.0f, 24.1f, true, true);   // 24.1 > 24.0 → stay ON
    assertFanThreshold(25.0f, 24.0f, false, true);  // 24.0 <= 24.0 → turn OFF
    assertFanThreshold(25.0f, 23.9f, false, true);  // 23.9 <= 24.0 → turn OFF

    // max=30.0, hysteresis=1.0 → off threshold = 29.0
    assertFanThreshold(30.0f, 29.9f, true, true);   // 29.9 > 29.0 → stay ON
    assertFanThreshold(30.0f, 29.1f, true, true);   // 29.1 > 29.0 → stay ON
    assertFanThreshold(30.0f, 29.0f, false, true);  // 29.0 <= 29.0 → turn OFF
    assertFanThreshold(30.0f, 28.9f, false, true);  // 28.9 <= 29.0 → turn OFF

    std::cout << "PASSED! (" << tests_passed << "/" << tests_run << ")" << std::endl;

    // ── Boundary tests ──
    std::cout << "[TEST] Fan automation: boundary conditions... ";

    // Exact threshold: fan OFF, temp == max → turn ON
    assertFanThreshold(25.0f, 25.0f, true, false);

    // Exact off threshold: fan ON, temp == (max - hysteresis) → turn OFF
    assertFanThreshold(25.0f, 24.0f, false, true);

    // Just below off threshold: fan ON → turn OFF
    assertFanThreshold(25.0f, 23.99f, false, true);

    // Default currentState parameter (false) → same as fan OFF
    assertFanThreshold(25.0f, 25.0f, true);        // no state arg → OFF
    assertFanThreshold(25.0f, 24.9f, false);       // no state arg → OFF

    std::cout << "PASSED! (" << tests_passed << "/" << tests_run << ")" << std::endl;

    // ── Feedback loop simulation ──
    std::cout << "[TEST] Fan automation: simulated feedback loop... ";

    // Simulate a scenario where temperature oscillates around the threshold.
    // Without hysteresis, the fan would flip every cycle.
    // With hysteresis (1.0°C), the fan turns ON at 25.0 and stays ON above 24.0.
    bool fanState = false;
    float temps[] = {25.0f, 24.9f, 25.1f, 24.8f, 25.0f, 24.5f, 24.1f, 23.9f, 24.0f, 25.2f};
    int toggleCount = 0;
    bool lastState = fanState;

    for (float t : temps) {
        fanState = automation::coolingFanShouldRun(t, 25.0f, fanState);
        if (fanState != lastState) toggleCount++;
        lastState = fanState;
    }

    // With hysteresis: OFF->ON at 25.0, stays ON while above 24.0,
    // ON->OFF at 23.9, remains OFF at 24.0, then OFF->ON at 25.2.
    // Without hysteresis it would be many more.
    assert(toggleCount <= 4);  // hysteresis should drastically reduce toggles

    std::cout << "PASSED! (" << tests_passed << "/" << tests_run << " toggles=" << toggleCount << ")" << std::endl;

    std::cout << "\nAll " << tests_passed << "/" << tests_run << " tests passed." << std::endl;
    return 0;
}
