#!/usr/bin/env python3
"""Step 3 acceptance checker (fault-injection framework), criteria 3.1-3.8.

Evaluates raw evidence only: the serial logs of the scenario runs, the GDB
harness outputs and the host unit-test output found in one results folder
(written by Tests/run_step3.sh):
  scenario_run{1,2,3}.log      Tests/baseline/step3_framework.yaml runs
  gdb_run{1,2,3}.{log,gdb.txt,harness.txt}
  unit.txt                     `make unit` output
Missing evidence makes the affected criteria FAIL (never assumed).

Writes a Markdown report and a CSV of all injections (for detection-latency
calculations in later steps).

Usage: check_step3.py --dir results/raw/step3/<ts> [--report md] [--csv csv]
"""
import argparse
import csv
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
import step3_cases as C  # noqa: E402

TOKEN = re.compile(r"\[([A-Z0-9]+)\] ([^\[\r\n]*)")
KV = re.compile(r"(\w+)=(\S+)")
EXP_RE = re.compile(r"^[A-Z0-9-]+_\d{3}$")
ORDER = ["SELECTED", "ARMED", "INJECTED", "OBSERVED", "COMPLETED"]
CYC_PER_MS = 72000
# Must match App/Inc/app.h APP_CONFIG_DEFAULT / App/Src/control.c
SETPOINT, KP, OUT_MIN, OUT_MAX = 2200, 15, 0, 100


def kv(s):
    return dict(KV.findall(s))


def trunc_div(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def control_ref(t):
    return max(OUT_MIN, min(OUT_MAX, trunc_div((t - SETPOINT) * KP, 100)))


class Run:
    """Parsed serial log of one simulation."""

    def __init__(self, path):
        self.path = path
        self.ok = os.path.exists(path)
        text = open(path, "rb").read().decode("latin-1") if self.ok else ""
        self.toks = [(m.group(1), m.group(2).strip()) for m in TOKEN.finditer(text)]
        self.cmd = [p for t, p in self.toks if t == "CMD"]
        self.fault = [(p, kv(p)) for t, p in self.toks if t == "FAULT" and "EXP=" in p]
        # the sim timeout can cut the last line mid-write; keep complete records only
        self.status = [r for r in (kv(p) for t, p in self.toks if t == "STATUS") if "fi_active" in r]
        self.boots = [p for t, p in self.toks if t == "BOOT"]
        self.sensors = [kv(p) for t, p in self.toks if t == "SENSOR"]
        self.controls = {int(kv(p)["seq"]): kv(p) for t, p in self.toks if t == "CONTROL" and "seq=" in p}
        # experiments in order of first appearance
        self.exps = {}
        for _p, d in self.fault:
            self.exps.setdefault(d["EXP"], []).append(d)

    def events(self, exp, state):
        return [d for d in self.exps.get(exp, []) if d.get("state") == state]

    def injected(self):
        return [d for _p, d in self.fault if d.get("state") == "INJECTED"]

    def response_after(self, sent, n=3):
        """CMD lines following the echo of `sent` (rx=...)."""
        echo = "rx=" + sent.strip() if isinstance(sent, str) else None
        for i, c in enumerate(self.cmd):
            if echo is not None and c.strip() == echo.strip():
                return self.cmd[i + 1:i + 1 + n]
        return []


def exp_seq(exp):
    return int(exp.rsplit("_", 1)[1])


def exp_fault(exp):
    return exp.rsplit("_", 1)[0]


# ---------------------------------------------------------------------------


def lifecycle_ok(run, exp):
    """Order, single terminal state, at most one INJECTED, consistent EXP."""
    states = [d["state"] for d in run.exps[exp]]
    problems = []
    terminal = [s for s in states if s in ("COMPLETED", "ERROR")]
    if len(terminal) != 1 or states[-1] not in ("COMPLETED", "ERROR"):
        problems.append(f"terminal states {terminal}")
    seq = [ORDER.index(s) for s in states if s in ORDER]
    if seq != sorted(seq) or len(seq) != len(set(seq)):
        problems.append(f"order {states}")
    if states[0] != "SELECTED":
        problems.append("does not start with SELECTED")
    if states.count("INJECTED") > 1:
        problems.append("multiple INJECTED")
    return problems


def health(run):
    """Application keeps running normally around the commands."""
    seqs = [int(s["seq"]) for s in run.sensors]
    contiguous = seqs == list(range(1, len(seqs) + 1))
    all_ok = all(s.get("status") == "OK" for s in run.sensors)
    ctrl_ok = all(int(s["seq"]) in run.controls and
                  int(run.controls[int(s["seq"])]["value"]) == control_ref(int(s["value"])) for s in run.sensors)
    return (len(run.boots) == 1 and contiguous and len(seqs) > 50 and all_ok and ctrl_ok,
            f"boots={len(run.boots)}, samples={len(seqs)} contiguous={contiguous} all_ok={all_ok}, "
            f"control_follows_sensor={ctrl_ok}")


def scenario_checks(run, catalog):
    r = {}
    ev = lambda ok, s: (bool(ok), s)  # noqa: E731

    # ---- 3.1 command interface ------------------------------------------
    acc = [kv(c) for c in run.cmd if c.startswith("result=accepted")]
    acc_ids = [a.get("id") for a in acc]
    r["3.1a"] = ev(any(a.get("cmd") == "FAULT" and a.get("id") == "FI-TEST" for a in acc),
                   f"{len(acc)} accepted commands; `FAULT FI-TEST` accepted={'FI-TEST' in acc_ids}")
    missing_ids = [i for i in catalog if i not in acc_ids]
    r["3.1b"] = ev(not missing_ids, f"registered IDs accepted: {len(catalog) - len(missing_ids)}/{len(catalog)}"
                   + (f"; missing {missing_ids}" if missing_ids else ""))

    def rej(group):
        bad, n = [], 0
        for line, grp, cmd, reason, arg in C.REJECT_CASES:
            if grp != group:
                continue
            n += 1
            resp = run.response_after(line, 1)
            want = f"result=rejected cmd={cmd} reason={reason}" + (f" arg={arg}" if arg else "")
            if not resp or resp[0] != want:
                bad.append((line, resp[:1]))
        for data, grp, reason in C.LINE_REJECT_CASES:
            if grp != group:
                continue
            n += 1
            hits = [c for c in run.cmd if c.startswith("result=rejected") and f"reason={reason}" in c]
            if not hits:
                bad.append((repr(data), "no rejection"))
        return bad, n

    for key, grp in (("3.1c", "invalid"), ("3.1d", "missing"), ("3.1e", "malformed")):
        bad, n = rej(grp)
        r[key] = ev(n and not bad, f"{n - len(bad)}/{n} {grp} commands rejected with the expected reason"
                    + (f"; wrong: {bad[:3]}" if bad else ""))

    inj = run.injected()
    implemented_exps = [e for e in run.exps if exp_fault(e) == "FI-TEST"]
    study_exps = [e for e in run.exps if exp_fault(e) in C.STUDY_IDS]
    study_clean = all(not run.events(e, "INJECTED") and run.events(e, "ERROR")
                      and run.events(e, "ERROR")[0].get("reason") == "not_implemented"
                      and run.events(e, "ERROR")[0].get("inject_count") == "0" for e in study_exps)
    final = run.status[-1] if run.status else {}
    fi_total = int(final.get("faults_injected", -1))
    h_ok, h_ev = health(run)
    r["3.1f"] = ev(study_clean and fi_total == len(inj) and all(exp_fault(d["EXP"]) == "FI-TEST" for d in inj)
                   and h_ok,
                   f"study-fault commands: {len(study_exps)} experiments, none injected={study_clean}; "
                   f"faults_injected={fi_total} == INJECTED events={len(inj)} (all FI-TEST); {h_ev}")
    clear = all(all(k in a for k in ("cmd", "id", "class", "name", "EXP")) for a in acc)
    echoed = all(any(c.startswith("rx=") or c.startswith("rx_gdb=") for c in run.cmd[max(0, i - 2):i])
                 for i, c in enumerate(run.cmd) if c.startswith("result=accepted"))
    r["3.1g"] = ev(acc and clear and echoed, f"{len(acc)} accepted lines with cmd,id,class,name,EXP={clear}; "
                   f"each preceded by its rx echo={echoed}")

    # ---- 3.2 experiment IDs ------------------------------------------------
    inj_exps = [d["EXP"] for d in inj]
    r["3.2a"] = ev(inj and len(set(inj_exps)) == len(inj_exps),
                   f"{len(inj_exps)} injections, {len(set(inj_exps))} distinct EXP ids")
    fmt_bad = [e for e in run.exps if not EXP_RE.match(e)]
    r["3.2b"] = ev(run.exps and not fmt_bad, f"{len(run.exps)} EXP ids match <FAULT-ID>_<nnn>"
                   + (f"; bad {fmt_bad}" if fmt_bad else ""))
    per = {}
    for e in run.exps:
        per.setdefault(exp_fault(e), []).append(exp_seq(e))
    seq_bad = {f: s for f, s in per.items() if s != list(range(1, len(s) + 1))}
    r["3.2c"] = ev(per and not seq_bad, f"per-fault sequences: " +
                   ", ".join(f"{f}:{s[0]}..{s[-1]}" for f, s in per.items())
                   + (f"; broken {seq_bad}" if seq_bad else ""))
    id_bad = [e for e in run.exps if run.events(e, "SELECTED") and
              run.events(e, "SELECTED")[0].get("fault") != exp_fault(e)]
    acc_bad = [a for a in acc if a.get("EXP") and exp_fault(a["EXP"]) != a.get("id")]
    r["3.2d"] = ev(not id_bad and not acc_bad, f"EXP prefix == selected fault for all {len(run.exps)}; "
                   f"== accepted command id for all {len(acc)}")
    # same EXP throughout: between SELECTED and terminal, no other EXP appears
    interleave = []
    active = None
    for _p, d in run.fault:
        e, st = d["EXP"], d.get("state")
        if st == "SELECTED":
            if active is not None:
                interleave.append((active, e))
            active = e
        elif e != active and st != "ERROR":
            interleave.append((active, e))
        if st in ("COMPLETED", "ERROR") and e == active:
            active = None
    acc_match = all(a.get("EXP") in run.exps for a in acc)
    r["3.2e"] = ev(not interleave and acc_match, f"lifecycle lines never interleave between experiments "
                   f"({len(interleave)} violations); accepted-command EXP == lifecycle EXP={acc_match}")
    fi = per.get("FI-TEST", [])
    r["3.2f"] = ev(len(fi) >= 2 and len(set(fi)) == len(fi),
                   f"repeated FI-TEST experiments: {['FI-TEST_%03d' % s for s in fi]}")

    # ---- 3.3 lifecycle -----------------------------------------------------
    problems = {e: lifecycle_ok(run, e) for e in run.exps}
    problems = {e: p for e, p in problems.items() if p}
    r["3.3a"] = ev(run.exps and not problems, f"{len(run.exps)} experiments follow "
                   f"SELECTED->ARMED->INJECTED->OBSERVED->COMPLETED or end in ERROR"
                   + (f"; problems {list(problems.items())[:3]}" if problems else ""))
    impl = [e for e in run.exps if exp_fault(e) == "FI-TEST"]
    for key, st in (("3.3b", "SELECTED"), ("3.3c", "ARMED"), ("3.3d", "INJECTED")):
        pool = run.exps if st == "SELECTED" else impl
        miss = [e for e in pool if not run.events(e, st)]
        r[key] = ev(pool and not miss, f"{st} logged for {len(pool) - len(miss)}/{len(pool)} "
                    f"{'experiments' if st == 'SELECTED' else 'implemented-fault experiments'}")
    term = [e for e in run.exps if run.events(e, "COMPLETED") or run.events(e, "ERROR")]
    r["3.3e"] = ev(len(term) == len(run.exps), f"COMPLETED/ERROR logged for {len(term)}/{len(run.exps)}")
    r["3.3f"] = ev(len(term) == len(run.exps) and final.get("fi_active") == "none",
                   f"terminal for all; final STATUS fi_active={final.get('fi_active')}")
    accepted_exps = [a["EXP"] for a in acc if "EXP" in a]
    lost = [e for e in accepted_exps if e not in term]
    r["3.3g"] = ev(not lost and len(accepted_exps) == len(acc),
                   f"{len(accepted_exps)} accepted commands -> {len(accepted_exps) - len(lost)} terminal experiments")

    # ---- 3.4 UART-triggered injection ----------------------------------------
    uart = [e for e in impl if run.events(e, "SELECTED") and run.events(e, "SELECTED")[0].get("mech") == "UART"]
    u_bad = []
    for e in uart:
        i = run.events(e, "INJECTED")
        a = run.events(e, "ARMED")
        if len(i) != 1 or i[0].get("mech") != "UART" or i[0].get("target") != "fi_test_target" \
                or i[0].get("before") != "0xC0FFEE00" or i[0].get("after") != "0xC0FFEE01" or not a:
            u_bad.append(e)
            continue
        if not (int(a[0]["t_ms"]) <= int(i[0]["t_ms"]) <= int(a[0]["t_ms"]) + 110):
            u_bad.append(e)
    r["3.4a"] = ev(len(uart) == C.UART_INJECTIONS and not u_bad,
                   f"{len(uart)} UART experiments injected FI-TEST" + (f"; bad {u_bad}" if u_bad else ""))
    r["3.4b"] = ev(uart and not u_bad, "handler = FI-TEST: target=fi_test_target, before=0xC0FFEE00 -> "
                   "after=0xC0FFEE01 for every UART experiment")
    r["3.4c"] = ev(uart and all(len(run.events(e, "INJECTED")) == 1 for e in uart),
                   f"INJECTED count per UART experiment: {[len(run.events(e, 'INJECTED')) for e in uart]}")
    r["3.4d"] = ev(uart and all(run.events(e, "INJECTED")[0].get("EXP") == e for e in uart if run.events(e, "INJECTED")),
                   f"INJECTED lines carry their EXP: {uart}")
    r["3.4e"] = r["3.4c"][0] and r["3.4d"][0], "see 3.4c/3.4d (INJECTED event logged with mech=UART)"
    r["3.4e"] = ev(*r["3.4e"])
    r["3.4f"] = ev(h_ok and uart and not u_bad,
                   f"injection only at the first control cycle after ARMED (<=110 ms); {h_ev}")

    # ---- 3.5 timer-triggered injection ---------------------------------------
    tim = [e for e in impl if run.events(e, "SELECTED") and run.events(e, "SELECTED")[0].get("mech") == "TIMER"]
    t_rows, t_bad = [], []
    for e in tim:
        a = run.events(e, "ARMED")
        i = run.events(e, "INJECTED")
        if not a or len(i) != 1:
            t_bad.append(e)
            continue
        d = int(a[0]["delay_ms"])
        dt = int(i[0]["t_ms"]) - int(a[0]["t_ms"])
        err = int(i[0]["trigger_error_cycles"])
        t_rows.append((e, d, dt, err))
        if abs(dt - d) > 2 or not (0 <= err <= CYC_PER_MS) or int(i[0]["target_cycle"]) != int(a[0]["target_cycle"]):
            t_bad.append(e)
    r["3.5a"] = ev(len(tim) == len(C.TIMER_DELAYS_MS) and all(run.events(e, "ARMED") for e in tim),
                   f"{len(tim)} timer experiments armed with delays "
                   f"{[int(run.events(e, 'ARMED')[0]['delay_ms']) for e in tim if run.events(e, 'ARMED')]}")
    r["3.5b"] = ev(t_rows and not t_bad, "armed->injected ms vs delay: " +
                   ", ".join(f"{e}: {dt}/{d} ms, trigger_error={err} cyc" for e, d, dt, err in t_rows))
    r["3.5c"] = ev(tim and all(run.events(e, "INJECTED") and run.events(e, "INJECTED")[0].get("before") == "0xC0FFEE00"
                               and run.events(e, "INJECTED")[0].get("after") == "0xC0FFEE01" for e in tim),
                   "FI-TEST target flipped by the timer injection")
    r["3.5d"] = ev(tim and all(len(run.events(e, "INJECTED")) == 1 for e in tim),
                   f"INJECTED per timer experiment: {[len(run.events(e, 'INJECTED')) for e in tim]}")
    r["3.5e"] = ev(tim and all(all(k in run.events(e, "INJECTED")[0] for k in ("cycle", "target_cycle", "trigger_error_cycles"))
                               for e in tim if run.events(e, "INJECTED")),
                   "INJECTED has cycle, target_cycle, trigger_error_cycles")
    busy = [c for c in run.cmd if "reason=busy" in c]
    r["_busy"] = ev(len(busy) == 1, f"busy rejections while timer armed: {len(busy)}")

    # ---- 3.7 / 3.8 per run ----------------------------------------------------------
    no_cycle = [d["EXP"] for d in inj if "cycle" not in d]
    r["3.7a"] = ev(inj and not no_cycle, f"{len(inj)} INJECTED events with cycle=")
    late = []
    for d in inj:
        s = run.events(d["EXP"], "SELECTED")
        if not s or int(d["cycle"]) - int(s[0]["cycle"]) <= 0:
            late.append(d["EXP"])
    r["3.7b"] = ev(inj and not late, "injection cycle is after (and distinct from) command/selection cycle "
                   "for every injection: " + ", ".join(
                       f"{d['EXP']}:+{int(d['cycle']) - int(run.events(d['EXP'], 'SELECTED')[0]['cycle'])}cyc"
                       for d in inj if run.events(d['EXP'], 'SELECTED')))
    r["3.7c"] = ev(inj and all(d.get("mech") in ("UART", "TIMER") for d in inj) and
                   {d.get("mech") for d in inj} == {"UART", "TIMER"},
                   f"mechanisms on INJECTED: {sorted({d.get('mech') for d in inj})}")
    once = {e: len(run.events(e, "INJECTED")) for e in impl}
    r["3.8a"] = ev(impl and all(v == 1 for v in once.values()), f"INJECTED per accepted FI-TEST command: {once}")
    # polling window: after FI-TEST_003 completes, no injection until the next selection
    last_uart = f"FI-TEST_{C.UART_INJECTIONS:03d}"
    quiet_ok = False
    seen_done = False
    for _p, d in run.fault:
        if d["EXP"] == last_uart and d.get("state") == "COMPLETED":
            seen_done, quiet_ok = True, True
        elif seen_done and d.get("state") == "SELECTED":
            break
        elif seen_done and d.get("state") == "INJECTED":
            quiet_ok = False
    console = [int(s.get("console_hb", 0)) for s in run.status if s.get("src") == "periodic"]
    r["3.8b"] = ev(quiet_ok, f"no INJECTED during the {C.POLL_WINDOW_MS} ms polling window after {last_uart} "
                   f"(console loop kept polling: console_hb {console[0] if console else '-'}"
                   f"->{console[-1] if console else '-'})")
    comp = {e: run.events(e, "COMPLETED")[0].get("inject_count") for e in impl if run.events(e, "COMPLETED")}
    r["3.8c"] = ev(impl and len(comp) == len(impl) and all(v == "1" for v in comp.values()),
                   f"COMPLETED inject_count per experiment: {comp}")
    r["3.8d"] = ev(r["3.8a"][0] and r["3.4c"][0] and r["3.5d"][0], "one INJECTED event per experiment (UART and TIMER)")
    r["3.8e"] = ev(r["3.8a"][0], "exactly one `state=INJECTED` line per EXP")
    r["3.8f"] = ev(final.get("fi_active") == "none" and len(run.boots) == 1 and h_ok,
                   f"after injections: fi_active={final.get('fi_active')}, later commands still processed, {h_ev}")
    return r


def gdb_checks(d):
    runs = []
    for i in (1, 2, 3):
        base = os.path.join(d, f"gdb_run{i}")
        h = open(base + ".harness.txt").read() if os.path.exists(base + ".harness.txt") else ""
        g = open(base + ".gdb.txt", errors="replace").read() if os.path.exists(base + ".gdb.txt") else ""
        run = Run(base + ".log")
        hit = re.search(r"GDB_HIT exp=(\S+) anchor_cycle=(\d+)", g)
        injd = re.search(r"GDB_INJECTED exp=(\S+) target_after=(\S+)", g)
        fw_inj = [x for x in run.injected() if x.get("mech") == "GDB"]
        exp = hit.group(1) if hit else None
        runs.append(dict(
            exists=bool(h), harness=h, gdb=g, run=run, exp=exp,
            attached="GDB_ATTACHED" in g, hit=bool(hit), injected=bool(injd),
            disconnected="GDB_DISCONNECTED" in g, gdb_exit="gdb_exit=0" in h, sim_end="sim_exit=42" in h,
            port_before="port_before=free" in h, port_after="port_after=free" in h, passed="result=PASS" in h,
            fw_inj=fw_inj, completed=bool(exp and run.events(exp, "COMPLETED")),
            anchor=int(hit.group(2)) if hit else None))
    have = [x for x in runs if x["exists"]]
    r = {}
    ev = lambda ok, s: (bool(ok), s)  # noqa: E731
    if not have:
        none = (False, "no GDB run evidence (gdb_run*.harness.txt missing)")
        return {k: none for k in ("3.6a", "3.6b", "3.6c", "3.6d", "3.6e", "3.6f", "3.6g", "3.6h", "3.6i")}, runs
    f = lambda key: [x[key] for x in runs]  # noqa: E731
    r["3.6a"] = ev(all(f("attached")), f"GDB attached per run: {f('attached')}")
    r["3.6b"] = ev(all(f("hit")) and all(f("completed")),
                   f"continued to the anchor: {f('hit')}; target ran on to COMPLETED: {f('completed')}")
    r["3.6c"] = ev(all(f("hit")), f"anchor (fi_gdb_anchor) reached: {f('hit')}")
    r["3.6d"] = ev(all(f("injected")) and all(len(x["fw_inj"]) == 1 for x in runs),
                   f"GDB wrote target+mailbox: {f('injected')}; firmware INJECTED mech=GDB count: "
                   f"{[len(x['fw_inj']) for x in runs]}")
    r["3.6e"] = ev(all(x["exp"] and x["fw_inj"] and x["fw_inj"][0]["EXP"] == x["exp"] for x in runs),
                   f"EXP seen by GDB == firmware INJECTED EXP: {[x['exp'] for x in runs]}")
    r["3.6f"] = ev(all(f("disconnected")) and all(f("gdb_exit")) and all(f("sim_end")),
                   f"disconnect: {f('disconnected')}, gdb exit 0: {f('gdb_exit')}, sim ran to its end: {f('sim_end')}")
    r["3.6g"] = ev(all(f("port_after")), f"port free after each run: {f('port_after')}")
    r["3.6h"] = ev(all(f("port_before")) and all(f("attached")),
                   f"port free before each run and new session attached: {f('port_before')}")
    r["3.6i"] = ev(len(have) == 3 and all(f("passed")), f"harness PASS: {f('passed')}")
    return r, runs


CRITERIA = [
    ("3.1", "Command interface", [
        ("3.1a", "`FAULT <ID>` command is accepted through UART"),
        ("3.1b", "Valid fault IDs are recognized"),
        ("3.1c", "Invalid fault IDs are rejected"),
        ("3.1d", "Missing fault ID is rejected"),
        ("3.1e", "Malformed commands are rejected"),
        ("3.1f", "A valid command does not itself cause an unintended fault"),
        ("3.1g", "Every accepted command produces a clear log entry")]),
    ("3.2", "Experiment ID generation", [
        ("3.2a", "Every actual injection receives a unique experiment ID"),
        ("3.2b", "Format is `EXP=<FAULT-ID>_<sequence>`"),
        ("3.2c", "Sequence number increments correctly"),
        ("3.2d", "Experiment ID contains the correct fault ID"),
        ("3.2e", "The same experiment ID is used throughout that experiment's logs"),
        ("3.2f", "IDs remain distinguishable between repeated experiments")]),
    ("3.3", "Fault lifecycle", [
        ("3.3a", "Each experiment follows selected -> armed -> injected -> observed -> completed"),
        ("3.3b", "Selection is logged"),
        ("3.3c", "Arming is logged"),
        ("3.3d", "Injection is logged"),
        ("3.3e", "Completion/error is logged"),
        ("3.3f", "Every experiment reaches a defined terminal state"),
        ("3.3g", "No experiment silently disappears from the framework")]),
    ("3.4", "UART-triggered injection", [
        ("3.4a", "UART can trigger a registered fault"),
        ("3.4b", "The correct fault handler is selected"),
        ("3.4c", "The injection occurs exactly once for one command"),
        ("3.4d", "The generated experiment ID is associated with the injection"),
        ("3.4e", "The injection event is logged"),
        ("3.4f", "The application remains stable until the intended injection point")]),
    ("3.5", "Timer-triggered injection", [
        ("3.5a", "A fault can be armed for timer-based injection"),
        ("3.5b", "The timer reaches the intended trigger point"),
        ("3.5c", "The correct fault is injected"),
        ("3.5d", "The injection occurs exactly once"),
        ("3.5e", "The injection timing/cycle is recorded"),
        ("3.5f", "Repeated runs produce reproducible trigger behavior")]),
    ("3.6", "GDB-assisted injection", [
        ("3.6a", "GDB can attach to the Wokwi target"),
        ("3.6b", "The target can be continued reliably"),
        ("3.6c", "The intended fault target can be reached"),
        ("3.6d", "The intended injection can be performed"),
        ("3.6e", "The injection is associated with an experiment ID"),
        ("3.6f", "GDB can detach/terminate cleanly"),
        ("3.6g", "No stale GDB session retains the Wokwi port"),
        ("3.6h", "A new GDB session can attach after the previous one ends"),
        ("3.6i", "Three consecutive GDB injection runs succeed")]),
    ("3.7", "Injection timing", [
        ("3.7a", "Every actual injection records its injection cycle"),
        ("3.7b", "The cycle is recorded at actual injection, not command reception"),
        ("3.7c", "UART, timer, and GDB injections identify their injection mechanism"),
        ("3.7d", "Injection-cycle data is available for detection-latency calculations"),
        ("3.7e", "Repeated deterministic runs produce consistent injection cycles")]),
    ("3.8", "Exactly-once injection", [
        ("3.8a", "One valid `FAULT <ID>` command produces exactly one injection"),
        ("3.8b", "Repeated UART polling does not cause repeated injection"),
        ("3.8c", "A fault cannot accidentally trigger multiple times from one command"),
        ("3.8d", "The experiment receives only one injection event"),
        ("3.8e", "The log contains exactly one `INJECTED` event for that experiment"),
        ("3.8f", "The framework remains in a controlled state after the injection")]),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True)
    ap.add_argument("--report")
    ap.add_argument("--csv")
    ap.add_argument("--root", default=".")
    a = ap.parse_args()

    catalog = C.catalog_ids(a.root)
    runs = [Run(os.path.join(a.dir, f"scenario_run{i}.log")) for i in (1, 2, 3)]
    present = [r for r in runs if r.ok and r.toks]
    per_run = [scenario_checks(r, catalog) for r in present]

    final = {}
    keys = [k for _g, _n, items in CRITERIA for k, _ in items]
    for k in keys:
        if per_run and k in per_run[0]:
            oks = [pr[k][0] for pr in per_run]
            final[k] = (len(present) == 3 and all(oks),
                        f"scenario runs passing {sum(oks)}/3; run1: {per_run[0][k][1]}")
    if not per_run:
        for k in keys:
            if not k.startswith("3.6"):
                final[k] = (False, "no scenario run evidence (scenario_run*.log missing)")

    # 3.5f / 3.7e: identical injection cycles across the 3 deterministic runs
    def sig(run, mech):
        return [(d["EXP"], d["cycle"], d.get("trigger_error_cycles")) for d in run.injected() if d.get("mech") == mech]
    if len(present) == 3:
        same_t = all(sig(r, "TIMER") == sig(present[0], "TIMER") for r in present) and sig(present[0], "TIMER")
        same_u = all(sig(r, "UART") == sig(present[0], "UART") for r in present) and sig(present[0], "UART")
        final["3.5f"] = (bool(same_t) and final.get("3.5b", (False,))[0],
                         f"timer (EXP, inject cycle, trigger error) identical in 3/3 runs: {bool(same_t)}; "
                         f"run1: {sig(present[0], 'TIMER')}")
    else:
        final["3.5f"] = (False, f"only {len(present)}/3 scenario runs")
        same_u = same_t = False

    g, gruns = gdb_checks(a.dir)
    final.update(g)
    anchors = [x["anchor"] for x in gruns if x["anchor"] is not None]
    gdb_mech = all(x["fw_inj"] and x["fw_inj"][0].get("mech") == "GDB" for x in gruns)
    if "3.7c" in final:
        final["3.7c"] = (final["3.7c"][0] and gdb_mech,
                         final["3.7c"][1] + f"; GDB runs INJECTED mech=GDB: {gdb_mech}")
    same_g = len(anchors) == 3 and len(set(anchors)) == 1
    final["3.7e"] = (bool(same_u) and bool(same_t) and same_g,
                     f"UART cycles identical across 3 runs: {bool(same_u)}; TIMER: {bool(same_t)}; "
                     f"GDB anchor cycles {anchors} identical: {same_g}")

    # CSV of every injection (3.7d)
    rows = []
    for i, r in enumerate(runs, 1):
        for d in r.injected():
            s = r.events(d["EXP"], "SELECTED")
            rows.append(dict(source=f"scenario_run{i}", exp=d["EXP"], mech=d.get("mech"),
                             select_cycle=s[0]["cycle"] if s else "", inject_cycle=d["cycle"],
                             inject_t_ms=d["t_ms"], target_cycle=d.get("target_cycle", ""),
                             trigger_error_cycles=d.get("trigger_error_cycles", ""),
                             before=d.get("before"), after=d.get("after")))
    for i, x in enumerate(gruns, 1):
        for d in x["fw_inj"]:
            s = x["run"].events(d["EXP"], "SELECTED")
            rows.append(dict(source=f"gdb_run{i}", exp=d["EXP"], mech="GDB",
                             select_cycle=s[0]["cycle"] if s else "", inject_cycle=d["cycle"],
                             inject_t_ms=d["t_ms"], target_cycle="", trigger_error_cycles="",
                             before=d.get("before"), after=d.get("after")))
    if a.csv and rows:
        with open(a.csv, "w", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=list(rows[0]))
            w.writeheader()
            w.writerows(rows)
    mechs = sorted({r["mech"] for r in rows})
    final["3.7d"] = (bool(rows) and mechs == ["GDB", "TIMER", "UART"] and bool(a.csv),
                     f"{len(rows)} injections with select/inject cycles written to {a.csv or '(no --csv)'}; "
                     f"mechanisms {mechs}")

    unit = open(os.path.join(a.dir, "unit.txt")).read() if os.path.exists(os.path.join(a.dir, "unit.txt")) else ""
    unit_lines = re.findall(r"\[UNIT\][^\n]*", unit)
    unit_ok = len(unit_lines) >= 2 and all("failures=0" in u for u in unit_lines)

    out = ["# Step 3 acceptance - fault-injection framework", "",
           f"Evidence: `{a.dir}` (scenario runs: {len(present)}/3, GDB runs: "
           f"{sum(1 for x in gruns if x['exists'])}/3)", "",
           f"Host unit tests: {'PASS' if unit_ok else 'FAIL/missing'} - {'; '.join(unit_lines) or 'no unit.txt'}", ""]
    total = passed = 0
    for gid, gname, items in CRITERIA:
        out += [f"## {gid} {gname}", "", "| # | Criterion | Result | Evidence |", "|---|---|---|---|"]
        for k, name in items:
            ok, evd = final.get(k, (False, "not evaluated"))
            total += 1
            passed += ok
            out.append(f"| {k} | {name} | {'PASS' if ok else 'FAIL'} | {str(evd).replace('|', '/')} |")
        out.append("")
    out.append(f"**{passed}/{total} criteria passed.**")
    report = "\n".join(out) + "\n"
    print(report)
    if a.report:
        open(a.report, "w").write(report)
    return 0 if passed == total and unit_ok else 1


if __name__ == "__main__":
    sys.exit(main())
