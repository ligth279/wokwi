# Simulation-based study of software fault detection and self-recovery on an STM32F103C8 (Wokwi)

STM32F103C8 Blue Pill (Cortex-M3, 72 MHz, 64 KB flash, 20 KB RAM) simulated in Wokwi. A small application
(I2C temperature sensor -> proportional control loop -> UART log, on FreeRTOS) is run with software-injected
faults, first **without protection** (baseline), then **with detection and recovery** (protected). Requirements
and the rules for reporting results are in `CLAUDE.md`.

## Fresh clone

`tools/fetch_deps.sh` (vendor HAL/CMSIS/FreeRTOS at pinned revisions, not stored in git), then `make env` (`tools/check_env.sh`: toolchain,
wokwi-cli, python matplotlib, vendor sources, token) and `make unit` (host tests, including the defect-injection self-tests of the checkers).
Simulation runs need `WOKWI_CLI_TOKEN`. `results/raw/README.md` (generated) says which raw directory of each campaign is the one the reports use;
the others are kept as evidence of infrastructure failures (quota, HTTP 503, DNS). ELF files stored in `results/raw` have their debug sections stripped.

## Where the results are

`make report` (or `python3 Tests/tools/make_report.py`) regenerates **`results/report/report.html`**: 10 figures (light and dark theme, SVG + PNG; acceptance
counts per step, baseline-vs-protected outcome per fault, detection coverage, detection latency, recovery time and success rate, resource overhead,
control output and application progress around the injection, escalation timeline) and all tables, directly from the raw logs; the numbers behind every
figure are in `results/report/data/*.csv`. `Tests/run_step4.sh`, `run_step5.sh` and `run_step6.sh` call it at the end, so the report is refreshed after every campaign.

| What | File |
|---|---|
| Final comparison table (7.9) | `results/tables/final_comparison.md` |
| Detection coverage / latency | `results/tables/step7_coverage.md`, `step7_latency.md`, `results/summaries/step7_latencies.csv` |
| Recovery success rate and time | `results/tables/step7_recovery.md`, `results/summaries/step7_recoveries.csv` |
| Flash / RAM / CPU overhead | `results/tables/step7_overhead.md` |
| Baseline fault effects (Step 4) | `results/tables/step4_fault_effects.md` |
| Detection results (Step 5) | `results/tables/step5_detection.md` |
| Acceptance reports | `results/summaries/step{2,3,4,5,6,7}_acceptance.md` |
| Raw serial logs of every run | `results/raw/step*/<timestamp>/*.log` (and `*.console.txt` = wokwi-cli output) |
| Simulator limits that shape the results | `docs/SIMULATOR_LIMITATIONS.md` (read this before quoting any number) |

All tables are generated from the raw logs by the scripts below; nothing in them is typed in.

## Builds (`make BUILD=<name>`; needs arm-none-eabi-gcc, vendor sources via `tools/fetch_deps.sh`)

| Build | Content |
|---|---|
| `smoke`, `i2ctest`, `uartrx`, `exctest` | probes of the simulator (Steps 0-2, 5) |
| `baseline` | application + fault-injection framework + the nine real faults, **no protection** |
| `fwtest` | baseline with the nine faults unimplemented (Step 3 framework tests) |
| `protected` | baseline + detection (WWDG, CRC, redundant copies, stack canary/painting/context seal, heartbeat, I2C counter, fault handlers) |
| `protfw` | protected with the faults unimplemented (Step 3 suite on the protected code) |
| `recovery` | protected + recovery levels 1-4 (**the "protected firmware" of the final comparison**) |
| `... CPU_STATS=1` | same image plus idle-cycle counters, only for the CPU overhead measurement |

`make unit` runs the host tests (parser, framework, fault wiring, detection logic, recovery policy, and the self-tests of the checkers).

## Reproducing every test

A Wokwi CLI token is needed (`set -Ux WOKWI_CLI_TOKEN <token>` in fish). The free plan has a small monthly CI-minute quota
(roughly 35-50 simulations); each suite below lists its simulation count. Run suites from fish: `fish -c 'bash Tests/run_stepN.sh'`.

| Step | Command | Simulations | Result |
|---|---|---:|---|
| 0-2 smoke, I2C, normal application | `bash Tests/run_step2.sh` | 6 | `step2_acceptance.md` (16 criteria) |
| 3 fault-injection framework | `bash Tests/run_step3.sh` | 6 | `step3_acceptance.md` (52) |
| 4 real fault effects (baseline) | `bash Tests/run_step4.sh` | 18 | `step4_acceptance.md` (63) |
| 5 detection (protected) | `bash Tests/run_step5.sh` | 21 + regression | `step5_acceptance.md` (79) |
| follow-up: timer and GDB mechanisms on study faults, equal-timing single-fault runs | `bash Tests/run_followup.sh` then `python3 Tests/tools/check_followup.py` | 9 + 12 + 27 | `followup_acceptance.md` |
| 6 + 7 recovery and full evaluation | `bash Tests/run_step6.sh` then `python3 Tests/tools/check_step6.py --dir <dir>` and `python3 Tests/tools/eval_step7.py --recovery <dir>` | 27 + 6 | `step6_acceptance.md` (45), `step7_acceptance.md` (48), tables |

`Tests/run_step6.sh` is resumable (`RESUME=1`) and stops with a marker file when the quota runs out.
`Tests/tools/gen_step*_scenarios.py` generate the Wokwi scenario files from `step*_cases.py`.

## Fault matrix

MEM-01 SRAM bit flip, MEM-02 stack corruption, CPU-01 PC corruption, CPU-02 SP corruption, TIM-01 infinite loop, TIM-02 blocked task,
DATA-01 sensor corruption, DATA-02 configuration corruption, PERIPH-01 I2C line held low (custom Wokwi chip `chips/i2c-stuck`).
Added in Steps 5/6 to exercise detectors and escalation (not part of the nine, reported separately): MEM-03 canary overwrite, MEM-04 stack over-use,
CPU-03 synthetic fault-handler invocation, TIM-03 persistent blocked task. Every fault gets an ID `EXP=<fault>_<n>`; injection is by UART command
`FAULT <ID>`. The timer mechanism `FAULT_AT` is wired for the study faults but was exercised in Wokwi only with the framework test fault FI-TEST; GDB injection (`FAULT_GDB`) works for FI-TEST only.

## What the results do and do not show

* Detection coverage is 27/27 for the nine faults **at these injection points** with these detectors; it says nothing about other faults.
  CPU-01/CPU-02 are detected only as hangs by the WWDG shim, because Wokwi never delivers a fault exception.
* Wokwi's WWDG neither resets by itself nor reloads reliably; the WWDG results use a shim driven by a progress token (simulator workaround).
  WWDG latencies are not silicon timings. `RCC_CSR` stays 0, so reset causes come from a `.noinit` breadcrumb.
* The simulation is deterministic: repeated runs give bit-identical cycles, so min/avg/max of latency and recovery time coincide.
* Recovery success rates include the escalation scenarios (designed failures); the study-fault-only rate is printed separately.
* The safe state (actuator held at 100 %) is an engineering choice; the project defines no safety policy.
* CPU overhead is the extra busy time of the same fault-free workload, measured with identical idle-cycle instrumentation in both images.
* Free CI minutes limited how many simulations could be run; Step 2-5 results were obtained on builds that are byte-identical to the ones used later
  (checked by `check_step6.py`, criterion 6.0a).
