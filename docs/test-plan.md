Comprehensive Test Plan for Project LEAF

Test environment:
- ESP32-S3 board with identical wiring to production.
- Serial monitor at 115200.
- Local Ubuntu laptop running Mosquitto, Laravel, MySQL, queue worker, Reverb, and Vite.
- API test server or local Laravel endpoint to simulate 200/500/timeouts for REST fallback.
- WiFi network that can be toggled.

Scenarios:
1. Sensors unplugged at boot
   - Expected: ADC reads flagged invalid; sensors shown as Unavailable; actuators remain disabled; system enters normal monitoring but with safety blocking actuator control.
   - Steps: Boot with EC and level disconnected; observe logs and ensure pumps are off.

2. Sensors disconnect mid-run
   - Expected: Consecutive bad reads increment; once stale/invalid, actuators disabled; queued telemetry saved.
   - Steps: Start with all sensors connected, then unplug EC probe; observe transition.

3. WiFi lost for extended periods
   - Expected: System continues sampling; telemetry queued locally; OTA disabled; on reconnect queued entries flushed.
   - Steps: Disconnect WiFi for >5 minutes; reconnect and verify queued telemetry uploads.

4. API server failures (500, timeout)
   - Expected: Post retries; on persistent failures telemetry enqueued locally; commands not applied until successful.
   - Steps: Point API_BASE_URL to server that returns 500; send telemetry and verify queue behavior.

5. Invalid authentication token
   - Expected: Authentication fails; no privileged API actions; system continues local safety behavior.
   - Steps: Set DEVICE_TOKEN wrong and observe behavior.

6. Water level at empty/full thresholds
   - Expected: Empty -> pump blocked; Full -> normal operation; mapping via calibration correct.
   - Steps: Simulate ADC values at emptyRaw and fullRaw and verify percentage mapping.

7. pH/EC noise and fluctuation
   - Expected: Minor fluctuations within tolerance do not trigger dosing; dosing guarded by dosing timers and attempt limits.
   - Steps: Feed pH values oscillating around thresholds and verify dosing logic.

8. Factory reset
   - Expected: Holding factory reset pin low for 2+ seconds clears stored preferences and restarts.
   - Steps: Hold GPIO0 low during boot and verify Preferences are cleared.

9. OTA update
   - Expected: Device advertises OTA; upload new firmware over network; device rebooted into new firmware.
   - Steps: Trigger OTA via `platformio run --target upload` over network after WiFi connected.

10. Local MQTT realtime telemetry
   - Expected: ESP32 publishes telemetry to `devices/{device_id}/telemetry`; Laravel `mqtt:subscribe` stores one MySQL row and dispatches one `TelemetryReceived` event.
   - Steps: Set `LEAF_MQTT_HOST` to the Ubuntu laptop LAN IP, run `mosquitto_sub -h 127.0.0.1 -t 'devices/+/telemetry' -v`, boot ESP32, and confirm the payload appears.

11. Reverb dashboard update
   - Expected: Browser receives `.TelemetryReceived` over Echo/Reverb, KPI values change immediately, and ApexCharts appends one point without a full chart rebuild.
   - Steps: Open the dashboard from the LAN URL, send sequential readings at known intervals, and compare ESP32 serial `measured_at` with chart x-axis ordering.

12. Duplicate and reconnect behavior
   - Expected: Re-sending the same telemetry timestamp creates no duplicate DB row or chart point; restarting Mosquitto or `mqtt:subscribe` recovers without duplicate listeners.
   - Steps: Publish the same JSON twice with `mosquitto_pub`, restart Mosquitto/subscriber, then send a new reading.

Verification checklist:
- Actuators never turn on when any critical sensor invalid.
- Local queue persists telemetry across soft reboots.
- NTP time synchronized (check logs or timestamps in telemetry).
- MQTT broker address uses the laptop LAN IP on the ESP32; 127.0.0.1 is only for services running on the laptop.
- Reverb host/port use environment variables and resolve from the browser over the LAN.
- Watchdog does not spuriously reset during normal operation.

Automation suggestions:
- Use a staging API to simulate error codes and validate queuing.
- Add unit tests for JSON serialization logic where feasible.
