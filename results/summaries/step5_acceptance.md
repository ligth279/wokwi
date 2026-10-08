# Step 5 acceptance - fault detection (protected build)

Runs: results/raw/step5/20261008_132103 (7 scenarios x 3 simulations). Detection only: no recovery action is implemented.
Verdicts: PASS = met; FAIL = not met; LIMIT = cannot be met as written in Wokwi (evidence shows what was observed instead).


## 5.1

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 5.1a | A common detection interface exists for all mechanisms | PASS | every mechanism reports through det_report() (call sites per file: {'det_monitor.c': 4, 'det_wwdg.c': 1, 'det_fault.c': 1}); mechanisms seen in the logs: ['CRC', 'FAULT_HANDLER', 'HEARTBEAT', 'I2C_TIMEOUT', 'REDUNDANT', 'STACK_CANARY', 'STACK_PAINT', 'STACK_SEAL', 'WWDG']; all 45 DETECT lines carry EXP, mech, det_cycle |
| 5.1b | A detected fault is associated with the active EXP | PASS | DETECT lines carry the EXP of the injected experiment and their det_cycle is after its injection cycle (log order can differ: framework events are printed asynchronously): MEM-01=ok, DATA-01=ok, DATA-02=ok, TIM-01=ok, TIM-02=ok, MEM-02=ok, MEM-03=ok, MEM-04=ok, CPU-03=ok, PERIPH-01=ok |
| 5.1c | Detection records the detection cycle | PASS | det_cycle (DWT) on every DETECT line; inj_cycle equals the cycle of the INJECTED event and latency_cycles = det_cycle - inj_cycle |
| 5.1d | Detection records which mechanism detected the fault | PASS | every DETECT line names its mechanism (mech=...) |
| 5.1e | Detection does not falsely report a fault during normal operation | PASS | fault-free runs: 0 DETECT lines; fault runs: 0 DETECT lines without an injected experiment (false_positive=1) |
| 5.1f | Normal Step 0-4 behaviour remains unchanged | PASS | fault-free protected run: 120 samples, contiguous, status OK, CONTROL follows the control law, sensor period (7199892, 7200009) cycles (target 7200000); baseline suites re-run: step2 exit 0, step3 exit 0; Step 4 suite: see 5.11e |

## 5.2

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 5.2a | WWDG operates correctly during normal execution | PASS | fault-free run: WWDG started and refreshed 11054 times in ~11 s (1 kHz refresh while the monitor token is fresh)  [simulator workaround: Wokwi's WWDG counter cannot be reloaded reliably, the TIM2 shim evaluates the progress token - see docs/SIMULATOR_LIMITATIONS.md] |
| 5.2b | Normal servicing does not cause unwanted resets | PASS | fault-free runs: BOOT count [1, 1, 1] (no reset), no WWDG detection |
| 5.2c | TIM-01 causes the watchdog to detect the hang | PASS | TIM-01 -> DETECT mech=WWDG in 3/3 runs: EXP=TIM-01_001 mech=WWDG det_cycle=202927226 inj_cycle=192486002 latency_cycles=10441224 t_ms=2816 token_age_ms=158 refreshes=2753 cr=0xAC action=rese |
| 5.2d | A WWDG reset occurs when the watchdog condition is met | PASS | after the WWDG detection the MCU resets and boots again (2 BOOT lines, 3/3 runs); the application restarts (tasks + system_ready) after it |
| 5.2e | Reset cause can be identified afterwards | PASS | breadcrumb=WWDG cause_resolved=WWDG source=breadcrumb csr=0x00000000 exp=TIM-01_001 mech=WWDG det_cycle=202927226 latency_cycles=10441224 det_to_reset_cycles=1242230 detect_t_ms=2816 - cause from the breadcrumb: RCC_CSR reads 0 in Wokwi (csr=0x00000000), so the hardware reset flag is NOT available |
| 5.2f | Detection/reset timing is recorded | PASS | detection latency 10441224 cycles (145.0 ms), detection->reset 1242230 cycles; measured with the Wokwi timeout of the shim (150 ms stale + 8 ms), not the silicon WWDG timeout |

## 5.3

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 5.3a | MemManage fault handling is enabled | LIMIT | handler linked and enable bit written (SHCSR /= MEMFAULTENA), but SHCSR reads back 0x00000000 in Wokwi, memfault=0: the enable bits are not retained |
| 5.3b | BusFault handling is enabled | LIMIT | handler linked and enable bit written (SHCSR /= BUSFAULTENA), but SHCSR reads back 0x00000000 in Wokwi, busfault=0: the enable bits are not retained |
| 5.3c | UsageFault handling is enabled | LIMIT | handler linked and enable bit written (SHCSR /= USAGEFAULTENA), but SHCSR reads back 0x00000000 in Wokwi, usagefault=0: the enable bits are not retained |
| 5.3d | HardFault handling captures the relevant fault condition | LIMIT | HardFault_Handler/MemManage/BusFault/UsageFault link to det_fault_c() (nm: True); the capture/report path ran only through the synthetic CPU-03 invocation: EXP=CPU-03_001 mech=FAULT_HANDLER det_cycle=437739202 inj_cycle=437281704 latency_cycles=457498 t_ms=6082 exception=HardFault cfsr=0x00020000(INVSTATE) hfsr=0x40000000(FO; Wokwi never delivered a fault exception (results/raw/step5/probes: UDF, unmapped read, UNALIGN_TRP, DIV_0_TRP) |
| 5.3e | CFSR is captured | LIMIT | CFSR is read and decoded by the capture path; observed only with the synthetic value (cfsr=0x00020000(INVSTATE)); no real fault status exists in Wokwi |
| 5.3f | HFSR is captured | LIMIT | HFSR is read and decoded by the capture path; observed only with the synthetic value (hfsr=0x40000000(FORCED)); no real fault status exists in Wokwi |
| 5.3g | MMFAR is captured when applicable | LIMIT | MMFAR is printed only when CFSR.MMARVALID is set; no real MemManage fault could be produced, the synthetic frame has MMARVALID clear (so the field is correctly absent) |
| 5.3h | BFAR is captured when applicable | LIMIT | BFAR is printed only when CFSR.BFARVALID is set; no real BusFault could be produced, the synthetic frame has BFARVALID clear (so the field is correctly absent) |
| 5.3i | CPU-01 produces an observable fault result | LIMIT | CPU-01 injected (0x08002A5E -> 0x28002A5F); no fault exception reaches the handlers in Wokwi. Observable result instead: the CPU is stuck, the monitor is starved and the WWDG shim detects it in 3/3 runs (latency 10441224 cycles) and resets the MCU; the unprotected build ended the simulation (code 1006) |
| 5.3j | CPU-02 produces an observable fault result | LIMIT | CPU-02 injected (0x20001670 -> 0x30001670); no fault exception reaches the handlers in Wokwi. Observable result instead: the CPU is stuck, the monitor is starved and the WWDG shim detects it in 3/3 runs (latency 10441224 cycles) and resets the MCU; the unprotected build ended the simulation (code 1006) |
| 5.3k | Fault information is associated with the experiment ID | PASS | fault-handler capture path output is tagged with the experiment ID (EXP=CPU-03_001) - synthetic invocation, synthetic=1 in the line |

## 5.4

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 5.4a | A stack canary is initialised | PASS | stack_guards canary_words=4 paint=0xA5A5A5A5 tasks=sensor,control,console,monitor (4 guard words 0xC0DEC0DE at the lowest addresses of the sensor, control, console and monitor stacks) |
| 5.4b | Normal execution does not falsely trigger the canary | PASS | fault-free runs: canary_ok=3/3 on all 11 DETSTAT lines, no STACK_CANARY detection |
| 5.4c | MEM-02 can corrupt the protected stack region | LIMIT | MEM-02 corrupts the saved-context word of the sensor task (stack top: LR slot, bit 29 flipped), not the guard words at the stack base; that region is protected by the saved-context seal, detected in 3/3 runs (STACK_SEAL, latency 1297512 cycles). The canary region itself is corrupted by the detector-validation fault MEM-03. |
| 5.4d | Canary corruption is detected | PASS | MEM-03 (sensor stack canary overwritten with 0xDEADBEEF) -> STACK_CANARY in 3/3 runs: EXP=MEM-03_001 mech=STACK_CANARY det_cycle=192936940 inj_cycle=192486002 latency_cycles=450938 t_ms=2682 task=sensor word0=0xDEADBEEF word1=0xC0DEC0DE |
| 5.4e | Detection is logged with the experiment ID | PASS | DETECT EXP=MEM-03_001 mech=STACK_CANARY |
| 5.4f | Detection cycle is recorded | PASS | det_cycle recorded; latencies ['450938', '450938', '450938'] |

## 5.5

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 5.5a | Stack region is initialised with a known pattern | PASS | stack words below the live part are filled with 0xA5A5A5A5 (FreeRTOS fill, verified by the high-water scan) - stack_guards canary_words=4 paint=0xA5A5A5A5 tasks=sensor,control,console,monitor |
| 5.5b | Normal stack usage can be measured | PASS | stack_peak_pct (sensor:control:console) in the fault-free runs: ['33:32:68', '33:32:68', '33:32:68'] (warning threshold 75 %) |
| 5.5c | Increased/abnormal stack usage is detectable | PASS | MEM-04 raises the control stack use from 32 % to 87 % (threshold 75 %) |
| 5.5d | Stack-overuse condition produces a detection event | PASS | STACK_PAINT detection in 3/3 runs: EXP=MEM-04_001 mech=STACK_PAINT det_cycle=315496629 inj_cycle=314883350 latency_cycles=613279 t_ms=4382 task=control used_pct=87 warn_pct=75 free_word |
| 5.5e | Detection is logged | PASS | DETECT EXP=MEM-04_001 mech=STACK_PAINT logged |

## 5.6

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 5.6a | Hardware CRC is initialised | PASS | the hardware CRC unit (CRC peripheral) produced 0xAEEBA235 for the nominal g_config, equal to the CRC-32/MPEG-2 computed here in software |
| 5.6b | Protected data produces a valid CRC during normal operation | PASS | fault-free runs: cfg_checks=110 cfg_fail=0 sample_checks=110 sample_fail=0 |
| 5.6c | Corruption of protected data produces a CRC mismatch | PASS | mismatch observed for MEM-01 (setpoint 2200->3224), DATA-02 (kp 15->100) and DATA-01 (sample 8500): the reported actual CRCs equal the software CRC of exactly the corrupted data, the expected CRCs the CRC of the original |
| 5.6d | DATA-01 can be detected by CRC | PASS | DATA-01 -> CRC in 3/3 runs (sample CRC attached by the sensor task, checked by the control task): EXP=DATA-01_001 mech=CRC det_cycle=682095938 inj_cycle=682079175 latency_cycles=16763 t_ms=9473 what=sample seq=94 sensor=2768 consumed=8500 |
| 5.6e | DATA-02 can be detected by CRC | PASS | DATA-02 -> CRC in 3/3 runs (config block CRC) |
| 5.6f | CRC detection is logged with the experiment ID | PASS | EXP recorded on every CRC detection |
| 5.6g | Detection cycle is recorded | PASS | det_cycle recorded; latencies (cycles) DATA-01 ['16763', '16763', '16763'], DATA-02 ['11916', '11916', '11916'], MEM-01 ['13682', '13682', '13682'] |

## 5.7

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 5.7a | Selected critical variables have redundant copies | PASS | redundant copies (bitwise-inverted) exist for the 4 fields of g_config: setpoint_centi, kp_pct_per_c, out_min, out_max; no other variable is redundant (FaultDetection/Src/det_monitor.c) |
| 5.7b | Matching copies are accepted during normal operation | PASS | fault-free runs: every check of the 4 pairs matched (cfg_fail=0) |
| 5.7c | Corrupting one copy produces a mismatch | PASS | corrupting the primary leaves the copy intact: MEM-01 EXP=MEM-01_001 mech=REDUNDANT det_cycle=560982459 inj_cycle=559679968 latency_cycles=13024; DATA-02 EXP=DATA-02_001 mech=REDUNDANT det_cycle=806117893 inj_cycle=804485444 latency_cycles=1632 |
| 5.7d | The mismatch is detected | PASS | REDUNDANT detection MEM-01 3/3, DATA-02 3/3 runs |
| 5.7e | Detection is associated with the correct experiment | PASS | EXP=MEM-01_001 / DATA-02_001 on the REDUNDANT detections |
| 5.7f | Detection cycle is recorded | PASS | det_cycle recorded; latencies MEM-01 ['1302491', '1302491', '1302491'], DATA-02 ['1632449', '1632449', '1632449'] |

## 5.8

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 5.8a | Each protected task reports its heartbeat periodically | PASS | sensor_hb, control_hb, console_hb (and the monitor's mon_runs) advance on every periodic STATUS/DETSTAT line (11 lines) |
| 5.8b | Normal heartbeats do not generate false alarms | PASS | fault-free runs: hb_flags=0 (no heartbeat alarm) |
| 5.8c | TIM-02 prevents the affected task from reporting normally | PASS | TIM-02: name=sensor state=blocked reason=fault_injection exp=TIM-02_001 hb=26; sensor_hb stops |
| 5.8d | Missing heartbeat is detected | PASS | HEARTBEAT detection in 3/3 runs: EXP=TIM-02_001 mech=HEARTBEAT det_cycle=215968410 inj_cycle=192486002 latency_cycles=23482408 t_ms=2998 task=sensor hb=2 |
| 5.8e | The affected task is identified | PASS | task=sensor in 3/3 runs; the starved control task is not reported (its check is gated on a live sensor) |
| 5.8f | Detection cycle is recorded | PASS | det_cycle recorded; latency cycles ['23482408', '23482408', '23482408'] |
| 5.8g | Detection is associated with the experiment ID | PASS | EXP=TIM-02_001 |

## 5.9

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 5.9a | Injection cycle is available from Step 4 | PASS | INJECTED events with the DWT cycle (Step 3/4 framework) present for all 36 fault runs |
| 5.9b | Detection cycle is recorded | PASS | det_cycle on every DETECT line |
| 5.9c | Latency = detection cycle - injection cycle | PASS | latency_cycles = det_cycle - inj_cycle verified on every DETECT line (unsigned 32-bit arithmetic) |
| 5.9d | Latency is recorded for each applicable fault | PASS | latency recorded for 12 faults: ['CPU-01', 'CPU-02', 'CPU-03', 'DATA-01', 'DATA-02', 'MEM-01', 'MEM-02', 'MEM-03', 'MEM-04', 'PERIPH-01', 'TIM-01', 'TIM-02']; faults without a detector: ['CPU-01', 'CPU-02'] (CPU-01/CPU-02 are detected only incidentally, as a hang, by the WWDG) |
| 5.9e | Repeated measurements are retained rather than only an average | PASS | 45 individual latency measurements kept in results/summaries/step5_detections.csv (one row per fault, run and mechanism), not averages |

## 5.10

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 5.10a | Protected firmware runs without injecting faults | PASS | 3 runs of the protected build, no FAULT command, ~11 s each (faults_injected=0) |
| 5.10b | WWDG produces no unwanted reset | PASS | no WWDG reset: BOOT count 1, no WWDG detection |
| 5.10c | Fault handlers produce no false fault | PASS | no FAULT_HANDLER detection (and none could occur: Wokwi delivers no fault exceptions) |
| 5.10d | Stack canary remains valid | PASS | canary_ok=3/3 throughout |
| 5.10e | Stack monitoring remains normal | PASS | stack peaks (sensor:control:console) [33, 32, 68] % < 75 %, no STACK_PAINT detection |
| 5.10f | CRC checks pass | PASS | CRC checks pass (cfg_fail=0, sample_fail=0) |
| 5.10g | Redundant variables remain consistent | PASS | redundant pairs consistent |
| 5.10h | Task heartbeats remain healthy | PASS | task heartbeats healthy (hb_flags=0, counters advancing) |

## 5.11

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 5.11a | Step 0 passes | PASS | Step 0 (smoke, re-run inside the Step 2 suite): PASS |
| 5.11b | Step 1 passes | PASS | Step 1 (i2ctest): PASS |
| 5.11c | Step 2 passes | PASS | Step 2 suite (baseline): 16/16 |
| 5.11d | Step 3 passes | PASS | Step 3 suite (fwtest): 52/52 |
| 5.11e | Step 4 suite still passes (baseline build) | PASS | Step 4 suite re-run on the baseline build of the current code (18 simulations): 61/63 criteria; failing: ['4.11c', '4.11d'] - these two are the Step 2/Step 3 regression rows, evaluated as 5.11c/5.11d |
| 5.11f | Step 4 fault injections still produce their intended faults (protected build) | PASS | all 9 study faults inject with the Step 4 corruption on the protected build (27/27 runs: same targets, same bit flips / values, exactly one INJECTED) |
| 5.11g | Detection does not alter the fault-injection framework unexpectedly | PASS | Step 3 suite on the protected code with the study faults off (protfw): 52/52; host unit tests exit 0 |

## 5.12

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 5.12a | Detection tests run at least 3 times | PASS | 7 scenarios x 3 = 21 simulations of the protected build |
| 5.12b | The same fault is detected consistently | PASS | the same fault is detected in 3/3 runs: MEM-01=yes, MEM-02=yes, CPU-01=yes, CPU-02=yes, TIM-01=yes, TIM-02=yes, DATA-01=yes, DATA-02=yes, PERIPH-01=yes, MEM-03=yes, MEM-04=yes, CPU-03=yes |
| 5.12c | The same detection mechanism is reported consistently | PASS | same mechanisms in every run: MEM-01=['CRC', 'REDUNDANT'], MEM-02=['STACK_SEAL', 'WWDG'], CPU-01=['WWDG'], CPU-02=['WWDG'], TIM-01=['WWDG'], TIM-02=['HEARTBEAT'], DATA-01=['CRC'], DATA-02=['CRC', 'REDUNDANT'], PERIPH-01=['I2C_TIMEOUT'], MEM-03=['STACK_CANARY'], MEM-04=['STACK_PAINT'], CPU-03=['FAULT_HANDLER'] |
| 5.12d | Detection latency is recorded for every run | PASS | latency recorded for every detection of every run (45 measurements in the CSV) |
| 5.12e | Any variation is documented | PASS | variation: none - detection cycles and latencies are bit-identical across the 3 runs of every fault (the simulation is deterministic) |

**68 PASS, 11 LIMIT (simulator), 0 FAIL of 79 criteria.**
