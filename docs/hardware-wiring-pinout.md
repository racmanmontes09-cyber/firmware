# Project L.E.A.F. Hardware Wiring & Pinout

This document records the final software-side GPIO wiring for the ESP32-S3 N16R8 controller in Project L.E.A.F.

## Final GPIO mapping

`proleaf/include/Config.h` is the authoritative software source for GPIO assignments. This table reflects the firmware as of the production-readiness repair pass.

| Component | ESP32 GPIO | Signal type | Direction | Power / signal voltage | Pull-up / pull-down | Driver / safety notes |
|---|---:|---|---|---|---|---|
| SHT31 SDA | GPIO8 | I2C SDA | Bidirectional | 3.3V logic | 4.7k pull-up to 3.3V if breakout lacks pull-ups | Do not connect 5V I2C pull-ups. |
| SHT31 SCL | GPIO9 | I2C SCL | Output | 3.3V logic | 4.7k pull-up to 3.3V if breakout lacks pull-ups | `Wire.begin(SHT31_SDA_PIN, SHT31_SCL_PIN)`. |
| DS18B20 DATA | GPIO4 | 1-Wire data | Bidirectional | 3.3V logic | 4.7k pull-up to 3.3V | Common GND required. |
| pH analog output | GPIO5 | ADC input | Input | 0-3.3V max | No internal firmware pull (plain INPUT) | Use divider/isolator if board outputs above 3.3V. |
| EC analog output | GPIO6 | ADC input | Input | 0-3.3V max | No internal firmware pull (plain INPUT) | Use divider/isolator if board outputs above 3.3V. |
| Water-level analog output | GPIO7 | ADC input | Input | 0-3.3V max | No internal firmware pull (plain INPUT) | Firmware defaults to analog mode; keep within 0-3.3V. |
| Flow sensor pulse | GPIO13 | Digital interrupt input | Input | 3.3V logic max | Internal pull-up enabled | Hall-effect pulse output; power sensor at 3.3V (open-collector to 3.3V). Never connect a 5V-pulled-up output directly - level-shift/divide to 3.3V. |
| Cooling fan relay IN | GPIO21 | Digital output | Output | 3.3V logic | External relay input requirements | Relay/MOSFET driver required; never drive fan directly. |
| Water pump relay IN | GPIO10 | Digital output | Output | 3.3V logic | External relay input requirements | Relay/MOSFET driver required; never drive pump directly. |
| Nutrient A relay IN | GPIO18 | Digital output | Output | 3.3V logic | External relay input requirements | Relay/MOSFET driver required. |
| Nutrient B relay IN | GPIO14 | Digital output | Output | 3.3V logic | External relay input requirements | Moved from GPIO19 to avoid native USB D- conflict. |
| pH Up relay IN | GPIO16 | Digital output | Output | 3.3V logic | External relay input requirements | Relay/MOSFET driver required. |
| pH Down relay IN | GPIO15 | Digital output | Output | 3.3V logic | External relay input requirements | Relay/MOSFET driver required. |

## Sensor wiring notes

- DS18B20:
  - Signal pin: `GPIO4`
  - Pull-up: `4.7kΩ` resistor from DATA to 3.3V is required for reliable 1-Wire operation.
  - Power: `VERIFY WITH SENSOR DATASHEET`
  - Ground: `VERIFY WITH SENSOR DATASHEET`

- EC sensor:
  - Signal pin: `GPIO6`
  - Power: `VERIFY WITH SENSOR DATASHEET`
  - Ground: `VERIFY WITH SENSOR DATASHEET`
  - Notes: analog read uses 12-bit resolution and assumes 0-3.3V input range.

- Water-level sensor:
  - Signal pin: `GPIO7`
  - Power: `VERIFY WITH SENSOR DATASHEET`
  - Ground: `VERIFY WITH SENSOR DATASHEET`
  - Notes: configured as analog input by default; if changed to digital, the code uses `digitalRead()`.

- Flow sensor:
  - Signal pin: `GPIO13`
  - Power: `VERIFY WITH SENSOR DATASHEET`
  - Ground: `VERIFY WITH SENSOR DATASHEET`
  - Notes: uses interrupt-based pulse counting on falling edge. No additional debounce is implemented in firmware.

- pH sensor:
  - Signal pin: `GPIO5`
  - Power: `VERIFY WITH SENSOR DATASHEET`
  - Ground: `VERIFY WITH SENSOR DATASHEET`
  - Notes: analog read uses 12-bit resolution and assumes 0-3.3V input range.

## Relay wiring notes

- pH Down relay IN: `GPIO15`
- pH Up relay IN: `GPIO16`
- Water Pump relay IN: `GPIO10`
- Nutrient A relay IN: `GPIO18`
- Nutrient B relay IN: `GPIO14`
- Cooling Fan relay IN: `GPIO21`

Relay outputs are initialized in firmware as outputs and are driven OFF by writing the inverse of the active level when disabled.

## ADC configuration

- `analogReadResolution()` is set to `12` bits for pH, EC, and water-level sensor inputs.
- `PH_ADC_REFERENCE_VOLTAGE = 3.3f`
- `EC_ADC_REFERENCE_VOLTAGE = 3.3f`
- All ADC pins are configured with `ADC_11db` attenuation for 0-3.3V range.
- **Internal pull-downs are NOT used on the analog sensor inputs.** pH, EC, and
  water level use plain `INPUT` so a resistive divider is not loaded and the
  reading is not biased. A disconnected/floating input (no pull-down) swings
  widely and is detected by ADC instability/spread, not by a low-voltage bias.
- Reported voltages use calibrated `analogReadMilliVolts()`.
- The water-level percentage is computed as a relative fraction between the
  calibrated EMPTY and FULL raw-ADC points (not an uncalibrated percentage).

## Flow sensor details

- This is a Hall-effect turbine flow sensor: its yellow output is a **digital
  pulse/frequency** signal. It is processed with digital input + ISR pulse
  counting (FALLING edge) on `GPIO13` - **never** as an ADC/analog signal.
- `FLOW_SENSOR_PULSES_PER_LITER` is the pulse-to-volume calibration constant.
  It is currently `450.0f` (the well-known YF-S201 value), but this MUST be
  verified against the ACTUAL installed sensor before use; adjust in
  `Config.h` only after comparing against a known measured volume/time.
- `FLOW_SENSOR_MEASUREMENT_WINDOW_MS` = `1000` ms accumulation window.
- `FLOW_SENSOR_MIN_PULSE_INTERVAL_US` = `1000` us ISR debounce: pulses closer
  together than this are rejected as bounce/EMI (turbine periods are ms-scale
  even at maximum flow), preventing multiple counts per physical pulse and
  dampening a glitchy-line interrupt storm.
- Zero/`< 2` pulses in a window reports **0.0 L/min as VALID NO_FLOW**, not a
  fault. Because the line is pulled up, an idle sensor and a disconnected
  sensor both read HIGH and yield 0 pulses - so NO_FLOW and a fully dead
  sensor are not individually distinguishable on the pulse line alone.
- The calculated rate uses `liters = pulses / pulsesPerLiter` then
  `L/min = liters / elapsedSeconds * 60`, and is bounded by
  `FLOW_SENSOR_MAX_RATE_LPM` (`1000`) as a sanity check.
- Multiple consecutive over-range/invalid results flag the sensor INVALID.

## Validation summary

- `Config.h` is the sole source of GPIO pin assignments for sensors and relays.
- No hardcoded GPIO numbers were found in `src/` or `include/` outside `Config.h`.
- `GPIO4`, `GPIO5`, `GPIO6`, `GPIO7`, `GPIO8`, `GPIO9`, `GPIO10`, `GPIO13`, `GPIO14`, `GPIO15`, `GPIO16`, `GPIO18`, and `GPIO21` are all used via macros.
- Relay control uses the configured active-level macros for ON/OFF logic.
- No duplicate GPIO assignments were found. GPIO19/GPIO20 are reserved for native USB D-/D+ and are not used by the firmware GPIO map.
