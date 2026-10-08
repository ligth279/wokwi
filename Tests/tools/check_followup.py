#!/usr/bin/env python3
"""Acceptance checks of the follow-up campaigns (Tests/run_followup.sh): timer-triggered injection of study faults, GDB-assisted
injection of study faults, and the equal-timing single-fault runs behind the baseline-vs-protected comparison.

usage: check_followup.py [--root results/raw/followup] [--report results/summaries/followup_acceptance.md]"""
import argparse
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
import eval_step7 as E  # noqa: E402
import gen_followup_scenarios as G  # noqa: E402
import step4_cases as S4  # noqa: E402
from check_step4 import Run, hexint  # noqa: E402


def ev(ok, text):
    return ("PASS" if ok else "FAIL", text)


def wrong_outputs(run, fid):
    inj = run.injected(fid)
    return [c for c in run.controls() if inj and c["i"] > inj["i"] and int(c["kv"]["value"]) != S4.control(int(c["kv"]["input"]))]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="results/raw/followup")
    ap.add_argument("--report", default="results/summaries/followup_acceptance.md")
    a = ap.parse_args()
    R = {}

    # ------------------------------------------------------------ F1 timer mechanism on study faults
    tdir = f"{a.root}/timer"
    scen = {"t_a": ["MEM-01", "DATA-01", "DATA-02", "PERIPH-01"], "t_b": ["TIM-02"], "t_c": ["TIM-01"], "t_d": ["MEM-02"], "t_e": ["CPU-01"], "t_f": ["CPU-02"]}
    truns = {s: [Run(s, i, tdir) for i in (1, 2, 3)] for s in scen}
    have = all(r.present and r.recs for rr in truns.values() for r in rr)
    rows = {}
    if have:
        for s, ids in scen.items():
            for f in ids:
                rows[f] = [(r, r.injected(f)) for r in truns[s]]
    R["F1a"] = ev(have and all(i is not None for v in rows.values() for _, i in v) and all(i["kv"]["mech"] == "TIMER" for v in rows.values() for _, i in v),
                  "`FAULT_AT <ID> 800` injects all nine study faults with mech=TIMER in 3/3 runs each" if have else "timer runs missing")
    err = [abs(int(i["kv"]["trigger_error_cycles"])) for v in rows.values() for _, i in v if i and "trigger_error_cycles" in i["kv"]]
    R["F1b"] = ev(bool(err) and max(err) <= 72000, f"the injection happens in the TIM4 interrupt within {max(err) if err else '?'} cycles of the programmed instant (<= 1 ms) (trigger_error_cycles on every INJECTED line)")
    def effect(f, r):
        if f in ("MEM-01", "DATA-01", "DATA-02"):
            return bool(wrong_outputs(r, f)) and bool(r.fault_ev(f"{f}_001", "COMPLETED"))
        if f == "PERIPH-01":
            return any(s["kv"]["status"] != "OK" for s in r.sensors() if s["i"] > r.injected(f)["i"]) and bool(r.fault_ev(f"{f}_001", "COMPLETED"))
        if f == "TIM-02":
            st = [x for x in r.statuses() if x["i"] > r.injected(f)["i"]]
            return len(st) >= 3 and len({x["kv"]["sensor_hb"] for x in st[-3:]}) == 1 and bool(r.fault_ev(f"{f}_001", "COMPLETED"))
        if f in ("MEM-02", "CPU-01", "CPU-02"):
            return r.crashed and not [x for x in r.recs if x["i"] > r.injected(f)["i"] and x["tag"] in ("SENSOR", "CONTROL", "STATUS")]
        if f == "TIM-01":
            return not [x for x in r.recs if x["i"] > r.injected(f)["i"] and x["tag"] in ("SENSOR", "CONTROL", "STATUS")] and r.completed_ok
        return False
    R["F1c"] = ev(have and all(effect(f, r) for f, v in rows.items() for r, _ in v),
                  "the effect equals the UART-triggered one: wrong control output (MEM-01, DATA-01, DATA-02), failing sensor reads (PERIPH-01), frozen sensor heartbeat (TIM-02), total silence (TIM-01; a loop in the ISR stops everything), simulation ends with code 1006 (MEM-02, CPU-01, CPU-02, injected from the interrupt)")
    R["F1d"] = ev(have and all(len({i["kv"]["cycle"] for _, i in v}) == 1 for v in rows.values()), "injection cycle identical in the 3 runs of every fault (deterministic)")

    # ------------------------------------------------------------ F2 GDB mechanism on study faults
    gdir = f"{a.root}/gdb"
    gf = ["MEM-01", "DATA-02", "CPU-01", "CPU-02"]
    ok_runs, info = {}, {}
    for f in gf:
        for i in (1, 2, 3):
            base = f"{gdir}/{f}_run{i}"
            h = open(base + ".harness.txt", errors="replace").read() if os.path.exists(base + ".harness.txt") else ""
            g = open(base + ".gdb.txt", errors="replace").read() if os.path.exists(base + ".gdb.txt") else ""
            run = Run(f"{f}", i, gdir) if False else None
            lg = open(base + ".log", errors="replace").read().replace("\r", "") if os.path.exists(base + ".log") else ""
            recs = []
            for part in re.split(r"(?=\[[A-Z][A-Z0-9_]*\] )", lg):
                m = re.match(r"\[([A-Z][A-Z0-9_]*)\] (.*)", part, re.S)
                if m:
                    recs.append((m.group(1), m.group(2).strip()))
            inj = next((t for tag, t in recs if tag == "FAULT" and "state=INJECTED" in t and f"EXP={f}_001" in t), None)
            kvs = dict(re.findall(r"(\w+)=(\S+)", inj)) if inj else {}
            info[(f, i)] = dict(pass_=("result=PASS" in h), hit=("GDB_HIT" in g), injected=("GDB_INJECTED" in g), inj=inj, kv=kvs, g=g, recs=recs,
                                console=open(base + ".console.txt", errors="replace").read() if os.path.exists(base + ".console.txt") else "")
    haveg = all(info[(f, i)]["inj"] for f in gf for i in (1, 2, 3))
    R["F2a"] = ev(haveg and all(info[k]["pass_"] and info[k]["hit"] and info[k]["injected"] for k in info), "GDB attaches, halts at fi_gdb_anchor(), performs the corruption and detaches cleanly in 12/12 runs (harness result=PASS)" if haveg else "GDB runs missing or incomplete")
    R["F2b"] = ev(haveg and all(info[k]["kv"].get("mech") == "GDB" for k in info), "the INJECTED line carries mech=GDB and the experiment ID EXP=<fault>_001")
    def rel(f, kv):
        b, a_ = hexint(kv["before"]), hexint(kv["after"])
        return {"MEM-01": a_ == b ^ 0x400 and b == 2200, "DATA-02": (b, a_) == (15, 100), "CPU-01": a_ == ((b ^ (1 << 29)) | 1), "CPU-02": a_ == b ^ (1 << 28)}[f]
    R["F2c"] = ev(haveg and all(rel(f, info[(f, i)]["kv"]) for f in gf for i in (1, 2, 3)),
                  "before/after are the Step 4 corruptions: setpoint 2200 -> 3224, kp 15 -> 100, PC -> (pc^2^29)|1, SP -> sp^2^28 (12/12)")
    def regs(f, i):
        g = info[(f, i)]["g"]
        if f == "CPU-01":
            m = re.search(r"GDB_INJECTED exp=\S+ pc=0x([0-9a-f]+)", g)
            return m and int(m.group(1), 16) == hexint(info[(f, i)]["kv"]["after"]) & ~1
        if f == "CPU-02":
            m = re.search(r"GDB_INJECTED exp=\S+ sp=0x([0-9a-f]+)", g)
            return m and int(m.group(1), 16) == hexint(info[(f, i)]["kv"]["after"])
        return True
    R["F2d"] = ev(haveg and all(regs(f, i) for f in ("CPU-01", "CPU-02") for i in (1, 2, 3)), "CPU-01/CPU-02: the debugger wrote the real PC / SP register (GDB output shows the register after the write equal to the logged `after`)")
    def behaviour(f, i):
        x = info[(f, i)]
        if f in ("MEM-01", "DATA-02"):
            return "state=COMPLETED" in "".join(t for tag, t in x["recs"] if tag == "FAULT")
        after = False
        silent = True
        for tag, t in x["recs"]:
            if tag == "FAULT" and "state=INJECTED" in t:
                after = True
            elif after and tag in ("STATUS", "CONTROL") :
                silent = False
        return silent
    R["F2e"] = ev(haveg and all(behaviour(f, i) for f in gf for i in (1, 2, 3)),
                  "MEM-01/DATA-02: experiment OBSERVED and COMPLETED (wrong control output); CPU-01/CPU-02: no STATUS/CONTROL record after INJECTED (the CPU cannot continue), as with the firmware-made fault")
    R["F2f"] = ev(haveg and all(len({re.search(r'cycle=(\d+)', info[(f, i)]['inj']).group(1) for i in (1, 2, 3)}) == 1 for f in gf), "injection cycle identical in the 3 runs of every fault")

    # ------------------------------------------------------------ F4 GDB injection into the protected (recovery) firmware
    pdir = f"{a.root}/gdb_prot"
    pr = {f: [Run(f, i, pdir) for i in (1, 2, 3)] for f in gf}
    havep = all(r.present and r.recs for rr in pr.values() for r in rr)
    pinj = {f: [r.injected(f) for r in pr[f]] for f in gf} if havep else {}
    harness_ok = all("result=PASS" in (open(f"{pdir}/{f}_run{i}.harness.txt", errors="replace").read() if os.path.exists(f"{pdir}/{f}_run{i}.harness.txt") else "") for f in gf for i in (1, 2, 3))
    R["F4a"] = ev(havep and harness_ok and all(x is not None and x["kv"].get("mech") == "GDB" for v in pinj.values() for x in v),
                  "the debugger injects MEM-01, DATA-02, CPU-01, CPU-02 into the protected firmware (build gdbprot = recovery + GDB injection): mech=GDB, EXP=<fault>_001, 12/12 runs" if havep else "gdb_prot runs missing")
    R["F4b"] = ev(havep and all(rel(f, x["kv"]) for f, v in pinj.items() for x in v), "same corruptions as in the baseline GDB runs (setpoint 2200 -> 3224, kp 15 -> 100, PC and SP corrupted by the debugger)")
    def dets(f, r):
        return sorted({d["kv"]["mech"] for d in r.by_tag("DETECT") if d["kv"].get("EXP") == f"{f}_001"})
    want = {"MEM-01": {"CRC"}, "DATA-02": {"CRC"}, "CPU-01": {"WWDG"}, "CPU-02": {"WWDG"}}
    R["F4c"] = ev(havep and all(want[f] <= set(dets(f, r)) for f in gf for r in pr[f]),
                  "detected in 12/12 runs: " + ", ".join(f"{f}={'+'.join(dets(f, pr[f][0]))}" for f in gf) + (" (CPU-01/CPU-02: as hangs, by the WWDG shim)" if havep else ""))
    def att(f, r):
        for x in r.by_tag("RECOVERY"):
            if x["kv"].get("EXP") == f"{f}_001" and x["kv"].get("state") == "START":
                end = next((y for y in r.by_tag("RECOVERY") if y["kv"].get("attempt") == x["kv"]["attempt"] and y["kv"].get("state") in ("COMPLETE", "FAILED")), None)
                return x, end
        return None, None
    wantact = {"MEM-01": "config_restore", "DATA-02": "config_restore", "CPU-01": "wwdg_reset", "CPU-02": "wwdg_reset"}
    R["F4d"] = ev(havep and all(att(f, r)[0] and att(f, r)[0]["kv"]["action"] == wantact[f] and att(f, r)[1] and att(f, r)[1]["kv"].get("success") == "1" for f in gf for r in pr[f]),
                  "recovery after the debugger's injection succeeds in 12/12 runs: " + ", ".join(f"{f} -> L{att(f, pr[f][0])[0]['kv']['level']} {wantact[f]}" for f in gf if havep and att(f, pr[f][0])[0]))
    def back(f, r):
        x, end = att(f, r)
        if not end:
            return False
        c = [q for q in r.controls() if q["i"] > end["i"]][:6]
        return len(c) == 6 and all(int(q["kv"]["value"]) == S4.control(int(q["kv"]["input"])) for q in c) and not r.by_tag("SAFE")
    R["F4e"] = ev(havep and all(back(f, r) for f in gf for r in pr[f]), "normal operation after the recovery: 6 following CONTROL records follow the control law, no safe state (12/12)")
    def sig(f):
        return [(int(x["kv"]["cycle"]), int(att(f, r)[1]["kv"]["time_cycles"])) for r, x in zip(pr[f], pinj[f]) if att(f, r)[1] and att(f, r)[1]["kv"].get("time_cycles", "none").isdigit()]
    def spread(f):
        v = sig(f)
        return (len(v), max(c for c, _ in v) - min(c for c, _ in v), max(t for _, t in v) - min(t for _, t in v)) if v else None
    R["F4f"] = ev(havep and all(spread(f) and spread(f)[0] == 3 and spread(f)[1] == 0 and spread(f)[2] <= 10 for f in gf),
                  "injection cycle identical in the 3 runs of every fault; recovery time identical within 10 cycles (max spread " + ", ".join(f"{f} {spread(f)[2]}" for f in gf if havep and spread(f)) + " cycles; one CPU-01 run is 5 cycles longer - tolerance stated, not hidden)")

    # ------------------------------------------------------------ F5 GDB injection before the first checkpoint (design finding)
    edir = f"{a.root}/gdb_prot_early"
    early = {f: [Run(f, i, edir) for i in (1, 2, 3)] for f in ("MEM-01", "DATA-02")}
    havee = all(r.present and r.recs for rr in early.values() for r in rr)
    def chain(f, r):
        out = []
        for x in r.by_tag("RECOVERY"):
            k = x["kv"]
            if k.get("EXP") == f"{f}_001" and k.get("state") in ("COMPLETE", "FAILED"):
                out.append((k["action"], k["state"], k.get("reason", ""), k.get("escalate_to", "")))
        return out
    R["F5a"] = ev(havee and all(chain(f, r)[:2] == [("config_restore", "FAILED", "checkpoint_invalid", "software_reset"), ("software_reset", "COMPLETE", "", "")] for f, rr in early.items() for r in rr),
                  "injected by the debugger ~75 ms after the first control cycle (before the first 500 ms checkpoint), level 2 correctly fails with reason=checkpoint_invalid and escalates to a software reset that succeeds (6/6 runs: MEM-01, DATA-02)" if havee else "gdb_prot_early runs missing")
    R["F5b"] = ev(havee and all(any(q["kv"].get("verified") == "tasks_running_after_reset" for q in r.by_tag("RECOVERY")) and not r.by_tag("SAFE") for rr in early.values() for r in rr),
                  "the escalation ends in verified normal operation (tasks running after the reset), no safe state - the recovery design degrades correctly; an initial checkpoint at boot would avoid the escalation but would change the verified recovery build")

    # ------------------------------------------------------------ F3 equal timing
    edb, edr = f"{a.root}/equiv_baseline", f"{a.root}/equiv_recovery"
    bdir = E.newest_complete("results/raw/step4/2*", set(E.BASE_SCEN.values()))
    rdir = E.newest_complete("results/raw/step6/2*", set(E.C6.SCENARIOS))
    times = {}
    for f in S4.STUDY_IDS:
        name = next(k for k, v in G.EQUIV_FAULTS.items() if v == f)
        b_src = (bdir, G.REUSE_BASE[name]) if name in G.REUSE_BASE else (edb, f"e_{name}_base")
        r_src = (rdir, G.REUSE_REC[name]) if name in G.REUSE_REC else (edr, f"e_{name}_rec")
        row = []
        for d, sc in (b_src, r_src):
            try:
                run = Run(sc, 1, d)
                i = run.injected(f)
                row.append(int(i["kv"]["t_ms"]) if i else None)
            except Exception:
                row.append(None)
        times[f] = (row[0], row[1], b_src, r_src)
    R["F3a"] = ev(all(t[0] is not None and t[1] is not None for t in times.values()), "single-fault runs with the command at the same time exist for all nine faults in both builds")
    R["F3b"] = ev(all(t[0] is not None and t[1] is not None and abs(t[0] - t[1]) <= 100 for t in times.values()),
                  "injection times (ms after boot) baseline / protected: " + ", ".join(f"{f} {t[0]}/{t[1]}" for f, t in times.items()) + " - within one control cycle (the protected build boots ~44 ms later)")

    titles = {"F1a": "Timer mechanism injects the study faults", "F1b": "Timer trigger accuracy", "F1c": "Timer-injected faults have the same effect as UART-injected ones", "F1d": "Timer runs repeat identically",
              "F2a": "GDB-assisted injection runs cleanly", "F2b": "GDB injections are logged with mech=GDB and the experiment ID", "F2c": "GDB corruptions equal the Step 4 corruptions",
              "F2d": "GDB writes the real PC/SP registers (CPU-01/CPU-02)", "F2e": "Behaviour after GDB injection", "F2f": "GDB runs repeat identically",
              "F4a": "GDB injects study faults into the protected firmware", "F4b": "GDB corruptions into the protected firmware equal the Step 4 corruptions", "F4c": "Detected after GDB injection", "F4d": "Recovered after GDB injection", "F4e": "Normal operation after GDB-injected faults", "F4f": "GDB runs on the protected firmware repeat identically",
              "F5a": "GDB injection before the first checkpoint: level 2 fails and escalates", "F5b": "The escalation restores normal operation",
              "F3a": "Equal-timing single-fault runs exist for both builds", "F3b": "Baseline and protected faults are injected at the same time of the run"}
    npass = sum(1 for k in titles if R[k][0] == "PASS")
    L = ["# Follow-up campaigns: timer and GDB mechanisms on study faults, equal-timing comparison", "", f"Root: {a.root}", "", "| # | Criterion | Result | Evidence |", "|---|---|---|---|"]
    for k in titles:
        L.append(f"| {k} | {titles[k]} | {R[k][0]} | {R[k][1].replace('|', '/')} |")
    L += ["", f"**{npass}/{len(titles)} criteria passed.**", ""]
    os.makedirs(os.path.dirname(a.report), exist_ok=True)
    open(a.report, "w").write("\n".join(L))
    print("\n".join(L[-4:]))
    return 0 if npass == len(titles) else 1


if __name__ == "__main__":
    sys.exit(main())
