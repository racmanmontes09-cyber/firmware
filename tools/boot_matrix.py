#!/usr/bin/env python3
"""
Project L.E.A.F. — Boot isolation matrix driver v2 (automated).

Modes:
  --mode auto   : no human needed. Each cycle = esptool-style hard reset
                  through the USB-UART bridge auto-reset circuit
                  (RTS->EN pulse, DTR held false => boot into app).
                  NOTE: electrically a CHIP_RST, not a power-on reset.
                  True POR cold cycles: use --mode manual.
  --mode manual : prompts operator to unplug/replug power each cold cycle.

Per cycle captures serial from reset and extracts:
  outcome, BOOT_START->APPLICATION_READY seconds, decoded reset reason(s),
  brownout/Guru/WDT/panic evidence, strap readings gpio0/3/45/46,
  heap snapshot (free/min/psram/freePsram), milestone chain deltas
  (sensor init, wifi begin, storage, watchdog...), GPIO11 live level,
  warmBootCount continuity (extra-reset detector).

Outputs: results/bootmatrix-<stamp>/{<env>.log, results.csv, cycles.jsonl,
RESULTS.md}
"""

import argparse
import csv
import json
import os
import re
import subprocess
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial missing: pip3 install pyserial")

EVIDENCE_PATTERNS = [
    ("BROWNOUT",      re.compile(r"Brownout detector was triggered", re.I)),
    ("GURU_MEDITATION", re.compile(r"Guru Meditation Error", re.I)),
    ("ABORT",         re.compile(r"\babort\(\)", re.I)),
    ("TASK_WDT",      re.compile(r"Task watchdog got triggered", re.I)),
    ("RST_CODE",      re.compile(r"rst:0x[0-9A-Fa-f]+ \([^)]+\)")),
    ("DOWNLOAD_MODE", re.compile(r"waiting for download|entering download mode", re.I)),
    ("SPIFFS_FAIL",   re.compile(r"SPIFFS mount FAILED|mount failed", re.I)),
    ("WIFI_TIMEOUT",  re.compile(r"WIFI_TIMEOUT")),
]
RESET_REASON_RE = re.compile(r"\[PROBE\] DIAG .*?reset=(\w+)\((\d+)\)")
BANNER_RE = re.compile(r"\[PROBE\] BANNER test=(TEST\d+) .*warmBootCount=(\d+) .*resetReasonChanged=(\d)")
STRAP_RE = re.compile(r"\[PROBE\] STRAP gpio0=(\d) gpio3=(\d) gpio45=(\d) gpio46=(\d)")
DIAG_RE = re.compile(
    r"reset=(\w+)\(\d+\) freeHeap=(\d+) minFreeHeap=(\d+) psramSize=(\d+) freePsram=(\d+)")
MILESTONE_RE = re.compile(r"\[PROBE\] MILESTONE (\w+) t=(\d+)ms")
GPIO_ROW_RE = re.compile(r"\[PROBE\] GPIO (-?\d+) \| ([^|]+)\| ([^|]+)\| ([^|]+)\| ([^|]+)\| ([^|]+)\| (\w+) \|")


def sh(cmd, timeout=900):
    print(f"  $ {' '.join(cmd)}")
    return subprocess.run(cmd, capture_output=True, text=True,
                          timeout=timeout, cwd=REPO_ROOT)


def open_port(port, tries=40, delay=0.5):
    for _ in range(tries):
        try:
            return serial.Serial(port, 115200, timeout=0.25)
        except (serial.SerialException, OSError):
            time.sleep(delay)
    raise RuntimeError(f"cannot open {port}")


def hard_reset_run_app(ser):
    """Classic auto-reset circuit: RTS pulses EN; DTR false keeps IO0 high."""
    ser.dtr = False
    ser.rts = True
    time.sleep(0.12)
    ser.rts = False


def read_for(ser, seconds):
    chunks, deadline = [], time.time() + seconds
    while time.time() < deadline:
        data = ser.read(8192)
        if data:
            chunks.append(data)
            deadline = time.time() + seconds  # extend while traffic flows
    return b"".join(chunks).decode("utf-8", errors="replace")


def parse_cycle(text):
    rec = {}
    banners = BANNER_RE.findall(text)
    rec["banners"] = [{"test": t, "warmBootCount": int(c), "reasonChanged": int(rc)}
                      for t, c, rc in banners]
    reasons = []
    for name, code in RESET_REASON_RE.findall(text):
        if name not in reasons:
            reasons.append(name)
    rec["reset_reasons"] = reasons
    m = STRAP_RE.search(text)
    rec["straps"] = dict(zip(("gpio0", "gpio3", "gpio45", "gpio46"), m.groups())) if m else None
    dm = None
    for name, free, minf, psram, fpsram in DIAG_RE.findall(text):
        dm = {"freeHeap": int(free), "minFreeHeap": int(minf),
              "psramSize": int(psram), "freePsram": int(fpsram)}
    rec["diag_last"] = dm
    ms = {}
    for name, t in MILESTONE_RE.findall(text):
        ms.setdefault(name, []).append(int(t))
    rec["milestones"] = {k: v[-1] for k, v in ms.items()}
    if "BOOT_START" in ms and "APPLICATION_READY" in ms:
        rec["ready_seconds"] = round(
            (rec["milestones"]["APPLICATION_READY"] -
             rec["milestones"]["BOOT_START"]) / 1000.0, 2)
    g11 = [r for r in GPIO_ROW_RE.findall(text) if r[0] == "11"]
    rec["gpio11"] = {"live": g11[0][6]} if g11 else None

    notes = [n for n, rx in EVIDENCE_PATTERNS if rx.search(text)]
    ready = "APPLICATION_READY" in rec["milestones"]
    starts = len(rec["banners"])
    if any(n == "DOWNLOAD_MODE" for n in notes):
        outcome = "DOWNLOAD_MODE"
    elif starts >= 3 and not ready:
        outcome = "RESET_LOOP"
    elif ready:
        outcome = ("IMMEDIATE" if rec.get("ready_seconds", 99) <= 8.0
                   else "DELAYED")
    elif "BOOT_START" in rec["milestones"]:
        outcome = "FAILED_NO_READY"
    else:
        outcome = "FAILED_SILENT"
    # unexpected extra resets inside the window?
    if starts >= 2 and ready:
        notes.append("MULTI_BANNER_WITH_READY")
    rec["outcome"], rec["evidence"] = outcome, sorted(set(notes))
    return rec


def run_matrix(args):
    stamp = time.strftime("%Y%m%d-%H%M%S")
    outdir = args.outdir or os.path.join(REPO_ROOT, "results",
                                         f"bootmatrix-{stamp}")
    os.makedirs(outdir, exist_ok=True)
    csv_path = os.path.join(outdir, "results.csv")
    jsonl_path = os.path.join(outdir, "cycles.jsonl")
    md_path = os.path.join(outdir, "RESULTS.md")

    new_csv = not os.path.exists(csv_path)
    cf = open(csv_path, "a", newline="")
    w = csv.writer(cf)
    if new_csv:
        w.writerow(["env", "kind", "cycle", "outcome", "ready_s",
                    "reset_reasons", "evidence", "gpio11_live",
                    "min_free_heap"])

    for env in args.envs:
        log = open(os.path.join(outdir, f"{env}.log"), "a")
        print(f"\n########## {env}: erase ##########")
        r = sh(["pio", "run", "-e", env, "-t", "erase"])
        if r.returncode != 0:
            sys.exit(f"erase failed: {r.stderr[-800:]}")
        print(f"########## {env}: flash ##########")
        r = sh(["pio", "run", "-e", env, "-t", "upload"])
        if r.returncode != 0:
            sys.exit(f"flash failed: {r.stderr[-800:]}")

        ser = open_port(args.port)
        for i in range(1, args.cycles * 2 + 1):
            kind = "warm" if i > args.cycles else "cycle"
            if args.mode == "manual" and kind == "cycle":
                input(f"[{env}] COLD {i}/{args.cycles}: UNPLUG, wait 5 s, "
                      f"PLUG BACK, press ENTER...")
                ser = open_port(args.port)
                ser.reset_input_buffer()
                text = read_for(ser, args.capture)
            else:
                ser.reset_input_buffer()
                hard_reset_run_app(ser)
                text = read_for(ser, min(args.capture, 20))
            label = f"{kind}{i if kind=='cycle' else i-args.cycles}"
            log.write(f"\n===== {label} @ {time.strftime('%H:%M:%S')} =====\n{text}\n")
            rec = parse_cycle(text)
            rec.update(env=env, kind=kind, cycle=i, label=label)
            with open(jsonl_path, "a") as jf:
                jf.write(json.dumps(rec) + "\n")
            w.writerow([env, kind, i, rec["outcome"], rec.get("ready_seconds"),
                        "|".join(rec["reset_reasons"]),
                        ";".join(rec["evidence"]),
                        rec["gpio11"]["live"] if rec["gpio11"] else "",
                        rec["diag_last"]["minFreeHeap"] if rec["diag_last"] else ""])
            cf.flush()
            print(f"  [{label}] {rec['outcome']} "
                  f"ready={rec.get('ready_seconds','-' )}s "
                  f"rst={','.join(rec['reset_reasons']) or '-'} "
                  f"ev={','.join(rec['evidence']) or '-'}")
        ser.close()
        log.close()

    cf.close()

    rows = []
    if os.path.exists(jsonl_path):
        with open(jsonl_path) as f:
            rows = [json.loads(l) for l in f if l.strip()]
    with open(md_path, "w") as f:
        f.write("# Boot Matrix Results\n\n")
        f.write(f"Generated: {time.strftime('%Y-%m-%d %H:%M:%S')} · "
                f"mode={args.mode} (auto = EN-pulse CHIP_RST, not power-on)\n\n")
        f.write("| TEST | subsystem | cold/auto passed | failed | warm failed | reset reason | ready_s | evidence |\n")
        f.write("|---|---|---|---|---|---|---|---|\n")
        names = {
            "diag0": "Serial baseline", "diag1": "+raw GPIO",
            "diag2": "+Sensors::begin", "diag3": "+Actuators::begin",
            "diag4": "+storage SPIFFS/NVS", "diag5": "+watchdog",
            "diag6": "+WiFi", "diag7": "+MQTT/API", "diag8": "+full services"}
        for env in args.envs:
            er = [r for r in rows if r["env"] == env and r["kind"] == "cycle"]
            wr = [r for r in rows if r["env"] == env and r["kind"] == "warm"]
            okc = sum(1 for r in er if r["outcome"] in ("IMMEDIATE", "DELAYED"))
            badc = len(er) - okc
            okw = sum(1 for r in wr if r["outcome"] in ("IMMEDIATE", "DELAYED"))
            badw = len(wr) - okw
            rr = ",".join({x for r in er + wr for x in r["reset_reasons"]}) or "-"
            secs = "/".join(str(r.get("ready_seconds")) if r.get("ready_seconds")
                            else "-" for r in er)
            ev = ",".join({e for r in er + wr for e in r["evidence"]}) or "-"
            f.write(f"| {env} | {names.get(env,'')} | {okc}/{len(er)} | {badc} "
                    f"| {badw}/{len(wr)} | {rr} | {secs} | {ev} |\n")
    print(f"\nResults dir: {outdir}")
    print(open(md_path).read())


if __name__ == "__main__":
    global REPO_ROOT
    REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser()
    ap.add_argument("--envs", nargs="+", required=True)
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--cycles", type=int, default=5)
    ap.add_argument("--capture", type=int, default=25)
    ap.add_argument("--mode", choices=("auto", "manual"), default="auto")
    ap.add_argument("--outdir", default=None)
    args = ap.parse_args()
    run_matrix(args)
