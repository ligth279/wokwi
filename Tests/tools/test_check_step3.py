#!/usr/bin/env python3
"""Self-test of check_step3.py (no simulator needed).

Synthesises a complete Step 3 evidence folder using the firmware's exact log
formats (App/Src/console.c, FaultInjection/Src/fault_fw.c, Tests/gdb/,
Tests/tools/gdb_inject_run.sh), checks that a correct run set passes every
criterion, then applies one defect at a time and checks that the matching
criterion FAILS. This validates the checker logic, not the firmware.

Run: python3 Tests/tools/test_check_step3.py
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(__file__))
import step3_cases as C  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CHECKER = os.path.join(ROOT, "Tests", "tools", "check_step3.py")
CPM = 72000


class Log:
    def __init__(self):
        self.lines, self.ms, self.seq = [], 0, {}
        self.injected = 0
        self.sensor_seq = 0

    def L(self, tag, text):
        self.lines.append(f"[{tag}] {text}")

    def cyc(self, extra=0):
        return self.ms * CPM + extra

    def tick(self, ms):
        """Advance time; emit sensor/control records every 100 ms."""
        for _ in range(ms):
            self.ms += 1
            if self.ms % 100 == 0:
                self.sensor_seq += 1
                t = 2200 + self.sensor_seq
                out = max(0, min(100, int((t - 2200) * 15 / 100)))
                self.L("SENSOR", f"seq={self.sensor_seq} value={t} sample={self.sensor_seq % 256} status=OK "
                       f"cyc={self.cyc()}")
                self.L("CONTROL", f"seq={self.sensor_seq} value={out} input={t}")
            if self.ms % 1000 == 0:
                self.status("periodic")

    def status(self, src, active="none"):
        self.L("STATUS", f"src={src} t_ms={self.ms} sensor_hb={self.sensor_seq} control_hb={self.sensor_seq} "
               f"console_hb={self.ms // 10} sensor_err=0 dropped=0 heap_free=2632 rx_bytes=1 rx_err=0 "
               f"fault_cmd_ok=0 fault_cmd_rej=0 faults_injected={self.injected} fi_active={active}")

    def exp(self, fid):
        self.seq[fid] = self.seq.get(fid, 0) + 1
        return f"{fid}_{self.seq[fid]:03d}"

    def accept(self, cmd, fid, cls, name, exp):
        self.L("CMD", f"result=accepted cmd={cmd} id={fid} class={cls} name={name} EXP={exp}")

    def selected(self, exp, fid, cls, name, mech):
        self.L("FAULT", f"EXP={exp} state=SELECTED fault={fid} class={cls} name={name} mech={mech} "
               f"cycle={self.cyc(10)} t_ms={self.ms}")


def boot(log):
    log.L("BOOT", "system_start build=baseline protection=0 sysclk=72000000 clock=HSE_PLL")
    log.L("RESET", "cause=NONE csr=0x00000000")
    log.L("FAULT", "framework=ready persist=cold")
    log.L("RTOS", "scheduler_start heap_free=2856")
    log.L("APP", "system_ready")


def fi_test_uart(log, defect=None):
    exp = log.exp("FI-TEST")
    log.L("CMD", "rx=FAULT FI-TEST")
    log.accept("FAULT", "FI-TEST", "TEST", "framework_selftest", exp)
    log.selected(exp, "FI-TEST", "TEST", "framework_selftest", "UART")
    log.L("FAULT", f"EXP={exp} state=ARMED mech=UART site=control_cycle cycle={log.cyc(20)} t_ms={log.ms}")
    log.tick(100 - log.ms % 100)
    n = 2 if defect == "double_inject" else 1
    for k in range(n):
        log.injected += 1
        log.L("FAULT", f"EXP={exp} state=INJECTED mech=UART target=fi_test_target before=0xC0FFEE00 "
               f"after=0xC0FFEE01 inject_count={k + 1} cycle={log.cyc(5)} t_ms={log.ms}")
    log.tick(100)
    log.L("FAULT", f"EXP={exp} state=OBSERVED cycle={log.cyc(5)} t_ms={log.ms} cycles_since_injection={100 * CPM}")
    if defect != "no_terminal":
        log.L("FAULT", f"EXP={exp} state=COMPLETED outcome=effect_observed inject_count={n} cycle={log.cyc(9)} "
               f"t_ms={log.ms}")


def fi_test_timer(log, delay, busy=False, jitter=0):
    exp = log.exp("FI-TEST")
    log.L("CMD", f"rx=FAULT_AT FI-TEST {delay}")
    log.accept("FAULT_AT", "FI-TEST", "TEST", "framework_selftest", exp)
    log.selected(exp, "FI-TEST", "TEST", "framework_selftest", "TIMER")
    armed = log.cyc(20)
    target = armed + delay * CPM
    log.L("FAULT", f"EXP={exp} state=ARMED mech=TIMER site=tim4_isr delay_ms={delay} target_cycle={target} "
           f"cycle={armed} t_ms={log.ms}")
    if busy:
        log.tick(5)
        log.L("CMD", "rx=FAULT FI-TEST")
        log.L("CMD", f"result=rejected cmd=FAULT reason=busy active={exp}")
    log.tick(delay - (5 if busy else 0))
    inj = target + 6786 + jitter
    log.injected += 1
    log.L("FAULT", f"EXP={exp} state=INJECTED mech=TIMER target=fi_test_target before=0xC0FFEE00 "
           f"after=0xC0FFEE01 inject_count=1 cycle={inj} t_ms={log.ms} target_cycle={target} "
           f"trigger_error_cycles={inj - target}")
    log.tick(100 - log.ms % 100)
    log.L("FAULT", f"EXP={exp} state=OBSERVED cycle={log.cyc(5)} t_ms={log.ms} cycles_since_injection=1")
    log.L("FAULT", f"EXP={exp} state=COMPLETED outcome=effect_observed inject_count=1 cycle={log.cyc(9)} "
           f"t_ms={log.ms}")


def scenario_log(defect=None, jitter=0):
    log = Log()
    boot(log)
    log.tick(1000)
    for line, _g, cmd, reason, arg in C.REJECT_CASES:
        if defect == "accept_invalid" and line == "FAULT XYZ":
            continue
        log.L("CMD", f"rx={line.strip()}")
        log.L("CMD", f"result=rejected cmd={cmd} reason={reason}" + (f" arg={arg}" if arg else ""))
        log.tick(60)
    for data, _g, reason in C.LINE_REJECT_CASES:
        log.L("CMD", f"result=rejected reason={reason}" + (" len=66 max=48" if reason == "line_too_long" else ""))
        log.tick(60)
    study = {"MEM": ("MEMORY", "x"), "CPU": ("CPU", "x"), "TIM": ("TIMING", "x"), "DAT": ("DATA", "x"),
             "PER": ("PERIPHERAL", "x")}
    for fid in C.STUDY_IDS:
        if defect == "skip_study" and fid == "CPU-02":
            continue
        cls = study[fid[:3]][0]
        exp = log.exp(fid)
        if defect == "seq_gap" and fid == "MEM-02":
            exp = "MEM-02_002"
        log.L("CMD", f"rx=FAULT {fid}")
        log.accept("FAULT", fid, cls, "n", exp)
        log.selected(exp, fid, cls, "n", "UART")
        log.L("FAULT", f"EXP={exp} state=ERROR reason=not_implemented inject_count=0 cycle={log.cyc(30)} t_ms={log.ms}")
        log.tick(60)
    for i in range(C.UART_INJECTIONS):
        fi_test_uart(log, defect if i == 1 else None)
        log.tick(100)
    log.tick(C.POLL_WINDOW_MS)
    if defect == "poll_reinject":
        log.L("FAULT", f"EXP=FI-TEST_{C.UART_INJECTIONS:03d} state=INJECTED mech=UART target=fi_test_target "
               f"before=0xC0FFEE00 after=0xC0FFEE01 inject_count=2 cycle={log.cyc()} t_ms={log.ms}")
        log.injected += 1
    for k, d in enumerate(C.TIMER_DELAYS_MS):
        fi_test_timer(log, d, busy=(k == 0), jitter=jitter)
        log.tick(100)
    log.L("CMD", "rx=STATUS")
    log.status("cmd")
    log.tick(1500)
    return "\r\n".join(log.lines) + "\r\n"


def gdb_files(d, i, defect=None):
    log = Log()
    boot(log)
    log.tick(500)
    exp = "FI-TEST_001"
    log.L("CMD", "rx_gdb=FI-TEST")
    log.accept("GDB_REQUEST", "FI-TEST", "TEST", "framework_selftest", exp)
    log.selected(exp, "FI-TEST", "TEST", "framework_selftest", "GDB")
    log.L("FAULT", f"EXP={exp} state=ARMED mech=GDB site=fi_gdb_anchor cycle={log.cyc(20)} t_ms={log.ms}")
    log.tick(100 - log.ms % 100)
    anchor = log.cyc(77) + (i if defect == "gdb_nondeterministic" else 0)
    log.injected += 1
    log.L("FAULT", f"EXP={exp} state=INJECTED mech=GDB target=fi_test_target before=0xC0FFEE00 "
           f"after=0xC0FFEE01 inject_count=1 cycle={anchor} t_ms={log.ms}")
    log.tick(100)
    log.L("FAULT", f"EXP={exp} state=OBSERVED cycle={log.cyc()} t_ms={log.ms} cycles_since_injection=1")
    log.L("FAULT", f"EXP={exp} state=COMPLETED outcome=effect_observed inject_count=1 cycle={log.cyc(5)} t_ms={log.ms}")
    log.tick(500)
    open(f"{d}/gdb_run{i}.log", "w").write("\r\n".join(log.lines) + "\r\n")
    open(f"{d}/gdb_run{i}.gdb.txt", "w").write(
        f"GDB_ATTACHED pc=0x08003378\nGDB_REQUESTED id=FI-TEST\n"
        f"GDB_HIT exp={exp} anchor_cycle={anchor} target_before=0xc0ffee00\n"
        f"GDB_INJECTED exp={exp} target_after=0xc0ffee01 mailbox=0x6db0d0e5\nGDB_DISCONNECTED\n")
    stale = defect == "stale_port" and i == 2
    open(f"{d}/gdb_run{i}.harness.txt", "w").write(
        "[gdbrun] port_before=free port=3333\n[gdbrun] gdb_exit=0\n[gdbrun] sim_exit=42\n"
        f"[gdbrun] port_after={'BUSY' if stale else 'free'}\n[gdbrun] result={'FAIL' if stale else 'PASS'}\n")


def build(d, defect=None):
    os.makedirs(d, exist_ok=True)
    for i in (1, 2, 3):
        jitter = i if defect == "timer_nondeterministic" else 0
        open(f"{d}/scenario_run{i}.log", "w").write(scenario_log(defect if i == 1 else None, jitter))
        gdb_files(d, i, defect)
    open(f"{d}/unit.txt", "w").write("[UNIT] test_fault_cmd total=67 failures=0\n"
                                     "[UNIT] test_fault_fw checks=137 failures=0 log_lines=55\n")


def check(d):
    p = subprocess.run([sys.executable, CHECKER, "--dir", d, "--root", ROOT, "--csv", f"{d}/inj.csv"],
                       capture_output=True, text=True)
    res = dict(re.findall(r"^\| (3\.\d[a-z]) \| [^|]* \| (PASS|FAIL) \|", p.stdout, re.M))
    return p.returncode, res, p.stdout


DEFECTS = {  # defect -> criteria that must FAIL
    "double_inject": ["3.4c", "3.8a", "3.8e", "3.2a"],
    "no_terminal": ["3.3e", "3.3f", "3.3g"],
    "accept_invalid": ["3.1c"],
    "skip_study": ["3.1b"],
    "seq_gap": ["3.2c"],
    "poll_reinject": ["3.8b"],
    "timer_nondeterministic": ["3.5f", "3.7e"],
    "gdb_nondeterministic": ["3.7e"],
    "stale_port": ["3.6g", "3.6i"],
}


def main():
    failures = 0
    tmp = tempfile.mkdtemp(prefix="chk3_")
    try:
        good = f"{tmp}/good"
        build(good)
        rc, res, out = check(good)
        bad = [k for k, v in res.items() if v != "PASS"]
        ok = rc == 0 and len(res) == 52 and not bad
        print(f"{'ok  ' if ok else 'FAIL'} correct evidence -> {len(res) - len(bad)}/{len(res)} PASS, exit {rc}"
              + (f", failing {bad}" if bad else ""))
        failures += not ok
        if not ok:
            print(out[-3000:])
        for defect, must_fail in DEFECTS.items():
            dd = f"{tmp}/{defect}"
            build(dd, defect)
            rc, res, _ = check(dd)
            missed = [k for k in must_fail if res.get(k) != "FAIL"]
            ok = rc != 0 and not missed
            failures += not ok
            print(f"{'ok  ' if ok else 'FAIL'} defect {defect:24s} -> failing: "
                  f"{sorted(k for k, v in res.items() if v == 'FAIL')}" + (f"  MISSED {missed}" if missed else ""))
    finally:
        shutil.rmtree(tmp)
    print(f"[UNIT] test_check_step3 failures={failures}")
    return failures != 0


if __name__ == "__main__":
    sys.exit(main())
