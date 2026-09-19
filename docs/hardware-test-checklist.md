# Project L.E.A.F. Hardware Test Checklist

This checklist is based on the current firmware configuration in [proleaf/include/Config.h](../include/Config.h) and the current PlatformIO build. It is intended for the first real ESP32-S3 hardware test.

## 1. GPIO Assignment Review

| Function | GPIO | Type | Notes |
|---|---:|---|---|
| SHT31 SDA | GPIO8 | I2C | 3.3V logic only; use pull-ups to 3.3V. |
| SHT31 SCL | GPIO9 | I2C | 3.3V logic only; use pull-ups to 3.3V. |
| DS18B20 data | GPIO4 | 1-Wire data | Requires a 4.7k pull-up to 3.3V. |
| pH sensor | GPIO5 | ADC input | Verify the board output is 0-3.3V before connection. |
| EC sensor | GPIO6 | ADC input | Verify the board output is 0-3.3V before connection. |
| Water level sensor | GPIO7 | ADC input | Ensure the sensor output stays within 3.3V max. |
| Water flow sensor | GPIO13 | Input / interrupt | Internal pull-up enabled; moved off USB GPIO20. |
| Cooling fan relay | GPIO21 | Output | Relay control pin. |
| Water pump relay | GPIO10 | Output | Relay control pin. |
| Nutrient pump A relay | GPIO18 | Output | Relay control pin. |
| Nutrient pump B relay | GPIO14 | Output | Relay control pin; moved off USB GPIO19. |
| pH up relay | GPIO16 | Output | Relay control pin. |
| pH down relay | GPIO15 | Output | Relay control pin. |

### GPIO Notes
- GPIO19/GPIO20 are native USB D-/D+ on ESP32-S3 and are intentionally not used by the firmware GPIO map.
- The relay pins are all standard digital outputs and are not shared with the sensor inputs.
- The SHT31 uses the board's default I2C pins; if the breakout uses non-default pins, the firmware will need explicit I2C configuration.

## 2. Wiring Checklist

### Sensors

| Sensor | Connection | Voltage | Notes |
|---|---|---|---|
| SHT31 | SDA to GPIO8, SCL to GPIO9 | 3.3V | Add pull-ups to 3.3V if required by the breakout. |
| DS18B20 | Data to GPIO4 | 3.3V | Add a 4.7k pull-up resistor between data and 3.3V. Common GND required. |
- Water Level | Analog output to GPIO7 | 3.3V max | Keep the signal inside the ESP32 ADC range. |
- Water Flow | Signal output to GPIO13 | 3.3V logic | Use the sensor's own conditioning if needed. |
- pH | Analog output to GPIO5 | 3.3V max | If the sensor board outputs 5V, add a divider or level shifter. |
- EC | Analog output to GPIO6 | 3.3V max | If the sensor board outputs 5V, add a divider or level shifter. |

### Actuators

| Actuator | Connection | Voltage | Notes |
|---|---|---|---|
| Cooling fan relay | GPIO21 | Relay module logic 3.3V or driver-compatible | Verify the relay board input logic matches the firmware. |
| Water pump relay | GPIO10 | Relay module logic 3.3V or driver-compatible | Verify the relay board input logic matches the firmware. |
| Nutrient pump A relay | GPIO18 | Relay module logic 3.3V or driver-compatible | Verify the relay board input logic matches the firmware. |
| Nutrient pump B relay | GPIO14 | Relay module logic 3.3V or driver-compatible | Verify the relay board input logic matches the firmware. |
| pH up relay | GPIO16 | Relay module logic 3.3V or driver-compatible | Verify the relay board input logic matches the firmware. |
| pH down relay | GPIO15 | Relay module logic 3.3V or driver-compatible | Verify the relay board input logic matches the firmware. |

### Power and Ground
- Provide a common GND between the ESP32, sensors, and relay board.
- The ESP32 must be powered from a stable 3.3V/5V supply depending on the board variant.
- Relay modules that use coils typically need their own 5V supply; the control pins still need 3.3V-compatible logic.
- Do not connect 5V sensor outputs directly to the ESP32 ADC pins without a divider or level shifter.

### FS-MCore V1.2 A7670C Electrical Identification (Unpowered)

The exact VIN range for this physical FS-MCore V1.2 board has not been established. Do not apply 5 V, or any other supply voltage, to VIN based on an assumption.

Complete these steps with the board disconnected from every power source:

1. Photograph and identify the power regulator IC, including its package marking and the surrounding inductors, diodes, capacitors, and resistors.
2. Trace the VIN path to determine whether VIN is regulated before reaching the A7670C VBAT rail. Do not infer this from the presence of a regulator alone.
3. Obtain the regulator datasheet and verify its input-voltage range, output voltage, current rating, thermal limits, and required external components.
4. Confirm the A7670C VBAT requirement and the carrier board's power-path limits against the module documentation.
5. Use the verified regulator and power-path specifications to select the bench PSU voltage and current capability. Until then, do not connect a bench PSU.
6. Perform only non-powered continuity and resistance measurements. Check for shorts between VIN and GND and document resistance from VIN to GND and, where accessible, from the suspected VBAT rail to GND. Resistance readings are not a substitute for the regulator specifications.
7. Identify the TX and RX level-shifting components, if present, and verify their voltage domains from their markings and datasheets.
8. Do not connect a USB-TTL adapter TX to modem RX until the modem RX input voltage compatibility is confirmed. Do not connect modem TX to an ESP32 input until its logic-high voltage is confirmed to be ESP32-safe.
9. Leave PEN, NET, and PWK unconnected until their electrical function and active polarity are confirmed from authoritative documentation.

No modem power test, AT command test, firmware flash, or ESP32 firmware modification is authorized by this checklist until steps 1-9 are complete and recorded.

## 3. First Power-On Checklist

1. Deferred: do not flash or modify the ESP32 firmware for the modem at this stage.
2. Deferred: do not power the FS-MCore V1.2 board until its VIN and power path are verified above.
3. For the existing ESP32-only test, connect the ESP32 to USB and open the serial monitor at 115200 baud only after confirming the modem board is disconnected.
4. Confirm the boot banner appears.
5. Confirm all sensor initialization messages appear:
   - [SHT31]
   - [DS18B20]
   - [LEVEL]
   - [FLOW]
   - [pH]
   - [EC]
6. Confirm the relays initialize to OFF.
7. Confirm Wi-Fi connects.
8. Confirm the device authenticates and the heartbeat succeeds.
9. Confirm telemetry uploads successfully.
10. Confirm the backend dashboard receives the new device data.
11. Confirm commands issued from the Laravel backend are executed by the ESP32.

## 4. Serial Monitor Checklist

During normal operation, the firmware should print readable lines for:
- [SHT31] Temperature / Humidity
- [DS18B20] Water Temperature
- [LEVEL] Water Level Percentage
- [FLOW] Flow Rate / Total Flow
- [pH] pH Value
- [EC] EC Value

If a sensor is disconnected or invalid, the firmware should log an error message and continue using the last valid value when available.

## 5. Relay Test Sequence

Use this sequence after the first boot:

1. Cooling Fan ON
2. Cooling Fan OFF
3. Water Pump ON
4. Water Pump OFF
5. Nutrient Pump A ON
6. Nutrient Pump A OFF
7. Nutrient Pump B ON
8. Nutrient Pump B OFF
9. pH Up ON
10. pH Up OFF
11. pH Down ON
12. pH Down OFF

Expected results:
- Only the intended relay activates.
- No relay conflicts occur.
- All relays return to OFF after startup and after a safe shutdown.

## 6. Backend Integration Verification Checklist

Validate the full chain:

ESP32 -> Laravel API -> Database -> Livewire Dashboard -> Realtime Updates

1. ESP32 sends a heartbeat successfully.
2. ESP32 uploads telemetry successfully.
3. Laravel accepts the payloads and stores them.
4. The dashboard shows the device and telemetry data.
5. A backend command reaches the ESP32.
6. The ESP32 executes the command and reports status back to the backend.

## 7. Calibration Checklist

### pH Calibration
- Verify the pH probe is connected to the correct analog input.
- Confirm the probe and board are powered correctly.
- Compare the reported value against a known pH standard.
- Adjust the calibration offset only if the hardware reading is clearly wrong.

### EC Calibration
- Verify the EC probe is connected to the correct analog input.
- Confirm the sensor board is powered correctly.
- Compare the reported value against a known standard or reference solution.
- Adjust the calibration factor only if the hardware reading is clearly wrong.

### Flow Sensor Calibration
- Compare the reported flow against a known volume over a measured time.
- Adjust the pulse-to-liter constant in Config.h only if the hardware reading is clearly wrong.

### Water Level Calibration
- Compare the reported percentage against the actual water level.
- Adjust the sensor scaling or threshold values only if the hardware reading is clearly wrong.

### Temperature Verification
- Compare the SHT31 and DS18B20 readings against a known thermometer.
- Confirm the sensor placement is appropriate for the environment.

## 8. Flashing Checklist

1. Ensure the correct board target is selected: 4d_systems_esp32s3_gen4_r8n16.
2. Ensure PlatformIO can see the required dependencies.
3. Set the environment variables for Wi-Fi, API URL, device ID, and device token.
4. Run `pio run`.
5. Flash the generated firmware image to the ESP32-S3.
6. Open the serial monitor and confirm startup messages appear.
