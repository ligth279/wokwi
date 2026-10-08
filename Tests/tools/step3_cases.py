"""Step 3 test cases: single source for the Wokwi scenario generator
(gen_step3_scenario.py) and the acceptance checker (check_step3.py).

Wait texts must not contain '[' or ']' (wokwi-cli wait-serial pattern
syntax, see docs/SIMULATOR_LIMITATIONS.md section 7)."""
import re

CATALOG_SRC = "FaultInjection/Src/fault_catalog.c"


def catalog_ids(root="."):
    """Fault IDs registered in the firmware catalog (source of truth)."""
    src = open(f"{root}/{CATALOG_SRC}").read()
    # the detector validation faults exist only in the protected build (#if PROTECTED ... #endif)
    src = re.sub(r"#if PROTECTED && FI_STUDY_FAULTS\n(?!#include).*?#endif\n", "", src, flags=re.S)
    return re.findall(r'(?:STUDY\(|\{)"([A-Z0-9-]+)"', src)


STUDY_IDS = ["MEM-01", "MEM-02", "CPU-01", "CPU-02", "TIM-01", "TIM-02", "DATA-01", "DATA-02", "PERIPH-01"]

# (line sent, criterion group, expected cmd word, expected reason, expected arg or None)
REJECT_CASES = [
    ("FAULT MEM-03",           "invalid",   "FAULT",       "unknown_id",     "MEM-03"),
    ("FAULT XYZ",              "invalid",   "FAULT",       "unknown_id",     "XYZ"),
    ("FAULT MEM01",            "invalid",   "FAULT",       "unknown_id",     "MEM01"),
    ("FAULT mem-01",           "invalid",   "FAULT",       "malformed_id",   "mem-01"),
    ("FAULT MEM_01",           "invalid",   "FAULT",       "malformed_id",   "MEM_01"),
    ("FAULT",                  "missing",   "FAULT",       "missing_id",     None),
    ("FAULT    ",              "missing",   "FAULT",       "missing_id",     None),
    ("FAULT_AT",               "missing",   "FAULT_AT",    "missing_id",     None),
    ("FAULT_GDB",              "missing",   "FAULT_GDB",   "missing_id",     None),
    ("FAULT MEM-01 X",         "malformed", "FAULT",       "extra_args",     "MEM-01"),
    ("FAULT  TIM-01   now",    "malformed", "FAULT",       "extra_args",     "TIM-01"),
    ("FAULT MEM-01;",          "malformed", "FAULT",       "malformed_id",   "MEM-01;"),
    ("FAULT ABCDEFGHIJKLMNOP", "malformed", "FAULT",       "malformed_id",   "ABCDEFGHIJKL"),
    ("FAULT_AT FI-TEST",       "malformed", "FAULT_AT",    "missing_delay",  "FI-TEST"),
    ("FAULT_AT FI-TEST 0",     "malformed", "FAULT_AT",    "bad_delay",      "FI-TEST"),
    ("FAULT_AT FI-TEST 5ms",   "malformed", "FAULT_AT",    "bad_delay",      "FI-TEST"),
    ("FAULT_AT FI-TEST 9999",  "malformed", "FAULT_AT",    "bad_delay",      "FI-TEST"),
    ("FAULT_GDB FI-TEST X",    "malformed", "FAULT_GDB",   "extra_args",     "FI-TEST"),
]
# Line-level rejections (no command word is parsed).
LINE_REJECT_CASES = [
    ("FAULT " + "A" * 60,   "malformed", "line_too_long"),
    (b"FAULT\tFI-TEST",     "malformed", "non_printable_char"),
    (b"FAULT FI-TEST\x01",  "malformed", "non_printable_char"),
    ("FAULTX FI-TEST",      "malformed", "unknown_command"),
    ("fault FI-TEST",       "malformed", "unknown_command"),
]

UART_INJECTIONS = 3          # FI-TEST_001..003 via `FAULT FI-TEST`
TIMER_DELAYS_MS = [500, 1234]  # FI-TEST_004, FI-TEST_005 via `FAULT_AT`
POLL_WINDOW_MS = 2000        # quiet period after the last UART injection
