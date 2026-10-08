#!/usr/bin/env python3
"""Self-test of check_step5.py: good evidence must pass without FAIL, and one defect at a time must fail the right criteria.
Uses the newest Step 5 raw directory that contains firmware.elf and regression.txt. REQUIRE_RESULTS=1 turns 'no results' into a failure."""
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def newest():
    ds = sorted(d for d in glob.glob("results/raw/step5/2*") if os.path.exists(f"{d}/firmware.elf") and os.path.exists(f"{d}/regression.txt"))
    return ds[-1] if ds else None


def run(d):
    rep = f"{d}/_r.md"
    subprocess.run([sys.executable, f"{HERE}/check_step5.py", "--dir", d, "--report", rep, "--table", f"{d}/_t.md", "--csv", f"{d}/_c.csv"], capture_output=True, text=True)
    return set(re.findall(r"\| (5\.\d+[a-z]) \|[^|]*\| FAIL \|", open(rep).read())) if os.path.exists(rep) else None


def sub(path, pat, repl):
    t = open(path, errors="replace").read()
    n, c = re.subn(pat, repl, t)
    assert c > 0, f"defect did not apply: {pat!r} in {path}"
    open(path, "w").write(n)


def all_runs(d, scen, fn):
    for i in (1, 2, 3):
        fn(f"{d}/{scen}_run{i}.log")


DEFECTS = {
    "data01_not_detected": (lambda d: all_runs(d, "group", lambda f: sub(f, r"\[DETECT\] EXP=DATA-01_001[^\[]*", "")), {"5.6d"}),
    "detection_for_wrong_experiment": (lambda d: all_runs(d, "tim02", lambda f: sub(f, r"(\[DETECT\] EXP=)TIM-02_001( mech=HEARTBEAT)", r"\g<1>TIM-01_001\2")), {"5.8g"}),
    "false_positive_in_fault_free_run": (lambda d: open(f"{d}/fp_run1.log", "a").write("[DETECT] EXP=none mech=CRC det_cycle=1 false_positive=1 t_ms=1 what=config\r\n"), {"5.1e"}),
    "latency_arithmetic_wrong": (lambda d: all_runs(d, "group", lambda f: sub(f, r"(mech=CRC det_cycle=\d+ inj_cycle=\d+ latency_cycles=)\d+", r"\g<1>5")), {"5.1c", "5.9c"}),
    "run_missing": (lambda d: os.remove(f"{d}/cpu02_run3.log"), {"5.12a"}),
    "canary_broken_in_fault_free_run": (lambda d: all_runs(d, "fp", lambda f: sub(f, r"canary_ok=3/3", "canary_ok=2/3")), {"5.4b"}),
    "no_reset_after_wwdg": (lambda d: all_runs(d, "tim01", lambda f: sub(f, r"\[BOOT\] system_start[^\[]*(?=\[RESET\] cause=NONE csr=0x00000000\r?\n?\[RESET\] breadcrumb=WWDG)", "")), {"5.2d"}),
    "heartbeat_names_wrong_task": (lambda d: all_runs(d, "tim02", lambda f: sub(f, r"(mech=HEARTBEAT [^\[]*)task=sensor", r"\1task=control")), {"5.8e"}),
    "stack_use_above_threshold": (lambda d: all_runs(d, "fp", lambda f: sub(f, r"stack_peak_pct=\d+:\d+:\d+", "stack_peak_pct=90:10:10")), {"5.10e"}),
    "crc_mismatch_not_real": (lambda d: all_runs(d, "group", lambda f: sub(f, r"(what=config crc_expected=0x[0-9A-F]+ crc_actual=)0x[0-9A-F]+", r"\g<1>0xDEADBEEF")), {"5.6c"}),
}


def main():
    src = newest()
    if src is None:
        print("[UNIT] test_check_step5 " + ("FAILED: no Step 5 results" if os.environ.get("REQUIRE_RESULTS") else "SKIPPED: no Step 5 results directory"))
        return 1 if os.environ.get("REQUIRE_RESULTS") else 0
    fails = 0
    with tempfile.TemporaryDirectory() as tmp:
        g = f"{tmp}/good"
        shutil.copytree(src, g)
        f = run(g)
        ok = f is not None and not f
        print(("ok   " if ok else "FAIL ") + f"good evidence -> failing: {sorted(f) if f is not None else '?'}")
        fails += not ok
        for name, (mut, expect) in DEFECTS.items():
            d = f"{tmp}/{name}"
            shutil.copytree(src, d)
            try:
                mut(d)
                f = run(d)
            except Exception as e:
                print(f"FAIL defect {name}: {type(e).__name__}: {e}")
                fails += 1
                continue
            good = f is not None and expect <= f
            print(("ok   " if good else "FAIL ") + f"defect {name:<34} -> failing: {sorted(f) if f is not None else 'checker aborted'}" + ("" if good else f"   (expected at least {sorted(expect)})"))
            fails += not good
    print(f"[UNIT] test_check_step5 failures={fails}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
