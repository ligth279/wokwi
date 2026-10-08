"""Step 6 (recovery) test cases for the `recovery` build; scenario generator and checker share this.
Every scenario is a fresh simulation. Gaps between faults exceed REC_HEALTHY_MS (4 s) so that each
fault is a separate episode (otherwise the safe-state limit would be reached on purpose)."""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import step4_cases as S4  # noqa: E402
import step5_cases as S5  # noqa: E402

START_DELAY_MS = 2500
GAP_MS = 4600      # > REC_HEALTHY_MS = 4000
TAIL_MS = 3000
FP_MS = 12000
REPEATS = 3
SCEN_DIR = "Tests/recovery"

# scenario -> ordered fault IDs (sent as `FAULT <ID>`)
SCENARIOS = {
    "fp": [],
    "g1": ["MEM-01", "DATA-01", "DATA-02", "MEM-03", "MEM-04", "MEM-02", "TIM-02"],
    "tim01": ["TIM-01"],
    "cpu01": ["CPU-01"],
    "cpu02": ["CPU-02"],
    "cpu03": ["CPU-03"],
    "periph": ["PERIPH-01"],
    "safe_rep": ["TIM-01", "TIM-01", "TIM-01", "TIM-01"],
    "safe_fail": ["TIM-03"],
}

# what the firmware should log after a FAULT command (wait text; no brackets) per fault
RECOVERY_WAIT = {
    "MEM-01": "action=config_restore state=COMPLETE",
    "DATA-02": "action=config_restore state=COMPLETE",
    "DATA-01": "action=sample_restore state=COMPLETE",
    "MEM-03": "action=task_restart_sensor state=COMPLETE",
    "MEM-02": "action=task_restart_sensor state=COMPLETE",
    "TIM-02": "action=task_restart_sensor state=COMPLETE",
    "MEM-04": "action=task_restart_control state=COMPLETE",
}

# fault -> (expected first recovery action, level) for the checker
EXPECT_ACTION = {
    "MEM-01": ("config_restore", 2), "DATA-02": ("config_restore", 2), "DATA-01": ("sample_restore", 2),
    "MEM-03": ("task_restart_sensor", 1), "MEM-02": ("task_restart_sensor", 1), "TIM-02": ("task_restart_sensor", 1),
    "MEM-04": ("task_restart_control", 1),
    "TIM-01": ("wwdg_reset", 3), "CPU-01": ("wwdg_reset", 3), "CPU-02": ("wwdg_reset", 3),
    "CPU-03": ("software_reset", 3), "PERIPH-01": ("i2c_bus_recovery", 1),
}
CLASS = dict(S5.CLASS)
CLASS["TIM-03"] = "TIMING"
STUDY_IDS = S4.STUDY_IDS


def scenario_of(fid):
    for n, ids in SCENARIOS.items():
        if fid in ids and n not in ("safe_rep", "safe_fail"):
            return n
    raise KeyError(fid)
