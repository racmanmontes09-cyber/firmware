Deployment Checklist for Project LEAF

Pre-deployment:
- Verify hardware wiring matches Config.h pinout.
- Calibrate sensors in a clean environment and persist calibration values.
- Set environment variables: LEAF_WIFI_SSID, LEAF_WIFI_PASSWORD, LEAF_DEVICE_ID, LEAF_DEVICE_NAME, LEAF_MQTT_HOST, LEAF_MQTT_USERNAME, LEAF_MQTT_PASSWORD, LEAF_MQTT_CLIENT_ID, LEAF_MQTT_TELEMETRY_TOPIC, LEAF_MQTT_STATUS_TOPIC, LEAF_MQTT_COMMAND_TOPIC, LEAF_MQTT_RESULT_TOPIC.
- Build in release mode: `pio run -e 4d_systems_esp32s3_gen4_r8n16 --target upload`.
- Confirm `platformio.ini` production flags set.

On-device checks after first boot:
- Confirm WiFi connected and RSSI acceptable (-70 dBm or better).
- Confirm NTP synchronized (check serial logs).
- Confirm MQTT connected (`[MQTT] Connected` in serial output).
- Confirm MQTT telemetry publishes to `leaf/devices/{device_id}/telemetry` and is visible with `mosquitto_sub`.
- Confirm MQTT status published (`[MQTT] Status published: online`).
- Confirm MQTT subscriptions active (`[MQTT] Subscribed: leaf/devices/{device_id}/commands`).
- Confirm sensors report valid values or show Unavailable when disconnected.
- Confirm actuators remain off when sensors invalid.

Operational:
- Monitor MQTT broker for device connectivity.
- Monitor for queued telemetry flush after MQTT reconnection.
- Ensure `php artisan mqtt:subscribe` is running on the Laravel server.
- Schedule periodic manual calibration checks.
- Keep a copy of the last working firmware binary for recovery.

Maintenance:
- Use OTA for field updates; ensure WiFi credentials and MQTT credentials are kept secure.
- Rotate device tokens on central server if compromised.
- Regularly inspect logs and verify watchdog resets are rare and intentional.
