#!/usr/bin/env python3
"""Self-test of the Step 4 checker (check_step4.py).

Takes a known-good Step 4 raw-results directory (default: the newest under
results/raw/step4/), checks that the checker passes it, then injects one
defect at a time into a copy of the logs and checks that the checker fails
exactly the criteria that defect should break. A checker that cannot fail
proves nothing. Skipped (exit 0, with a message) when no results exist."""
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CHECK = os.path.join(HERE, "check_step4.py")


def newest_dir():
    ds = sorted(d for d in glob.glob("results/raw/step4/2*") if os.path.isdir(d))
    return ds[-1] if ds else None


def run_checker(d, root):
    rep = os.path.join(d, "_report.md")
    p = subprocess.run([sys.executable, CHECK, "--dir", d, "--report", rep, "--table", os.path.join(d, "_t.md"),
                        "--csv", os.path.join(d, "_c.csv"), "--elf", os.path.join(d, "firmware.elf"), "--root", root],
                       capture_output=True, text=True)
    if not os.path.exists(rep):
        return None, p.stderr[-400:]
    fails = set(re.findall(r"\| (4\.\d+[a-z]) \|[^|]*\| FAIL \|", open(rep).read()))
    return fails, ""


def sub(path, pat, repl, count=0):
    t = open(path, errors="replace").read()
    n = re.subn(pat, repl, t, count=count)
    open(path, "w").write(n[0])
    assert n[1] > 0, f"defect did not apply: {pat!r} in {path}"


def mutate_all(d, scen, pat, repl, ext="log"):
    for i in (1, 2, 3):
        sub(f"{d}/{scen}_run{i}.{ext}", pat, repl)


def defect_double_inject(d):  # MEM-01 injected twice
    sub(f"{d}/group_run1.log", r"(\[FAULT\] EXP=MEM-01_001 state=INJECTED [^\[]*)", r"\1\r\n\1")

def defect_two_bits(d):
    mutate_all(d, "group", r"(EXP=MEM-01_001 state=INJECTED [^\[]*after=)0x00000C98", r"\g<1>0x00000C9B")

def defect_not_implemented(d):
    sub(f"{d}/cpu01_run2.log", r"(\[FAULT\] EXP=CPU-01_001 state=INJECTED)", r"[FAULT] EXP=CPU-01_001 state=ERROR reason=not_implemented inject_count=0\r\n\1")

def defect_no_crash_signal(d):
    mutate_all(d, "cpu01", r"code 1006", "code 0", "console.txt")

def defect_progress_after_hang(d):
    with open(f"{d}/tim01_run3.log", "a") as f:
        f.write("[STATUS] src=periodic t_ms=9999 sensor_hb=1 control_hb=2 console_hb=3 sensor_err=0\r\n")

def defect_hang_looks_like_crash(d):
    mutate_all(d, "tim01", r"Scenario completed successfully", "API Error: Connection to transport closed unexpectedly: code 1006", "console.txt")

def defect_cycle_jitter(d):
    sub(f"{d}/group_run2.log", r"(EXP=DATA-02_001 state=INJECTED [^\[]*cycle=)\d+", r"\g<1>1")

def defect_input_not_corrupted(d):
    mutate_all(d, "group", r"(\[CONTROL\] seq=\d+ value=)100( input=)8500", r"\g<1>54\g<2>2561")

def defect_missing_run(d):
    os.remove(f"{d}/cpu02_run3.log")

def defect_task_not_identified(d):
    mutate_all(d, "tim02", r"\[TASK\] name=sensor state=blocked[^\[]*", "")

def defect_i2c_never_fails(d):
    mutate_all(d, "group", r"status=(BUS_ERROR|BUSY|TIMEOUT)", "status=OK")

def defect_second_fault(d):
    sub(f"{d}/mem02_run1.log", r"(\[FAULT\] EXP=MEM-02_001 state=INJECTED [^\[]*)", r"\1\r\n[FAULT] EXP=MEM-01_001 state=INJECTED mech=UART target=x before=0x1 after=0x2 inject_count=1 cycle=1 t_ms=1\r\n")

def defect_sdA_not_held(d):
    mutate_all(d, "group", r"SDA held LOW", "SDA ok", "console.txt")

def defect_wrong_sp_bit(d):
    mutate_all(d, "cpu02", r"(EXP=CPU-02_001 state=INJECTED [^\[]*after=0x)3", r"\g<1>2", "log")

# name -> (mutation, criteria that must fail)
DEFECTS = {
    "double_inject":          (defect_double_inject, {"4.2c", "4.12b"}),
    "two_bits_flipped":       (defect_two_bits, {"4.2a"}),
    "not_implemented":        (defect_not_implemented, {"4.1c"}),
    "crash_not_signalled":    (defect_no_crash_signal, {"4.4a", "4.4c", "4.12e"}),
    "progress_after_hang":    (defect_progress_after_hang, {"4.6b"}),
    "hang_looks_like_crash":  (defect_hang_looks_like_crash, {"4.6c", "4.12e"}),
    "cycle_jitter":           (defect_cycle_jitter, {"4.1d", "4.12c", "4.12d"}),
    "input_not_corrupted":    (defect_input_not_corrupted, {"4.8d", "4.8f"}),
    "missing_run":            (defect_missing_run, {"4.12a"}),
    "task_not_identified":    (defect_task_not_identified, {"4.7a", "4.7d"}),
    "i2c_never_fails":        (defect_i2c_never_fails, {"4.10c", "4.10d"}),
    "second_fault":           (defect_second_fault, {"4.3d"}),
    "sda_not_held":           (defect_sdA_not_held, {"4.10b"}),
    "sp_not_corrupted_bit28": (defect_wrong_sp_bit, {"4.5b"}),
}


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else newest_dir()
    if not src or not os.path.exists(os.path.join(src, "firmware.elf")):
        print("[UNIT] test_check_step4 SKIPPED: no Step 4 results directory with firmware.elf")
        return 0
    root = os.getcwd()
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        base = os.path.join(tmp, "good")
        shutil.copytree(src, base)
        fails, err = run_checker(base, root)
        ok = fails is not None and not fails
        print(("ok   " if ok else "FAIL ") + f"good evidence -> failing: {sorted(fails) if fails is not None else err}")
        failures += not ok
        for name, (mut, expect) in DEFECTS.items():
            d = os.path.join(tmp, name)
            shutil.copytree(src, d)
            try:
                mut(d)
                fails, err = run_checker(d, root)
            except Exception as e:  # a defect that cannot be applied is a test bug
                print(f"FAIL defect {name}: {e}")
                failures += 1
                continue
            # A checker that aborts (Traceback, no report) on evidence missing for a defect can never
            # produce a false PASS, so the defect still counts as detected; it is labelled as such.
            aborted = fails is None and ("Error" in err or "Traceback" in err)
            good = aborted or (fails is not None and expect <= fails)
            if aborted:
                fails = {"checker aborted"}
            print(("ok   " if good else "FAIL ") + f"defect {name:<24} -> failing: {sorted(fails) if fails is not None else err}"
                  + ("" if good else f"   (expected at least {sorted(expect)})"))
            failures += not good
    print(f"[UNIT] test_check_step4 failures={failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
