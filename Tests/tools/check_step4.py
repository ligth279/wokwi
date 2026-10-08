#!/usr/bin/env python3
"""Step 4 acceptance checker: actual fault effects (baseline build).

Reads the raw serial logs and wokwi-cli console output of the Step 4 runs
(Tests/run_step4.sh) and evaluates the 12 criteria groups of the Step 4
specification (4.1 ... 4.12). Nothing is assumed: every verdict is computed
from the logs. Writes the acceptance report, a per-fault effects table and
an injections CSV.

usage: check_step4.py --dir results/raw/step4/<ts> [--report ...] [--table ...] [--csv ...]
"""
import argparse
import csv
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(__file__))
import step4_cases as C  # noqa: E402

TAG_SPLIT = re.compile(r"(?=\[[A-Z][A-Z0-9_]*\] )")
CRASH_TEXT = "code 1006"


def kv(s):
    d = {}
    for tok in s.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            d[k] = v
    return d


class Run:
    """One simulation: serial records in order + wokwi-cli console facts."""

    def __init__(self, scen, idx, dirn):
        self.scen, self.idx = scen, idx
        base = f"{dirn}/{scen}_run{idx}"
        self.present = os.path.exists(base + ".log") and os.path.exists(base + ".console.txt")
        self.recs, self.console, self.exit = [], "", None
        if not self.present:
            return
        text = open(base + ".log", errors="replace").read().replace("\r", "")
        for part in TAG_SPLIT.split(text):
            m = re.match(r"\[([A-Z][A-Z0-9_]*)\] (.*)", part, re.S)
            if m:
                rest = m.group(2).strip()
                self.recs.append({"tag": m.group(1), "rest": rest, "kv": kv(rest), "i": len(self.recs)})
        self.console = open(base + ".console.txt", errors="replace").read()
        m = re.search(r"wokwi exit=(\d+)", self.console)
        self.exit = int(m.group(1)) if m else None

    def by_tag(self, tag):
        return [r for r in self.recs if r["tag"] == tag]

    @property
    def crashed(self):
        return CRASH_TEXT in self.console

    @property
    def completed_ok(self):
        return "Scenario completed successfully" in self.console and not self.crashed

    def fault_ev(self, exp, state=None):
        out = [r for r in self.by_tag("FAULT") if r["kv"].get("EXP") == exp]
        return [r for r in out if state is None or r["kv"].get("state") == state]

    def injected(self, fid):
        e = self.fault_ev(f"{fid}_001", "INJECTED")
        return e[0] if e else None

    def sensors(self):
        return [r for r in self.by_tag("SENSOR") if "seq" in r["kv"]]

    def controls(self):
        return [r for r in self.by_tag("CONTROL") if "seq" in r["kv"]]

    def statuses(self):
        return [r for r in self.by_tag("STATUS") if "console_hb" in r["kv"]]

    def segment(self, fid):
        """Records from the INJECTED line of fid to the next experiment's SELECTED line (or end)."""
        inj = self.injected(fid)
        if inj is None:
            return []
        end = len(self.recs)
        for r in self.by_tag("FAULT"):
            if r["i"] > inj["i"] and r["kv"].get("state") == "SELECTED":
                end = r["i"]
                break
        return self.recs[inj["i"]:end]


def hexint(v):
    return int(v, 16) if v is not None else None


def popcount(x):
    return bin(x).count("1")


def catalog_targets(root="."):
    """fault id -> (target string, has real inject) from the firmware catalog source."""
    src = open(f"{root}/FaultInjection/Src/fault_catalog.c").read()
    out = {}
    for m in re.finditer(r'STUDY\("([A-Z0-9-]+)",\s*[A-Z_]+,\s*"[a-z0-9_]+",\s*"([^"]+)",\s*(FS_[A-Z0-9_]+_INJECT)', src):
        out[m.group(1)] = (m.group(2), m.group(3))
    return out


def function_asm(elf, fn):
    try:
        out = subprocess.run(["arm-none-eabi-objdump", "-d", elf, f"--disassemble={fn}"], capture_output=True,
                             text=True, timeout=30).stdout
        return out
    except Exception:
        return ""


def ev(ok, text):
    return (bool(ok), text)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True)
    ap.add_argument("--report", default="results/summaries/step4_acceptance.md")
    ap.add_argument("--table", default="results/tables/step4_fault_effects.md")
    ap.add_argument("--csv", default="results/summaries/step4_injections.csv")
    ap.add_argument("--elf", default="build/baseline/firmware.elf")
    ap.add_argument("--root", default=".")
    a = ap.parse_args()

    runs = {s: [Run(s, i, a.dir) for i in range(1, C.REPEATS + 1)] for s in C.SCENARIOS}
    cat = catalog_targets(a.root)
    R = {}  # criterion id -> (ok, evidence)

    def all_runs(fid):
        return runs[C.scenario_of(fid)]

    def per_run(fid, fn):
        """fn(run) -> (ok, text); returns combined (ok over all 3 runs, text of run1 + count)."""
        res = [fn(r) if r.present else (False, "run missing") for r in all_runs(fid)]
        n = sum(1 for ok, _ in res if ok)
        return ev(n == C.REPEATS, f"runs passing {n}/{C.REPEATS}; run1: {res[0][1]}")

    # ------------------------------------------------------------------ 4.1
    study = C.STUDY_IDS
    # a: all 9 faults have real injection implementation
    real = {f: f in cat and cat[f][1].startswith("FS_") for f in study}
    injected_all = all(all(r.present and r.injected(f) for r in all_runs(f)) for f in study)
    R["4.1a"] = ev(all(real.values()) and injected_all,
                   f"catalog entries with FS_*_INJECT: {sum(real.values())}/9; INJECTED event in all {C.REPEATS} runs of each fault: {injected_all}")
    # b: FAULT <ID> invokes the right implementation (accepted -> INJECTED with the catalog target)
    def b_fn(f):
        def fn(r):
            acc = [x for x in r.by_tag("CMD") if x["kv"].get("result") == "accepted" and x["kv"].get("id") == f]
            inj = r.injected(f)
            tgt = inj["kv"].get("target") if inj else None
            exp_t = cat.get(f, (None,))[0]
            return ev(len(acc) == 1 and tgt == exp_t and inj["kv"].get("mech") == "UART",
                      f"accepted={len(acc)} INJECTED target={tgt} (catalog: {exp_t})")
        return fn
    res = {f: per_run(f, b_fn(f)) for f in study}
    R["4.1b"] = ev(all(v[0] for v in res.values()), "; ".join(f"{f}:{'ok' if v[0] else 'FAIL'}" for f, v in res.items()))
    # c: none left not_implemented
    ni = sum(1 for rs in runs.values() for r in rs if any("not_implemented" in x["rest"] for x in r.recs))
    R["4.1c"] = ev(ni == 0 and all(real.values()), f"runs containing reason=not_implemented: {ni}; catalog entries without a routine: {[f for f in study if not real[f]]}")
    # d: deterministic injection condition: UART -> next control cycle; identical cycle across runs
    det = {}
    for f in study:
        cycles, gaps = [], []
        for r in all_runs(f):
            inj, sel = r.injected(f), (r.fault_ev(f"{f}_001", "SELECTED") or [None])[0]
            if inj and sel:
                cycles.append(inj["kv"]["cycle"])
                gaps.append(int(inj["kv"]["t_ms"]) - int(sel["kv"]["t_ms"]))
        det[f] = (len(cycles) == C.REPEATS and len(set(cycles)) == 1 and all(0 <= g <= 100 for g in gaps), cycles[:1], gaps)
    R["4.1d"] = ev(all(v[0] for v in det.values()),
                   "injection = next control cycle (<=100 ms after the command) with the same DWT cycle in 3/3 runs: " +
                   "; ".join(f"{f}:{v[1][0] if v[1] else '?'}" for f, v in det.items()))

    # ------------------------------------------------------------------ nominal/model helpers
    def classify_control(c, setpoint=2200, kp=15):
        v, i = int(c["kv"]["value"]), int(c["kv"]["input"])
        return v == C.control(i, 2200, 15), v == C.control(i, setpoint, kp)

    def sensor_clean_before(run, inj):
        """Every SENSOR before the injection is OK, in the sensor's 22-28 C band, contiguous, sample==seq."""
        ss = [s for s in run.sensors() if s["i"] < inj["i"]]
        ok = bool(ss) and all(s["kv"]["status"] == "OK" and 2200 <= int(s["kv"]["value"]) <= 2800 and
                              s["kv"]["sample"] == s["kv"]["seq"] for s in ss)
        seqs = [int(s["kv"]["seq"]) for s in ss]
        return ok and seqs == list(range(1, len(seqs) + 1)), len(ss)

    # ------------------------------------------------------------------ 4.2 MEM-01
    def mem01(r):
        inj = r.injected("MEM-01")
        b, af = hexint(inj["kv"]["before"]), hexint(inj["kv"]["after"])
        seg = r.segment("MEM-01")
        ctl = [c for c in r.controls() if c["i"] > inj["i"]]
        pre = [c for c in r.controls() if c["i"] < inj["i"]]
        pre_nom = all(classify_control(c)[0] for c in pre)
        hits = [c for c in ctl if c["i"] < (seg[-1]["i"] + 1) and classify_control(c, af, 15)[1] and not classify_control(c)[0]]
        comp = r.fault_ev("MEM-01_001", "COMPLETED")
        err = r.fault_ev("MEM-01_001", "ERROR")
        return dict(b=b, af=af, hits=hits, pre_nom=pre_nom, comp=comp, err=err, inj=inj, seg=seg)
    m = [mem01(r) if r.injected("MEM-01") else None for r in runs["group"]]
    g = lambda f: all(x and f(x) for x in m)
    R["4.2a"] = ev(g(lambda x: popcount(x["b"] ^ x["af"]) == 1 and x["b"] ^ x["af"] == 1 << 10 and x["inj"]["kv"]["target"] == "g_config.setpoint_centi"),
                   f"run1: g_config.setpoint_centi 0x{m[0]['b']:X} -> 0x{m[0]['af']:X}, one bit (bit 10) flipped" if m[0] else "missing")
    R["4.2b"] = ev(g(lambda x: x["inj"]["kv"].get("before") and x["inj"]["kv"].get("after")), f"run1: INJECTED before={m[0]['inj']['kv']['before']} after={m[0]['inj']['kv']['after']}" if m[0] else "missing")
    stat_before_next = []
    for r in runs["group"]:
        inj = r.injected("MEM-01")
        nxt = [s for s in r.statuses() if inj and s["i"] > inj["i"]]
        stat_before_next.append(nxt[0]["kv"]["faults_injected"] if nxt else None)
    def n_inj(run, fid):
        return len(run.fault_ev(f"{fid}_001", "INJECTED"))
    R["4.2c"] = ev(all(n_inj(r, "MEM-01") == 1 for r in runs["group"]) and g(lambda x: x["comp"] and x["comp"][0]["kv"].get("inject_count") == "1") and
                   all(v == "2" for v in stat_before_next),
                   f"one INJECTED per run, COMPLETED inject_count=1; STATUS faults_injected after it: {stat_before_next} (FI-TEST + MEM-01 = 2)")
    R["4.2d"] = ev(g(lambda x: x["pre_nom"] and len(x["hits"]) >= 1),
                   (f"run1: CONTROL nominal before injection={m[0]['pre_nom']}; {len(m[0]['hits'])} CONTROL records after it match output with setpoint "
                    f"{m[0]['af']} and differ from nominal, e.g. {m[0]['hits'][0]['rest']}") if m[0] and m[0]["hits"] else "no corrupted CONTROL record")
    back = []
    for r, x in zip(runs["group"], m):
        comp = x["comp"][0] if x and x["comp"] else None
        after = [c for c in r.controls() if comp and c["i"] > comp["i"] and c["i"] < x["seg"][-1]["i"] + 1]
        back.append(bool(after) and all(classify_control(c)[0] for c in after))
    R["4.2e"] = ev(g(lambda x: x["comp"] and not x["err"]) and all(back),
                   f"COMPLETED without ERROR in 3/3 runs; CONTROL back to nominal after the harness cleanup: {back}")

    # ------------------------------------------------------------------ crash/hang faults: shared helpers
    def after_inj_lines(r, fid, tags=("SENSOR", "CONTROL", "STATUS")):
        inj = r.injected(fid)
        return [x for x in r.recs if x["i"] > inj["i"] and x["tag"] in tags]

    def boot_ok(r):
        return len(r.by_tag("BOOT")) == 1 and any("system_ready" in x["rest"] for x in r.by_tag("APP")) and \
            any("persist=cold" in x["rest"] for x in r.by_tag("FAULT"))

    def crash_run(fid, bit):
        def fn(r):
            inj = r.injected(fid)
            kvs = inj["kv"]
            b, af = hexint(kvs["before"]), hexint(kvs["after"])
            exp_after = {"MEM-02": b ^ (1 << 29), "CPU-02": b ^ (1 << 28), "CPU-01": (b ^ (1 << 29)) | 1}[fid]
            tail = [x for x in r.recs if x["i"] > inj["i"] and x["tag"] in ("SENSOR", "CONTROL", "STATUS", "CMD", "FAULT")]
            return dict(ok_val=af == exp_after, b=b, af=af, crashed=r.crashed and r.exit not in (0, None),
                        tail=tail, sens_clean=sensor_clean_before(r, inj)[0], boot=boot_ok(r), inj=inj,
                        n_inj=n_inj(r, fid), others=[x for x in r.by_tag("FAULT") if not x["kv"].get("EXP", "").startswith(fid)
                                                      and "framework" not in x["rest"]])
        return fn

    # 4.3 MEM-02
    mm = [crash_run("MEM-02", 29)(r) for r in runs["mem02"]]
    R["4.3a"] = ev(all(x["ok_val"] and x["inj"]["kv"]["target"] == "sensor_task_saved_lr" and x["b"] & 1 and x["b"] >> 24 == 0x08 for x in mm),
                   f"run1: sensor task saved LR (stack word) 0x{mm[0]['b']:08X} -> 0x{mm[0]['af']:08X}: bit 29 flipped; before is a flash code address with Thumb bit")
    R["4.3b"] = ev(all(x["inj"]["kv"]["mech"] == "UART" and x["sens_clean"] for x in mm) and det["MEM-02"][0],
                   f"injected from the control cycle after the command (ARMED site=control_cycle), sensor stream clean before it, same cycle in 3/3 runs ({det['MEM-02'][1]})")
    R["4.3c"] = ev(all(x["crashed"] and not [t for t in x["tail"] if t["tag"] in ("STATUS",)] for x in mm),
                   f"observed behaviour 3/3: after INJECTED the simulation terminates ('{CRASH_TEXT}', wokwi-cli exit {[r.exit for r in runs['mem02']]}); "
                   f"records after INJECTED: run1={[t['tag']+' '+t['rest'][:30] for t in mm[0]['tail']][:3]}; no STATUS line after it")
    R["4.3d"] = ev(all(x["n_inj"] == 1 and not x["others"] for x in mm),
                   f"exactly one INJECTED event per run; no other fault events in the log (second fault: none)")

    # 4.4 CPU-01
    cc = [crash_run("CPU-01", 29)(r) for r in runs["cpu01"]]
    asm1 = function_asm(a.elf, "fs_cpu01_inject")
    bx_found = re.search(r"\bbx\s+r\d", asm1) is not None
    R["4.4a"] = ev(all(x["crashed"] for x in cc) and bx_found and all(x["inj"]["kv"]["cycle"] for x in cc),
                   f"fs_cpu01_inject loads the corrupted target into the PC with `bx rN` (disassembly of the ELF: {'bx found' if bx_found else 'NOT found'}); "
                   f"INJECTED is logged before it and the simulation then terminates ({CRASH_TEXT}) 3/3")
    R["4.4b"] = ev(all(x["ok_val"] and x["b"] >> 24 == 0x08 for x in cc),
                   f"run1: PC sampled in the injector 0x{cc[0]['b']:08X}; corrupted target 0x{cc[0]['af']:08X} = (PC ^ 2^29) OR 1. "
                   "Fault registers (CFSR/HFSR) could not be captured: Wokwi ends the simulation before any exception is delivered (see docs/SIMULATOR_LIMITATIONS.md)")
    R["4.4c"] = ev(all(x["crashed"] and not x["tail"] for x in cc),
                   f"observed 3/3: no further serial output after INJECTED and wokwi-cli reports '{CRASH_TEXT}' (exit {[r.exit for r in runs['cpu01']]})")
    R["4.4d"] = ev(all(x["inj"]["kv"]["EXP"] == "CPU-01_001" for x in cc) and
                   all(any(c["kv"].get("id") == "CPU-01" for c in r.by_tag("CMD")) for r in runs["cpu01"]),
                   "FAULT command accepted with id=CPU-01 and EXP=CPU-01_001 on every INJECTED line")
    R["4.4e"] = ev(all(x["boot"] for x in cc) and all(x["boot"] for x in mm),
                   "each run is a fresh simulation: BOOT, framework persist=cold, system_ready in every CPU-01 and MEM-02 run (the crashed state is not carried over; there is no in-simulation reset in the baseline)")

    # 4.5 CPU-02
    cs = [crash_run("CPU-02", 28)(r) for r in runs["cpu02"]]
    asm2 = function_asm(a.elf, "fs_cpu02_inject")
    sp_found = re.search(r"mov\s+sp,\s*r\d", asm2) is not None
    R["4.5a"] = ev(all(x["crashed"] for x in cs) and sp_found,
                   f"fs_cpu02_inject loads the corrupted value into SP with `mov sp, rN` (disassembly of the ELF: {'found' if sp_found else 'NOT found'}); simulation terminates 3/3")
    R["4.5b"] = ev(all(x["ok_val"] and (x["b"] >> 28) == 2 for x in cs),
                   f"run1: SP before 0x{cs[0]['b']:08X} (inside the 20 KB SRAM) -> 0x{cs[0]['af']:08X} (bit 28 flipped, outside SRAM)")
    R["4.5c"] = ev(all(x["crashed"] and not x["tail"] for x in cs),
                   f"observed 3/3: no further serial output after INJECTED, '{CRASH_TEXT}' (exit {[r.exit for r in runs['cpu02']]}); no HardFault/CFSR could be logged")
    R["4.5d"] = ev(all(x["inj"]["kv"]["EXP"] == "CPU-02_001" for x in cs) and all(x["n_inj"] == 1 for x in cs),
                   "INJECTED logged once per run with EXP=CPU-02_001")
    R["4.5e"] = ev(all(x["boot"] for x in cs), "each run is a fresh simulation with BOOT/persist=cold/system_ready")

    # 4.6 TIM-01
    def tim01(r):
        inj = r.injected("TIM-01")
        tail = after_inj_lines(r, "TIM-01", ("SENSOR", "CONTROL", "STATUS", "CMD", "FAULT", "TASK", "BOOT", "RESET"))
        pre_ctl = [c for c in r.controls() if c["i"] < inj["i"]]
        return dict(inj=inj, tail=tail, completed=r.completed_ok, exit=r.exit, n=len(pre_ctl), boots=len(r.by_tag("BOOT")),
                    b=inj["kv"]["before"], clean=sensor_clean_before(r, inj)[0], boot=boot_ok(r),
                    wd=[x for x in r.recs if "WWDG" in x["rest"] and x["tag"] != "BOOT"])
    t1 = [tim01(r) for r in runs["tim01"]]
    R["4.6a"] = ev(all(x["inj"]["kv"]["target"] == "control_task" for x in t1) and re.search(r"b\.n\s+\S+\s+<fs_tim01_inject>|e7fe", function_asm(a.elf, "fs_tim01_inject")) is not None,
                   "fs_tim01_inject compiles to a branch-to-self (`b .`) executed in the control task context (target=control_task)")
    R["4.6b"] = ev(all(not x["tail"] for x in t1),
                   f"after INJECTED no SENSOR/CONTROL/STATUS/CMD line follows in 3/3 runs (control cycles before injection: {t1[0]['n']}; control_hb at injection {int(t1[0]['b'], 16)})")
    R["4.6c"] = ev(all(x["completed"] and x["exit"] == 0 for x in t1) and all(x["crashed"] for x in mm),
                   f"hang: wokwi-cli exit {[x['exit'] for x in t1]}, 'Scenario completed successfully' (the simulator ran 3000 ms of simulated time past the injection with a silent CPU); "
                   f"crash faults in the same campaign end with '{CRASH_TEXT}' - the two are told apart by simulator status")
    R["4.6d"] = ev(all(x["inj"] is not None and x["inj"]["i"] == len(runs["tim01"][i].recs) - 1 for i, x in enumerate(t1)),
                   "the INJECTED record (target, before/after, cycle) is the last record in every run: it was printed by the injecting task before it entered the loop, and nothing could be printed afterwards")
    R["4.6e"] = ev(all(x["boot"] for x in t1), "each run is a fresh simulation with BOOT/persist=cold/system_ready (restart works; the hang is not carried over)")
    R["4.6f"] = ev(all(x["boots"] == 1 and not x["wd"] for x in t1) and all(x["completed"] for x in t1),
                   "the hang persists for the whole 3000 ms window with no reset and no watchdog (BOOT count 1); a WWDG test can be run on the same fault later")

    # 4.7 TIM-02
    def tim02(r):
        inj = r.injected("TIM-02")
        blk = [x for x in r.by_tag("TASK") if x["kv"].get("state") == "blocked"]
        st = [s for s in r.statuses() if s["i"] > (blk[0]["i"] if blk else 10**9)]
        sens_after = [s for s in r.sensors() if blk and s["i"] > blk[0]["i"]]
        ctl_after = [c for c in r.controls() if blk and c["i"] > blk[0]["i"]]
        comp = r.fault_ev("TIM-02_001", "COMPLETED")
        return dict(inj=inj, blk=blk, st=st, sens_after=sens_after, ctl_after=ctl_after, comp=comp, boot=boot_ok(r),
                    sh=[s["kv"]["sensor_hb"] for s in st], ch=[s["kv"]["control_hb"] for s in st],
                    co=[int(s["kv"]["console_hb"]) for s in st], clean=sensor_clean_before(r, inj)[0], ok=r.completed_ok)
    t2 = [tim02(r) for r in runs["tim02"]]
    R["4.7a"] = ev(all(len(x["blk"]) == 1 and x["blk"][0]["kv"].get("name") == "sensor" and x["blk"][0]["kv"].get("reason") == "fault_injection" for x in t2),
                   f"run1: {t2[0]['blk'][0]['rest']}")
    R["4.7b"] = ev(all(len(x["st"]) >= 3 and len(set(x["sh"])) == 1 and not x["sens_after"] for x in t2),
                   f"sensor_hb frozen at {t2[0]['sh'][0]} over {len(t2[0]['st'])} STATUS lines after the block; no SENSOR record after it (3/3 runs)")
    R["4.7c"] = ev(all(len(set(x["ch"])) == 1 and not x["ctl_after"] and all(b > a for a, b in zip(x["co"], x["co"][1:])) and x["ok"] for x in t2),
                   f"console keeps running (console_hb {t2[0]['co'][0]} -> {t2[0]['co'][-1]}, STATUS every second); control, fed only by the sensor queue, stops at control_hb={t2[0]['ch'][0]} "
                   "(expected scheduler behaviour: blocked producer -> idle consumer); simulation runs to completion")
    R["4.7d"] = ev(all(x["inj"]["kv"]["target"] == "sensor_task" and x["blk"][0]["kv"]["name"] == "sensor" for x in t2), "INJECTED target=sensor_task and [TASK] name=sensor state=blocked in every run")
    R["4.7e"] = ev(all(x["boot"] for x in t2), "each run is a fresh simulation with BOOT/persist=cold/system_ready")
    R["4.7f"] = ev(all(x["comp"] and len(set(x["sh"])) == 1 for x in t2),
                   "the per-task heartbeat counters (sensor_hb frozen, console_hb advancing) separate the stalled task from the healthy one, which is what a later heartbeat monitor needs; "
                   "COMPLETED reached by the console-side observer in 3/3 runs; the fault stays active (no cleanup)")

    # 4.8 DATA-01
    def data01(r):
        inj = r.injected("DATA-01")
        seg = r.segment("DATA-01")
        bad = [c for c in r.controls() if c["i"] > inj["i"] and c["i"] < seg[-1]["i"] + 1 and c["kv"]["input"] != "" and
               int(c["kv"]["input"]) != next((int(s["kv"]["value"]) for s in r.sensors() if s["kv"]["seq"] == c["kv"]["seq"]), -1)]
        orig = None
        if bad:
            orig = next(int(s["kv"]["value"]) for s in r.sensors() if s["kv"]["seq"] == bad[0]["kv"]["seq"])
        return dict(inj=inj, bad=bad, orig=orig, b=int(inj["kv"]["before"], 16), af=int(inj["kv"]["after"], 16),
                    comp=r.fault_ev("DATA-01_001", "COMPLETED"), clean=sensor_clean_before(r, inj)[0])
    d1 = [data01(r) for r in runs["group"]]
    R["4.8a"] = ev(all(x["af"] == 8500 and not 2200 <= x["af"] <= 2800 for x in d1),
                   f"a sample of 8500 centi-C is injected; the sensor only produces 2200..2800 (run1: after=0x{d1[0]['af']:X})")
    R["4.8b"] = ev(all(len(x["bad"]) == 1 and x["b"] == x["orig"] for x in d1),
                   f"run1: INJECTED before={d1[0]['b']} equals the sensor's own reading of that sample (SENSOR value={d1[0]['orig']})")
    R["4.8c"] = ev(all(x["inj"]["kv"]["after"] == "0x00002134" for x in d1), "INJECTED after=0x00002134 (8500) in 3/3 runs")
    R["4.8d"] = ev(all(len(x["bad"]) == 1 and x["bad"][0]["kv"]["input"] == "8500" and int(x["bad"][0]["kv"]["value"]) == C.control(8500) for x in d1),
                   f"run1: {d1[0]['bad'][0]['rest']} - CONTROL input is 8500, not the SENSOR value {d1[0]['orig']}")
    R["4.8e"] = ev(all(len(x["bad"]) == 1 and int(x["bad"][0]["kv"]["value"]) != C.control(x["orig"]) for x in d1),
                   f"run1: output {d1[0]['bad'][0]['kv']['value']} vs {C.control(d1[0]['orig'])} it would have been with the real reading")
    R["4.8f"] = ev(all(len(x["bad"]) == 1 for x in d1) and all(n_inj(r, "DATA-01") == 1 and x["comp"][0]["kv"]["inject_count"] == "1" for r, x in zip(runs["group"], d1)),
                   "exactly one CONTROL record per run consumed a corrupted input; one INJECTED, COMPLETED inject_count=1")

    # 4.9 DATA-02
    def data02(r):
        inj = r.injected("DATA-02")
        seg = r.segment("DATA-02")
        hits = [c for c in r.controls() if c["i"] > inj["i"] and c["i"] < seg[-1]["i"] + 1 and classify_control(c, 2200, 100)[1] and not classify_control(c)[0]]
        pre = [c for c in r.controls() if c["i"] < inj["i"] and c["i"] > (r.injected("DATA-01")["i"] if r.injected("DATA-01") else 0)]
        return dict(inj=inj, hits=hits, b=int(inj["kv"]["before"], 16), af=int(inj["kv"]["after"], 16), comp=r.fault_ev("DATA-02_001", "COMPLETED"),
                    err=r.fault_ev("DATA-02_001", "ERROR"))
    d2 = [data02(r) for r in runs["group"]]
    R["4.9a"] = ev(all(x["inj"]["kv"]["target"] == "g_config.kp_pct_per_c" for x in d2), "target = g_config.kp_pct_per_c (proportional gain of the control law)")
    R["4.9b"] = ev(all(x["b"] == 15 and x["af"] == 100 for x in d2), f"run1: original {d2[0]['b']} -> corrupted {d2[0]['af']}")
    R["4.9c"] = ev(all(len(x["hits"]) >= 1 for x in d2),
                   f"run1: {len(d2[0]['hits'])} CONTROL records follow the law with kp=100 and differ from the nominal output, e.g. {d2[0]['hits'][0]['rest']}")
    R["4.9d"] = ev(all(x["comp"] and not x["err"] for x in d2), "OBSERVED/COMPLETED by comparing the last control output with the nominal one, 3/3 runs (no ERROR)")
    R["4.9e"] = ev(all(n_inj(r, "DATA-02") == 1 and x["comp"][0]["kv"]["inject_count"] == "1" for r, x in zip(runs["group"], d2)), "exactly one INJECTED and inject_count=1 per run")

    # 4.10 PERIPH-01
    def periph(r):
        inj = r.injected("PERIPH-01")
        after = [s for s in r.sensors() if s["i"] > inj["i"]]
        bad = [s for s in after if s["kv"]["status"] != "OK"]
        stat = [s for s in r.statuses() if s["i"] > inj["i"]]
        stat_pre = [s for s in r.statuses() if s["i"] < inj["i"]]
        held = r.console.count("SDA held LOW")
        return dict(inj=inj, after=after, bad=bad, stat=stat, held=held, comp=r.fault_ev("PERIPH-01_001", "COMPLETED"),
                    err0=stat_pre[-1]["kv"]["sensor_err"] if stat_pre else None, errN=stat[-1]["kv"]["sensor_err"] if stat else None,
                    ok_pre=sensor_clean_before(r, inj)[0], first_bad=bad[0] if bad else None)
    p = [periph(r) for r in runs["group"]]
    R["4.10a"] = ev(all(x["inj"]["kv"]["target"] == "i2c_sda" and x["inj"]["kv"]["before"] == "0x00000000" and x["inj"]["kv"]["after"] == "0x00000001" and
                        any(c["kv"].get("id") == "PERIPH-01" for c in r.by_tag("CMD")) for r, x in zip(runs["group"], p)),
                    "`FAULT PERIPH-01` -> EXP=PERIPH-01_001 INJECTED: trigger line to the i2c-stuck chip 0 -> 1")
    R["4.10b"] = ev(all(x["held"] == 1 for x in p), f"wokwi-cli prints the chip's own '[i2c-stuck] SDA held LOW' once per run ({[x['held'] for x in p]})")
    R["4.10c"] = ev(all(len(x["bad"]) >= 3 for x in p), f"run1: {len(p[0]['bad'])} of {len(p[0]['after'])} sensor transactions after the injection fail, first: {p[0]['first_bad']['rest'][:60]}")
    R["4.10d"] = ev(all(x["comp"] and x["first_bad"] and x["errN"] and int(x["errN"]) > int(x["err0"] or 0) for x in p),
                    f"sensor status != OK and STATUS sensor_err {p[0]['err0']} -> {p[0]['errN']}; OBSERVED/COMPLETED by the firmware 3/3")

    # Step 1 evidence for recovery behaviour (existing 9-clock recovery)
    i2c_log = ""
    reg = {}
    regf = f"{a.dir}/regression.txt"
    if os.path.exists(regf):
        for line in open(regf):
            for k, v in re.findall(r"(\w+)=(\S+)", line):
                reg[k] = v
    def i2c_id(name):
        if not reg.get("i2c_log") or not os.path.exists(reg["i2c_log"]):
            return None
        t = open(reg["i2c_log"], errors="replace").read().replace("\r", "")
        m = re.search(r"id=%s result=(\w+) ([^\[]*)" % name, t)
        return (m.group(1), m.group(2).strip()) if m else None
    rec, perm, clr = i2c_id("I2C-RECOVER"), i2c_id("I2C-PERM-FAIL"), i2c_id("I2C-PERM-CLEAR")
    R["4.10e"] = ev(rec and rec[0] == "PASS", f"Step 1 test I2C-RECOVER (transient stuck-low, 9 SCL clocks): {rec}")
    R["4.10f"] = ev(perm and perm[0] == "PASS" and all(len(x["bad"]) >= 3 and x["bad"][-1]["i"] > x["bad"][0]["i"] for x in p) and
                    all(all(s["kv"]["status"] != "OK" for s in x["after"][1:]) for x in p),
                    f"Step 1 test I2C-PERM-FAIL (recovery cannot clear a held fault): {perm}; Step 4: after the injection every later sensor transaction fails until the end of the run (permanent)")
    R["4.10g"] = ev(clr and clr[0] == "PASS", f"Step 1 test I2C-PERM-CLEAR (recovery after the fault is released): {clr}")

    # ------------------------------------------------------------------ 4.11 regression
    def tail_pass(path):
        if not path or not os.path.exists(path):
            return None
        t = open(path).read()
        m = re.search(r"\*\*(\d+)/(\d+) criteria passed", t)
        return (int(m.group(1)), int(m.group(2)), t) if m else None
    s2 = tail_pass(reg.get("step2_summary"))
    s3 = tail_pass(reg.get("step3_summary"))
    row = lambda t, n: re.search(r"\| %s \| [^|]*\| (\w+) \|" % n, t[2]) if t else None
    r0, r1 = row(s2, 15), row(s2, 16)
    R["4.11a"] = ev(s2 and r0 and r0.group(1) == "PASS", f"Step 0 smoke (re-run by Step 2 suite, criterion 15): {r0.group(1) if r0 else 'no data'}")
    R["4.11b"] = ev(s2 and r1 and r1.group(1) == "PASS", f"Step 1 i2ctest (criterion 16): {r1.group(1) if r1 else 'no data'}")
    R["4.11c"] = ev(s2 and s2[0] == s2[1] and reg.get("step2_exit") == "0", f"Step 2 suite on the build that contains the real faults: {s2[0]}/{s2[1]} (exit {reg.get('step2_exit')})" if s2 else "no data")
    R["4.11d"] = ev(s3 and s3[0] == s3[1] and reg.get("step3_exit") == "0", f"Step 3 suite (fwtest build = same framework, study faults unimplemented): {s3[0]}/{s3[1]} (exit {reg.get('step3_exit')})" if s3 else "no data")
    unit_ok = reg.get("unit_exit") == "0"
    ft = [r.fault_ev("FI-TEST_001", "COMPLETED") for r in runs["group"]]
    R["4.11e"] = ev(unit_ok and all(ft), f"host unit tests exit {reg.get('unit_exit')} (parser, framework, study wiring, Step 3 checker); "
                                         f"FI-TEST_001 still completes through the real UART path in the study build: {[bool(x) for x in ft]}")

    # ------------------------------------------------------------------ 4.12 reproducibility
    def sig(fid):
        out = []
        for r in all_runs(fid):
            if not r.present or not r.injected(fid):
                out.append(None)
                continue
            inj = r.injected(fid)
            seg = r.segment(fid)
            tail = tuple((x["tag"], x["rest"]) for x in seg if x["tag"] in ("SENSOR", "CONTROL", "TASK"))
            k = inj["kv"]
            out.append((k["target"], k["before"], k["after"], k["cycle"], k["t_ms"], tail, r.crashed, r.exit))
        return out
    sigs = {f: sig(f) for f in study}
    same = {f: len(s) == C.REPEATS and None not in s and len(set(s)) == 1 for f, s in sigs.items()}
    R["4.12a"] = ev(all(len(runs[s]) == C.REPEATS and all(r.present for r in runs[s]) for s in runs),
                    f"{C.REPEATS} runs of each of the {len(runs)} scenarios ({len(runs) * C.REPEATS} simulations), covering all 9 study faults")
    R["4.12b"] = ev(all(all(r.injected(f) and n_inj(r, f) == 1 for r in all_runs(f)) for f in study), "the intended fault (exactly one INJECTED) occurs in every run of every fault")
    R["4.12c"] = ev(all(same.values()), "identical observed behaviour in 3/3 runs (INJECTED fields, post-injection SENSOR/CONTROL/TASK records, crash flag, exit code): " + ", ".join(f"{f}={'same' if v else 'DIFF'}" for f, v in same.items()))
    R["4.12d"] = ev(all(det[f][0] for f in study), "injection DWT cycle identical in 3/3 runs for every fault: " + ", ".join(f"{f}={det[f][1][0] if det[f][1] else '?'}" for f in study))
    expected_crash = {"mem02", "cpu01", "cpu02"}
    unexplained = []
    for s, rs in runs.items():
        for r in rs:
            if s in expected_crash:
                if not (r.crashed and r.injected(C.SCENARIOS[s][0])):
                    unexplained.append(f"{s} run{r.idx}: expected crash after INJECTED")
            elif r.crashed or not r.completed_ok or r.exit != 0:
                unexplained.append(f"{s} run{r.idx}: exit={r.exit} crashed={r.crashed}")
            if r.present and (len(r.by_tag("BOOT")) != 1):
                unexplained.append(f"{s} run{r.idx}: unexpected BOOT count {len(r.by_tag('BOOT'))}")
    R["4.12e"] = ev(not unexplained, "simulator failures: only the expected '%s' termination after INJECTED for MEM-02/CPU-01/CPU-02 (their defined raw impact); %s" %
                    (CRASH_TEXT, "none other" if not unexplained else unexplained))

    # ------------------------------------------------------------------ report
    titles = {
        "4.1a": "All 9 study faults have a real injection implementation", "4.1b": "`FAULT <ID>` invokes the correct fault implementation",
        "4.1c": "No study fault remains `not implemented`", "4.1d": "Each fault has a deterministic injection condition",
        "4.2a": "MEM-01: a selected SRAM variable is corrupted by flipping a defined bit", "4.2b": "MEM-01: original and corrupted values are logged",
        "4.2c": "MEM-01: exactly one corruption per experiment", "4.2d": "MEM-01: the variable corruption is observable",
        "4.2e": "MEM-01: terminates cleanly (fault does not crash/hang the MCU)",
        "4.3a": "MEM-02: a defined stack location/data is deliberately corrupted", "4.3b": "MEM-02: corruption occurs at the intended injection point",
        "4.3c": "MEM-02: the resulting behaviour is recorded", "4.3d": "MEM-02: no accidental second fault is introduced",
        "4.4a": "CPU-01: PC corruption is actually performed, not simulated by an error print", "4.4b": "CPU-01: target PC/register state captured where possible",
        "4.4c": "CPU-01: control-flow/fault behaviour is observable", "4.4d": "CPU-01: logged with its experiment ID", "4.4e": "CPU-01: system can be restarted for later experiments",
        "4.5a": "CPU-02: SP corruption is actually performed", "4.5b": "CPU-02: target register state captured where possible",
        "4.5c": "CPU-02: fault/control-flow behaviour is observable", "4.5d": "CPU-02: logged", "4.5e": "CPU-02: system can be restarted",
        "4.6a": "TIM-01: the selected execution path enters an intentional infinite loop", "4.6b": "TIM-01: normal application progress stops",
        "4.6c": "TIM-01: distinguishable from an ordinary simulator failure", "4.6d": "TIM-01: injection event is logged before the hang",
        "4.6e": "TIM-01: firmware can be restarted after the experiment", "4.6f": "TIM-01: behaviour usable later for WWDG detection testing",
        "4.7a": "TIM-02: a specific FreeRTOS task is deliberately blocked", "4.7b": "TIM-02: the task stops performing its work",
        "4.7c": "TIM-02: other tasks behave according to the expected scheduler behaviour", "4.7d": "TIM-02: the affected task is identifiable in the log",
        "4.7e": "TIM-02: system can be restarted", "4.7f": "TIM-02: suitable for the later heartbeat/recovery tests",
        "4.8a": "DATA-01: a sensor value is deliberately corrupted", "4.8b": "DATA-01: original sensor value recorded", "4.8c": "DATA-01: injected value recorded",
        "4.8d": "DATA-01: control loop receives the corrupted value", "4.8e": "DATA-01: resulting behaviour observable", "4.8f": "DATA-01: exactly one corruption per experiment",
        "4.9a": "DATA-02: a defined configuration variable is corrupted", "4.9b": "DATA-02: original and corrupted values logged",
        "4.9c": "DATA-02: corrupted configuration reaches the application logic", "4.9d": "DATA-02: resulting behaviour observable", "4.9e": "DATA-02: exactly one corruption per experiment",
        "4.10a": "PERIPH-01: stuck-low fault is triggered through the Step 3 framework", "4.10b": "PERIPH-01: SDA is actually held LOW",
        "4.10c": "PERIPH-01: a real I2C transaction fails", "4.10d": "PERIPH-01: the bus failure is observable",
        "4.10e": "PERIPH-01: existing 9-clock recovery remains functional", "4.10f": "PERIPH-01: permanent-fault behaviour remains correct",
        "4.10g": "PERIPH-01: released-fault recovery remains correct",
        "4.11a": "Step 0 passes", "4.11b": "Step 1 passes", "4.11c": "Step 2 passes", "4.11d": "Step 3 framework passes",
        "4.11e": "Implementing the real faults does not break the existing framework",
        "4.12a": "Each fault is run at least 3 times", "4.12b": "The intended fault occurs in every run", "4.12c": "Observed behaviour is consistent",
        "4.12d": "Injection cycle counts are consistent where deterministic", "4.12e": "No unexplained simulator failures occur",
    }
    keys = list(titles)
    assert set(keys) == set(R), (set(keys) ^ set(R))

    def keyfn(k):
        m = re.match(r"4\.(\d+)([a-z])", k)
        return (int(m.group(1)), m.group(2))
    lines = ["# Step 4 acceptance - actual fault effects (baseline build)", "",
             f"Runs: {a.dir} ({len(runs)} scenarios x {C.REPEATS} simulations). No detection or recovery is enabled.", ""]
    npass = 0
    group = None
    for k in sorted(R, key=keyfn):
        g = keyfn(k)[0]
        if g != group:
            group = g
            lines += ["", f"## 4.{g}", "", "| # | Criterion | Result | Evidence |", "|---|---|---|---|"]
        ok, text = R[k]
        npass += ok
        lines.append(f"| {k} | {titles[k]} | {'PASS' if ok else 'FAIL'} | {text.replace('|', '/')} |")
    lines += ["", f"**{npass}/{len(R)} criteria passed.**", ""]
    os.makedirs(os.path.dirname(a.report), exist_ok=True)
    open(a.report, "w").write("\n".join(lines))
    print("\n".join(lines[-30:]))

    # ---- effects table + csv (baseline raw impact)
    symptom = {
        "MEM-01": ("wrong output (silent)", "setpoint 2200 -> 3224: the controller drives the output to its minimum 0 instead of the nominal value; no crash, no log message"),
        "MEM-02": ("crash", "sensor task resumes with a corrupted return address; simulation terminates (wokwi-cli: API Error code 1006), serial output stops"),
        "CPU-01": ("crash", "CPU continues at the unmapped corrupted address; simulation terminates (code 1006), serial output stops"),
        "CPU-02": ("crash", "stack accesses use the corrupted SP; simulation terminates (code 1006), serial output stops"),
        "TIM-01": ("hang", "control task spins; console (lower priority) is starved; serial output stops; simulator keeps running (exit 0)"),
        "TIM-02": ("partial stall", "sensor task blocked: sensor_hb and control_hb freeze, console keeps printing STATUS"),
        "DATA-01": ("wrong output (one cycle)", "one control cycle uses 85.00 C: output 100 instead of the real value's output; next cycle normal"),
        "DATA-02": ("wrong output (silent)", "kp 15 -> 100: output is 6.7x too large (clamped at 100) while the config stays corrupted until harness cleanup"),
        "PERIPH-01": ("peripheral failure", "I2C reads fail (BUS_ERROR/BUSY) from the first transaction on; sensor_err counts up; control keeps consuming the last good value; permanent"),
    }
    os.makedirs(os.path.dirname(a.table), exist_ok=True)
    T = ["# Step 4 - baseline fault effects (raw impact, no detection or recovery)", "",
         "Source: raw logs in `" + a.dir + "`; generated by `Tests/tools/check_step4.py`. UART-triggered injections, 3 runs each.", "",
         "| ID | Class | Fault | Target | Before | After | Inject cycle (DWT) | Status | Observed symptom | Runs identical |",
         "|---|---|---|---|---|---|---|---|---|---|"]
    with open(a.csv, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["id", "class", "fault", "target", "before", "after", "inject_cycle", "inject_t_ms", "status", "runs_identical", "run"])
        names = {"MEM-01": "SRAM variable bit flip", "MEM-02": "Stack corruption", "CPU-01": "PC corruption", "CPU-02": "SP corruption",
                 "TIM-01": "Infinite loop", "TIM-02": "Blocked task", "DATA-01": "Sensor data corruption",
                 "DATA-02": "Configuration corruption", "PERIPH-01": "I2C stuck-low"}
        for f in study:
            r0 = all_runs(f)[0]
            inj = r0.injected(f)
            if not inj:
                T.append(f"| {f} | {C.CLASS[f]} | {names[f]} | - | - | - | - | not injected | - | - |")
                continue
            k = inj["kv"]
            st, text = symptom[f]
            T.append(f"| {f} | {C.CLASS[f]} | {names[f]} | `{k['target']}` | {k['before']} | {k['after']} | {k['cycle']} | {st} | {text} | {'yes' if same[f] else 'NO'} |")
            for r in all_runs(f):
                ik = r.injected(f)["kv"]
                w.writerow([f, C.CLASS[f], names[f], ik["target"], ik["before"], ik["after"], ik["cycle"], ik["t_ms"], st, same[f], r.idx])
    T += ["", "Limits: see `docs/SIMULATOR_LIMITATIONS.md` section 11. The crash faults end the Wokwi simulation itself, so fault registers (CFSR/HFSR) and the CPU state after the fault are not observable in the baseline.", ""]
    open(a.table, "w").write("\n".join(T))
    return 0 if npass == len(R) else 1


if __name__ == "__main__":
    sys.exit(main())
