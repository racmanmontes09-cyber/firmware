#ifndef CONFIG_H
#define CONFIG_H

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#ifndef DEVICE_ID
#define DEVICE_ID "esp32-001"
#endif

#ifndef DEVICE_NAME
#define DEVICE_NAME "Project LEAF Controller"
#endif

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "1.0.0"
#endif

#ifndef MQTT_HOST
// Broker host is injected from .env (LEAF_MQTT_HOST via tools/load_env.py);
// it must never be hardcoded here so only one configuration path exists.
#define MQTT_HOST ""
#endif

#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif

#ifndef MQTT_USERNAME
#define MQTT_USERNAME ""
#endif

#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD ""
#endif

#ifndef MQTT_CLIENT_ID
#define MQTT_CLIENT_ID ""
#endif

#ifndef MQTT_TELEMETRY_TOPIC
#define MQTT_TELEMETRY_TOPIC "leaf/devices/{device_id}/telemetry"
#endif

#ifndef MQTT_STATUS_TOPIC
#define MQTT_STATUS_TOPIC "leaf/devices/{device_id}/status"
#endif

#ifndef MQTT_COMMAND_TOPIC
#define MQTT_COMMAND_TOPIC "leaf/devices/{device_id}/commands"
#endif

#ifndef MQTT_RESULT_TOPIC
#define MQTT_RESULT_TOPIC "leaf/devices/{device_id}/results"
#endif

#ifndef MQTT_SETTINGS_TOPIC
#define MQTT_SETTINGS_TOPIC "leaf/devices/{device_id}/settings"
#endif

#ifndef MQTT_KEEPALIVE
#define MQTT_KEEPALIVE 120
#endif

#ifndef MQTT_SOCKET_TIMEOUT_S
#define MQTT_SOCKET_TIMEOUT_S 1
#endif

#ifndef MQTT_CONNECT_TIMEOUT_MS
#define MQTT_CONNECT_TIMEOUT_MS 1000UL
#endif

#ifndef MQTT_RECONNECT_MIN_MS
#define MQTT_RECONNECT_MIN_MS 5000UL
#endif

#ifndef MQTT_RECONNECT_MAX_MS
#define MQTT_RECONNECT_MAX_MS 60000UL
#endif

#ifndef WIFI_CONNECT_ATTEMPT_TIMEOUT_MS
#define WIFI_CONNECT_ATTEMPT_TIMEOUT_MS 15000UL
#endif

#ifndef WIFI_CONNECT_STATUS_LOG_MS
#define WIFI_CONNECT_STATUS_LOG_MS 2000UL
#endif

#ifndef WIFI_RECONNECT_INITIAL_BACKOFF_MS
#define WIFI_RECONNECT_INITIAL_BACKOFF_MS 1000UL
#endif

#ifndef WIFI_RECONNECT_MAX_BACKOFF_MS
#define WIFI_RECONNECT_MAX_BACKOFF_MS 30000UL
#endif

#ifndef CELLULAR_ENABLED
#define CELLULAR_ENABLED 0
#endif

#ifndef CELLULAR_UART_RX_PIN
#define CELLULAR_UART_RX_PIN -1
#endif

#ifndef CELLULAR_UART_TX_PIN
#define CELLULAR_UART_TX_PIN -1
#endif

#ifndef CELLULAR_UART_BAUD
#define CELLULAR_UART_BAUD 115200UL
#endif

#ifndef CELLULAR_INIT_RETRY_MS
#define CELLULAR_INIT_RETRY_MS 30000UL
#endif

#ifndef CELLULAR_APN
#define CELLULAR_APN ""
#endif

#ifndef CELLULAR_APN_USER
#define CELLULAR_APN_USER ""
#endif

#ifndef CELLULAR_APN_PASSWORD
#define CELLULAR_APN_PASSWORD ""
#endif

#ifndef MQTT_COMMAND_QUEUE_SIZE
#define MQTT_COMMAND_QUEUE_SIZE 10
#endif

#ifndef MQTT_STATUS_INTERVAL_MS
#define MQTT_STATUS_INTERVAL_MS 4000UL
#endif

#ifndef TELEMETRY_INTERVAL_MS
#define TELEMETRY_INTERVAL_MS 2000UL
#endif

#ifndef WIFI_MAINTENANCE_INTERVAL_MS
#define WIFI_MAINTENANCE_INTERVAL_MS 100UL
#endif

#ifndef MQTT_MAINTENANCE_INTERVAL_MS
#define MQTT_MAINTENANCE_INTERVAL_MS 50UL
#endif

#ifndef LED_STATUS_PIN
#define LED_STATUS_PIN 12
#endif

#ifndef FACTORY_RESET_PIN
// GPIO0 is the ESP32-S3 download-mode strap and must not be reused by the app.
#define FACTORY_RESET_PIN -1
#endif

#ifndef SHT31_SDA_PIN
#define SHT31_SDA_PIN 8
#endif

#ifndef SHT31_SCL_PIN
#define SHT31_SCL_PIN 9
#endif

#ifndef DS18B20_PIN
#define DS18B20_PIN 4
#endif

#ifndef PH_SENSOR_PIN
// GPIO5 = ADC1_CH4. This is the pH analog input and MUST remain the pH ADC pin.
// Hardware safety: if the DIY pH module's Po output can exceed 3.3V, do NOT
// connect Po directly to GPIO5. Install a resistor divider / clamp (see
// PH_INPUT_DIVIDER_RATIO) so the ADC-side voltage stays within 0-3.3V. Firmware
// cannot protect the physical pin.
#define PH_SENSOR_PIN 5
#endif

#ifndef EC_SENSOR_PIN
#define EC_SENSOR_PIN 6
#endif

#ifndef WATER_LEVEL_PIN
#define WATER_LEVEL_PIN 7
#endif

#ifndef WATER_LEVEL_SENSOR_TYPE
#define WATER_LEVEL_SENSOR_TYPE "analog"
#endif

#ifndef FLOW_SENSOR_PIN
#define FLOW_SENSOR_PIN 13
#endif

#ifndef FLOW_SENSOR_PULSES_PER_LITER
// Pulse-to-volume calibration: pulses per liter. This is the single most
// important calibration constant for the flow sensor and must be verified
// against the ACTUAL installed sensor (see docs/hardware-wiring-pinout.md).
// The current 450.0f value is the well-known YF-S201 figure; do not rely on it
// unless the installed part is confirmed to match that specification.
#define FLOW_SENSOR_PULSES_PER_LITER 450.0f
#endif

#ifndef FLOW_SENSOR_MIN_PULSE_INTERVAL_US
// Minimum time between accepted pulses (microseconds). Bounce / EMI / contact
// noise on the Hall sensor pulse line arrives far faster than a real turbine
// pulse (turbine periods are ms-scale even at maximum flow), so pulses closer
// together than this are treated as noise and ignored in the ISR. This also
// dampens the interrupt-storm risk from a glitching line (see boot-isolation
// report H4). 1000us = 1kHz cap, well above any realistic turbine flow rate.
#define FLOW_SENSOR_MIN_PULSE_INTERVAL_US 1000
#endif

#ifndef FLOW_SENSOR_MEASUREMENT_WINDOW_MS
// Length of the pulse-accumulation window for rate calculation (ms).
#define FLOW_SENSOR_MEASUREMENT_WINDOW_MS 1000
#endif

#ifndef FLOW_SENSOR_MAX_RATE_LPM
// Sanity ceiling for a calculated flow rate (L/min); above this is INVALID.
#define FLOW_SENSOR_MAX_RATE_LPM 1000.0f
#endif

#ifndef PH_SENSOR_ENABLED
#define PH_SENSOR_ENABLED "1"
#endif

#ifndef EC_SENSOR_ENABLED
#define EC_SENSOR_ENABLED "1"
#endif

#ifndef WATER_LEVEL_SENSOR_ENABLED
#define WATER_LEVEL_SENSOR_ENABLED "1"
#endif

#ifndef FLOW_SENSOR_ENABLED
#define FLOW_SENSOR_ENABLED "1"
#endif

#ifndef WATER_PUMP_RELAY_PIN
#define WATER_PUMP_RELAY_PIN 10
#endif

#ifndef AIR_PUMP_RELAY_PIN
#define AIR_PUMP_RELAY_PIN 11
#endif

#ifndef COOLING_FAN_RELAY_PIN
#define COOLING_FAN_RELAY_PIN 21
#endif

#ifndef NUTRIENT_A_RELAY_PIN
#define NUTRIENT_A_RELAY_PIN 18
#endif

#ifndef NUTRIENT_B_RELAY_PIN
#define NUTRIENT_B_RELAY_PIN 14
#endif

#ifndef PH_UP_RELAY_PIN
#define PH_UP_RELAY_PIN 16
#endif

#ifndef PH_DOWN_RELAY_PIN
#define PH_DOWN_RELAY_PIN 15
#endif

#ifndef FAN_ACTIVE_LEVEL
#define FAN_ACTIVE_LEVEL HIGH
#endif

#ifndef FAN_HYSTERESIS_C
// Hysteresis band (°C) for the cooling-fan thermostat. The fan turns ON when
// temperature >= temperatureMax and turns OFF only when temperature drops to
// (temperatureMax - FAN_HYSTERESIS_C), preventing relay chatter when the
// reading hovers near the threshold.
#define FAN_HYSTERESIS_C 1.0f
#endif

#ifndef LEAF_FAN_DIAG
// TEMPORARY diagnostic build flag (default 0 = production, keep it 0).
// Set to 1 ONLY via the `leaf_fan_diag` env in platformio_override.ini; never
// change this default in source. When 1 it enables (a) fan RELAY-ISOLATION
// mode: the software automation/hysteresis state advances exactly as normal
// but COOLING_FAN_RELAY_PIN is never physically driven, and (b) extra boot
// reset-reason + 1s fan cadence instrumentation. See
// docs/fan-relay-isolation-test.md. Revert = build+flash the main env again.
#define LEAF_FAN_DIAG 0
#endif

#ifndef PUMP_ACTIVE_LEVEL
#define PUMP_ACTIVE_LEVEL HIGH
#endif

#ifndef NUTRIENT_ACTIVE_LEVEL
#define NUTRIENT_ACTIVE_LEVEL HIGH
#endif

#ifndef PH_ACTIVE_LEVEL
#define PH_ACTIVE_LEVEL HIGH
#endif

#ifndef WATER_LEVEL_MIN
#define WATER_LEVEL_MIN 15.0f
#endif

#ifndef TARGET_PH_MIN
#define TARGET_PH_MIN 6.2f
#endif

#ifndef TARGET_PH_MAX
#define TARGET_PH_MAX 6.8f
#endif

#ifndef TARGET_EC_MIN
#define TARGET_EC_MIN 1.2f
#endif

#ifndef TARGET_EC_MAX
#define TARGET_EC_MAX 1.8f
#endif

#ifndef PH_ADC_RESOLUTION
#define PH_ADC_RESOLUTION 12
#endif

#ifndef PH_ADC_SAMPLES
#define PH_ADC_SAMPLES 21
#endif

#ifndef PH_ADC_SAMPLE_DELAY_MS
#define PH_ADC_SAMPLE_DELAY_MS 5
#endif

#ifndef PH_ADC_TRIM_SAMPLES
#define PH_ADC_TRIM_SAMPLES 3
#endif

#ifndef PH_ADC_NOISE_STDDEV_MV
#define PH_ADC_NOISE_STDDEV_MV 25.0f
#endif

#ifndef PH_ADC_UNSTABLE_SPREAD_MV
#define PH_ADC_UNSTABLE_SPREAD_MV 80.0f
#endif

#ifndef PH_ADC_SAFE_INPUT_MV
#define PH_ADC_SAFE_INPUT_MV 3300.0f
#endif

#ifndef PH_INPUT_DIVIDER_RATIO
// Module Po voltage / GPIO5 voltage. Set to < 1.0 if you use a resistor divider
// or other protection network between module Po and GPIO5 (e.g. 0.5 for a 2:1
// divider). Runtime reads GPIO5 calibrated millivolts and recovers the module
// voltage by dividing here so pH conversion always uses the module-side voltage.
// EXAMPLE: a PH-4502C onboard trimmer outputs up to ~3.2V at pH3; with a 5V
// module supply the safest choice is a divider such that GPIO5 < 3.3V.
#define PH_INPUT_DIVIDER_RATIO 1.0f
#endif

#ifndef PH_ADC_SATURATION_RAW
// Raw ADC count at/near the top of the ESP32-S3 ADC range. Readings at or above
// this are treated as an ADC/saturation fault (input driven above the safe
// range, bad divider, or a short), not a valid pH reading.
#define PH_ADC_SATURATION_RAW 4040
#endif

#ifndef PH_ADC_SAFE_MAX_MV
// Calibrated millivolts at/near the 3.3V rail. Readings >= this are treated as
// an ADC saturation fault. This mirrors EC_ADC_SAFE_MAX_MV for the pH input.
#define PH_ADC_SAFE_MAX_MV 3300
#endif

#ifndef PH_EMA_ALPHA
// Exponential moving average smoothing across reading cycles (0.35 = retains
// ~2-3 cycles of history). Suppresses sample-to-sample pH bounce/noise.
#define PH_EMA_ALPHA 0.35f
#endif

#ifndef PH_LOW_VOLTAGE_WARN_MV
// Diagnostic hint only (not a disconnect proof). For a calibrated PH-4502C-style
// amplified module a healthy Po sits roughly in the 1.5-3.3V range (before any
// divider). A stable-but-much-lower voltage is reported as a wiring/probe hint
// while the computed pH (if within range) is still honored.
#define PH_LOW_VOLTAGE_WARN_MV 1500
#endif

#ifndef PH_ADC_REFERENCE_VOLTAGE
// Legacy display/reference constant for user-facing logs. The runtime path now
// uses calibrated millivolts from analogReadMilliVolts().
#define PH_ADC_REFERENCE_VOLTAGE 3.3f
#endif

#ifndef PH_MV_REF_VOLTAGE
// Nominal module output at pH 7 for amplified analog pH boards such as the
// PH-4502C / DIY analog pH modules. Two-point calibration should override this.
#define PH_MV_REF_VOLTAGE 2.5f
#endif

#ifndef PH_MV_PER_PH_SLOPE
// Nominal amplified-module output slope in mV per pH unit. Typical DIY analog
// pH boards are trimmed around ~180 mV/pH and should still be two-point
// calibrated in software.
#define PH_MV_PER_PH_SLOPE 180.0f
#endif

#ifndef PH_CALIBRATION_OFFSET
#define PH_CALIBRATION_OFFSET 0.0f
#endif

#ifndef PH_VALID_MIN
#define PH_VALID_MIN 3.0f
#endif

#ifndef PH_VALID_MAX
#define PH_VALID_MAX 11.0f
#endif

#ifndef EC_ADC_RESOLUTION
#define EC_ADC_RESOLUTION 12
#endif

#ifndef EC_ADC_REFERENCE_VOLTAGE
#define EC_ADC_REFERENCE_VOLTAGE 3.3f
#endif

#ifndef EC_CALIBRATION_FACTOR
#define EC_CALIBRATION_FACTOR 1.0f
#endif

#ifndef EC_DFROBOT_RES2
#define EC_DFROBOT_RES2 820.0f
#endif

#ifndef EC_DFROBOT_ECREF
#define EC_DFROBOT_ECREF 200.0f
#endif

#ifndef EC_RANGE_LOW_THRESHOLD
#define EC_RANGE_LOW_THRESHOLD 2.0f
#endif

#ifndef EC_RANGE_HIGH_THRESHOLD
#define EC_RANGE_HIGH_THRESHOLD 2.5f
#endif

#ifndef EC_KVALUE_MIN
#define EC_KVALUE_MIN 0.5f
#endif

#ifndef EC_KVALUE_MAX
#define EC_KVALUE_MAX 1.5f
#endif

#ifndef EC_VALID_MAX
#define EC_VALID_MAX 20.0f
#endif

#ifndef EC_ADC_SATURATION_RAW
#define EC_ADC_SATURATION_RAW 4088
#endif

#ifndef EC_ADC_SAFE_MAX_MV
#define EC_ADC_SAFE_MAX_MV 3300
#endif

#ifndef PH_DOSING_TIME
#define PH_DOSING_TIME 1500UL
#endif

#ifndef EC_DOSING_TIME
#define EC_DOSING_TIME 2000UL
#endif

#ifndef MIXING_DELAY
#define MIXING_DELAY 4000UL
#endif

#ifndef MAX_DOSING_ATTEMPTS
#define MAX_DOSING_ATTEMPTS 3
#endif

#ifndef ADC_VALID_MIN_RAW
#define ADC_VALID_MIN_RAW 10
#endif

#ifndef ADC_VALID_MAX_RAW
#define ADC_VALID_MAX_RAW 4040
#endif

#ifndef ADC_SAMPLES
#define ADC_SAMPLES 5
#endif

#ifndef ADC_SAMPLE_DELAY_MS
#define ADC_SAMPLE_DELAY_MS 1
#endif

#ifndef ADC_SPREAD_REJECT_THRESHOLD
// Reject water-level readings whose min-to-max spread exceeds this value.
// A stable resistive sensor on 12-bit ADC should have spread well under
// 3000 counts across 10 samples.  Higher values = more tolerant of noise.
#define ADC_SPREAD_REJECT_THRESHOLD 1500
#endif

#ifndef EC_MIN_DISCONNECT_VOLTAGE
// Legacy setting retained for compatibility. DFRobot DFR0300 readings near 0V
// can be valid very-low-conductivity water, so EC no longer uses this as a
// disconnected-probe threshold.
#define EC_MIN_DISCONNECT_VOLTAGE 0.05f
#endif

#ifndef WATER_LEVEL_SATURATION_RAW
// Raw ADC count at/near the top of the ESP32-S3 ADC range. Readings at or above
// this are treated as an ADC/saturation fault for the water-level input (signal
// driven above the safe range, bad divider, or short).
#define WATER_LEVEL_SATURATION_RAW 4040
#endif

#ifndef WATER_LEVEL_SAFE_MAX_MV
// Calibrated millivolts at/near the 3.3V rail. Readings >= this are treated as
// an ADC/saturation fault on the water-level input.
#define WATER_LEVEL_SAFE_MAX_MV 3300
#endif

#ifndef WATER_LEVEL_ADC_SAMPLES
// Number of raw ADC samples collected per water-level read burst.
#define WATER_LEVEL_ADC_SAMPLES 9
#endif

#ifndef WATER_LEVEL_ADC_SAMPLE_DELAY_US
// Delay between burst samples (microseconds) for the water-level read.
#define WATER_LEVEL_ADC_SAMPLE_DELAY_US 50
#endif

#ifndef WATER_LEVEL_UNSTABLE_STDDEV_MV
// Per-burst calibrated-mV stddev above which the water-level input is treated
// as UNSTABLE / possibly disconnected. A floating ESP32-S3 ADC pin with no
// pull-down swings across ~0-3.3V (stddev ~950mV), while a connected resistive
// sensor even under pump-induced surface ripple stays well below this
// (typically < 150mV). Selected to separate the two with margin.
#define WATER_LEVEL_UNSTABLE_STDDEV_MV 250.0f
#endif

#ifndef WATER_LEVEL_UNSTABLE_STRICT_MV
// Lower per-burst stddev that only warns (noisy but valid) rather than failing.
#define WATER_LEVEL_UNSTABLE_STRICT_MV 60.0f
#endif

#ifndef EC_TEMP_COEFF
// DFRobot Gravity EC V2 temperature compensation coefficient relative to 25C.
#define EC_TEMP_COEFF 0.0185f
#endif

#ifndef MAX_PH_DOSING_ATTEMPTS
// Maximum consecutive pH dosing cycles allowed before locking out
#define MAX_PH_DOSING_ATTEMPTS 3
#endif

#ifndef WATER_LEVEL_EMA_ALPHA
// Exponential moving average smoothing factor (0.25 = smooth ripple while remaining responsive)
#define WATER_LEVEL_EMA_ALPHA 0.25f
#endif

#ifndef WATER_LEVEL_DEADBAND_PCT
// Hysteresis deadband threshold in % to eliminate dashboard noise/flashing
#define WATER_LEVEL_DEADBAND_PCT 0.5f
#endif

#ifndef WATER_LEVEL_RECOVERY_MARGIN_PCT
// Hysteresis recovery margin above WATER_LEVEL_MIN (15%) to clear low-water safety lock
#define WATER_LEVEL_RECOVERY_MARGIN_PCT 2.0f
#endif

#ifndef SENSOR_REQUIRED_GOOD_READS
// Number of consecutive valid ADC reads required before a sensor is marked
// valid.  Filters out noise during cold-start or after reconnection.
#define SENSOR_REQUIRED_GOOD_READS 5
#endif

#ifndef SHT31_DEBOUNCE_READS
// Number of consecutive SHT31 reads in the same state (good or bad) required
// before air-temperature / humidity validity is toggled.  Brief I2C glitches
// (e.g. EMI from relay switching) must not flip the sensor between VALID and
// INVALID, because that drives the fan ON/OFF safety path and produces relay
// chatter.  At 400 ms sensor intervals, 3 reads = 1.2 s debounce window.
#define SHT31_DEBOUNCE_READS 3
#endif

#ifndef SENSOR_STALE_TIMEOUT_MS
#define SENSOR_STALE_TIMEOUT_MS 100000UL
#endif

constexpr unsigned long SENSOR_SAMPLE_INTERVAL_MS = 400UL;
constexpr unsigned long SAFETY_CONTROL_INTERVAL_MS = 900UL;
constexpr unsigned long WATCHDOG_TIMEOUT_MS = 25000UL;

// Set to 1 to suppress all automated dosing while debugging sensors.
// Safety system (lock on invalid water level) remains active.
#ifndef DEBUG_NO_AUTOMATION
#define DEBUG_NO_AUTOMATION 0
#endif

// ─── EMI / Relay-Chatter Diagnostic Flags ──────────────────────────────────
// Set to 1 to enable high-frequency SHT31 I²C diagnostic logging.
// Logs every I²C read attempt, raw temperature/humidity, Wire error codes,
// sensor validity transitions, and correlation with fan GPIO state.
// Intended for debugging the relay-EMI → SHT31 invalidation feedback loop.
// Costs ~200 bytes flash and adds Serial.printf overhead each sensor cycle.
// Note: Fan relay bypass is already provided by LEAF_FAN_DIAG (see Actuators.h
// and platformio.ini [env:leaf_fan_diag]). Use 'diag fan-isolation on' to
// suppress GPIO writes while keeping automation logic running.
#ifndef EMI_DIAG_LOGGING
#define EMI_DIAG_LOGGING 0
#endif

// Set to 1 to enable RELAY_BYPASS diagnostic mode.
// The automation logic runs normally and tracks the logical fan state,
// but the GPIO write to COOLING_FAN_RELAY_PIN is suppressed (GPIO stays LOW).
// This proves whether SHT31 failures are caused by relay switching EMI.
// Expected result: if SHT31 stays stable with relay bypass, EMI is confirmed.
// NOTE: If LEAF_FAN_DIAG is also enabled, that mechanism takes precedence
// (it provides the same bypass via 'diag fan-isolation on').
#ifndef RELAY_BYPASS_MODE
#define RELAY_BYPASS_MODE 0
#endif

#endif // CONFIG_H
