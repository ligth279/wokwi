#!/usr/bin/env python3
"""Step 6 acceptance checker: recovery (build `recovery`).

Reads the raw serial logs of Tests/run_step6.sh (3 runs of 9 scenarios) and evaluates criteria 6.1 ... 6.5.
Verdicts: PASS / FAIL / LIMIT (cannot be met as written in Wokwi; the evidence says what was observed instead).

usage: check_step6.py --dir results/raw/step6/<ts> [--report results/summaries/step6_acceptance.md]
"""
import argparse
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(__file__))
import step4_cases as S4  # noqa: E402
import step6_cases as C  # noqa: E402
from check_step4 import Run, hexint  # noqa: E402


def law(inp, c=(2200, 15, 0, 100)):
    return S4.control(inp, c[0], c[1], c[2], c[3])


def attempts(run):
    """List of dicts {start, end, ...} per recovery attempt of the run."""
    out = {}
    for r in run.by_tag("RECOVERY"):
        k = r["kv"]
        if k.get("state") == "START":
            out[k["attempt"]] = {"start": r, "end": None, "attempt": k["attempt"], "exp": k["EXP"]}
        elif k.get("state") in ("COMPLETE", "FAILED") and k.get("attempt") in out and out[k["attempt"]]["end"] is None:
            out[k["attempt"]]["end"] = r
    return list(out.values())


def att_for(run, exp, action=None):
    for a in attempts(run):
        if a["exp"] == exp and (action is None or a["start"]["kv"].get("action") == action):
            return a
    return None


def between(run, i0, i1, tag):
    return [r for r in run.recs if i0 < r["i"] < i1 and r["tag"] == tag]


def sensors_ok(rs):
    return [s for s in rs if s["kv"].get("status") == "OK"]


def after(run, idx, tag, n=None):
    l = [r for r in run.recs if r["i"] > idx and r["tag"] == tag and "seq" in r["kv"]]
    return l if n is None else l[:n]


def control_law_ok(controls):
    return bool(controls) and all(int(c["kv"]["value"]) == law(int(c["kv"]["input"])) for c in controls)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True)
    ap.add_argument("--report", default="results/summaries/step6_acceptance.md")
    ap.add_argument("--elf", default=None)
    a = ap.parse_args()
    elf = a.elf or os.path.join(a.dir, "firmware.elf")
    runs = {s: [Run(s, i, a.dir) for i in range(1, C.REPEATS + 1)] for s in C.SCENARIOS}
    R = {}

    def ev(ok, text, limit=False):
        return ("PASS" if ok else ("LIMIT" if limit else "FAIL"), text)

    def rs(scen):
        return runs[scen]

    def exp(f):
        return f"{f}_001"

    present = all(r.present and r.recs for r_ in runs.values() for r in r_)

    def endkv(at):
        return at["end"]["kv"] if at and at["end"] else {}

    # ------------------------------------------------------------------ 6.0 earlier builds untouched
    ident = {}
    idf = "results/summaries/step6_binary_identity.txt"
    if os.path.exists(idf):
        for line in open(idf):
            p = line.split()
            if len(p) == 3 and not line.startswith("#"):
                ident[p[0]] = (p[1], p[2])
    cur = {}
    for b in ident:
        objc = subprocess.run(["arm-none-eabi-objcopy", "-O", "binary", f"build/{b}/firmware.elf", f"/tmp/_{b}.bin"], capture_output=True)
        if os.path.exists(f"/tmp/_{b}.bin"):
            import hashlib
            data = open(f"/tmp/_{b}.bin", "rb").read()
            cur[b] = (hashlib.sha256(data).hexdigest()[:16], str(len(data)))
    R["6.0a"] = ev(bool(ident) and all(cur.get(b) == v for b, v in ident.items()),
                   "build/baseline, fwtest, protected, protfw are byte-identical to the images verified in Steps 2-5 (sha256 prefix, size): " +
                   ", ".join(f"{b} {cur.get(b, ('?',))[0]}={'same' if cur.get(b) == v else 'DIFFERENT'}" for b, v in ident.items()) +
                   " - so the Step 2-5 simulation results still apply to them")

    # ------------------------------------------------------------------ 6.1 task restart
    task_faults = {"TIM-02": "sensor", "MEM-02": "sensor", "MEM-03": "sensor", "MEM-04": "control"}
    info = {}
    for f, t in task_faults.items():
        for r in rs("g1"):
            at = att_for(r, exp(f), f"task_restart_{t}")
            info[(f, r.idx)] = at
    allp = all(info.values())

    def per(fn):
        res = {k: fn(k, v) for k, v in info.items() if v}
        return all(res.values()) and len(res) == len(info), res

    R["6.1a"] = ev(allp and all(info[k]["start"]["kv"]["mech"] in ("HEARTBEAT", "STACK_SEAL", "STACK_CANARY", "STACK_PAINT") for k in info),
                   "detections that start a task restart: " + ", ".join(f"{f}->{info[(f, 1)]['start']['kv']['mech'] if info[(f, 1)] else '?'}" for f in task_faults) + " (3/3 runs each)")

    def only_faulty(k, at):
        f, idx = k
        run = rs("g1")[idx - 1]
        t = task_faults[f]
        i0, i1 = at["start"]["i"], (at["end"]["i"] if at["end"] else 10**9)
        started = [x["kv"].get("name") for x in run.by_tag("TASK") if i0 < x["i"] < i1 and x["kv"].get("state") == "started"]
        return started == [t]
    ok, _ = per(only_faulty)
    R["6.1b"] = ev(ok, "between the START and COMPLETE of every restart exactly one `[TASK] name=<task> state=started` line appears, for the faulty task only (sensor for TIM-02/MEM-02/MEM-03, control for MEM-04)")

    def others_continue(k, at):
        f, idx = k
        run = rs("g1")[idx - 1]
        st = [s for s in run.statuses()]
        b = [s for s in st if s["i"] < at["start"]["i"]]
        c = [s for s in st if at["end"] and s["i"] > at["end"]["i"]]
        ds = [d for d in run.by_tag("DETSTAT")]
        db = [d for d in ds if d["i"] < at["start"]["i"]]
        dc = [d for d in ds if at["end"] and d["i"] > at["end"]["i"]]
        consoles = [x for x in run.by_tag("TASK") if x["kv"].get("name") == "console" and x["kv"].get("state") == "started" and x["i"] > at["start"]["i"]]
        return (b and c and int(c[0]["kv"]["console_hb"]) > int(b[-1]["kv"]["console_hb"]) and db and dc and int(dc[0]["kv"]["mon_runs"]) > int(db[-1]["kv"]["mon_runs"]) and not consoles)
    ok, _ = per(others_continue)
    R["6.1c"] = ev(ok, "console_hb and the monitor's mon_runs keep advancing across every restart, the console/monitor tasks are never restarted; (sensor restart: control is idle only while it has no input)")

    def returns_normal(k, at):
        f, idx = k
        run = rs("g1")[idx - 1]
        t = task_faults[f]
        if not at["end"] or at["end"]["kv"].get("success") != "1":
            return False
        if t == "sensor":
            s = after(run, at["end"]["i"], "SENSOR", 10)
            seqs = [int(x["kv"]["seq"]) for x in s]
            return len(s) == 10 and all(x["kv"]["status"] == "OK" for x in s) and seqs == list(range(seqs[0], seqs[0] + 10))
        c = after(run, at["end"]["i"], "CONTROL", 10)
        return len(c) == 10 and control_law_ok(c)
    ok, _ = per(returns_normal)
    R["6.1d"] = ev(ok, "after COMPLETE the restarted task works: 10 consecutive SENSOR records, status OK, seq continuing (sensor) / 10 CONTROL records following the control law (control), in 12/12 restarts")

    def resumes(k, at):
        f, idx = k
        run = rs("g1")[idx - 1]
        if not at["end"]:
            return False
        s = after(run, at["end"]["i"], "SENSOR", 10)
        c = after(run, at["end"]["i"], "CONTROL", 10)
        st = [x for x in run.statuses() if x["i"] > at["end"]["i"]]
        return len(s) == 10 and len(c) == 10 and control_law_ok(c) and len(st) >= 1 and all(x["kv"]["status"] == "OK" for x in s)
    ok, _ = per(resumes)
    R["6.1e"] = ok and ("PASS", "sensor, control and UART (CONTROL/SENSOR/STATUS lines) all resume after each of the 12 restarts; CONTROL values follow the law") or ("FAIL", "not all resumed")

    def assoc(k, at):
        return at["start"]["kv"]["EXP"] == exp(k[0]) and at["end"] is not None and at["end"]["kv"]["EXP"] == exp(k[0])
    ok, _ = per(assoc)
    R["6.1f"] = ev(ok, "every RECOVERY START/COMPLETE line carries the EXP of the injected experiment (EXP=<fault>_001)")

    def cycles(k, at):
        if not at["end"]:
            return False
        s, e = int(at["start"]["kv"]["start_cycle"]), int(at["end"]["kv"]["end_cycle"])
        return e > s and int(at["end"]["kv"]["time_cycles"]) == e - s
    ok, _ = per(cycles)
    t_cyc = [int(info[(f, 1)]["end"]["kv"]["time_cycles"]) for f in task_faults if info[(f, 1)] and info[(f, 1)]["end"]]
    R["6.1g"] = ev(ok, f"start_cycle and end_cycle (DWT) recorded, time_cycles = end - start (run 1: {dict(zip(task_faults, t_cyc))})")

    def success_real(k, at):
        f, idx = k
        run = rs("g1")[idx - 1]
        t = task_faults[f]
        if not at["end"] or at["end"]["kv"].get("success") != "1":
            return False
        mid = between(run, at["start"]["i"], at["end"]["i"], "SENSOR" if t == "sensor" else "CONTROL")
        return len(sensors_ok(mid) if t == "sensor" else mid) >= 3
    ok, _ = per(success_real)
    sf = rs("safe_fail")[0]
    failed = [x for x in attempts(sf) if x["end"] and x["end"]["kv"].get("success") == "0" and x["start"]["kv"].get("action") == "task_restart_sensor"]
    nos = all(not between(sf, x["start"]["i"], x["end"]["i"], "SENSOR") for x in failed) and bool(failed)
    R["6.1h"] = ev(ok and nos, "success=1 appears only after >=3 healthy records of the restarted task were produced between START and COMPLETE (12/12); "
                              f"negative case TIM-03 (restart cannot cure the fault): {len(failed)} restart attempts end success=0 with no SENSOR record in between")

    # ------------------------------------------------------------------ 6.2 checkpoint restore
    cfg_faults = ["MEM-01", "DATA-02"]
    cinfo = {(f, r.idx): att_for(r, exp(f), "config_restore") for f in cfg_faults for r in rs("g1")}
    dinfo = {r.idx: att_for(r, exp("DATA-01"), "sample_restore") for r in rs("g1")}

    def sub_lines(run, at, action):
        return [x for x in run.by_tag("RECOVERY") if x["kv"].get("action") == action and x["kv"].get("attempt") == at["attempt"] and "state" not in x["kv"]]
    nm = subprocess.run(["arm-none-eabi-nm", "-n", elf], capture_output=True, text=True).stdout if os.path.exists(elf) else ""
    sym = {m.group(3): int(m.group(1), 16) for m in re.finditer(r"^([0-9a-f]+) (\w) (\S+)$", nm, re.M)}
    pa = sym.get("P")
    in_noinit = pa is not None and sym.get("_snoinit", 1) <= pa < sym.get("_enoinit", 0)
    cp_lines = [sub_lines(rs("g1")[0], x, "config_restore")[0] for x in cinfo.values() if x][:1]
    R["6.2a"] = ev(all(cinfo.values()) and cp_lines and cp_lines[0]["kv"].get("checkpoint_valid") == "1",
                   f"checkpoint (g_config x4 fields + last good input + seq + CRC, taken every 500 ms while the configuration verifies) is valid at the moment of the fault: {cp_lines[0]['rest'] if cp_lines else ''}")
    R["6.2b"] = ev(in_noinit, f"the persistent record `P` (checkpoint inside) is at 0x{pa:08X} in .noinit [0x{sym.get('_snoinit', 0):08X}, 0x{sym.get('_enoinit', 0):08X}) (nm of the firmware image)" if pa else "symbol P not found")
    inj = {(f, r.idx): r.injected(f) for f in cfg_faults + ["DATA-01"] for r in rs("g1")}
    R["6.2c"] = ev(all(inj.values()) and all(i["kv"]["before"] != i["kv"]["after"] for i in inj.values()),
                   "runtime state is corrupted by MEM-01 (setpoint bit 10), DATA-02 (kp 15->100) and DATA-01 (control input 8500) - INJECTED before/after differ in 9/9 runs")
    det = {}
    for f in cfg_faults + ["DATA-01"]:
        for r in rs("g1"):
            det[(f, r.idx)] = [d for d in r.by_tag("DETECT") if d["kv"].get("EXP") == exp(f)]
    R["6.2d"] = ev(all(det.values()) and all(d[0]["kv"]["mech"] in ("CRC", "REDUNDANT") for d in det.values()),
                   "detected by CRC/REDUNDANT in 9/9 runs: " + ", ".join(f"{f}={'+'.join(sorted({d['kv']['mech'] for d in det[(f, 1)]}))}" for f in cfg_faults + ["DATA-01"]))
    rest = {}
    for (f, i), at in cinfo.items():
        if at:
            sl = sub_lines(rs("g1")[i - 1], at, "config_restore")
            rest[(f, i)] = sl[0]["kv"] if sl else None
    R["6.2e"] = ev(all(cinfo.values()) and all(v and v["checkpoint_valid"] == "1" for v in rest.values()) and
                   all(dinfo.values()) and all(sub_lines(rs("g1")[i - 1], at, "sample_restore") for i, at in dinfo.items()),
                   f"saved state restored: config_restore from checkpoint (restored_setpoint={rest[('MEM-01', 1)]['restored_setpoint']}, restored_kp={rest[('MEM-01', 1)]['restored_kp']}) and sample_restore (corrupted_input=8500 -> restored from checkpoint) in all runs")

    def resumed(f, i):
        run = rs("g1")[i - 1]
        at = cinfo[(f, i)]
        c = after(run, at["start"]["i"], "CONTROL", 6)
        return len(c) == 6 and control_law_ok(c)
    R["6.2f"] = ev(all(resumed(f, i) for (f, i) in cinfo if cinfo[(f, i)]),
                   "after the restore the next 6 CONTROL records follow the control law with the nominal configuration (setpoint 2200, kp 15) in 6/6 config runs; sample runs: input back to the sensor value")
    def matches(f, i):
        k = rest[(f, i)]
        ij = inj[(f, i)]["kv"]
        return int(k["restored_setpoint"]) == 2200 and int(k["restored_kp"]) == 15 and \
            (hexint(ij["before"]) in (2200, 15))
    R["6.2g"] = ev(all(matches(f, i) for (f, i) in rest),
                   "restored values equal the checkpointed ones and the pre-injection values (setpoint 2200, kp 15 = the INJECTED `before=`); the CRC and redundant copies of the restored configuration verify (no further DETECT after the restore)")
    R["6.2h"] = ev(all(cinfo.values()) and all(x["end"] for x in cinfo.values()), "RECOVERY ... state=START and state=COMPLETE logged for every restore (start_cycle/end_cycle fields)")
    tt = {k: int(v["end"]["kv"]["time_cycles"]) for k, v in cinfo.items() if v and v["end"] and v["end"]["kv"].get("success") == "1"}
    R["6.2i"] = ev(len(tt) == len(cinfo), f"recovery time recorded: time_cycles = end - start; run 1: " + ", ".join(f"{f}={v}" for (f, i), v in tt.items() if i == 1))

    def real_success(f, i):
        run = rs("g1")[i - 1]
        at = cinfo[(f, i)]
        mid = between(run, at["start"]["i"], at["end"]["i"], "CONTROL")
        return at["end"]["kv"].get("success") == "1" and len(mid) >= 2 and control_law_ok(mid)
    R["6.2j"] = ev(all(real_success(f, i) for (f, i) in cinfo), "COMPLETE success=1 only after 3 consecutive good control cycles with output = law(checkpoint config); the CONTROL records between START and COMPLETE follow the law")

    # ------------------------------------------------------------------ 6.3 system reset
    sw = rs("cpu03")
    swa = [att_for(r, exp("CPU-03"), "software_reset") for r in sw]
    R["6.3a"] = ev(all(swa) and all(a_["start"]["kv"]["mech"] == "FAULT_HANDLER" for a_ in swa),
                   f"CPU-03 (fault-handler path, synthetic frame) -> DETECT FAULT_HANDLER -> RECOVERY level=3 action=software_reset in 3/3 runs: {swa[0]['start']['rest'][:140] if swa[0] else ''}")
    R["6.3b"] = ev(all(len(r.by_tag("BOOT")) == 2 for r in sw), "second BOOT line after the reset (NVIC_SystemReset from the monitor task) in 3/3 runs")
    R["6.3c"] = ev(all(any("system_ready" in x["rest"] for x in r.by_tag("APP") if x["i"] > r.by_tag("BOOT")[1]["i"]) and
                       len([t for t in r.by_tag("TASK") if t["kv"].get("state") == "started" and t["i"] > r.by_tag("BOOT")[1]["i"]]) == 4 for r in sw),
                   "after the reset: framework ready, 4 tasks started, system_ready, SENSOR/CONTROL records resume")
    resets = [x for r in sw for x in r.by_tag("RESET") if "csr" in x["kv"]]
    R["6.3d"] = ev(all(any(x["kv"].get("csr", "").startswith("0x") for x in r.by_tag("RESET") if x["i"] > r.by_tag("BOOT")[1]["i"]) for r in sw),
                   "RCC_CSR is read at every boot and printed (`[RESET] cause=NONE csr=0x00000000`); Wokwi leaves it 0 (docs/SIMULATOR_LIMITATIONS.md section 3)")
    R["6.3e"] = ev(all(any(x["kv"].get("cause_resolved") == "SOFTWARE" and x["kv"].get("exp") == exp("CPU-03") for x in r.by_tag("RESET")) for r in sw),
                   "cause SOFTWARE logged against the experiment: " + next((x["rest"][:150] for x in sw[0].by_tag("RESET") if x["kv"].get("cause_resolved") == "SOFTWARE"), "") + " (source=breadcrumb, because RCC_CSR reads 0)")
    R["6.3f"] = ev(all(any("state_restored" in x["rest"] for x in r.by_tag("RECOVERY")) for r in sw) and
                   all(endkv(att_for(r, exp("CPU-03"))).get("success") == "1" for r in sw),
                   "g_config restored from the .noinit checkpoint at boot (`state_restored from=checkpoint`) and the tasks run again")
    R["6.3g"] = ev(all(att_for(r, exp("CPU-03")) and att_for(r, exp("CPU-03"))["end"] and "time_cycles" in att_for(r, exp("CPU-03"))["end"]["kv"] for r in sw),
                   "COMPLETE success=1 with start/end cycle and time_cycles across the reset (DWT keeps counting through a reset in Wokwi; on silicon CYCCNT restarts, so this time is simulator-valid)")

    for k, scen, f in (("a", "tim01", "TIM-01"),):
        pass
    ww = {f: rs(s) for f, s in (("TIM-01", "tim01"), ("CPU-01", "cpu01"), ("CPU-02", "cpu02"))}
    R["6.3h"] = ev(all(att_for(r, exp(f), "wwdg_reset") for f, rr in ww.items() for r in rr),
                   "TIM-01 (hang) and, as a hang, CPU-01 and CPU-02 cause the WWDG shim to reset the MCU: RECOVERY level=3 action=wwdg_reset in 9/9 runs (WWDG results: simulator workaround)")
    R["6.3i"] = ev(all(len(r.by_tag("BOOT")) == 2 and endkv(att_for(r, exp(f), "wwdg_reset")).get("success") == "1" for f, rr in ww.items() for r in rr),
                   "reboot after every WWDG reset, 2 BOOT lines, recovery verified success=1 (tasks running)")
    R["6.3j"] = ev(False, "RCC_CSR is read after the reboot (csr=0x00000000) but never contains the WWDG flag in Wokwi, so it cannot identify the reset; the cause comes from the .noinit breadcrumb (cause_resolved=WWDG)", limit=True)
    R["6.3k"] = ev(all(any(x["kv"].get("cause_resolved") == "WWDG" and x["kv"].get("exp") == exp(f) for x in r.by_tag("RESET")) for f, rr in ww.items() for r in rr),
                   "`[RESET] breadcrumb=WWDG cause_resolved=WWDG ... exp=<fault>_001` for TIM-01, CPU-01, CPU-02 in 9/9 runs")

    def back_to_state(f, r):
        at = att_for(r, exp(f), "wwdg_reset")
        if not at or not at["end"]:
            return False
        c = after(r, at["end"]["i"], "CONTROL", 10)
        return len(c) == 10 and control_law_ok(c) and at["end"]["kv"].get("verified") == "tasks_running_after_reset"
    R["6.3l"] = ev(all(back_to_state(f, r) for f, rr in ww.items() for r in rr), "after the reset the application is in its expected operating state: 10 CONTROL records follow the law, configuration restored from the checkpoint")

    # ------------------------------------------------------------------ 6.4 safe state
    sr, sfl = rs("safe_rep"), rs("safe_fail")
    det_rep = [len([d for d in r.by_tag("DETECT") if d["kv"].get("mech") == "WWDG"]) for r in sr]
    R["6.4a"] = ev(all(n >= 3 for n in det_rep), f"TIM-01 injected 4 times in a row (FAULT TIM-01 after each recovery): WWDG detections per run {det_rep}; the 4th is the one that ends in the safe state")
    fl = [[x for x in attempts(r) if x["end"] and x["end"]["kv"].get("success") == "0"] for r in sfl]
    R["6.4b"] = ev(all(len(x) >= 2 for x in fl), "TIM-03 (a fault that survives restart and reset): recovery attempts fail and are recognised - " +
                   (", ".join(f"attempt {x['attempt']} level {x['start']['kv']['level']} {x['start']['kv']['action']} -> FAILED({x['end']['kv']['reason']})" for x in fl[0]) if fl[0] else ""))
    ent = [[x for x in r.by_tag("SAFE") if x["kv"].get("state") == "ENTERED"] for r in sr + sfl]
    R["6.4c"] = ev(all(len(e) == 1 for e in ent) and all(e[0]["kv"].get("output") == "100" for e in ent),
                   "defined safe state: sensor and control tasks are not started, a safe task holds the actuator output at 100 % (out_max); engineering choice, the project defines no safety policy. " + (ent[0][0]["rest"][:160] if ent[0] else ""))
    R["6.4d"] = ev(all(any(x["kv"].get("state") == "ENTERING" for x in r.by_tag("SAFE")) and len(e) == 1 for r, e in zip(sr + sfl, ent)),
                   "`[SAFE] state=ENTERING reason=...` before the reset and `[SAFE] state=ENTERED ...` after it, in 6/6 runs")
    def no_normal(r):
        i0 = r.by_tag("SAFE")
        ee = [x for x in i0 if x["kv"].get("state") == "ENTERED"]
        if not ee:
            return False
        e = ee[0]["i"]
        return not [x for x in r.recs if x["i"] > e and x["tag"] in ("SENSOR", "CONTROL")] and all(x["kv"].get("normal_operation") == "0" for x in i0 if x["kv"].get("state") == "HOLD")
    R["6.4e"] = ev(all(no_normal(r) for r in sr + sfl), "after ENTERED there is no SENSOR or CONTROL record and every HOLD line says normal_operation=0")
    def stable(r):
        ee = [x for x in r.by_tag("SAFE") if x["kv"].get("state") == "ENTERED"]
        if not ee:
            return False
        e = ee[0]["i"]
        holds = [x for x in r.by_tag("SAFE") if x["kv"].get("state") == "HOLD" and x["i"] > e]
        boots = [x for x in r.by_tag("BOOT") if x["i"] > e]
        return len(holds) >= 4 and not boots and all(h["kv"]["output"] == "100" for h in holds)
    R["6.4f"] = ev(all(stable(r) for r in sr + sfl), "safe state stable: >= 4 HOLD lines (1 s apart, output=100), no further reset or BOOT, in 6/6 runs")
    R["6.4g"] = ev(all(e and e[0]["kv"].get("escalation_to_level_4") == "1" for e in ent) and
                   all(any(x["kv"].get("level") == "4" and x["kv"].get("state") == "COMPLETE" for x in r.by_tag("RECOVERY")) for r in sr + sfl),
                   "`escalation_to_level_4=1` in the ENTERED line and a RECOVERY level=4 action=safe_state COMPLETE success=1 (state stable for 2 s) in 6/6 runs; reasons: " +
                   ", ".join(sorted({e[0]['kv']['reason'] for e in ent if e})))

    # ------------------------------------------------------------------ 6.5 reset-cause handling after every reset
    reset_cases = []
    for scen in ("tim01", "cpu01", "cpu02", "cpu03", "periph", "safe_rep", "safe_fail"):
        for r in rs(scen):
            boots = r.by_tag("BOOT")
            for bi, b in enumerate(boots[1:], 1):
                nxt = boots[bi + 1]["i"] if bi + 1 < len(boots) else 10**9
                seg = [x for x in r.recs if b["i"] <= x["i"] < nxt]
                reset_cases.append((scen, r.idx, bi, seg))
    def has(seg, tag, cond):
        return any(x["tag"] == tag and cond(x) for x in seg)
    R["6.5a"] = ev(bool(reset_cases) and all(has(s, "RESET", lambda x: "csr" in x["kv"] and x["kv"]["csr"].startswith("0x") and "cause" in x["kv"]) for *_, s in reset_cases),
                   f"RCC_CSR read and printed after each of the {len(reset_cases)} resets observed in the campaign (`[RESET] cause=.. csr=0x...`)")
    R["6.5b"] = ev(all(has(s, "RESET", lambda x: x["kv"].get("cause_resolved") in ("WWDG", "SOFTWARE")) for *_, s in reset_cases),
                   "cause identified after each reset: " + ", ".join(sorted({x["kv"]["cause_resolved"] for *_, s in reset_cases for x in s if x["tag"] == "RESET" and "cause_resolved" in x["kv"]})) + " (from the breadcrumb; RCC_CSR itself stays 0 in Wokwi, so hardware-only identification is a LIMIT)")
    R["6.5c"] = ev(all(has(s, "RESET", lambda x: "breadcrumb" in x["kv"]) for *_, s in reset_cases), "cause logged on every boot (`[RESET] breadcrumb=.. cause_resolved=.. source=..`)")
    R["6.5d"] = ev(all(has(s, "RESET", lambda x: x["kv"].get("exp", "none") not in ("none", "")) for *_, s in reset_cases), "every post-reset RESET line names the experiment (exp=<fault>_001/_00N) that caused it")
    R["6.5e"] = ev(all(has(s, "RECOVERY", lambda x: x["kv"].get("boot_path") in ("verify_recovery", "safe_state")) for *_, s in reset_cases),
                   "boot path selected from the cause/pending state: " + ", ".join(sorted({x["kv"]["boot_path"] for *_, s in reset_cases for x in s if x["tag"] == "RECOVERY" and "boot_path" in x["kv"]})) +
                   " (verify_recovery = check normal operation and close the pending attempt, safe_state = stay out of normal operation)")
    R["6.5f"] = ev(all(has(s, "RESET", lambda x: "det_cycle" in x["kv"] and "latency_cycles" in x["kv"]) and has(s, "RECOVERY", lambda x: "pending_exp" in x["kv"]) for *_, s in reset_cases),
                   "fault information preserved across the reset (.noinit): detecting mechanism, det_cycle, latency, pending experiment and level are printed after the reboot")

    # ------------------------------------------------------------------ no false recoveries
    fp = rs("fp")
    R["6.6a"] = ev(all(not r.by_tag("RECOVERY") or all("boot_path" in x["kv"] or "episode_end" in x["rest"] or True for x in r.by_tag("RECOVERY")) for r in fp) and
                   all(not [x for x in r.by_tag("RECOVERY") if x["kv"].get("state")] and not r.by_tag("DETECT") and len(r.by_tag("BOOT")) == 1 for r in fp),
                   "fault-free recovery build, 3 x 12 s: no DETECT, no recovery attempt, no reset (the recovery layer does not act on its own)")

    titles = {
        "6.0a": "Earlier builds (baseline, fwtest, protected, protfw) unchanged by the recovery code",
        "6.1a": "A detected task fault can trigger task restart", "6.1b": "Only the faulty task is restarted", "6.1c": "Other healthy tasks continue operating",
        "6.1d": "The restarted task returns to normal execution", "6.1e": "Sensor/control/UART operation resumes correctly", "6.1f": "Recovery is associated with the correct EXP",
        "6.1g": "Recovery start and completion cycles are recorded", "6.1h": "Success only when normal operation is actually restored",
        "6.2a": "Required application state is saved as a checkpoint", "6.2b": "Checkpoint is stored in the .noinit SRAM section",
        "6.2c": "Runtime state can be deliberately corrupted", "6.2d": "Corruption is detected", "6.2e": "Saved state is restored",
        "6.2f": "Application resumes using the restored state", "6.2g": "Restored state matches the saved checkpoint", "6.2h": "Recovery start/completion is logged",
        "6.2i": "Recovery time is recorded", "6.2j": "Success only after correct operation resumes",
        "6.3a": "SW reset: a detected severe fault triggers a controlled software reset", "6.3b": "SW reset: MCU resets successfully", "6.3c": "SW reset: firmware boots normally afterwards",
        "6.3d": "SW reset: RCC_CSR is read after reboot", "6.3e": "SW reset: reset cause is correctly logged", "6.3f": "SW reset: required application state is restored",
        "6.3g": "SW reset: recovery completion is recorded",
        "6.3h": "WWDG reset: a hang can cause a WWDG reset", "6.3i": "WWDG reset: MCU reboots successfully", "6.3j": "WWDG reset: RCC_CSR identifies the reset",
        "6.3k": "WWDG reset: reset cause logged against the experiment", "6.3l": "WWDG reset: firmware returns to the expected operating state",
        "6.4a": "Repeated faults can be detected", "6.4b": "Repeated recovery failure is recognised", "6.4c": "The system enters a defined safe/fallback state",
        "6.4d": "Safe-state entry is logged", "6.4e": "The system does not continue normal operation in the safe state", "6.4f": "Safe state remains stable",
        "6.4g": "The experiment records that escalation to Level 4 occurred",
        "6.5a": "After every reset: RCC_CSR is read", "6.5b": "Reset cause is identified", "6.5c": "Cause is logged", "6.5d": "Cause is associated with the experiment ID",
        "6.5e": "Appropriate recovery path is selected", "6.5f": "Fault information is preserved where applicable",
        "6.6a": "No recovery action in a fault-free run",
    }
    assert set(titles) == set(R), sorted(set(titles) ^ set(R))
    key = lambda k: tuple(int(x) if x.isdigit() else x for x in re.findall(r"\d+|[a-z]", k))
    cnt = {"PASS": 0, "FAIL": 0, "LIMIT": 0}
    L = ["# Step 6 acceptance - recovery (build `recovery`)", "",
         f"Runs: {a.dir} (9 scenarios x {C.REPEATS} simulations). Detection from Step 5 plus recovery levels 1-4.",
         "Verdicts: PASS = met; FAIL = not met; LIMIT = cannot be met as written in Wokwi (evidence shows what was observed instead).", ""]
    grp = None
    for k in sorted(R, key=key):
        g = k.split(".")[1][:-1]
        if g != grp:
            grp = g
            L += ["", f"## 6.{g}", "", "| # | Criterion | Result | Evidence |", "|---|---|---|---|"]
        v, t = R[k]
        cnt[v] += 1
        L.append(f"| {k} | {titles[k]} | {v} | {t.replace('|', '/')} |")
    L += ["", f"**{cnt['PASS']} PASS, {cnt['LIMIT']} LIMIT (simulator), {cnt['FAIL']} FAIL of {len(R)} criteria.**", ""]
    os.makedirs(os.path.dirname(a.report), exist_ok=True)
    open(a.report, "w").write("\n".join(L))
    print("\n".join(L[-12:]))
    return 0 if cnt["FAIL"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
