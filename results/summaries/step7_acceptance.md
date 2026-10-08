# Step 7 acceptance - full evaluation

Baseline logs: results/raw/step4/20261008_135006; protected logs: results/raw/step6/20261008_144835. Tables: results/tables/final_comparison.md, step7_coverage.md, step7_latency.md, step7_recovery.md, step7_overhead.md.


## 7.1

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 7.1a | Baseline: fault is actually injected | PASS | 27/27 baseline runs contain exactly the INJECTED event of their fault (9 faults x 3 runs, results/raw/step4/20261008_135006) |
| 7.1b | Baseline: raw behaviour is recorded | PASS | raw behaviour recorded for every run |
| 7.1c | Baseline: crash/hang/wrong-output behaviour is recorded | PASS | classified as crash / hang / wrong output / task stall / sensor failure: MEM-01=wrong output, MEM-02=crash, CPU-01=crash, CPU-02=crash, TIM-01=hang, TIM-02=task stall, DATA-01=wrong output, DATA-02=wrong output, PERIPH-01=sensor reads fail |
| 7.1d | Baseline: experiment ID is recorded | PASS | experiment ID EXP=<fault>_001 on every INJECTED line |
| 7.1e | Baseline: injection cycle is recorded | PASS | injection DWT cycle recorded for every run |
| 7.1f | Baseline: no protected recovery is applied | PASS | baseline logs contain no DETECT or RECOVERY lines (protection absent: BOOT protection=0) |

## 7.2

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 7.2a | Protected: same/equivalent fault condition as baseline | PASS | equivalent fault conditions: the same 9 faults, same mechanism (UART, next control cycle), same target and corruption (same before/after or same bit relation). NOT identical in time: injection times differ between the campaigns (MEM-01 4331/2675 ms, MEM-02 2631/27462 ms, CPU-01 2631/2675 ms, CPU-02 2631/2675 ms, TIM-01 2631/2675 ms, TIM-02 2631/32547 ms, DATA-01 6031/7575 ms, DATA-02 7731/12475 ms, PERIPH-01 9431/2675 ms); see results/tables/step7_conditions.md |
| 7.2b | Protected: fault is detected where protection applies | PASS | detected in 27/27 runs; CPU-01/CPU-02 only through the resulting hang (WWDG) |
| 7.2c | Protected: detection mechanism recorded | PASS | mechanism recorded: MEM-01=CRC+REDUNDANT, MEM-02=STACK_SEAL, CPU-01=WWDG, CPU-02=WWDG, TIM-01=WWDG, TIM-02=HEARTBEAT, DATA-01=CRC, DATA-02=CRC+REDUNDANT, PERIPH-01=I2C_TIMEOUT |
| 7.2d | Protected: recovery mechanism recorded | PASS | recovery mechanism recorded: MEM-01=config_restore(L2), MEM-02=task_restart_sensor(L1), CPU-01=wwdg_reset(L3), CPU-02=wwdg_reset(L3), TIM-01=wwdg_reset(L3), TIM-02=task_restart_sensor(L1), DATA-01=sample_restore(L2), DATA-02=config_restore(L2), PERIPH-01=i2c_bus_recovery(L1) |
| 7.2e | Protected: recovery succeeds/fails explicitly | PASS | every recovery attempt ends with an explicit COMPLETE success=1 or FAILED success=0 line |
| 7.2f | Protected: detection latency recorded | PASS | detection latency (cycles) recorded for every injection |
| 7.2g | Protected: recovery time recorded | PASS | recovery time (cycles) recorded for 27 successful attempts of the study faults |
| 7.2h | Protected: final system state recorded | PASS | final system state recorded: MEM-01=normal operation, MEM-02=normal operation, CPU-01=normal operation, CPU-02=normal operation, TIM-01=normal operation, TIM-02=normal operation, DATA-01=normal operation, DATA-02=normal operation, PERIPH-01=normal operation |

## 7.3

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 7.3a | Coverage: every injection has a detection result | PASS | every one of the 27 study injections has a detection result |
| 7.3b | Coverage: detected/not-detected is unambiguous | PASS | detected / not detected is a boolean from the presence of a DETECT line with the experiment's EXP |
| 7.3c | Coverage: per fault class | PASS | coverage per class: MEMORY 6/6, CPU 6/6, TIMING 6/6, DATA 6/6, PERIPHERAL 3/3 |
| 7.3d | Coverage: overall | PASS | overall coverage 27/27 = 100.0 % |
| 7.3e | Coverage: raw counts retained | PASS | raw counts in results/tables/step7_coverage.md and results/summaries/step7_runs.csv |

## 7.4

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 7.4a | Latency: injection cycle recorded | PASS | injection cycle recorded for 51 detections (from the INJECTED event) |
| 7.4b | Latency: detection cycle recorded | PASS | detection cycle recorded |
| 7.4c | Latency: calculated in DWT cycles | PASS | latency = detection cycle - injection cycle verified on every row, in DWT cycles |
| 7.4d | Latency: individual measurements retained | PASS | 51 individual measurements retained (results/summaries/step7_latencies.csv) |
| 7.4e | Latency: average/min/max can be calculated | PASS | min/avg/max computed per fault and mechanism from the retained measurements |

## 7.5

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 7.5a | Recovery rate: every attempt has a result | PASS | every recovery attempt (66) has a success/failure result |
| 7.5b | Recovery rate: success based on actual restoration | PASS | success only with a `verified=` reason (operation actually checked); failures carry a `reason=` |
| 7.5c | Recovery rate: per recovery level | PASS | success rate per level: L1 15/24, L2 9/9, L3 21/27, L4 6/6 |
| 7.5d | Recovery rate: overall | PASS | overall recovery success 51/66 = 77.3 % |

## 7.6

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 7.6a | Recovery time: start recorded | PASS | recovery start cycle recorded for every attempt |
| 7.6b | Recovery time: completion recorded | PASS | recovery completion cycle recorded for every finished attempt |
| 7.6c | Recovery time: calculated | PASS | time_cycles = end - start verified for every successful attempt |
| 7.6d | Recovery time: failed recoveries clearly marked | PASS | 15 failed attempts carry time_cycles=none (no fake time) |

## 7.7

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 7.7a | Flash: baseline recorded | PASS | baseline flash 23000 bytes |
| 7.7b | Flash: protected recorded | PASS | protected flash 37280 bytes |
| 7.7c | Flash: difference | PASS | flash difference +14280 bytes |
| 7.7d | Flash: percentage overhead | PASS | flash overhead 62.1 % |
| 7.7e | RAM: baseline and protected recorded | PASS | RAM baseline 10168, protected 15232 bytes |
| 7.7f | RAM: difference and percentage overhead | PASS | RAM difference +5064 bytes, overhead 49.8 % |
| 7.7g | CPU: baseline and protected cycle cost measured | PASS | baseline and protected CPU cycles measured with the same instrumentation (3 runs each) |
| 7.7h | CPU: difference and percentage overhead | PASS | CPU busy cycles baseline 147 199 377 vs protected 254 162 557: +106 963 180 cycles, 72.7 % more work for the same time window |

## 7.8

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 7.8a | Each fault is run repeatedly | PASS | each study fault run 3x on the baseline and 3x on the protected build (+ validation faults 3x) |
| 7.8b | Baseline runs recorded | PASS | baseline runs recorded (results/raw/step4/20261008_135006) |
| 7.8c | Protected runs recorded | PASS | protected runs recorded (results/raw/step6/20261008_144835) |
| 7.8d | Detection results consistent | PASS | detection results identical across the 3 runs of every fault |
| 7.8e | Recovery results consistent | PASS | recovery results (actions, levels, success) identical across the 3 runs of every fault |
| 7.8f | Nondeterministic behaviour documented | PASS | nondeterminism: none observed - cycles, latencies and recovery times are bit-identical across the repeated runs (Wokwi is deterministic); baseline identical; protected VARIES: ['CPU-01', 'CPU-02', 'TIM-01', 'CPU-03', 'TIM-03'] |
| 7.8g | Raw logs retained | PASS | raw logs retained: results/raw/step4/20261008_135006, results/raw/step6/20261008_144835 |

## 7.9

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 7.9a | Final comparison table produced | PASS | final table has all 9 study faults with the 8 required columns (results/tables/final_comparison.md) |

**48/48 criteria passed.**
