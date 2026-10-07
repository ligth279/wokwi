#!/usr/bin/env python3
"""Step 2 acceptance checker (normal application, baseline build).

Evaluates every Step 2 acceptance criterion from raw serial logs only, and
writes a Markdown report. Nothing is assumed: a criterion passes only if the
evidence for it is present in the logs (or, for the protection check, in the
ELF symbol table).

Usage:
  check_step2.py --runs RUN1.log RUN2.log RUN3.log --elf firmware.elf \
                 --smoke smoke.log --i2c i2ctest.log [--report out.md]
"""
import argparse
import re
import subprocess
import sys

TOKEN = re.compile(r"\[([A-Z0-9]+)\] ([^\[\r\n]*)")
KV = re.compile(r"(\w+)=(\S+)")

# Must match App/Inc/app.h APP_CONFIG_DEFAULT and App/Src/control.c
SETPOINT, KP, OUT_MIN, OUT_MAX = 2200, 15, 0, 100
SENSOR_PERIOD_CYC = 72_000_000 // 1000 * 100  # 100 ms at 72 MHz
KNOWN_TAGS = {"BOOT", "RESET", "RTOS", "TASK", "APP", "SENSOR", "CONTROL", "STATUS", "CMD"}

# Symbols that would indicate a fault-protection mechanism is linked in.
PROTECTION_SYMBOLS = [
    "HAL_WWDG_Init", "HAL_WWDG_Refresh", "HAL_CRC_Init", "HAL_CRC_Calculate",
    "i2c_bus_recover", "vApplicationStackOverflowHook", "vApplicationMallocFailedHook",
    "HardFault_Handler", "MemManage_Handler", "BusFault_Handler", "UsageFault_Handler",
]


def trunc_div(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def control_ref(temp):
    out = trunc_div((temp - SETPOINT) * KP, 100)
    return max(OUT_MIN, min(OUT_MAX, out))


def parse(path):
    raw = open(path, "rb").read()
    text = raw.decode("latin-1")
    toks = [(m.group(1), m.group(2).strip()) for m in TOKEN.finditer(text)]
    nonprint = [b for b in raw if b not in (9, 10, 13) and not (32 <= b < 127)]
    return text, toks, nonprint


def kv(s):
    return dict(KV.findall(s))


def check_run(path):
    """Return dict criterion -> (ok, evidence) for one run."""
    text, toks, nonprint = parse(path)
    r = {}
    tags = [t for t, _ in toks]

    boots = [p for t, p in toks if t == "BOOT"]
    r["rtos_start"] = (
        any(t == "RTOS" and p.startswith("scheduler_start") for t, p in toks)
        and any(t == "APP" and p == "system_ready" for t, p in toks),
        "scheduler_start + system_ready present",
    )
    started = {kv(p).get("name") for t, p in toks if t == "TASK" and "state=started" in p}

    status = [kv(p) for t, p in toks if t == "STATUS" and "sensor_hb=" in p and "src=periodic" in p]
    status = [s for s in status if all(k in s for k in ("t_ms", "sensor_hb", "control_hb", "console_hb"))]

    def hb_increasing(key):
        vals = [int(s[key]) for s in status]
        return len(vals) >= 3 and all(b > a for a, b in zip(vals, vals[1:])), vals

    for task, key, crit in (("sensor", "sensor_hb", "sensor_task"),
                            ("control", "control_hb", "control_task"),
                            ("console", "console_hb", "uart_task")):
        ok, vals = hb_increasing(key)
        r[crit] = (task in started and ok,
                   f"started={task in started}, {key} over {len(vals)} STATUS lines: "
                   f"{vals[0] if vals else '-'} -> {vals[-1] if vals else '-'} (strictly increasing={ok})")

    sensors = [kv(p) for t, p in toks if t == "SENSOR"]
    controls = [kv(p) for t, p in toks if t == "CONTROL"]
    seqs = [int(s["seq"]) for s in sensors]
    last_t = int(status[-1]["t_ms"]) if status else 0
    expected_min = int(0.95 * last_t / 100)
    contiguous = seqs == list(range(1, len(seqs) + 1))
    all_ok = all(s.get("status") == "OK" for s in sensors)
    sensor_err = int(status[-1].get("sensor_err", -1)) if status else -1
    r["sensor_values"] = (
        contiguous and len(seqs) >= expected_min and all_ok and sensor_err == 0,
        f"{len(seqs)} samples (seq 1..{seqs[-1] if seqs else 0}, contiguous={contiguous}), "
        f">= {expected_min} expected for t={last_t} ms, all status=OK={all_ok}, sensor_err={sensor_err}",
    )

    samples = [int(s["sample"]) for s in sensors]
    inc = all((b - a) % 256 == 1 for a, b in zip(samples, samples[1:]))
    r["sample_counter"] = (len(samples) > 1 and inc,
                           f"chip sample counter {samples[0] if samples else '-'} -> "
                           f"{samples[-1] if samples else '-'}, +1 every record={inc}")

    by_seq = {int(c["seq"]): c for c in controls}
    mismatches = []
    for s in sensors:
        c = by_seq.get(int(s["seq"]))
        if c is None or int(c["input"]) != int(s["value"]) or int(c["value"]) != control_ref(int(s["value"])):
            mismatches.append(s["seq"])
    outs = sorted({int(c["value"]) for c in controls})
    temps = [int(s["value"]) for s in sensors]
    r["control_follows"] = (
        not mismatches and len(by_seq) == len(sensors) and len(outs) >= 2,
        f"{len(sensors) - len(mismatches)}/{len(sensors)} records: input==sensor value and "
        f"output==clamp((T-{SETPOINT})*{KP}/100,{OUT_MIN},{OUT_MAX}); temp {min(temps) if temps else '-'}.."
        f"{max(temps) if temps else '-'} -> output {outs[0] if outs else '-'}..{outs[-1] if outs else '-'}"
        + (f"; mismatched seq: {mismatches[:5]}" if mismatches else ""),
    )

    cyc = [int(s["cyc"]) for s in sensors]
    d = [(b - a) & 0xFFFFFFFF for a, b in zip(cyc, cyc[1:])]
    # Periodic STATUS must sit on a 1000 ms grid (no drift); the console is the
    # lowest-priority task, so a few ms of lateness per line is allowed.
    st_t = [int(s["t_ms"]) for s in status]
    st_late = [t - (st_t[0] + 1000 * i) for i, t in enumerate(st_t)]
    st_d = [b - a for a, b in zip(st_t, st_t[1:])]
    if d:
        mean = sum(d) / len(d)
        worst = max(abs(x - SENSOR_PERIOD_CYC) for x in d)
        ok = abs(mean - SENSOR_PERIOD_CYC) <= SENSOR_PERIOD_CYC * 0.001 and worst <= SENSOR_PERIOD_CYC * 0.01
        ok = ok and bool(st_late) and all(0 <= x <= 5 for x in st_late)
        r["periods_stable"] = (
            ok,
            f"sensor period (DWT cycles): min={min(d)} max={max(d)} mean={mean:.0f} "
            f"target={SENSOR_PERIOD_CYC} worst_dev={worst} ({100 * worst / SENSOR_PERIOD_CYC:.3f}%); "
            f"periodic STATUS: {len(st_t)} lines, intervals ms {sorted(set(st_d))}, "
            f"lateness vs 1000 ms grid ms {min(st_late) if st_late else '-'}..{max(st_late) if st_late else '-'} (limit 0..5)",
        )
    else:
        r["periods_stable"] = (False, "no sensor periods")

    unknown = sorted({t for t in tags if t not in KNOWN_TAGS})
    dropped = int(status[-1].get("dropped", -1)) if status else -1
    r["uart_stable"] = (
        not unknown and not nonprint and dropped == 0 and len(controls) == len(sensors),
        f"{len(toks)} log records, unknown tags={unknown or 'none'}, non-printable bytes={len(nonprint)}, "
        f"dropped records={dropped}, SENSOR/CONTROL lines {len(sensors)}/{len(controls)}",
    )

    cmd = [p for t, p in toks if t == "CMD"]
    def after(rx, resp):
        try:
            i = cmd.index("rx=" + rx)
        except ValueError:
            return False
        return any(c.startswith(resp) for c in cmd[i + 1:i + 3])
    status_after = False
    for i, (t, p) in enumerate(toks):
        if t == "CMD" and p == "rx=STATUS":
            status_after = any(tt == "STATUS" for tt, _ in toks[i + 1:i + 6])
    rx_err = int(status[-1].get("rx_err", -1)) if status else -1
    checks = {"PING": after("PING", "PONG"), "STATUS": status_after,
              "HELP": after("HELP", "commands="), "BOGUS": after("BOGUS", "error=unknown_command"),
              "FAULT MEM_VAR": after("FAULT MEM_VAR", "error=fault_injection_not_available")}
    r["uart_commands"] = (all(checks.values()) and rx_err == 0,
                          f"responses: {checks}, rx_err={rx_err}")

    r["no_protection_log"] = (len(boots) == 1 and "protection=0" in boots[0],
                              f"BOOT: {boots[0] if boots else '-'}")
    faults = [p for t, p in toks if "FAULT" in t]
    r["no_crash"] = (len(boots) == 1 and not faults,
                     f"BOOT lines={len(boots)} (1 = no reset), fault records={len(faults)}")

    last_seq_t = seqs[-1] * 100 if seqs else 0
    r["no_hang"] = (bool(status) and last_seq_t >= last_t - 300 and all(
        r[k][0] for k in ("sensor_task", "control_task", "uart_task")),
        f"last STATUS t={last_t} ms, last sample seq={seqs[-1] if seqs else 0} (~{last_seq_t} ms), "
        f"all heartbeats increasing to the end")

    signature = [(s["seq"], s["value"], s["sample"], s["cyc"]) for s in sensors]
    return r, signature


FAULT_VECTORS = ["HardFault_Handler", "MemManage_Handler", "BusFault_Handler", "UsageFault_Handler"]


def check_elf(elf):
    out = subprocess.run(["arm-none-eabi-nm", elf], capture_output=True, text=True, check=True).stdout
    addr = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            addr[parts[2]] = parts[0]
    present = [s for s in PROTECTION_SYMBOLS if s in addr and s not in FAULT_VECTORS]
    # Fault vectors always exist; in the baseline they must be the startup
    # file's weak aliases of Default_Handler (an empty infinite loop).
    custom = [v for v in FAULT_VECTORS if v in addr and addr[v] != addr.get("Default_Handler")]
    ok = not present and not custom
    return ok, (f"protection functions linked: {present or 'none'}; fault vectors not aliased to "
                f"Default_Handler: {custom or 'none'}")


def check_summary(path, done_marker):
    text, toks, _ = parse(path)
    summ = [p for t, p in toks if "summary" in p]
    fails = [p for t, p in toks if "result=FAIL" in p]
    ok = done_marker in text and summ and "failures=0" in summ[-1] and not fails
    return bool(ok), f"{summ[-1] if summ else 'no summary'}; FAIL lines={len(fails)}"


CRITERIA = [
    ("rtos_start", "FreeRTOS starts successfully"),
    ("sensor_task", "Sensor task runs"),
    ("control_task", "Control task runs"),
    ("uart_task", "UART task runs"),
    ("sensor_values", "Sensor value is continuously produced"),
    ("sample_counter", "Sample counter increases continuously"),
    ("control_follows", "Control output continuously follows sensor input"),
    ("periods_stable", "Task periods are stable"),
    ("uart_stable", "UART logging is stable"),
    ("uart_commands", "UART command input works"),
    ("no_protection", "No fault protection is enabled yet"),
    ("no_crash", "No unexpected crashes"),
    ("no_hang", "No unexpected hangs"),
    ("repeat", "3/3 repeated runs produce the same expected behavior"),
    ("step0", "Step 0 still passes"),
    ("step1", "Step 1 still passes"),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", nargs="+", required=True)
    ap.add_argument("--elf", required=True)
    ap.add_argument("--smoke", required=True)
    ap.add_argument("--i2c", required=True)
    ap.add_argument("--report")
    a = ap.parse_args()

    per_run, sigs = [], []
    for p in a.runs:
        r, sig = check_run(p)
        per_run.append(r)
        sigs.append(sig)

    final = {}
    for key, _ in CRITERIA:
        if key in per_run[0]:
            oks = [r[key][0] for r in per_run]
            final[key] = (all(oks), f"runs passing {sum(oks)}/{len(oks)}; run1: {per_run[0][key][1]}")
    elf_ok, elf_ev = check_elf(a.elf)
    log_ok = all(r["no_protection_log"][0] for r in per_run)
    final["no_protection"] = (elf_ok and log_ok, f"{per_run[0]['no_protection_log'][1]}; {elf_ev}")
    n = min(len(s) for s in sigs)
    identical = all(s[:n] == sigs[0][:n] for s in sigs)
    all_pass = all(all(v[0] for k, v in r.items() if k != "no_protection_log") for r in per_run)
    final["repeat"] = (len(a.runs) >= 3 and all_pass,
                       f"{sum(all(v[0] for v in r.values()) for r in per_run)}/{len(a.runs)} runs pass every "
                       f"per-run criterion; first {n} records (seq,value,sample,DWT cycle) identical across "
                       f"runs: {identical}")
    final["step0"] = check_summary(a.smoke, "SMOKE_DONE")
    final["step1"] = check_summary(a.i2c, "I2CTEST_DONE")

    lines = ["# Step 2 acceptance - normal application (baseline build)", "",
             f"Runs: {', '.join(a.runs)}", f"ELF: {a.elf}", "",
             "| # | Criterion | Result | Evidence |", "|---|---|---|---|"]
    for i, (key, name) in enumerate(CRITERIA, 1):
        ok, ev = final[key]
        lines.append(f"| {i} | {name} | {'PASS' if ok else 'FAIL'} | {ev.replace('|', '/')} |")
    passed = sum(final[k][0] for k, _ in CRITERIA)
    lines += ["", f"**{passed}/{len(CRITERIA)} criteria passed.**"]
    report = "\n".join(lines) + "\n"
    print(report)
    if a.report:
        open(a.report, "w").write(report)
    return 0 if passed == len(CRITERIA) else 1


if __name__ == "__main__":
    sys.exit(main())
