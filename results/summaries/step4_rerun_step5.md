# Step 4 acceptance - actual fault effects (baseline build)

Runs: results/raw/step4/20261008_135006 (6 scenarios x 3 simulations). No detection or recovery is enabled.


## 4.1

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 4.1a | All 9 study faults have a real injection implementation | PASS | catalog entries with FS_*_INJECT: 9/9; INJECTED event in all 3 runs of each fault: True |
| 4.1b | `FAULT <ID>` invokes the correct fault implementation | PASS | MEM-01:ok; MEM-02:ok; CPU-01:ok; CPU-02:ok; TIM-01:ok; TIM-02:ok; DATA-01:ok; DATA-02:ok; PERIPH-01:ok |
| 4.1c | No study fault remains `not implemented` | PASS | runs containing reason=not_implemented: 0; catalog entries without a routine: [] |
| 4.1d | Each fault has a deterministic injection condition | PASS | injection = next control cycle (<=100 ms after the command) with the same DWT cycle in 3/3 runs: MEM-01:311933985; MEM-02:189535684; CPU-01:189535684; CPU-02:189535684; TIM-01:189535684; TIM-02:189535684; DATA-01:434332320; DATA-02:556730603; PERIPH-01:679130038 |

## 4.2

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 4.2a | MEM-01: a selected SRAM variable is corrupted by flipping a defined bit | PASS | run1: g_config.setpoint_centi 0x898 -> 0xC98, one bit (bit 10) flipped |
| 4.2b | MEM-01: original and corrupted values are logged | PASS | run1: INJECTED before=0x00000898 after=0x00000C98 |
| 4.2c | MEM-01: exactly one corruption per experiment | PASS | one INJECTED per run, COMPLETED inject_count=1; STATUS faults_injected after it: ['2', '2', '2'] (FI-TEST + MEM-01 = 2) |
| 4.2d | MEM-01: the variable corruption is observable | PASS | run1: CONTROL nominal before injection=True; 1 CONTROL records after it match output with setpoint 3224 and differ from nominal, e.g. seq=43 value=0 input=2459 |
| 4.2e | MEM-01: terminates cleanly (fault does not crash/hang the MCU) | PASS | COMPLETED without ERROR in 3/3 runs; CONTROL back to nominal after the harness cleanup: [True, True, True] |

## 4.3

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 4.3a | MEM-02: a defined stack location/data is deliberately corrupted | PASS | run1: sensor task saved LR (stack word) 0x080032B3 -> 0x280032B3: bit 29 flipped; before is a flash code address with Thumb bit |
| 4.3b | MEM-02: corruption occurs at the intended injection point | PASS | injected from the control cycle after the command (ARMED site=control_cycle), sensor stream clean before it, same cycle in 3/3 runs (['189535684']) |
| 4.3c | MEM-02: the resulting behaviour is recorded | PASS | observed behaviour 3/3: after INJECTED the simulation terminates ('code 1006', wokwi-cli exit [1, 1, 1]); records after INJECTED: run1=['SENSOR seq=26 value=2357 sample=26 st', 'CONTROL seq=26 value=23 input=2357']; no STATUS line after it |
| 4.3d | MEM-02: no accidental second fault is introduced | PASS | exactly one INJECTED event per run; no other fault events in the log (second fault: none) |

## 4.4

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 4.4a | CPU-01: PC corruption is actually performed, not simulated by an error print | PASS | fs_cpu01_inject loads the corrupted target into the PC with `bx rN` (disassembly of the ELF: bx found); INJECTED is logged before it and the simulation then terminates (code 1006) 3/3 |
| 4.4b | CPU-01: target PC/register state captured where possible | PASS | run1: PC sampled in the injector 0x08002982; corrupted target 0x28002983 = (PC ^ 2^29) OR 1. Fault registers (CFSR/HFSR) could not be captured: Wokwi ends the simulation before any exception is delivered (see docs/SIMULATOR_LIMITATIONS.md) |
| 4.4c | CPU-01: control-flow/fault behaviour is observable | PASS | observed 3/3: no further serial output after INJECTED and wokwi-cli reports 'code 1006' (exit [1, 1, 1]) |
| 4.4d | CPU-01: logged with its experiment ID | PASS | FAULT command accepted with id=CPU-01 and EXP=CPU-01_001 on every INJECTED line |
| 4.4e | CPU-01: system can be restarted for later experiments | PASS | each run is a fresh simulation: BOOT, framework persist=cold, system_ready in every CPU-01 and MEM-02 run (the crashed state is not carried over; there is no in-simulation reset in the baseline) |

## 4.5

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 4.5a | CPU-02: SP corruption is actually performed | PASS | fs_cpu02_inject loads the corrupted value into SP with `mov sp, rN` (disassembly of the ELF: found); simulation terminates 3/3 |
| 4.5b | CPU-02: target register state captured where possible | PASS | run1: SP before 0x20001120 (inside the 20 KB SRAM) -> 0x30001120 (bit 28 flipped, outside SRAM) |
| 4.5c | CPU-02: fault/control-flow behaviour is observable | PASS | observed 3/3: no further serial output after INJECTED, 'code 1006' (exit [1, 1, 1]); no HardFault/CFSR could be logged |
| 4.5d | CPU-02: logged | PASS | INJECTED logged once per run with EXP=CPU-02_001 |
| 4.5e | CPU-02: system can be restarted | PASS | each run is a fresh simulation with BOOT/persist=cold/system_ready |

## 4.6

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 4.6a | TIM-01: the selected execution path enters an intentional infinite loop | PASS | fs_tim01_inject compiles to a branch-to-self (`b .`) executed in the control task context (target=control_task) |
| 4.6b | TIM-01: normal application progress stops | PASS | after INJECTED no SENSOR/CONTROL/STATUS/CMD line follows in 3/3 runs (control cycles before injection: 25; control_hb at injection 25) |
| 4.6c | TIM-01: distinguishable from an ordinary simulator failure | PASS | hang: wokwi-cli exit [0, 0, 0], 'Scenario completed successfully' (the simulator ran 3000 ms of simulated time past the injection with a silent CPU); crash faults in the same campaign end with 'code 1006' - the two are told apart by simulator status |
| 4.6d | TIM-01: injection event is logged before the hang | PASS | the INJECTED record (target, before/after, cycle) is the last record in every run: it was printed by the injecting task before it entered the loop, and nothing could be printed afterwards |
| 4.6e | TIM-01: firmware can be restarted after the experiment | PASS | each run is a fresh simulation with BOOT/persist=cold/system_ready (restart works; the hang is not carried over) |
| 4.6f | TIM-01: behaviour usable later for WWDG detection testing | PASS | the hang persists for the whole 3000 ms window with no reset and no watchdog (BOOT count 1); a WWDG test can be run on the same fault later |

## 4.7

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 4.7a | TIM-02: a specific FreeRTOS task is deliberately blocked | PASS | run1: name=sensor state=blocked reason=fault_injection exp=TIM-02_001 hb=26 |
| 4.7b | TIM-02: the task stops performing its work | PASS | sensor_hb frozen at 26 over 4 STATUS lines after the block; no SENSOR record after it (3/3 runs) |
| 4.7c | TIM-02: other tasks behave according to the expected scheduler behaviour | PASS | console keeps running (console_hb 265 -> 557, STATUS every second); control, fed only by the sensor queue, stops at control_hb=26 (expected scheduler behaviour: blocked producer -> idle consumer); simulation runs to completion |
| 4.7d | TIM-02: the affected task is identifiable in the log | PASS | INJECTED target=sensor_task and [TASK] name=sensor state=blocked in every run |
| 4.7e | TIM-02: system can be restarted | PASS | each run is a fresh simulation with BOOT/persist=cold/system_ready |
| 4.7f | TIM-02: suitable for the later heartbeat/recovery tests | PASS | the per-task heartbeat counters (sensor_hb frozen, console_hb advancing) separate the stalled task from the healthy one, which is what a later heartbeat monitor needs; COMPLETED reached by the console-side observer in 3/3 runs; the fault stays active (no cleanup) |

## 4.8

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 4.8a | DATA-01: a sensor value is deliberately corrupted | PASS | a sample of 8500 centi-C is injected; the sensor only produces 2200..2800 (run1: after=0x2134) |
| 4.8b | DATA-01: original sensor value recorded | PASS | run1: INJECTED before=2561 equals the sensor's own reading of that sample (SENSOR value=2561) |
| 4.8c | DATA-01: injected value recorded | PASS | INJECTED after=0x00002134 (8500) in 3/3 runs |
| 4.8d | DATA-01: control loop receives the corrupted value | PASS | run1: seq=60 value=100 input=8500 - CONTROL input is 8500, not the SENSOR value 2561 |
| 4.8e | DATA-01: resulting behaviour observable | PASS | run1: output 100 vs 54 it would have been with the real reading |
| 4.8f | DATA-01: exactly one corruption per experiment | PASS | exactly one CONTROL record per run consumed a corrupted input; one INJECTED, COMPLETED inject_count=1 |

## 4.9

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 4.9a | DATA-02: a defined configuration variable is corrupted | PASS | target = g_config.kp_pct_per_c (proportional gain of the control law) |
| 4.9b | DATA-02: original and corrupted values logged | PASS | run1: original 15 -> corrupted 100 |
| 4.9c | DATA-02: corrupted configuration reaches the application logic | PASS | run1: 1 CONTROL records follow the law with kp=100 and differ from the nominal output, e.g. seq=77 value=100 input=2663 |
| 4.9d | DATA-02: resulting behaviour observable | PASS | OBSERVED/COMPLETED by comparing the last control output with the nominal one, 3/3 runs (no ERROR) |
| 4.9e | DATA-02: exactly one corruption per experiment | PASS | exactly one INJECTED and inject_count=1 per run |

## 4.10

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 4.10a | PERIPH-01: stuck-low fault is triggered through the Step 3 framework | PASS | `FAULT PERIPH-01` -> EXP=PERIPH-01_001 INJECTED: trigger line to the i2c-stuck chip 0 -> 1 |
| 4.10b | PERIPH-01: SDA is actually held LOW | PASS | wokwi-cli prints the chip's own '[i2c-stuck] SDA held LOW' once per run ([1, 1, 1]) |
| 4.10c | PERIPH-01: a real I2C transaction fails | PASS | run1: 30 of 31 sensor transactions after the injection fail, first: seq=95 value=2765 sample=0 status=BUS_ERROR cyc=686135038 |
| 4.10d | PERIPH-01: the bus failure is observable | PASS | sensor status != OK and STATUS sensor_err 0 -> 26; OBSERVED/COMPLETED by the firmware 3/3 |
| 4.10e | PERIPH-01: existing 9-clock recovery remains functional | PASS | Step 1 test I2C-RECOVER (transient stuck-low, 9 SCL clocks): ('PASS', 'sda_low_before=1 clocks=9 sda_high_after=1 reinit=1 sensor_ok=1 cycles=80775') |
| 4.10f | PERIPH-01: permanent-fault behaviour remains correct | PASS | Step 1 test I2C-PERM-FAIL (recovery cannot clear a held fault): ('PASS', 'sda_low_before=1 clocks=16 sda_high_after=0 reinit=1 sensor_ok=0 cycles=111319'); Step 4: after the injection every later sensor transaction fails until the end of the run (permanent) |
| 4.10g | PERIPH-01: released-fault recovery remains correct | PASS | Step 1 test I2C-PERM-CLEAR (recovery after the fault is released): ('PASS', 'sda_low_before=1 clocks=8 sda_high_after=1 reinit=1 sensor_ok=1 cycles=76642') |

## 4.11

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 4.11a | Step 0 passes | PASS | Step 0 smoke (re-run by Step 2 suite, criterion 15): PASS |
| 4.11b | Step 1 passes | PASS | Step 1 i2ctest (criterion 16): PASS |
| 4.11c | Step 2 passes | FAIL | Step 2 suite on the build that contains the real faults: 2/16 (exit skipped) |
| 4.11d | Step 3 framework passes | FAIL | Step 3 suite (fwtest build = same framework, study faults unimplemented): 52/52 (exit skipped) |
| 4.11e | Implementing the real faults does not break the existing framework | PASS | host unit tests exit 0 (parser, framework, study wiring, Step 3 checker); FI-TEST_001 still completes through the real UART path in the study build: [True, True, True] |

## 4.12

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 4.12a | Each fault is run at least 3 times | PASS | 3 runs of each of the 6 scenarios (18 simulations), covering all 9 study faults |
| 4.12b | The intended fault occurs in every run | PASS | the intended fault (exactly one INJECTED) occurs in every run of every fault |
| 4.12c | Observed behaviour is consistent | PASS | identical observed behaviour in 3/3 runs (INJECTED fields, post-injection SENSOR/CONTROL/TASK records, crash flag, exit code): MEM-01=same, MEM-02=same, CPU-01=same, CPU-02=same, TIM-01=same, TIM-02=same, DATA-01=same, DATA-02=same, PERIPH-01=same |
| 4.12d | Injection cycle counts are consistent where deterministic | PASS | injection DWT cycle identical in 3/3 runs for every fault: MEM-01=311933985, MEM-02=189535684, CPU-01=189535684, CPU-02=189535684, TIM-01=189535684, TIM-02=189535684, DATA-01=434332320, DATA-02=556730603, PERIPH-01=679130038 |
| 4.12e | No unexplained simulator failures occur | PASS | simulator failures: only the expected 'code 1006' termination after INJECTED for MEM-02/CPU-01/CPU-02 (their defined raw impact); none other |

**61/63 criteria passed.**
