"""Step 5 test cases (fault detection, protected build): single source for the
scenario generator and the acceptance checker. Same structure as step4_cases.py;
every run is a fresh simulation of build/protected."""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import step4_cases as S4  # noqa: E402

START_DELAY_MS = S4.START_DELAY_MS
GAP_MS = S4.GAP_MS
TAIL_MS = 3000
FP_MS = 12000          # fault-free run length

SCENARIOS = {
    "fp": [],
    "group": ["MEM-03", "MEM-04", "CPU-03", "MEM-01", "DATA-01", "DATA-02", "PERIPH-01"],
    "tim01": ["TIM-01"],
    "tim02": ["TIM-02"],
    "mem02": ["MEM-02"],
    "cpu01": ["CPU-01"],
    "cpu02": ["CPU-02"],
}
FINAL_STATE = dict(S4.FINAL_STATE)
FINAL_STATE.update({"MEM-03": "COMPLETED", "MEM-04": "COMPLETED", "CPU-03": "COMPLETED"})
CLASS = dict(S4.CLASS)
CLASS.update({"MEM-03": "MEMORY", "MEM-04": "MEMORY", "CPU-03": "CPU"})

# fault -> mechanisms expected to detect it (any one of the set is the "correct mechanism";
# all of them are reported when they fire). None: no detector can see it in Wokwi.
EXPECT = {
    "MEM-01": {"CRC", "REDUNDANT"},
    "DATA-01": {"CRC"},
    "DATA-02": {"CRC", "REDUNDANT"},
    "TIM-01": {"WWDG"},
    "TIM-02": {"HEARTBEAT"},
    "MEM-02": {"STACK_SEAL"},
    "MEM-03": {"STACK_CANARY"},
    "MEM-04": {"STACK_PAINT"},
    "CPU-03": {"FAULT_HANDLER"},
    "PERIPH-01": {"I2C_TIMEOUT"},
    "CPU-01": None,
    "CPU-02": None,
}
STUDY_IDS = S4.STUDY_IDS
VALIDATION_IDS = ["MEM-03", "MEM-04", "CPU-03"]
REPEATS = 3


def scenario_of(fid):
    for name, ids in SCENARIOS.items():
        if fid in ids:
            return name
    raise KeyError(fid)
