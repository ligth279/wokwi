"""Step 4 test cases: single source for the scenario generator
(gen_step4_scenarios.py) and the acceptance checker (check_step4.py).

Every run is a separate Wokwi simulation that boots fresh. Faults that take
the system down (hang/crash) get a run of their own; the non-fatal ones are
chained in one run, each completed (and cleaned up by the harness) before the
next. PERIPH-01 is last because the baseline never releases the stuck bus."""

START_DELAY_MS = 2500   # after system_ready: sensor stream established, temperature past its minimum
GAP_MS = 1500           # quiet time after each chained experiment
TAIL_MS = 3000          # observation time after the last injection

# scenario name -> ordered fault IDs sent as `FAULT <ID>`
SCENARIOS = {
    "group": ["FI-TEST", "MEM-01", "DATA-01", "DATA-02", "PERIPH-01"],
    "tim01": ["TIM-01"],
    "tim02": ["TIM-02"],
    "mem02": ["MEM-02"],
    "cpu01": ["CPU-01"],
    "cpu02": ["CPU-02"],
}

# After the command the firmware must reach this state (log text):
#   COMPLETED = effect observed by the firmware
#   INJECTED  = fatal fault; the firmware cannot report anything after it
FINAL_STATE = {
    "FI-TEST": "COMPLETED",  # Step 3 self-test fault: proves the framework still works in the study build
    "MEM-01": "COMPLETED", "DATA-01": "COMPLETED", "DATA-02": "COMPLETED",
    "PERIPH-01": "COMPLETED", "TIM-02": "COMPLETED",
    "TIM-01": "INJECTED", "MEM-02": "INJECTED", "CPU-01": "INJECTED", "CPU-02": "INJECTED",
}

STUDY_IDS = ["MEM-01", "MEM-02", "CPU-01", "CPU-02", "TIM-01", "TIM-02", "DATA-01", "DATA-02", "PERIPH-01"]
CLASS = {"MEM-01": "MEMORY", "MEM-02": "MEMORY", "CPU-01": "CPU", "CPU-02": "CPU",
         "TIM-01": "TIMING", "TIM-02": "TIMING", "DATA-01": "DATA", "DATA-02": "DATA",
         "PERIPH-01": "PERIPHERAL"}

# Control law and nominal configuration (App/Src/control.c, App/Inc/app.h).
NOMINAL = dict(setpoint=2200, kp=15, out_min=0, out_max=100)
def control(temp, setpoint=2200, kp=15, lo=0, hi=100):
    """output = clamp(((temp - setpoint) * kp) / 100, lo, hi), C truncation toward zero."""
    num = (temp - setpoint) * kp
    out = abs(num) // 100 * (1 if num >= 0 else -1)
    return max(lo, min(hi, out))

# Which scenario/run set exercises each fault.
def scenario_of(fid):
    for name, ids in SCENARIOS.items():
        if fid in ids:
            return name
    raise KeyError(fid)

REPEATS = 3
