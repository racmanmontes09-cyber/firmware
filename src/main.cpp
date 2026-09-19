#include <Arduino.h>
#include "Config.h"
#include "WifiManager.h"
#include "Sensors.h"
#include "Actuators.h"
#include "ApiClient.h"
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include "TelemetryQueue.h"
#include "OtaRollback.h"
#include "Diag.h"
#include "PerfBenchmark.h"
#include "RuntimeConfig.h"

namespace {
    constexpr uint32_t RTC_BOOT_MAGIC = 0x4C454146UL;
    RTC_DATA_ATTR uint32_t g_rtcBootMagic = 0;
    RTC_DATA_ATTR uint32_t g_rtcBootCount = 0;
    unsigned long g_bootStartMs = 0;
    WifiManager g_wifiManager;
    ApiClient g_apiClient;
    uint32_t g_nextSensorMs = 0;
    uint32_t g_nextSafetyMs = 0;
    uint32_t g_nextTelemetryMs = 0;
    uint32_t g_nextHeartbeatMs = 0;
    uint32_t g_nextWifiMaintenanceMs = 0;
    uint32_t g_nextMqttMaintenanceMs = 0;
    uint32_t g_nextQueuedTelemetryFlushMs = 0;
    SensorReadings g_latestReadings;
    bool g_hasLatestReadings = false;
    bool g_ledState = false;
    unsigned long g_lastLedBlinkMs = 0;

    bool timerDue(uint32_t now, uint32_t& nextRun, uint32_t intervalMs) {
        if (nextRun == 0) {
            nextRun = now + intervalMs;
            return true;
        }
        if (static_cast<uint32_t>(now - nextRun) >= 0x80000000UL) return false;

        nextRun += intervalMs;
        // Drop stale periods after a long block instead of replaying missed work.
        if (static_cast<uint32_t>(now - nextRun) < 0x80000000UL) {
            nextRun = now + intervalMs;
        }
        return true;
    }

    void feedWatchdog() {
        esp_task_wdt_reset();
    }

    void logTaskDuration(const char* name, unsigned long elapsedUs, unsigned long intervalMs) {
        if (elapsedUs >= intervalMs * 1000UL / 2UL) {
            Serial.printf("[TIMING] WARNING %s took %luus (interval=%lums)\n",
                          name, elapsedUs, intervalMs);
        }
        if (elapsedUs >= 2000000UL) {
            Serial.printf("[TIMING] CRITICAL blocking operation: %s took %luus\n",
                          name, elapsedUs);
        }
    }

#if LEAF_FAN_DIAG
    const char* resetReasonName() {
        switch (esp_reset_reason()) {
            case ESP_RST_UNKNOWN:    return "UNKNOWN";
            case ESP_RST_POWERON:    return "POWERON";
            case ESP_RST_EXT:        return "EXT_RESET";
            case ESP_RST_SW:         return "SW_RESET";
            case ESP_RST_PANIC:      return "PANIC";
            case ESP_RST_INT_WDT:    return "INT_WDT";
            case ESP_RST_TASK_WDT:   return "TASK_WDT";
            case ESP_RST_WDT:        return "OTHER_WDT";
            case ESP_RST_DEEPSLEEP:  return "DEEPSLEEP";
            case ESP_RST_BROWNOUT:   return "BROWNOUT";
            case ESP_RST_SDIO:       return "SDIO";
            default:                 return "OTHER";
        }
    }
#endif
}

namespace hw {
    Sensors g_sensors;
    Actuators g_actuators;
}

void setup() {
    g_bootStartMs = millis();
    Serial.begin(115200);
    Serial.setTimeout(0);
    if (g_rtcBootMagic != RTC_BOOT_MAGIC) {
        g_rtcBootMagic = RTC_BOOT_MAGIC;
        g_rtcBootCount = 0;
    }
    ++g_rtcBootCount;
    Serial.printf("[BOOT] Reset reason=%d\n", static_cast<int>(esp_reset_reason()));
#if LEAF_FAN_DIAG
    Serial.printf("[BOOT] reset_reason=%s boot_count=%lu\n",
                  resetReasonName(), static_cast<unsigned long>(g_rtcBootCount));
#endif
    Serial.printf("[BOOT] Boot count=%lu\n", static_cast<unsigned long>(g_rtcBootCount));
    Serial.printf("[BOOT] Free heap=%u min heap=%u PSRAM=%s chip rev=%u firmware=%s\n",
                  ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                  psramFound() ? "yes" : "no", ESP.getChipRevision(), FIRMWARE_VERSION);

    Serial.println("[BOOT] Milestone: BOOT_START");

    // Force every external output safe before touching sensors, storage, or radio.
    hw::g_actuators.begin();
    Serial.println("[BOOT] Milestone: GPIO_SAFE");

    esp_task_wdt_init(WATCHDOG_TIMEOUT_MS / 1000, true);
    esp_task_wdt_add(NULL);
    otaRollback_checkBoot();

    g_runtimeConfig.begin();
    Serial.println("[BOOT] Milestone: RUNTIME_CONFIG_READY");

    hw::g_sensors.begin();
    Serial.println("[BOOT] Milestone: SENSORS_READY");
    Serial.println("[BOOT] Milestone: GPIO_READY");

    g_wifiManager.begin();
    Serial.println("[BOOT] Milestone: RADIO_CONFIGURED");

    feedWatchdog();
    if (TelemetryQueue::begin()) {
        Serial.println("[Queue] SPIFFS ready");
        Serial.println("[BOOT] Milestone: STORAGE_READY");
        g_apiClient.discardQueuedTelemetryOnSensorProfileChange();
    }
    feedWatchdog();

    // Setup OTA
    ArduinoOTA.setHostname(DEVICE_ID);
    ArduinoOTA.onStart([]() {
        Serial.println("[OTA] Begin");
        hw::g_actuators.disableAllOutputs();
        Preferences p; p.begin("leaf", false); p.putBool("pending_ota", true); p.putInt("boot_fail_count", 0); p.end();
    });
    ArduinoOTA.onEnd([]() {
        hw::g_actuators.disableAllOutputs();
        Serial.println("[OTA] End");
    });
    ArduinoOTA.onProgress([](unsigned int, unsigned int) {
        esp_task_wdt_reset();
    });
    ArduinoOTA.onError([](ota_error_t e) {
        hw::g_actuators.disableAllOutputs();
        Serial.printf("[OTA] Error: %d\n", e);
    });

    Serial.println("[BOOT] Setup complete");

    // Full setup finished: this image has proven itself, clear rollback state.
    otaRollback_markSuccess();
    // Diagnostic helpers
    diagBegin();
    g_apiClient.logMqttConfiguration();
    
    // Initialize performance benchmark
    perfBegin();
    Serial.println("[PERF] Performance benchmarking enabled");
    Serial.println("[PERF] Use 'diag perf' to view statistics");
    Serial.println("[BOOT] Milestone: TASKS_READY");
    Serial.printf("[BOOT] Milestone: SYSTEM_READY elapsed_ms=%lu\n", millis() - g_bootStartMs);
}

void loop() {
    unsigned long loopStartUs = micros();
    uint32_t now = millis();

    // The watchdog is a liveness guard; task cadence is controlled below.
    esp_task_wdt_reset();

    // Heartbeat LED blink - indicates MCU is alive
    if (now - g_lastLedBlinkMs >= 1000) {
        g_lastLedBlinkMs = now;
        g_ledState = !g_ledState;
        digitalWrite(LED_STATUS_PIN, g_ledState ? HIGH : LOW);
    }

#if LEAF_FAN_DIAG
    // TEMPORARY 1s fan cadence probe (see docs/fan-relay-isolation-test.md):
    // proves the automation is stable/parked and lets a ~1s external writer be
    // distinguished from the 900ms safety-control task.
    {
        static uint32_t g_nextFanDiagMs = 0;
        if (timerDue(now, g_nextFanDiagMs, 1000UL)) {
            char tempBuf[16];
            if (g_latestReadings.airTemperatureValid) {
                snprintf(tempBuf, sizeof(tempBuf), "%.2f", g_latestReadings.airTemperature);
            } else {
                snprintf(tempBuf, sizeof(tempBuf), "N/A");
            }
            Serial.printf("[FAN-DIAG] millis=%lu temp=%s valid=%s fan=%s gpio=%d isolated=%s\n",
                          now, tempBuf,
                          g_latestReadings.airTemperatureValid ? "YES" : "NO",
                          hw::g_actuators.isCoolingFanOn() ? "ON" : "OFF",
                          digitalRead(COOLING_FAN_RELAY_PIN),
                          hw::g_actuators.fanRelayIsolation() ? "YES" : "NO");
        }
    }
#endif

    if (timerDue(now, g_nextWifiMaintenanceMs, WIFI_MAINTENANCE_INTERVAL_MS)) {
        PERF_SCOPE("wifi_maintenance");
        g_wifiManager.loop();
    }
    ArduinoOTA.handle();

    if (timerDue(now, g_nextMqttMaintenanceMs, MQTT_MAINTENANCE_INTERVAL_MS)) {
        PERF_SCOPE("mqtt_maintenance");
        g_apiClient.loopMqtt();
    }

    if (timerDue(now, g_nextSensorMs, SENSOR_SAMPLE_INTERVAL_MS)) {
        unsigned long taskStartUs = micros();
        {
            PERF_SCOPE("sensors_read");
            g_latestReadings = hw::g_sensors.read();
        }
        g_hasLatestReadings = true;
        logTaskDuration("sensor", micros() - taskStartUs, SENSOR_SAMPLE_INTERVAL_MS);
    }

    if (timerDue(now, g_nextSafetyMs, SAFETY_CONTROL_INTERVAL_MS) && g_hasLatestReadings) {
        unsigned long taskStartUs = micros();
        {
            PERF_SCOPE("actuator_update");
            hw::g_actuators.updateAutoControl(g_latestReadings);
        }
        logTaskDuration("safety_control", micros() - taskStartUs, SAFETY_CONTROL_INTERVAL_MS);
    }

    // Process MQTT-queued commands through safety-gated actuators
    g_apiClient.processQueuedCommands(hw::g_actuators);

    if (timerDue(now, g_nextTelemetryMs, g_runtimeConfig.telemetryIntervalMs()) && g_hasLatestReadings) {
        g_apiClient.sendTelemetry(g_latestReadings);
    }

    // Flush at most one queued item per period so recovery cannot monopolize control.
    if (timerDue(now, g_nextQueuedTelemetryFlushMs, 5000UL)) {
        g_apiClient.flushQueuedTelemetry(1);
    }

    if (timerDue(now, g_nextHeartbeatMs, MQTT_STATUS_INTERVAL_MS)) {
        g_apiClient.publishStatusHeartbeat(hw::g_actuators);
    }

    // Handle serial commands (calibration and diag)
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            // Let diag handle lines that start with 'diag'
            if (!diagHandleLine(line)) {
                String l = line;
                l.toLowerCase();
                // calibration commands: cal ph <target> <measured>, cal ec <ref> [measured], cal level <emptyRaw> <fullRaw>
                if (l == "fan on") {
                    hw::g_actuators.setState("cooling_fan", true, "SERIAL_COMMAND");
                } else if (l == "fan off") {
                    hw::g_actuators.setState("cooling_fan", false, "SERIAL_COMMAND");
                } else if (l.startsWith("cal ")) {
                    if (l.startsWith("cal ph-interactive")) {
                        // Safety: disable all actuators before blocking the main loop
                        // during calibration. The normal auto-control loop will
                        // re-evaluate and re-enable actuators on the next cycle.
                        hw::g_actuators.disableAllOutputs();
                        if (hw::g_sensors.interactiveCalibratePh()) {
                            Serial.println("[CAL] Interactive pH calibration complete");
                        } else {
                            Serial.println("[CAL] Interactive pH calibration failed or cancelled");
                        }
                    } else if (l.startsWith("cal ph2 ")) {
                        float t1 = 0.0f, m1 = 0.0f, t2 = 0.0f, m2 = 0.0f;
                        int got = sscanf(l.c_str(), "cal ph2 %f %f %f %f", &t1, &m1, &t2, &m2);
                        if (got == 4) {
                            if (hw::g_sensors.calibratePhTwoPoint(t1, m1, t2, m2)) {
                                Serial.println("[CAL] Two-point pH calibration saved");
                            } else {
                                Serial.println("[CAL] Two-point pH calibration failed");
                            }
                        } else {
                            Serial.println("[CAL] Usage: cal ph2 <pH1> <voltage1> <pH2> <voltage2>");
                            Serial.println("[CAL] Measure pH and voltage at two known buffer solutions.");
                            Serial.println("[CAL] Example: cal ph2 4.00 2.170 7.00 1.800");
                        }
                    } else if (l.startsWith("cal ph ")) {
                        float target = 0.0f, measured = 0.0f;
                        int got = sscanf(l.c_str(), "cal ph %f %f", &target, &measured);
                        if (got == 2) {
                            if (hw::g_sensors.calibratePh(target, measured)) {
                                Serial.println("[CAL] pH calibration saved");
                            } else {
                                Serial.println("[CAL] pH calibration failed");
                            }
                        } else {
                            Serial.println("[CAL] Usage: cal ph <targetPh> <measuredVoltage>");
                            Serial.println("[CAL] Example: cal ph 7.00 1.808");
                            Serial.println("[CAL] The 'measuredVoltage' is the voltage shown in [PH] log lines");
                        }
                    } else if (l.startsWith("cal ec ")) {
                        float ref = 0.0f, measured = 0.0f;
                        int got = sscanf(l.c_str(), "cal ec %f %f", &ref, &measured);
                        if (got == 1) {
                            if (hw::g_sensors.calibrateEc(ref)) {
                                Serial.println("[CAL] EC calibration saved");
                            } else {
                                Serial.println("[CAL] EC calibration failed");
                            }
                        } else if (got == 2) {
                            if (hw::g_sensors.calibrateEc(ref, measured)) {
                                Serial.println("[CAL] EC calibration saved");
                            } else {
                                Serial.println("[CAL] EC calibration failed");
                            }
                        } else {
                            Serial.println("[CAL] Usage: cal ec <refMsPerCm> [measuredMsPerCm]");
                            Serial.println("[CAL] DFR0300: use 1.413 and 12.88 mS/cm buffers; omit measured value to calibrate from current EC voltage");
                        }
                    } else if (l.startsWith("cal level ")) {
                        float emptyRaw = 0.0f, fullRaw = 0.0f;
                        int got = sscanf(l.c_str(), "cal level %f %f", &emptyRaw, &fullRaw);
                        if (got == 2) {
                            if (hw::g_sensors.calibrateWaterLevel(emptyRaw, fullRaw)) {
                                Serial.println("[CAL] Water level calibration saved");
                            } else {
                                Serial.println("[CAL] Water level calibration failed");
                            }
                        } else {
                            Serial.println("[CAL] Usage: cal level <emptyRaw> <fullRaw>");
                        }
                    }
                }
            }
        }
    }

    yield();

    // Record total loop time
    unsigned long loopEndUs = micros();
    static unsigned long loopTotalUs = 0;
    static uint32_t loopCount = 0;
    static unsigned long loopMinUs = UINT32_MAX;
    static unsigned long loopMaxUs = 0;
    
    loopTotalUs += (loopEndUs - loopStartUs);
    loopCount++;
    if ((loopEndUs - loopStartUs) < loopMinUs) loopMinUs = (loopEndUs - loopStartUs);
    if ((loopEndUs - loopStartUs) > loopMaxUs) loopMaxUs = (loopEndUs - loopStartUs);

    if ((loopEndUs - loopStartUs) >= 2000000UL) {
        Serial.printf("[TIMING] CRITICAL blocking operation: main loop took %luus\n",
                      loopEndUs - loopStartUs);
    }
    
    // Print stats every 100 loops
    if (loopCount % 100 == 0) {
        unsigned long avgUs = loopTotalUs / loopCount;
        Serial.printf("[PERF] Loop #%u: avg=%luus min=%luus max=%luus\n", 
                     loopCount, avgUs, loopMinUs, loopMaxUs);
        
        // Print heap stats periodically
        HeapStats heap = perfGetHeapStats();
        Serial.printf("[PERF] Heap: free=%u min=%u largest=%u\n", 
                     heap.freeHeap, heap.minFreeHeap, heap.largestBlock);
        
        // Reset min/max for next interval
        loopMinUs = UINT32_MAX;
        loopMaxUs = 0;
        loopTotalUs = 0;
        loopCount = 0;
    }
}
