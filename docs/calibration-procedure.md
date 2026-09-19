Calibration Procedure for Project LEAF

Overview:
- pH: Two-point calibration using known buffer solutions (pH 4 and pH 7 or pH 7 and pH 10).
- EC: Single-point calibration with a reference solution (known mS/cm) and measured raw conversion value.
- Water level: Map empty and full raw ADC readings.

Serial calibration commands (via USB serial):
- `cal ph <targetPh> <measuredPh>`
  - Example: `cal ph 7.00 6.85` — sets offset to make measured read 7.00.
- `cal ec <refMsPerCm> <measuredMsPerCm>`
  - Example: `cal ec 1.41 1.33` — sets EC factor to scale measured to reference.
- `cal level <emptyRaw> <fullRaw>`
  - Example: `cal level 190 3800` — maps raw ADC to 0-100%.

Procedure:
1. Power up the unit and ensure stable power and sensors installed.
2. For pH:
   - Place probe in pH 7 buffer, wait for stable reading (30s).
   - Read the measured pH from serial logs or telemetry.
   - Run `cal ph 7.00 <measured>` to set offset for pH 7.
   - Optionally repeat with pH 4 or 10 to verify slope; if slope adjustment needed, update firmware.
3. For EC:
   - Place EC probe in known reference solution.
   - Wait for stable reading.
   - Run `cal ec <referenceMsPerCm> <measuredMsPerCm>` to adjust factor.
4. For Water Level:
   - With tank empty, note raw ADC value and run `cal level <emptyRaw> <fullRaw>` using empty and full measured values.
   - With tank full, note raw ADC value and run command.

Notes:
- Always allow sensors to stabilize (30s - 2min) before recording measured values.
- Save calibration results are persisted in Preferences and survive reboots.
- For best results, perform calibration in temperature-controlled conditions.

Safety:
- Calibration commands only adjust stored offsets/factors; they do not enable actuators.
- If sensors are disconnected, ADC validation prevents false calibrations (raw must be within valid ADC range).