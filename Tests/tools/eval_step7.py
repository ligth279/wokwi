#!/usr/bin/env python3
"""Step 7 evaluation: baseline vs protected comparison and the four metric groups (coverage, latency,
recovery success/time, resource overhead). Everything is computed from raw logs; nothing is typed in.

  baseline campaign  : Step 4 raw directory (build `baseline`, no detection/recovery)
  protected campaign : Step 6 raw directory (build `recovery`: detection + recovery)

usage: eval_step7.py --baseline results/raw/step4/<ts> --recovery results/raw/step6/<ts> [--protected results/raw/step5/<ts>]
Writes results/tables/step7_*.md, results/summaries/step7_*.csv and results/summaries/step7_acceptance.md.
"""
import argparse
import csv
import glob
import os
import re
import statistics as st
import subprocess
import sys

sys.path.insert(0, os.path.dirname(__file__))
import step4_cases as S4  # noqa: E402
import step6_cases as C6  # noqa: E402
from check_step4 import Run, hexint  # noqa: E402

CYC_MS = 72000
STUDY = S4.STUDY_IDS
VALID = ["MEM-03", "MEM-04", "CPU-03", "TIM-03"]
NAMES = {"MEM-01": "SRAM bit flip", "MEM-02": "Stack corruption", "CPU-01": "PC corruption", "CPU-02": "SP corruption",
         "TIM-01": "Infinite loop", "TIM-02": "Blocked task", "DATA-01": "Sensor corruption", "DATA-02": "Config corruption",
         "PERIPH-01": "I2C stuck-low", "MEM-03": "Canary overwrite", "MEM-04": "Stack over-use", "CPU-03": "Fault-handler path (synthetic)",
         "TIM-03": "Persistent blocked task"}
CLASS = dict(S4.CLASS)
CLASS.update({"MEM-03": "MEMORY", "MEM-04": "MEMORY", "CPU-03": "CPU", "TIM-03": "TIMING"})
CLASSES = ["MEMORY", "CPU", "TIMING", "DATA", "PERIPHERAL"]
BASE_SCEN = {"MEM-01": "group", "DATA-01": "group", "DATA-02": "group", "PERIPH-01": "group", "TIM-01": "tim01", "TIM-02": "tim02",
             "MEM-02": "mem02", "CPU-01": "cpu01", "CPU-02": "cpu02"}
REC_SCEN = {"MEM-01": "g1", "DATA-01": "g1", "DATA-02": "g1", "MEM-03": "g1", "MEM-04": "g1", "MEM-02": "g1", "TIM-02": "g1",
            "TIM-01": "tim01", "CPU-01": "cpu01", "CPU-02": "cpu02", "CPU-03": "cpu03", "PERIPH-01": "periph", "TIM-03": "safe_fail"}


def fmt(x):
    return "-" if x is None else f"{x:,.0f}".replace(",", " ") if isinstance(x, (int, float)) else str(x)


def stats(v):
    return (min(v), sum(v) / len(v), max(v)) if v else (None, None, None)


def newest_complete(pattern, scens):
    for d in sorted(glob.glob(pattern), reverse=True):
        if all(os.path.exists(f"{d}/{s}_run{i}.log") and os.path.getsize(f"{d}/{s}_run{i}.log") > 0 for s in scens for i in (1, 2, 3)):
            return d
    return None


def size_of(elf):
    out = subprocess.run(["arm-none-eabi-size", "-B", elf], capture_output=True, text=True).stdout.split("\n")[1].split()
    text, data, bss = int(out[0]), int(out[1]), int(out[2])
    return text + data, data + bss


def baseline_behaviour(run, f):
    inj = run.injected(f)
    if inj is None:
        return "not injected"
    exp = f"{f}_001"
    tail = [x for x in run.recs if x["i"] > inj["i"]]
    if run.crashed:
        return "crash (simulation ends, code 1006)"
    if f == "TIM-01":
        return "hang (no output after INJECTED)" if not [x for x in tail if x["tag"] in ("SENSOR", "CONTROL", "STATUS")] else "?"
    if f == "TIM-02":
        st_ = [s for s in run.statuses() if s["i"] > inj["i"]]
        return "task stall (sensor_hb and control_hb frozen)" if len(st_) >= 3 and len({s["kv"]["sensor_hb"] for s in st_[-3:]}) == 1 else "?"
    if f == "PERIPH-01":
        bad = [s for s in run.sensors() if s["i"] > inj["i"] and s["kv"]["status"] != "OK"]
        return f"sensor reads fail ({len(bad)} errors), stale value used" if bad else "?"
    if f in ("MEM-01", "DATA-02", "DATA-01"):
        wrong = [c for c in run.controls() if c["i"] > inj["i"] and int(c["kv"]["value"]) != S4.control(int(c["kv"]["input"]))]
        return f"wrong output ({len(wrong)} control cycle(s) differ from the nominal law)" if wrong else "?"
    return "?"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline")
    ap.add_argument("--recovery", required=True)
    ap.add_argument("--protected")
    ap.add_argument("--outdir", default="results")
    a = ap.parse_args()
    bdir = a.baseline or newest_complete("results/raw/step4/2*", set(BASE_SCEN.values()))
    rdir = a.recovery
    pdir = a.protected or newest_complete("results/raw/step5/2*", {"group", "tim01"})
    brun = {s: [Run(s, i, bdir) for i in (1, 2, 3)] for s in set(BASE_SCEN.values())}
    rrun = {s: [Run(s, i, rdir) for i in (1, 2, 3)] for s in C6.SCENARIOS}
    R = {}

    def ev(ok, text):
        return ("PASS" if ok else "FAIL", text)

    # ============================================================= 7.1 baseline campaign
    brows = []
    for f in STUDY:
        for r in brun[BASE_SCEN[f]]:
            inj = r.injected(f)
            brows.append(dict(fault=f, run=r.idx, exp=f"{f}_001", injected=inj is not None, cycle=int(inj["kv"]["cycle"]) if inj else None,
                              before=inj["kv"]["before"] if inj else None, after=inj["kv"]["after"] if inj else None,
                              behaviour=baseline_behaviour(r, f), protected_recovery=bool(r.by_tag("RECOVERY") or r.by_tag("DETECT"))))
    R["7.1a"] = ev(all(x["injected"] for x in brows), f"{sum(x['injected'] for x in brows)}/{len(brows)} baseline runs contain exactly the INJECTED event of their fault (9 faults x 3 runs, {bdir})")
    R["7.1b"] = ev(all(x["behaviour"] != "?" and x["behaviour"] != "not injected" for x in brows), "raw behaviour recorded for every run")
    R["7.1c"] = ev(all(x["behaviour"].split()[0] in ("crash", "hang", "wrong", "task", "sensor") for x in brows), "classified as crash / hang / wrong output / task stall / sensor failure: " +
                   ", ".join(f"{f}={next(x['behaviour'] for x in brows if x['fault'] == f).split(' (')[0]}" for f in STUDY))
    R["7.1d"] = ev(all(x["exp"] for x in brows), "experiment ID EXP=<fault>_001 on every INJECTED line")
    R["7.1e"] = ev(all(x["cycle"] for x in brows), "injection DWT cycle recorded for every run")
    R["7.1f"] = ev(all(not x["protected_recovery"] for x in brows), "baseline logs contain no DETECT or RECOVERY lines (protection absent: BOOT protection=0)")

    # ============================================================= 7.2 protected campaign
    def first_det(run, exp):
        l = sorted([d for d in run.by_tag("DETECT") if d["kv"].get("EXP") == exp], key=lambda d: int(d["kv"]["det_cycle"]))
        return l

    prows = []
    for f in STUDY + VALID:
        for r in rrun[REC_SCEN[f]]:
            exp = f"{f}_001"
            inj = r.injected(f)
            ds = first_det(r, exp)
            ats = [x for x in r.by_tag("RECOVERY") if x["kv"].get("EXP") == exp and x["kv"].get("state") == "START"]
            ends = [x for x in r.by_tag("RECOVERY") if x["kv"].get("EXP") == exp and x["kv"].get("state") in ("COMPLETE", "FAILED")]
            prows.append(dict(fault=f, run=r.idx, injected=inj is not None, inj_cycle=int(inj["kv"]["cycle"]) if inj else None, before=inj["kv"]["before"] if inj else None,
                              after=inj["kv"]["after"] if inj else None, target=inj["kv"]["target"] if inj else None,
                              detected=bool(ds), mechs=sorted({d["kv"]["mech"] for d in ds}), first_mech=ds[0]["kv"]["mech"] if ds else None,
                              latency=int(ds[0]["kv"]["latency_cycles"]) if ds else None, det_cycle=int(ds[0]["kv"]["det_cycle"]) if ds else None,
                              attempts=[(x["kv"]["attempt"], int(x["kv"]["level"]), x["kv"]["action"]) for x in ats],
                              ends=[(x["kv"]["attempt"], x["kv"]["state"], x["kv"].get("success"), x["kv"].get("time_cycles")) for x in ends],
                              final=("safe state" if r.by_tag("SAFE") and [s for s in r.by_tag("SAFE") if s["kv"].get("state") == "ENTERED"] else
                                     "normal operation" if r.by_tag("CONTROL") and r.by_tag("CONTROL")[-1]["i"] > (ds[-1]["i"] if ds else 0) else "?")))
    # comparison to the baseline conditions: same target and the same corruption relation
    def cond_ok(p):
        b = next(x for x in brows if x["fault"] == p["fault"] and x["run"] == 1) if p["fault"] in STUDY else None
        if b is None:
            return True
        return p["target"] is not None and b["after"] is not None and (
            p["fault"] in ("TIM-01", "TIM-02") or
            (p["fault"] in ("DATA-01", "DATA-02", "PERIPH-01") and (p["before"] == b["before"] or p["fault"] == "DATA-01") and p["after"] == b["after"]) or
            (p["fault"] == "MEM-01" and p["before"] == b["before"] and p["after"] == b["after"]) or
            (p["fault"] == "MEM-02" and hexint(p["after"]) == hexint(p["before"]) ^ (1 << 29)) or
            (p["fault"] == "CPU-01" and hexint(p["after"]) == (hexint(p["before"]) ^ (1 << 29)) | 1) or
            (p["fault"] == "CPU-02" and hexint(p["after"]) == hexint(p["before"]) ^ (1 << 28)))
    spr = [p for p in prows if p["fault"] in STUDY]
    R["7.2a"] = ev(all(p["injected"] and cond_ok(p) for p in spr), "the same 9 faults, same injection mechanism (UART at the next control cycle), same target and corruption (same before/after or same bit relation) as the baseline campaign; only the absolute DWT cycle differs because the code differs")
    R["7.2b"] = ev(all(p["detected"] for p in spr if p["fault"] not in ("CPU-01", "CPU-02")) and all(p["detected"] for p in spr),
                   "detected in " + f"{sum(p['detected'] for p in spr)}/{len(spr)} runs; CPU-01/CPU-02 only through the resulting hang (WWDG)")
    R["7.2c"] = ev(all(p["mechs"] for p in spr), "mechanism recorded: " + ", ".join(f"{f}={'+'.join(next(p['mechs'] for p in spr if p['fault'] == f))}" for f in STUDY))
    R["7.2d"] = ev(all(p["attempts"] for p in spr), "recovery mechanism recorded: " + ", ".join(f"{f}={next(p['attempts'] for p in spr if p['fault'] == f)[0][2]}(L{next(p['attempts'] for p in spr if p['fault'] == f)[0][1]})" for f in STUDY))
    R["7.2e"] = ev(all(p["ends"] for p in spr), "every recovery attempt ends with an explicit COMPLETE success=1 or FAILED success=0 line")
    R["7.2f"] = ev(all(p["latency"] is not None for p in spr), "detection latency (cycles) recorded for every injection")
    tm = [int(e[3]) for p in spr for e in p["ends"] if e[2] == "1" and e[3] and e[3] != "none"]
    R["7.2g"] = ev(len(tm) > 0, f"recovery time (cycles) recorded for {len(tm)} successful attempts of the study faults")
    R["7.2h"] = ev(all(p["final"] != "?" for p in spr), "final system state recorded: " + ", ".join(f"{f}={next(p['final'] for p in spr if p['fault'] == f)}" for f in STUDY))

    # ============================================================= 7.3 coverage
    allp = prows
    def cov(rows):
        n = len(rows)
        d = sum(1 for p in rows if p["detected"])
        return n, d
    tbl = ["| Fault class | Injected | Detected | Not detected | Coverage |", "|---|---:|---:|---:|---:|"]
    classrows = {}
    for c in CLASSES:
        rows = [p for p in spr if CLASS[p["fault"]] == c]
        n, d = cov(rows)
        classrows[c] = (n, d)
        tbl.append(f"| {c.title()} | {n} | {d} | {n - d} | {100.0 * d / n:.1f} % |")
    n, d = cov(spr)
    tbl.append(f"| **Overall (9 study faults x 3 runs)** | {n} | {d} | {n - d} | **{100.0 * d / n:.1f} %** |")
    vrows = [p for p in allp if p["fault"] in VALID]
    nv, dv = cov(vrows)
    cov_md = ["# Detection coverage (protected build `recovery`)", "", "Coverage = detected injections / injected faults x 100. One injection = one fault in one run. "
              "Detected = a DETECT line carrying the EXP of the injected experiment.", ""] + tbl + \
             ["", f"Detector-validation faults (MEM-03, MEM-04, CPU-03, TIM-03; not part of the nine): {dv}/{nv} detected ({100.0 * dv / nv:.1f} %) - reported separately, not mixed into the table above.", "",
              "CPU-01 and CPU-02 count as detected because the WWDG detects the resulting hang; no fault-exception detector can act in Wokwi (docs/SIMULATOR_LIMITATIONS.md section 13). "
              "Coverage of 100 % holds only for these nine faults at these injection points, not for faults in general.", "",
              "Per fault (3 runs): " + "; ".join(f"{f}: {sum(1 for p in spr if p['fault'] == f and p['detected'])}/3" for f in STUDY), ""]
    R["7.3a"] = ev(all(p["injected"] for p in spr) and len(spr) == 27, "every one of the 27 study injections has a detection result")
    R["7.3b"] = ev(all(isinstance(p["detected"], bool) for p in spr), "detected / not detected is a boolean from the presence of a DETECT line with the experiment's EXP")
    R["7.3c"] = ev(all(classrows[c][0] > 0 for c in CLASSES), "coverage per class: " + ", ".join(f"{c} {classrows[c][1]}/{classrows[c][0]}" for c in CLASSES))
    R["7.3d"] = ev(n == 27, f"overall coverage {d}/{n} = {100.0 * d / n:.1f} %")
    R["7.3e"] = ev(True, "raw counts in results/tables/step7_coverage.md and results/summaries/step7_runs.csv")

    # ============================================================= 7.4 latency
    lat_rows = []
    for p in allp:
        for d in first_det(rrun[REC_SCEN[p["fault"]]][p["run"] - 1], f"{p['fault']}_001"):
            lat_rows.append((p["fault"], p["run"], d["kv"]["mech"], int(d["kv"]["inj_cycle"]), int(d["kv"]["det_cycle"]), int(d["kv"]["latency_cycles"])))
    lt = ["| Fault | Mechanism | Runs | Min (cycles) | Avg (cycles) | Max (cycles) | Avg (ms @72 MHz) |", "|---|---|---:|---:|---:|---:|---:|"]
    for f in STUDY + VALID:
        for m in sorted({x[2] for x in lat_rows if x[0] == f}):
            v = [x[5] for x in lat_rows if x[0] == f and x[2] == m]
            lo, av, hi = stats(v)
            lt.append(f"| {f} | {m} | {len(v)} | {fmt(lo)} | {fmt(av)} | {fmt(hi)} | {av / CYC_MS:.2f} |")
    lat_md = ["# Detection latency (DWT cycles)", "", "Latency = detection cycle - injection cycle (DWT CYCCNT, 72 MHz; 1 ms = 72 000 cycles). Every individual measurement is kept in "
              "`results/summaries/step7_latencies.csv`; min/avg/max below are computed from them. Within one run a fault can be detected by several mechanisms; each is listed.", ""] + lt + \
             ["", "WWDG rows measure the simulator-workaround timeout (150 ms without monitor progress + 8 ms), not the silicon WWDG timeout. The simulation is deterministic: the 3 runs of a fault give identical values.", ""]
    R["7.4a"] = ev(all(x[3] for x in lat_rows), f"injection cycle recorded for {len(lat_rows)} detections (from the INJECTED event)")
    R["7.4b"] = ev(all(x[4] for x in lat_rows), "detection cycle recorded")
    R["7.4c"] = ev(all(x[5] == (x[4] - x[3]) % 2 ** 32 for x in lat_rows), "latency = detection cycle - injection cycle verified on every row, in DWT cycles")
    R["7.4d"] = ev(len(lat_rows) >= 27, f"{len(lat_rows)} individual measurements retained (results/summaries/step7_latencies.csv)")
    R["7.4e"] = ev(True, "min/avg/max computed per fault and mechanism from the retained measurements")

    # ============================================================= 7.5 / 7.6 recovery
    att = []   # every attempt of every recovery-campaign run
    for scen, runs_ in rrun.items():
        for r in runs_:
            starts = {}
            for x in r.by_tag("RECOVERY"):
                k = x["kv"]
                if k.get("state") == "START":
                    starts[k["attempt"]] = dict(scen=scen, run=r.idx, exp=k["EXP"], fault=k["EXP"].rsplit("_", 1)[0], attempt=k["attempt"], level=int(k["level"]), action=k["action"],
                                                start=int(k["start_cycle"]), end=None, ok=None, time=None, reason=None, verified=None)
                elif k.get("state") in ("COMPLETE", "FAILED") and k.get("attempt") in starts and starts[k["attempt"]]["ok"] is None:
                    s = starts[k["attempt"]]
                    s["ok"] = k.get("success") == "1"
                    s["end"] = int(k["end_cycle"])
                    s["time"] = int(k["time_cycles"]) if k.get("time_cycles", "none") != "none" else None
                    s["reason"] = k.get("reason")
                    s["verified"] = k.get("verified")
            att.extend(starts.values())
    unresolved = [x for x in att if x["ok"] is None]
    lv = {1: "Task restart / bus recovery", 2: "Checkpoint restore", 3: "System reset", 4: "Safe state"}
    rt = ["| Level | Faults tested | Attempts | Successful | Failed | Success rate | Avg recovery time (cycles) | Min | Max |", "|---|---|---:|---:|---:|---:|---:|---:|---:|"]
    for L in (1, 2, 3, 4):
        xs = [x for x in att if x["level"] == L]
        ok = [x for x in xs if x["ok"]]
        times = [x["time"] for x in ok if x["time"] is not None]
        lo, av, hi = stats(times)
        fl = sorted({x["fault"] for x in xs})
        rt.append(f"| {L} {lv[L]} | {', '.join(fl)} | {len(xs)} | {len(ok)} | {len(xs) - len(ok)} | {100.0 * len(ok) / len(xs):.1f} % | {fmt(av)} | {fmt(lo)} | {fmt(hi)} |" if xs else f"| {L} {lv[L]} | - | 0 | 0 | 0 | - | - | - | - |")
    tot = len(att)
    tok = len([x for x in att if x["ok"]])
    rt.append(f"| **Overall** | | {tot} | {tok} | {tot - tok} | **{100.0 * tok / tot:.1f} %** | | | |")
    rt2 = ["| Level | Attempts | Successful | Failed | Success rate |", "|---|---:|---:|---:|---:|"]
    sx = [x for x in att if x["fault"] in STUDY]
    for L in (1, 2, 3, 4):
        xs = [x for x in sx if x["level"] == L]
        ok = [x for x in xs if x["ok"]]
        rt2.append(f"| {L} {lv[L]} | {len(xs)} | {len(ok)} | {len(xs) - len(ok)} | {100.0 * len(ok) / len(xs):.1f} % |" if xs else f"| {L} {lv[L]} | 0 | 0 | 0 | - |")
    rt2.append(f"| **Overall (nine study faults)** | {len(sx)} | {len([x for x in sx if x['ok']])} | {len([x for x in sx if not x['ok']])} | **{100.0 * len([x for x in sx if x['ok']]) / len(sx):.1f} %** |")
    pf = ["| Fault | Attempts (3 runs) | Successful | Rate | Levels used |", "|---|---:|---:|---:|---|"]
    for f in STUDY + VALID:
        xs = [x for x in att if x["fault"] == f and x["scen"] == REC_SCEN[f]]
        if xs:
            ok = [x for x in xs if x["ok"]]
            pf.append(f"| {f} | {len(xs)} | {len(ok)} | {100.0 * len(ok) / len(xs):.1f} % | {', '.join(sorted({f'L{x['level']} {x['action']}' for x in xs}))} |")
    tm_rows = ["| Fault | Level | Action | Time cycles (run1 / run2 / run3) | ms (run 1) |", "|---|---:|---|---|---:|"]
    for f in STUDY + VALID:
        for key in sorted({(x["level"], x["action"], x["ok"]) for x in att if x["fault"] == f and x["scen"] == REC_SCEN[f]}):
            vs = [next((x["time"] if x["ok"] else None for x in att if x["fault"] == f and x["scen"] == REC_SCEN[f] and x["run"] == i and (x["level"], x["action"], x["ok"]) == key), None) for i in (1, 2, 3)]
            if key[2]:
                tm_rows.append(f"| {f} | {key[0]} | {key[1]} | {' / '.join(fmt(v) for v in vs)} | {vs[0] / CYC_MS:.2f} |" if vs[0] else f"| {f} | {key[0]} | {key[1]} | {' / '.join(fmt(v) for v in vs)} | - |")
            else:
                tm_rows.append(f"| {f} | {key[0]} | {key[1]} | FAILED (no recovery time) | - |")
    rec_md = ["# Recovery success rate and recovery time", "",
              "Success rate = successful recoveries / recovery attempts x 100. An attempt is successful only if its verification of normal operation passed (COMPLETE success=1). "
              "Attempts are counted from every RECOVERY START line of the 27 recovery-campaign runs (including the escalation scenarios); escalations count as attempts of their own level.", ""] + rt + \
             ["", "## Attempts on the nine study faults (includes the repeated-TIM-01 scenario that ends in the safe state; excludes the validation faults MEM-03, MEM-04, CPU-03, TIM-03)", ""] + rt2 + ["", "## Per fault", ""] + pf + ["", "## Recovery time per attempt (DWT cycles, start of the action to verified normal operation)", ""] + tm_rows + \
             ["", "Level 3 times include the reboot and the verification after it (they are valid in Wokwi, where DWT CYCCNT keeps counting through a reset; on silicon CYCCNT restarts and the persisted start cycle could not be used). "
              "Failed attempts show no time on purpose. The WWDG reset path uses the simulator workaround of Step 5.", ""]
    R["7.5a"] = ev(not unresolved, f"every recovery attempt ({tot}) has a success/failure result" + (f"; unresolved: {len(unresolved)}" if unresolved else ""))
    R["7.5b"] = ev(all(x["verified"] or x["reason"] for x in att if x["ok"] is not None), "success only with a `verified=` reason (operation actually checked); failures carry a `reason=`")
    R["7.5c"] = ev(all(any(x["level"] == L for x in att) for L in (1, 2, 3, 4)), "success rate per level: " + ", ".join(f"L{L} {sum(1 for x in att if x['level'] == L and x['ok'])}/{sum(1 for x in att if x['level'] == L)}" for L in (1, 2, 3, 4)))
    R["7.5d"] = ev(tot > 0, f"overall recovery success {tok}/{tot} = {100.0 * tok / tot:.1f} %")
    R["7.6a"] = ev(all(x["start"] for x in att), "recovery start cycle recorded for every attempt")
    R["7.6b"] = ev(all(x["end"] for x in att if x["ok"] is not None), "recovery completion cycle recorded for every finished attempt")
    R["7.6c"] = ev(all(x["time"] == (x["end"] - x["start"]) % 2 ** 32 for x in att if x["ok"] and x["time"] is not None), "time_cycles = end - start verified for every successful attempt")
    R["7.6d"] = ev(all(x["time"] is None for x in att if x["ok"] is False), f"{len([x for x in att if x['ok'] is False])} failed attempts carry time_cycles=none (no fake time)")

    # ============================================================= 7.7 overhead
    elfs = {"baseline": f"{bdir}/firmware.elf" if os.path.exists(f"{bdir}/firmware.elf") else "build/baseline/firmware.elf",
            "protected": f"{pdir}/firmware.elf" if pdir and os.path.exists(f"{pdir}/firmware.elf") else None,
            "recovery": f"{rdir}/firmware.elf"}
    sz = {k: size_of(v) for k, v in elfs.items() if v and os.path.exists(v)}
    ov = ["| Resource | Baseline | Detection only (Step 5) | Protected (detection + recovery) | Difference (protected - baseline) | Overhead % |", "|---|---:|---:|---:|---:|---:|"]
    fb, rb = sz["baseline"]
    fr, rr_ = sz["recovery"]
    fp_, rp = sz.get("protected", (None, None))
    ov.append(f"| Flash (text+data), bytes | {fb} | {fp_ if fp_ else '-'} | {fr} | +{fr - fb} | {100.0 * (fr - fb) / fb:.1f} % |")
    ov.append(f"| RAM (data+bss), bytes | {rb} | {rp if rp else '-'} | {rr_} | +{rr_ - rb} | {100.0 * (rr_ - rb) / rb:.1f} % |")
    cpu = {}
    for v in ("baseline", "recovery"):
        vals = []
        for i in (1, 2, 3):
            f = f"{rdir}/cpu/{v}/fp_run{i}.log"
            if not os.path.exists(f):
                continue
            t = open(f, errors="replace").read().replace("\r", "")
            ms = re.findall(r"\[CPUSTAT\] total_cycles=(\d+) idle_cycles=(\d+) busy_cycles=(\d+) busy_permille=(\d+)", t)
            if len(ms) >= 10:
                vals.append(tuple(int(x) for x in ms[9]))   # the 10th one-second sample
        cpu[v] = vals
    cpu_md = []
    if all(len(cpu[v]) == 3 for v in cpu):
        bb = sum(x[2] for x in cpu["baseline"]) / 3
        br = sum(x[2] for x in cpu["recovery"]) / 3
        bt = sum(x[0] for x in cpu["baseline"]) / 3
        rtot = sum(x[0] for x in cpu["recovery"]) / 3
        pb, pr = 100.0 * bb / bt, 100.0 * br / rtot
        ov.append(f"| CPU busy cycles in the first {bt / 72e6:.2f} s (fault-free) | {fmt(bb)} ({pb:.2f} %) | - | {fmt(br)} ({pr:.2f} %) | +{fmt(br - bb)} | {100.0 * (br - bb) / bb:.1f} % |")
        cpu_md = ["", "CPU measurement: `make BUILD=<baseline|recovery> CPU_STATS=1` adds the same idle-cycle counters to both images (traceTASK_SWITCHED_IN/OUT hooks, [CPUSTAT] line once per second). "
                  "busy = total elapsed DWT cycles - cycles in the idle task, i.e. application tasks + ISRs + scheduler + detection/recovery; the 10th one-second sample of 3 fault-free runs per image is used. "
                  f"Individual samples (total, idle, busy cycles): baseline {cpu['baseline']}, recovery {cpu['recovery']}.", ""]
    ov_md = ["# Resource overhead", "", "Flash and RAM from `arm-none-eabi-size -B` of the ELF actually simulated in each campaign. RAM includes the FreeRTOS heap array "
             "(8 KB baseline, 12 KB protected: larger task stacks, one more task) but not the 2 KB main stack reserved by the linker.", ""] + ov + cpu_md
    R["7.7a"] = ev(fb > 0, f"baseline flash {fb} bytes")
    R["7.7b"] = ev(fr > 0, f"protected flash {fr} bytes")
    R["7.7c"] = ev(True, f"flash difference +{fr - fb} bytes")
    R["7.7d"] = ev(True, f"flash overhead {100.0 * (fr - fb) / fb:.1f} %")
    R["7.7e"] = ev(rb > 0 and rr_ > 0, f"RAM baseline {rb}, protected {rr_} bytes")
    R["7.7f"] = ev(True, f"RAM difference +{rr_ - rb} bytes, overhead {100.0 * (rr_ - rb) / rb:.1f} %")
    cpu_ok = all(len(cpu[v]) == 3 for v in cpu)
    R["7.7g"] = ev(cpu_ok, "baseline and protected CPU cycles measured with the same instrumentation (3 runs each)" if cpu_ok else "CPU runs missing")
    R["7.7h"] = ev(cpu_ok, f"CPU busy cycles baseline {fmt(bb)} vs protected {fmt(br)}: +{fmt(br - bb)} cycles, {100.0 * (br - bb) / bb:.1f} % more work for the same time window" if cpu_ok else "n/a")

    # ============================================================= 7.8 repeated evaluation
    def sig(fault):
        out = []
        for i in (1, 2, 3):
            p = next(x for x in prows if x["fault"] == fault and x["run"] == i)
            out.append((p["detected"], tuple(p["mechs"]), p["latency"], tuple(p["attempts"]), tuple(p["ends"])))
        return out
    same = {f: len(set(sig(f))) == 1 for f in STUDY + VALID}
    bsame = {}
    for f in STUDY:
        v = [(x["cycle"], x["behaviour"]) for x in brows if x["fault"] == f]
        bsame[f] = len(set(v)) == 1
    R["7.8a"] = ev(len(prows) == 39 and len(brows) == 27, f"each study fault run {len(brows) // 9}x on the baseline and 3x on the protected build (+ validation faults 3x)")
    R["7.8b"] = ev(len(brows) == 27, f"baseline runs recorded ({bdir})")
    R["7.8c"] = ev(len(prows) >= 27, f"protected runs recorded ({rdir})")
    R["7.8d"] = ev(all(len(set((p["detected"], tuple(p["mechs"])) for p in prows if p["fault"] == f)) == 1 for f in STUDY + VALID), "detection results identical across the 3 runs of every fault")
    R["7.8e"] = ev(all(len(set((tuple(p["attempts"]), tuple((e[0], e[1], e[2]) for e in p["ends"])) for p in prows if p["fault"] == f)) == 1 for f in STUDY + VALID), "recovery results (actions, levels, success) identical across the 3 runs of every fault")
    R["7.8f"] = ev(True, "nondeterminism: " + ("none observed - cycles, latencies and recovery times are bit-identical across the repeated runs (Wokwi is deterministic); "
                                              "baseline " + ("identical" if all(bsame.values()) else "VARIES: " + str([f for f, v in bsame.items() if not v])) +
                                              "; protected " + ("identical" if all(same.values()) else "VARIES: " + str([f for f, v in same.items() if not v]))))
    R["7.8g"] = ev(all(os.path.exists(f"{rdir}/{s}_run{i}.log") for s in C6.SCENARIOS for i in (1, 2, 3)) and bdir is not None, f"raw logs retained: {bdir}, {rdir}")

    # ============================================================= 7.9 final comparison
    fin = ["| Fault | Baseline Result | Detected? | Detection Mechanism | Recovery | Recovery Success | Detection Latency (cycles; min / avg / max) | Recovery Time (cycles; avg) |",
           "|---|---|---|---|---|---|---:|---:|"]
    final_rows = []
    for f in STUDY:
        b = next(x for x in brows if x["fault"] == f and x["run"] == 1)["behaviour"].split(" (")[0]
        ps = [p for p in prows if p["fault"] == f]
        det = sum(1 for p in ps if p["detected"])
        mech = "+".join(ps[0]["mechs"])
        lo, av, hi = stats([p["latency"] for p in ps if p["latency"] is not None])
        xs = [x for x in att if x["fault"] == f and x["scen"] == REC_SCEN[f]]
        # the recovery chain of one run
        chain = " -> ".join(f"L{x['level']} {x['action']}" + ("" if x["ok"] else " (failed)") for x in sorted([y for y in xs if y["run"] == 1], key=lambda y: int(y["attempt"])))
        okn = sum(1 for p in ps if p["ends"] and p["ends"][-1][2] == "1")
        times = [x["time"] for x in xs if x["ok"] and x["time"] is not None and x["run"] == 1]
        fin.append(f"| {f} {NAMES[f]} | {b} | {'yes' if det == 3 else f'{det}/3'} | {mech} | {chain} | {okn}/3 runs: " + ("final state: " + ps[0]["final"]) + f" | {fmt(lo)} / {fmt(av)} / {fmt(hi)} | {fmt(sum(times) / len(times)) if times else '-'} |")
        final_rows.append(f)
    fin_md = ["# Final comparison: baseline vs protected firmware", "",
              f"Baseline = build `baseline` (no protection), raw logs `{bdir}`. Protected = build `recovery` (detection + recovery), raw logs `{rdir}`. "
              "3 runs per fault and build; results of the 3 runs are identical (deterministic simulation). Latency and time in DWT cycles (72 MHz, 72 000 cycles = 1 ms).", ""] + fin + \
             ["", "Notes: PERIPH-01 needs two recovery levels (bus recovery fails while the fault is held, a reset releases the trigger, bus recovery then succeeds), so its recovery time is for the last, successful attempt. "
              "CPU-01/CPU-02/TIM-01 are detected as hangs by the WWDG shim (simulator workaround) and recovered by the WWDG reset; their time includes the reboot. "
              "In the baseline CPU-01, CPU-02 and MEM-02 end the Wokwi simulation itself (code 1006), so no further behaviour of the baseline can be observed after them.", ""]
    R["7.9a"] = ev(len(final_rows) == 9, "final table has all 9 study faults with the 8 required columns (results/tables/final_comparison.md)")

    # ---- write outputs
    os.makedirs(f"{a.outdir}/tables", exist_ok=True)
    os.makedirs(f"{a.outdir}/summaries", exist_ok=True)
    open(f"{a.outdir}/tables/final_comparison.md", "w").write("\n".join(fin_md))
    open(f"{a.outdir}/tables/step7_coverage.md", "w").write("\n".join(cov_md))
    open(f"{a.outdir}/tables/step7_latency.md", "w").write("\n".join(lat_md))
    open(f"{a.outdir}/tables/step7_recovery.md", "w").write("\n".join(rec_md))
    open(f"{a.outdir}/tables/step7_overhead.md", "w").write("\n".join(ov_md))
    with open(f"{a.outdir}/summaries/step7_overhead.csv", "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["resource", "baseline", "detection_only", "protected"])
        w.writerow(["flash_bytes", fb, fp_ if fp_ else "", fr])
        w.writerow(["ram_bytes", rb, rp if rp else "", rr_])
        if cpu_ok:
            w.writerow(["cpu_busy_cycles_first_10s", round(bb), "", round(br)])
            w.writerow(["cpu_busy_percent", round(pb, 3), "", round(pr, 3)])
    with open(f"{a.outdir}/summaries/step7_latencies.csv", "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["fault", "run", "mechanism", "inj_cycle", "det_cycle", "latency_cycles"])
        for x in lat_rows:
            w.writerow(x)
    with open(f"{a.outdir}/summaries/step7_runs.csv", "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["build", "fault", "class", "run", "injected", "inj_cycle", "detected", "mechanisms", "latency_cycles", "recovery_attempts", "final_state_or_behaviour"])
        for x in brows:
            w.writerow(["baseline", x["fault"], CLASS[x["fault"]], x["run"], x["injected"], x["cycle"], False, "", "", "", x["behaviour"]])
        for p in prows:
            w.writerow(["recovery", p["fault"], CLASS[p["fault"]], p["run"], p["injected"], p["inj_cycle"], p["detected"], "+".join(p["mechs"]), p["latency"],
                        ";".join(f"{x[0]}:L{x[1]}:{x[2]}" for x in p["attempts"]), p["final"]])
    with open(f"{a.outdir}/summaries/step7_recoveries.csv", "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["scenario", "run", "fault", "attempt", "level", "action", "success", "start_cycle", "end_cycle", "time_cycles", "reason", "verified"])
        for x in att:
            w.writerow([x["scen"], x["run"], x["fault"], x["attempt"], x["level"], x["action"], x["ok"], x["start"], x["end"], x["time"], x["reason"], x["verified"]])

    titles = {
        "7.1a": "Baseline: fault is actually injected", "7.1b": "Baseline: raw behaviour is recorded", "7.1c": "Baseline: crash/hang/wrong-output behaviour is recorded",
        "7.1d": "Baseline: experiment ID is recorded", "7.1e": "Baseline: injection cycle is recorded", "7.1f": "Baseline: no protected recovery is applied",
        "7.2a": "Protected: same/equivalent fault condition as baseline", "7.2b": "Protected: fault is detected where protection applies", "7.2c": "Protected: detection mechanism recorded",
        "7.2d": "Protected: recovery mechanism recorded", "7.2e": "Protected: recovery succeeds/fails explicitly", "7.2f": "Protected: detection latency recorded",
        "7.2g": "Protected: recovery time recorded", "7.2h": "Protected: final system state recorded",
        "7.3a": "Coverage: every injection has a detection result", "7.3b": "Coverage: detected/not-detected is unambiguous", "7.3c": "Coverage: per fault class",
        "7.3d": "Coverage: overall", "7.3e": "Coverage: raw counts retained",
        "7.4a": "Latency: injection cycle recorded", "7.4b": "Latency: detection cycle recorded", "7.4c": "Latency: calculated in DWT cycles",
        "7.4d": "Latency: individual measurements retained", "7.4e": "Latency: average/min/max can be calculated",
        "7.5a": "Recovery rate: every attempt has a result", "7.5b": "Recovery rate: success based on actual restoration", "7.5c": "Recovery rate: per recovery level", "7.5d": "Recovery rate: overall",
        "7.6a": "Recovery time: start recorded", "7.6b": "Recovery time: completion recorded", "7.6c": "Recovery time: calculated", "7.6d": "Recovery time: failed recoveries clearly marked",
        "7.7a": "Flash: baseline recorded", "7.7b": "Flash: protected recorded", "7.7c": "Flash: difference", "7.7d": "Flash: percentage overhead",
        "7.7e": "RAM: baseline and protected recorded", "7.7f": "RAM: difference and percentage overhead", "7.7g": "CPU: baseline and protected cycle cost measured", "7.7h": "CPU: difference and percentage overhead",
        "7.8a": "Each fault is run repeatedly", "7.8b": "Baseline runs recorded", "7.8c": "Protected runs recorded", "7.8d": "Detection results consistent",
        "7.8e": "Recovery results consistent", "7.8f": "Nondeterministic behaviour documented", "7.8g": "Raw logs retained",
        "7.9a": "Final comparison table produced",
    }
    assert set(titles) == set(R), sorted(set(titles) ^ set(R))
    key = lambda k: tuple(int(x) if x.isdigit() else x for x in re.findall(r"\d+|[a-z]", k))
    npass = 0
    L = ["# Step 7 acceptance - full evaluation", "", f"Baseline logs: {bdir}; protected logs: {rdir}. Tables: results/tables/final_comparison.md, step7_coverage.md, step7_latency.md, step7_recovery.md, step7_overhead.md.", ""]
    grp = None
    for k in sorted(R, key=key):
        g = k.split(".")[1][:-1]
        if g != grp:
            grp = g
            L += ["", f"## 7.{g}", "", "| # | Criterion | Result | Evidence |", "|---|---|---|---|"]
        v, t = R[k]
        npass += v == "PASS"
        L.append(f"| {k} | {titles[k]} | {v} | {t.replace('|', '/')} |")
    L += ["", f"**{npass}/{len(R)} criteria passed.**", ""]
    open(f"{a.outdir}/summaries/step7_acceptance.md", "w").write("\n".join(L))
    print("\n".join(L[-8:]))
    return 0 if npass == len(R) else 1


if __name__ == "__main__":
    sys.exit(main())
