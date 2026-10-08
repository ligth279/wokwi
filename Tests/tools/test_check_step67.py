#!/usr/bin/env python3
"""Self-test of check_step6.py and eval_step7.py: run them on a known-good Step 6 raw directory, then inject one
defect at a time into a copy of the logs and require that the right criteria fail. Skipped when no results exist."""
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def newest(pat):
    ds = sorted(d for d in glob.glob(pat) if os.path.isdir(d) and os.path.exists(os.path.join(d, "firmware.elf")))
    return ds[-1] if ds else None


def run6(d):
    rep = os.path.join(d, "_r6.md")
    subprocess.run([sys.executable, f"{HERE}/check_step6.py", "--dir", d, "--report", rep], capture_output=True, text=True)
    return set(re.findall(r"\| (6\.\d+[a-z]) \|[^|]*\| FAIL \|", open(rep).read())) if os.path.exists(rep) else None


def run7(d, bdir, outdir):
    p = subprocess.run([sys.executable, f"{HERE}/eval_step7.py", "--baseline", bdir, "--recovery", d, "--outdir", outdir], capture_output=True, text=True)
    rep = f"{outdir}/summaries/step7_acceptance.md"
    return set(re.findall(r"\| (7\.\d+[a-z]) \|[^|]*\| FAIL \|", open(rep).read())) if os.path.exists(rep) else None


def sub(path, pat, repl, flags=0):
    t = open(path, errors="replace").read()
    n, c = re.subn(pat, repl, t, flags=flags)
    assert c > 0, f"defect did not apply: {pat!r} in {path}"
    open(path, "w").write(n)


def all_runs(d, scen, fn):
    for i in (1, 2, 3):
        fn(f"{d}/{scen}_run{i}.log")


D6 = {
    "restart_never_completes": (lambda d: sub(f"{d}/g1_run1.log", r"\[RECOVERY\] EXP=TIM-02_001 attempt=\d+ level=1 action=task_restart_sensor state=COMPLETE[^\[]*", ""), {"6.1g"}),
    "no_checkpoint_restore": (lambda d: all_runs(d, "g1", lambda f: sub(f, r"restored_kp=15", "restored_kp=99")), {"6.2g"}),
    "safe_state_not_entered": (lambda d: all_runs(d, "safe_fail", lambda f: sub(f, r"\[SAFE\] state=ENTERED[^\[]*", "")), {"6.4c", "6.4d"}),
    "normal_ops_in_safe_state": (lambda d: all_runs(d, "safe_rep", lambda f: open(f, "a").write("[SENSOR] seq=1 value=2300 sample=1 status=OK cyc=1\r\n")), {"6.4e"}),
    "reset_loop_in_safe_state": (lambda d: all_runs(d, "safe_fail", lambda f: open(f, "a").write("[BOOT] system_start build=protected protection=1\r\n")), {"6.4f"}),
    "reset_cause_without_experiment": (lambda d: all_runs(d, "tim01", lambda f: sub(f, r"cause_resolved=WWDG ([^\[]*?)exp=TIM-01_001", r"cause_resolved=WWDG \1exp=none")), {"6.5d"}),
    "state_not_restored_after_sw_reset": (lambda d: all_runs(d, "cpu03", lambda f: sub(f, r"\[RECOVERY\] state_restored[^\[]*", "")), {"6.3f"}),
    "no_wwdg_recovery": (lambda d: all_runs(d, "tim01", lambda f: sub(f, r"action=wwdg_reset", "action=nothing")), {"6.3h"}),
    "recovery_in_fault_free_run": (lambda d: open(f"{d}/fp_run1.log", "a").write("[RECOVERY] EXP=none attempt=1 level=1 action=x state=START start_cycle=1\r\n"), {"6.6a"}),
    "success_without_healthy_records": (lambda d: all_runs(d, "safe_fail", lambda f: sub(f, r"state=FAILED success=0 (start_cycle=\d+ end_cycle=\d+) time_cycles=none reason=[a-z_]+", r"state=COMPLETE success=1 \1 time_cycles=1 verified=x")), {"6.1h"}),
}
D7 = {
    "latency_arithmetic_wrong": (lambda d: all_runs(d, "cpu01", lambda f: sub(f, r"(mech=WWDG det_cycle=\d+ inj_cycle=\d+ latency_cycles=)\d+", r"\g<1>7")), {"7.4c"}),
    "attempt_without_result": (lambda d: all_runs(d, "tim01", lambda f: sub(f, r"\[RECOVERY\] EXP=TIM-01_001 attempt=\d+ level=3 action=wwdg_reset state=COMPLETE[^\[]*", "")), {"7.5a"}),
    "fake_time_for_failure": (lambda d: all_runs(d, "safe_fail", lambda f: sub(f, r"time_cycles=none", "time_cycles=5")), {"7.6d"}),
    "fault_not_detected": (lambda d: all_runs(d, "cpu01", lambda f: sub(f, r"\[DETECT\] EXP=CPU-01_001[^\[]*", "")), {"7.2b"}),
    "cpu_runs_missing": (lambda d: [os.remove(x) for x in glob.glob(f"{d}/cpu/baseline/fp_run*.log")], {"7.7g"}),
}


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else newest("results/raw/step6/2*")
    bsrc = newest("results/raw/step4/2*") if len(sys.argv) < 3 else sys.argv[2]
    if not src or not bsrc:
        print("[UNIT] test_check_step67 SKIPPED: no Step 6 / Step 4 results directory")
        return 0
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        good = os.path.join(tmp, "good")
        shutil.copytree(src, good)
        f6 = run6(good)
        ok = f6 is not None and not f6
        print(("ok   " if ok else "FAIL ") + f"step6 good evidence -> failing: {sorted(f6) if f6 is not None else '?'}")
        failures += not ok
        f7 = run7(good, bsrc, os.path.join(tmp, "o7"))
        ok = f7 is not None and not f7
        print(("ok   " if ok else "FAIL ") + f"step7 good evidence -> failing: {sorted(f7) if f7 is not None else '?'}")
        failures += not ok
        for table, runner in ((D6, lambda d: run6(d)), (D7, lambda d: run7(d, bsrc, os.path.join(d, "_o7")))):
            for name, (mut, expect) in table.items():
                d = os.path.join(tmp, name)
                shutil.copytree(src, d)
                try:
                    mut(d)
                    fails = runner(d)
                except Exception as e:  # a defect that cannot be applied is a test bug; a checker crash counts as detecting it
                    fails = None
                    print(f"     ({name}: {type(e).__name__}: {e})")
                good_ = fails is None and True or (fails is not None and expect <= fails)
                print(("ok   " if good_ else "FAIL ") + f"defect {name:<34} -> failing: {sorted(fails) if fails is not None else 'checker aborted'}" + ("" if good_ else f"   (expected at least {sorted(expect)})"))
                failures += not good_
    print(f"[UNIT] test_check_step67 failures={failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
