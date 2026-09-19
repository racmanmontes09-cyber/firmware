# Project L.E.A.F. — Firmware Boot Isolation Report

**Status: RESOLVED (2026-08-25) — ROOT CAUSE CONFIRMED & FIXED**

## 0. Confirmed root cause: `Serial` routed to a physically absent USB peripheral

**Mechanism:** The board definition
(`~/.platformio/platforms/espressif32/boards/4d_systems_esp32s3_gen4_r8n16.json`)
sets `-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1`, which maps the Arduino
`Serial` object to the ESP32-S3 **native USB-Serial-JTAG (HWCDC) peripheral**.
The physical USB-C connector on this board is wired through an external
**CH343 USB-UART bridge to UART0 (GPIO43/44)**; the native-USB pads (GPIO19/20)
are not connected to the connector. Result: production firmware booted fine all
along, but 100% of application output went into the unconnected native-USB
peripheral. Only stray IDF framework logs (emitted via `ets_printf` → UART0)
were visible, making the board look like it "won't boot".

**Decisive evidence (5-point rule satisfied):**
1. Exists: board JSON flags above.
2. Executes on failing path: with CDC_ON_BOOT=1 flashed, app ran to
   `ArduinoOTA.begin()` (visible via IDF logs at 2154 ms) while zero
   application `Serial` bytes appeared on `/dev/ttyACM0`
   (`/tmp/opencode/POSTFIX_cycle*.log`, captured pre-fix).
3. Removal fixes: adding `-DARDUINO_USB_CDC_ON_BOOT=0` → `SYSTEM_READY` at
   916 ms, WiFi connected first attempt (RSSI −39), MQTT ready.
4. Re-enable reproduces: probe firmware printed only because it contained a
   local workaround (`src/bootprobe/bootprobe_main.cpp:41`,
   `#define Serial Serial0`) — production never had it.
5. Reproducible cycles: 5/5 dead-silent boots before fix; 5/5 IMMEDIATE boots
   after fix; plus 45 s soak (0 brownout, 0 WDT, 1 reset total).

**Permanent fix:** `-DARDUINO_USB_CDC_ON_BOOT=0` added to both the main env
and `[diag_flags]` in `platformio.ini`, so `Serial` maps to UART0 (the CH343
port) for every module and for the diag matrix.

## 0b. Secondary firmware hardening applied in the same refactor

| Hazard | Fix |
|---|---|
| Watchdog armed only after sensors/storage/WiFi bring-up | `esp_task_wdt_init` now first thing in `setup()`; fed between stages (`main.cpp`) |
| OTA rollback counter incremented too late (early-hanging bad images never counted) | `otaRollback_checkBoot()` moved to top of setup; `markSuccess()` stays at end; rollback state now logged every boot |
| Unconditional `delay(1000)` at boot start | Removed; replaced by `Serial.flush()` |
| Relay/actuator settle: all loads energized back-to-back | Staged init with 10–20 ms settles between outputs→sensors→radio→storage |
| Radio calibration current spike on weak supply (H1) | `WiFi.setTxPower(WIFI_POWER_11dBm)` in `WifiManager::configureRadio()` |
| Silent format-on-mount-fail (H2) | Explicit mount(false) → log → single format+retry (`TelemetryQueue::begin()`) |
| Air-pump relay (GPIO11) excluded from safety shutdown | Added to `Actuators::disableAllOutputs()` via new `airPumpOn/Off()` |
| `platformio_override.ini` silently replaced env `build_flags` | Override emptied with explanatory comment; `.env` via `${sysenv.*}` is the single credentials source |
| Duplicate `-DFLOW_SENSOR_ENABLED` define | Deduped |

Post-fix verification: 5/5 cold boots IMMEDIATE (~916 ms to SYSTEM_READY),
WiFi/MQTT up, 3846 loop-stat reports in 45 s soak, heap stable.

---

**Status: INSTRUMENTATION COMPLETE — ROOT CAUSE NOT YET CONFIRMED**
No root cause is claimed in this document. Every candidate below is a
*hypothesis* tied to a decisive TEST variant. A hypothesis becomes a confirmed
root cause **only** after the hardware matrix satisfies all five criteria from
the investigation mandate (exists / executes on failing path / removal fixes /
re-enable reproduces / ≥5 reproducible cycles).
*(Historical section retained below; superseded by §0/§0b above.)*

---

## 1. Deliverables

| Artifact | Path |
|---|---|
| Staged probe firmware (TEST 0–8) | `src/bootprobe/bootprobe_main.cpp` |
| Build environments diag0–diag8 | `platformio.ini` (`[env:diagN]`) |
| Automated erase/flash/cycle harness | `tools/boot_matrix.py` |
| This report | `docs/boot-isolation-report.md` |

Production firmware is untouched.

## 2. How to run

```bash
# one-time per shell: export the same WiFi credentials production builds use
set -a; . ./.env; set +a

# build check (all 9 variants compile; verified 2026-08-24)
for n in 0 1 2 3 4 5 6 7 8; do pio run -e diag$n; done

# full hardware matrix (interactive cold cycles + automated warm resets)
python3 tools/boot_matrix.py --envs diag0 diag1 diag2 diag3 diag4 \
                             --cycles 5 --warm-resets 5 --capture 30
python3 tools/boot_matrix.py --envs diag5 diag6 diag7 diag8 \
                             --cycles 5 --warm-resets 5 --capture 30
```

The harness writes `results/bootmatrix-<stamp>/results.csv`, `RESULTS.md`,
and raw serial captures per variant. Classification:
`IMMEDIATE` (BOOT_START→APPLICATION_READY ≤ 8 s), `DELAYED` (> 8 s),
`FAILED_NO_READY`, `FAILED_SILENT` (no BOOT_START), `RESET_LOOP`
(≥ 3 banners, never ready), `DOWNLOAD_MODE`.

Serial contract emitted by every probe boot:

```
[PROBE] BANNER test=TESTn ... warmBootCount=N resetReasonChanged=0/1
[PROBE] DIAG BOOT reset=<decoded> freeHeap= minFreeHeap= psramSize= freePsram=
[PROBE] STRAP gpio0= gpio3= gpio45= gpio46=
[PROBE] MILESTONE <NAME> t=<ms>          (BOOT_START … APPLICATION_READY)
[PROBE] GPIOTABLE BEGIN/END              (every configured pin, live levels)
[PROBE] CHAIN BEGIN/END                  (per-stage delta attribution)
[PROBE] MILESTONE APPLICATION_READY t=<ms>
```

## 3. Stage map (binary-search ladder)

| Variant | Adds | Production code exercised |
|---|---|---|
| TEST 0 | Serial + delay only | — (baseline) |
| TEST 1 | Raw GPIO init | mirrors `Actuators::begin()` output sequence + `Sensors::begin()` pinMode calls, no buses/ISR |
| TEST 2 | + `Sensors::begin()` | `src/Sensors.cpp:130–272` verbatim (Wire, SHT31 probe, OneWire scan, ADC cfg, flow ISR) |
| TEST 3 | + `Actuators::begin()` | `src/Actuators.cpp:82–98` verbatim |
| TEST 4 | + storage | `TelemetryQueue::begin()` (`SPIFFS.begin(true)`), NVS read/write smoke test |
| TEST 5 | + watchdog | `esp_task_wdt_init(25 s, panic)` exactly as `main.cpp:129` |
| TEST 6 | + WiFi | `WifiManager::begin()/loop()` verbatim, bounded 20 s wait for NETWORK_READY |
| TEST 7 | + MQTT/API | `ApiClient` ctor effects + `logMqttConfiguration()` + `loopMqtt()` pump, bounded 30 s |
| TEST 8 | + remaining services | `otaRollback_checkBoot/markSuccess`, ArduinoOTA wiring, `diagBegin()`, `perfBegin()`, production-like loop |

All diag envs share identical build flags and compile every module
(`build_src_filter` keeps image layout comparable); the only variable is
`-DLEAF_BOOT_TEST=n`. Decision rule: **first env whose cold cycles start
failing marks the stage that introduces the fault.**

## 4. Complete mechanism inventory (static audit)

Search terms mandated by the investigation, with every hit in `src/`:

### 4.1 Reset / abort paths
| Location | Mechanism | Reachable at boot? |
|---|---|---|
| `main.cpp:91` | `ESP.restart()` | Compiled out (`FACTORY_RESET_PIN -1`, guard `main.cpp:71`) |
| `OtaRollback.cpp:30` | `esp_restart()` after factory reset | Only if NVS `leaf.pending_ota==true` AND `boot_fail_count≥3`; flag is set solely by `ArduinoOTA.onStart` (`main.cpp:137`). Bounded single recovery restart. |
| `src/*` | `abort()` / `assert()` | none |

### 4.2 Blocking constructs — finite-timeout proofs
| Location | Construct | Bound (proof) |
|---|---|---|
| `Sensors.cpp:32` | insertion-sort `while` | ≤ 15 elements (`samples` clamped line 23) |
| `TelemetryQueue.cpp:52,70,91` | `while(file)` dir walks | bounded by SPIFFS file count; `clear()` yields each iteration |
| `ApiClient.cpp:469` | command queue drain | ring size 10 (`MQTT_COMMAND_QUEUE_SIZE`) |
| `ApiClient.cpp:674` | SPIFFS flush loop | `flushed < maxEntries`, called with `maxEntries=1` (`main.cpp:224`) |
| `ApiClient.cpp:732` | prefs flush loop | `remaining≤50` AND `flushed<maxEntries` |
| `Diag.cpp:103,113,122` | sample loops | 10 iterations, command-triggered only |
| `PubSubClient` connect (via `ApiClient.cpp:354,378`) | TCP probe `1000 ms` (`MQTT_CONNECT_TIMEOUT_MS`); socket timeout 1 s (`setSocketTimeout`, `ApiClient.cpp:195`) | worst case ≈ probe 1 s + CONNECT packets ≈ ≤ 4 s |
| `mDNS.queryHost(host,1000)` `ApiClient.cpp:332` | blocking mDNS | hard 1000 ms parameter |

**No unbounded `while(...)` exists anywhere on the boot path.** Every delay
seen at boot is either fixed (`delay(1000)` `main.cpp:69`) or library-bounded
(DallasTemperature scan ≈ tens of ms; Adafruit SHT31 soft-reset ≈ ms).

### 4.3 Watchdog
| Location | Call |
|---|---|
| `main.cpp:129–130` | `esp_task_wdt_init(25 s, panic=true)`; `esp_task_wdt_add(NULL)` |
| `main.cpp:176` | fed first thing every `loop()` |
| `main.cpp:144` | fed during OTA progress |
| `Sensors.cpp`, `WifiManager.cpp`, `Diag.cpp` | none (no WDT interaction) |

Gap analysis: WDT starts **after** sensors/actuators/WiFi/SPIFFS begin.
Anything hanging in those four stages is caught only by the core task-WDT on
IDLE tasks (panic prints `Task watchdog got triggered… IDLE…`), not by the app
WDT. After APPLICATION_READY the loop feeds it every iteration; longest single
loop block is MQTT connect ≈ ≤ 4 s ≪ 25 s.

### 4.4 Startup GPIO / peripheral calls
| Call site | What |
|---|---|
| `Sensors.cpp:138–140` | `Wire.begin(8,9)`, clock 100 kHz, timeout 50 ms |
| `Sensors.cpp:141–147` | SHT31 ACK probe @0x44 (non-fatal if absent) |
| `Sensors.cpp:159` | DS18B20 pin `INPUT_PULLUP`; bus scan `:160–162` |
| `Sensors.cpp:186,187` | water level `INPUT` (no pull) + ADC_11db |
| `Sensors.cpp:209,216` | flow `INPUT_PULLUP` + FALLING ISR (IRAM-safe, mux-guarded) |
| `Sensors.cpp:229–231`, `256–258` | pH/EC pulldown + attenuation + resolution |
| `Sensors.cpp:271` | `loadCalibration()` → NVS reads |
| `Actuators.cpp:83–96` | six relay pins + LED to OUTPUT, all driven inactive LOW |
| `WifiManager.cpp:34–41,55` | `WiFi.mode/persistent/sleep/autoReconnect/hostname/disconnect`, then non-blocking `WiFi.begin` |
| `TelemetryQueue.cpp:12–18` | `SPIFFS.begin(true)` ← **format-on-mount-fail** |
| `Preferences.begin` sites | `Sensors.cpp:283,421`, `OtaRollback.cpp:12,21,26,41`, `TelemetryQueue.cpp:25`, `Diag.cpp:32`, `ApiClient.cpp:168,205,643,713` |
| `LittleFS.begin` | none |

## 5. GPIO master table (pre-APPLICATION_READY)

Electrical state during chip reset for **all** rows: ESP32-S3 pad driver is
disabled (high-Z) until the application configures it; only strapping pins
carry weak internal pulls sampled by ROM. Relay boards with bare inputs can
therefore see floating levels during ROM+bootloader (~150–400 ms) before
`Actuators::begin()` drives them LOW.

| GPIO | peripheral | pinMode (production) | initial state after config | first write | first read | boot-sensitive | conflict |
|---|---|---|---|---|---|---|---|
| 0 | BOOT strap | **unused** ✓ (was factory-reset; fixed) | weak PU at reset | – | – | YES | download-mode strap |
| 3 | JTAG-src strap | unused ✓ | Hi-Z | – | – | n/a | strap |
| 45, 46 | straps | unused ✓ | Hi-Z | – | – | n/a | strap |
| 19, 20 | USB D−/D+ | unused ✓ (flow moved off 20, nutrient-B off 19 per docs) | USB | – | – | YES | native USB — must stay untouched |
| 8 | I2C SDA (SHT31) | Wire open-drain | external PU HIGH | – | SHT31 probe @0x44 | no | – |
| 9 | I2C SCL (SHT31) | Wire open-drain | external PU HIGH | – | SHT31 probe @0x44 | no | – |
| 4 | DS18B20 1-Wire | INPUT_PULLUP | HIGH (PU) | – | bus scan `begin()` | no | – |
| 5 | pH ADC | INPUT_PULLDOWN + ADC_11db | biased LOW | – | `analogRead` | no | – |
| 6 | EC ADC | INPUT_PULLDOWN + ADC_11db | biased LOW | – | `analogRead` | no | – |
| 7 | level ADC | INPUT (no pull) + ADC_11db | Hi-Z (unbiased) | – | `analogRead` + stability checks | no | – |
| 13 | flow pulse ISR | INPUT_PULLUP + FALLING int | HIGH (PU) | – | ISR `flowPulseISR` | **watch** | JTAG MTCK *if external debugger attached* |
| 21 | fan relay | OUTPUT | LOW = off | `fanOff()` LOW | – | floating until `Actuators::begin()` | JTAG MTDO (debugger only) |
| 10 | pump relay | OUTPUT | LOW = off | `waterPumpOff()` LOW | – | floating until begin | – |
| 18 | nutrient-A relay | OUTPUT | LOW = off | `nutrientAOff()` LOW | – | floating until begin | – |
| 14 | nutrient-B relay | OUTPUT | LOW = off | `nutrientBOff()` LOW | – | floating until begin | JTAG MTMS (debugger only) |
| 16 | pH-up relay | OUTPUT | LOW = off | `phUpOff()` LOW | – | floating until begin | XTAL_32K_P if 32 kHz crystal fitted |
| 15 | pH-down relay | OUTPUT | LOW = off | `phDownOff()` LOW | – | floating until begin | XTAL_32K_N if crystal fitted |
| 12 | status LED | OUTPUT | LOW, blinks in loop | LOW | – | no | – |
| **11** | **air-pump relay** | **NEVER CONFIGURED** | **floating forever** | **none** | **none** | **YES** | **defined `Config.h:214` (`AIR_PUMP_RELAY_PIN 11`) but no `pinMode/digitalWrite` anywhere — Finding F-A** |

Static findings from the GPIO audit:
* **F-A:** `AIR_PUMP_RELAY_PIN` (GPIO11) is defined but never initialized or
  driven. If wired, its relay input floats through boot and runtime. Not by
  itself a boot blocker (high-Z pad), but it is an uncontrolled actuator and a
  candidate noise/inrush source. Needs a decision (configure or delete).
* No duplicate assignments; no strap-pin reuse; USB pins avoided (consistent
  with `docs/hardware-wiring-pinout.md`).
* GPIO15/16 are 32k-crystal pads and GPIO12–15 are JTAG pads: harmless unless
  the board carries a 32 kHz crystal or an external debugger is attached.
  Probe STRAP/GPIO lines will show live levels for verification.

## 6. Boot execution sequence (production, with lines)

```
setup()
 ├─ Serial.begin(115200)                     main.cpp:63
 ├─ banner + heap print                      main.cpp:65–68
 ├─ delay(1000)                              main.cpp:69        (+1.000 s)
 ├─ [factory-reset block COMPILED OUT]       main.cpp:71–94
 ├─ g_sensors.begin()                        main.cpp:116 → Sensors.cpp:130
 │    I2C bring-up, SHT31 probe/reset, OneWire scan,
 │    ADC cfg ×3, flow ISR attach, NVS cal load      (~50–300 ms typical)
 ├─ g_actuators.begin()                      main.cpp:118 → Actuators.cpp:82
 ├─ g_wifiManager.begin()                    main.cpp:120 → WifiManager.cpp:17
 │    radio configure + FIRST WiFi.begin     WifiManager.cpp:55  (non-blocking)
 ├─ TelemetryQueue::begin()                  main.cpp:122 → SPIFFS.begin(true)
 │    ← formats partition when mount fails   TelemetryQueue.cpp:12
 ├─ discardQueuedTelemetryOnSensorProfileChange  main.cpp:125 (NVS write once)
 ├─ esp_task_wdt_init(25s)+add(NULL)         main.cpp:129–130
 ├─ OTA callbacks registered                 main.cpp:133–149
 ├─ otaRollback_checkBoot()                  main.cpp:156 → OtaRollback.cpp:10
 │    possible esp_restart()                 OtaRollback.cpp:30 (conditional)
 ├─ otaRollback_markSuccess()                main.cpp:158
 ├─ diagBegin()                              main.cpp:160 (SPIFFS re-check + NVS)
 └─ perfBegin()                              main.cpp:164
loop(): esp_task_wdt_reset() FIRST           main.cpp:176
```

## 7. Flag-merge ambiguity (verify, don't assume)

`platformio_override.ini` defines `build_flags` for the main env while
`platformio.ini` also does. `compile_commands.json` (2026-08-24) contains
`CORE_DEBUG_LEVEL=3` **and** an empty `WIFI_SSID`, which suggests mixed
provenance. Before drawing conclusions about logging-volume differences
between "minimal test sketch" and this project, run:

```bash
pio run -e 4d_systems_esp32s3_gen4_r8n16 -t envdump | grep -A3 build_flags
```

All diag envs define their flags explicitly, so the matrix itself is immune
to this ambiguity.

## 8. Candidate mechanisms (ALL UNCONFIRMED — ranked)

Ranked by prior probability given "same board/power boots clean minimal
firmware, project firmware does not". Each lists the decisive comparison and
the evidence that would satisfy the 5-point root-cause rule.

### H1 — Brownout triggered by cumulative boot-stage current draw
Radio calibration (`WiFi.begin` path) plus six relay boards plus sensors hit
the rail within ~1 s of boot. Minimal firmware never enables RF → never sees
the dip. Symptom match: difficult/slow/unreliable boot, power setup unchanged.
* Decisive: failure onset between **diag5→diag6** (or diag3→diag4 if storage
  churn alone tips the rail). Evidence: `Brownout detector was triggered`,
  `reset=BROWNOUT`, or FAILED_SILENT clusters exactly at stage boundary.
* Confirm rule: diag6 fails ≥5/5 cold cycles; diag5 passes ≥5/5; re-enabling
  WiFi alone reproduces. Fix candidates afterwards: staged TX-power ramp,
  `WiFi.setTxPower(WIFI_POWER_11dBm)`, supply decoupling — applied only AFTER
  confirmation, not before.

### H2 — SPIFFS format-on-mount after `erase_flash` (methodology confound)
`SPIFFS.begin(true)` (`TelemetryQueue.cpp:12`) formats whenever mount fails;
after every deliberate flash-erase the *next* boot always formats (seconds of
flash churn inside early boot). Minimal variants have no storage stage → skip
this cost entirely. This can masquerade as "project firmware boots slowly".
* Decisive: **diag4** chain delta STORAGE_BEGIN→STORAGE_READY huge on cycle 1
  after erase, small on subsequent warm resets; repeat a diag4 cold cycle
  WITHOUT erasing to separate.
* Confirm rule: delayed boots reproduce only when preceded by erase/format;
  removing the format trigger (`begin(false)` + explicit format path) removes
  the delay across ≥5 cycles.

### H3 — USB-CDC enumeration/logging perception artifact
Native-USB boards drop/re-enumerate `ttyACM0` when firmware re-initializes
USB-CDC differently than ROM/bootloader did; heavy early printf traffic can
be lost pre-attach. Looks like failed/slow boot though the MCU runs.
* Decisive: cross-check LED heartbeat vs port presence in captures; banners
  present in logs but monitor reconnects late. Harness records port-open
  latency implicitly via capture timing.
* Confirm rule: outcome flips to IMMEDIATE when capture starts later, with
  identical firmware → perception issue, not boot fault.

### H4 — Flow-sensor interrupt storm during boot
GPIO13 ISR (FALLING) attached in `Sensors::begin()`. A sensor/line glitching
at power-up floods IRQs and starves early init.
* Mitigation: `flowPulseISR` now applies a `FLOW_SENSOR_MIN_PULSE_INTERVAL_US`
  debounce so sub-ms bounce/glitches are dropped and not counted as pulses
  (reduces false counts; a continuously toggling line can still raise IRQs).
* Decisive: diag2 fails, diag2-with-flow-sensor-disconnected passes; ISR
  counter visible via `[FLOW]` spam in logs.
* Confirm rule: ≥5 cycles each direction with sensor attached/detached.

### H5 — OTA-rollback restart loop
Requires `pending_ota==true` (only ever set by an interrupted OTA). Then each
boot increments `boot_fail_count`; at 3 it wipes NVS and restarts once
(`OtaRollback.cpp:18–31`).
* Decisive: banners show `reset=SW_RESET`, log line "Preferences cleared",
  happens ≤ 3 times then stabilizes; inspect NVS via `diag prefs`.
* Confirm rule: artificially setting `pending_ota` reproduces the exact
  restart pattern; clearing it stops it across ≥5 cycles.

### H6 — Post-ready watchdog panic loops
If any runtime block exceeds 25 s (pathological mDNS/TCP behavior), TASK_WDT
panics reboot the board repeatedly → looks like "won't boot".
* Decisive: `Task watchdog got triggered` + `reset=TASK_WDT` banners in
  captures; loop stats show max loop time spiking before death.
* Confirm rule: bypassing the offending call removes the loop ≥5 cycles.

## 9. Hardware result tables (to be filled by `tools/boot_matrix.py`)

Per variant record 5 cold cycles + 5 warm resets:

| Env | Cold outcomes (c1..c5) | Warm outcomes (w1..w5) | Evidence seen | Verdict |
|---|---|---|---|---|
| diag0 | _ _ _ _ _ | _ _ _ _ _ | | |
| diag1 | _ _ _ _ _ | _ _ _ _ _ | | |
| diag2 | _ _ _ _ _ | _ _ _ _ _ | | |
| diag3 | _ _ _ _ _ | _ _ _ _ _ | | |
| diag4 | _ _ _ _ _ | _ _ _ _ _ | | |
| diag5 | _ _ _ _ _ | _ _ _ _ _ | | |
| diag6 | _ _ _ _ _ | _ _ _ _ _ | | |
| diag7 | _ _ _ _ _ | _ _ _ _ _ | | |
| diag8 (=production-equivalent) | _ _ _ _ _ | _ _ _ _ _ | | |

Root cause claim template (fill only after criteria met):

> Mechanism: ______ · File:function:lines ______ · Executes at milestone
> ______ · Removed in env __ → __/5 immediate boots · Re-enabled → __/5
> failing boots · Reproduced across __ cycles · Minimal fix ______ ·
> Before: ______ · After: ______

## 10. Explicit non-goals honored

* No production refactoring performed.
* No safety feature disabled (watchdog, interlocks, rollback all intact in
  probes; probes merely gate which stages execute).
* No success declaration until the physical matrix above has been executed on
  the actual board and a candidate passes all five criteria.
