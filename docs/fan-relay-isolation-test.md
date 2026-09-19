# TEMPORARY — Fan Relay Isolation Test

Status: ACTIVE diagnostic build. NOT a production change and NOT a permanent fix.
Delete everything described here when the root cause is confirmed.

## Purpose

The working hypothesis is: when air temperature reaches the max threshold and the
fan relay energizes, the relay coil/inrush or the shared ground (diode in the
ESP32-to-relay-to-battery common path) pulls down VDD and the ESP32-S3 brownout
resets. After boot (~1 s), the automation re-decides FAN=ON, the relay re-fires,
and it brownout-resets again — appearing as a ~1 s ON/OFF fan cycle that the
firmware log cannot explain (a software cycle is limited to ~2.7–3.5 s).

This test runs the firmware automation IDENTICALLY while suppressing the physical
relay output, then enables the physical output again, so we can isolate electrical
load from software behavior.

## How it works

- Build env `leaf_fan_diag` (defined in `platformio.ini`) = production env +
  `-DLEAF_FAN_DIAG=1`. Production env `4d_systems_esp32s3_gen4_r8n16` is NOT
  modified.
- `LEAF_FAN_DIAG=1` enables:
  1. `[BOOT] reset_reason=<decoded>` + boot count at every boot.
  2. A 1-second `[FAN-DIAG] ...` cadence line (time, temp, validity, logical fan,
     GPIO read, isolation state).
  3. Relay-isolation mode inside `Actuators::writeFanState`: automation state
     (`m_coolingFan`, hysteresis) advances exactly as normal, but
     `digitalWrite(COOLING_FAN_RELAY_PIN)` is skipped and
     `[FAN][ISOLATION] ... PHYSICAL WRITE SUPPRESSED` is logged.
  4. Runtime toggle: `diag fan-isolation on | off | status` (serial).
- Isolation defaults to ON at boot (`m_fanRelayIsolation = true`).
- Hysteresis/thresholds are untouched (`FAN_HYSTERESIS_C` still 1.0 °C).

## Files changed (all revertible)

| File | Change | Revert |
| --- | --- | --- |
| `include/Config.h` | `LEAF_FAN_DIAG` flag (default 0) + comment | flag stays; no prod effect while it is 0 |
| `include/Actuators.h` | guarded `setFanRelayIsolation`/`fanRelayIsolation` + member | guarded off |
| `src/Actuators.cpp` | isolation bypass branch in `writeFanState` | guarded off |
| `src/main.cpp` | decoded reset reason + 1s `[FAN-DIAG]` line | guarded off |
| `src/Diag.cpp` | `diag fan-isolation` command | guarded off |
| `src/Sensors.cpp` | fix pre-existing build break (`diagStartUs` scoped under `EMI_DIAG_LOGGING` but used unconditionally) | keep (bug fix, prod was uncompilable) |
| `platformio.ini` | `[env:leaf_fan_diag]` section | delete section |

To fully revert ALL instrumentation: delete the `[env:leaf_fan_diag]` section from
`platformio.ini` and build `4d_systems_esp32s3_gen4_r8n16`. Production behavior is
byte-identical to before because every code path is `#if LEAF_FAN_DIAG`-guarded.

## Test procedure

You must be physically at the device for serial capture. 115200 baud.

### 1. Flash the diagnostic build

```
cd proleaf
pio run -e leaf_fan_diag -t upload
pio device monitor -b 115200        # log this entire session
```

Confirm boot prints:
```
[BOOT] Reset reason=<n>
[BOOT] reset_reason=<NAME> boot_count=N
[BOOT] firmware=...
[DIAG] TEMPORARY fan relay isolation test active ...
```

### 2. Verify isolation is active

Send `diag fan-isolation status` → expect `ENABLED (physical relay writes SUPPRESSED)`.

### 3. Trigger the exact failure condition (temp >= threshold)

Firmware applies settings published to `leaf/devices/esp32-001/settings` (retained).
Latest known threshold on the broker is `temperature_max=35` (NOT 30 — see notes).
The room is currently ~31.7 °C, so to force FAN=ON publish a temporary test setting
current-temp-based (example max=30, off=29 via existing 1.0 °C hysteresis). This is
a runtime value, reversible, and does not change firmware hysteresis:

```
mosquitto_pub -h 187.127.213.71 -p 1883 -u leafesp32 -P 'R@cmanm0ntes' \
  -t leaf/devices/esp32-001/settings -r \
  -m '{"settings":{"temperature_max":30.0,"temperature_min":18.0,"water_level_min":20.0,"sensor_upload_interval":2.0,"heartbeat_interval":2.0}}'
```

If the device already starts the fan without this step, skip it.

### 4. Observe 30–60 s

With isolation ON the relay must NOT physically fire (check relay LED/coil — it
stays silent) while serial shows the automation deciding:

```
[FAN] reason=AUTO_TEMP temperature=31.7x valid=VALID threshold=30.00 ...
[FAN-DIAG] millis=... temp=31.7x valid=YES fan=ON gpio=0 isolated=YES
[FAN][ISOLATION] logical OFF -> ON gpio LOW->HIGH pin=21 PHYSICAL WRITE SUPPRESSED millis=...
```

Expected in the healthy case: logical fan settles ON, no resets, no flapping,
ONLY 900 ms automation traces. Any ~1 s reset (`[BOOT] reset_reason=BROWNOUT
boot_count=N` incrementing) or logical ON/OFF flapping is a firmware/truth issue.

### 5. Re-enable the physical relay (A/B)

```
diag fan-isolation off
```

Now the SAME automation drives the real relay. Watch 30–60 s:
- If resets/cycling RETURN the instant the relay energizes → electrical (relay load
  or ground path), firmware exonerated.
- If still perfect with the relay live → the original symptom needs the relay's
  channel/load re-checked (different fault).
Re-enable suppression anytime: `diag fan-isolation on`.

### 6. Restore

Republish the original settings (temperature_max=35 etc.) if you changed them.
Return to production firmware:

```
pio run -e 4d_systems_esp32s3_gen4_r8n16 -t upload
```

## Reading the evidence → classification

| Observation during isolation-ON | During isolation-OFF | Verdict |
| --- | --- | --- |
| Stable, fan ON, no resets | Resets start + `[BOOT] reset_reason=BROWNOUT boot_count` increments | A / B electrical-rooted (relay/brownout → likely ground diode) |
| Logical fan flapping ON/OFF (~1 s) trace shows decision toggling | same | D (sensor validity / threshold) or F (another state machine) |
| `[SHT31] DEBOUNCE`/`DISCONNECTED` spam before flapping | — | D sensor/EMI path (debounce window ~1.2 s) |
| resets even with relay suppressed | — | not relay-driven; power supply / wiring fault independent of relay |
| No resets, stable logical, but physical relay chatters anyway | — | C relay hardware chatter (firmware exonerated) |

Full A–F definitions are in the delivery notes from this session.

## Notes / caveats

- Threshold mismatch: user believes max=30/off=28. Broker retained settings say
  `temperature_max=35`; firmware hysteresis is `FAN_HYSTERESIS_C=1.0` → off=34
  (with max=35) or 29 (with max=30). Not 28. We did not change hysteresis per
  instructions.
- `RELAY_BYPASS_MODE` and `EMI_DIAG_LOGGING` already exist in `Config.h` (both 0,
  unused) from earlier EMI work; we added a new mechanism rather than reuse them.
- The backend is NOT involved (separate broker, `mqtt:subscribe` not running, no
  queued commands). Serial/MQTT `enabled` force-ON commands, MQTT reconnect etc.
  all funnel through the SAME `writeFanState`, so this instrumentation covers them.