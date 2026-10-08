# Step 6 acceptance - recovery (build `recovery`)

Runs: results/raw/step6/20261008_144835 (9 scenarios x 3 simulations). Detection from Step 5 plus recovery levels 1-4.
Verdicts: PASS = met; FAIL = not met; LIMIT = cannot be met as written in Wokwi (evidence shows what was observed instead).


## 6.0

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 6.0a | Earlier builds (baseline, fwtest, protected, protfw) unchanged by the recovery code | PASS | build/baseline, fwtest, protected, protfw are byte-identical to the images verified in Steps 2-5 (sha256 prefix, size): baseline f2569e18e5833751=same, fwtest d20d19978390dab3=same, protected 9a75adf8d4cf7a76=same, protfw 4100d28ee0bcfe7c=same - so the Step 2-5 simulation results still apply to them |

## 6.1

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 6.1a | A detected task fault can trigger task restart | PASS | detections that start a task restart: TIM-02->HEARTBEAT, MEM-02->STACK_SEAL, MEM-03->STACK_CANARY, MEM-04->STACK_PAINT (3/3 runs each) |
| 6.1b | Only the faulty task is restarted | PASS | between the START and COMPLETE of every restart exactly one `[TASK] name=<task> state=started` line appears, for the faulty task only (sensor for TIM-02/MEM-02/MEM-03, control for MEM-04) |
| 6.1c | Other healthy tasks continue operating | PASS | console_hb and the monitor's mon_runs keep advancing across every restart, the console/monitor tasks are never restarted; (sensor restart: control is idle only while it has no input) |
| 6.1d | The restarted task returns to normal execution | PASS | after COMPLETE the restarted task works: 10 consecutive SENSOR records, status OK, seq continuing (sensor) / 10 CONTROL records following the control law (control), in 12/12 restarts |
| 6.1e | Sensor/control/UART operation resumes correctly | PASS | sensor, control and UART (CONTROL/SENSOR/STATUS lines) all resume after each of the 12 restarts; CONTROL values follow the law |
| 6.1f | Recovery is associated with the correct EXP | PASS | every RECOVERY START/COMPLETE line carries the EXP of the injected experiment (EXP=<fault>_001) |
| 6.1g | Recovery start and completion cycles are recorded | PASS | start_cycle and end_cycle (DWT) recorded, time_cycles = end - start (run 1: {'TIM-02': 24833338, 'MEM-02': 25112052, 'MEM-03': 24499728, 'MEM-04': 18522792}) |
| 6.1h | Success only when normal operation is actually restored | PASS | success=1 appears only after >=3 healthy records of the restarted task were produced between START and COMPLETE (12/12); negative case TIM-03 (restart cannot cure the fault): 2 restart attempts end success=0 with no SENSOR record in between |

## 6.2

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 6.2a | Required application state is saved as a checkpoint | PASS | checkpoint (g_config x4 fields + last good input + seq + CRC, taken every 500 ms while the configuration verifies) is valid at the moment of the fault: EXP=MEM-01_001 attempt=1 action=config_restore checkpoint_valid=1 cp_seq=5 restored_setpoint=2200 restored_kp=15 |
| 6.2b | Checkpoint is stored in the .noinit SRAM section | PASS | the persistent record `P` (checkpoint inside) is at 0x200038C8 in .noinit [0x20003844, 0x20003978) (nm of the firmware image) |
| 6.2c | Runtime state can be deliberately corrupted | PASS | runtime state is corrupted by MEM-01 (setpoint bit 10), DATA-02 (kp 15->100) and DATA-01 (control input 8500) - INJECTED before/after differ in 9/9 runs |
| 6.2d | Corruption is detected | PASS | detected by CRC/REDUNDANT in 9/9 runs: MEM-01=CRC+REDUNDANT, DATA-02=CRC+REDUNDANT, DATA-01=CRC |
| 6.2e | Saved state is restored | PASS | saved state restored: config_restore from checkpoint (restored_setpoint=2200, restored_kp=15) and sample_restore (corrupted_input=8500 -> restored from checkpoint) in all runs |
| 6.2f | Application resumes using the restored state | PASS | after the restore the next 6 CONTROL records follow the control law with the nominal configuration (setpoint 2200, kp 15) in 6/6 config runs; sample runs: input back to the sensor value |
| 6.2g | Restored state matches the saved checkpoint | PASS | restored values equal the checkpointed ones and the pre-injection values (setpoint 2200, kp 15 = the INJECTED `before=`); the CRC and redundant copies of the restored configuration verify (no further DETECT after the restore) |
| 6.2h | Recovery start/completion is logged | PASS | RECOVERY ... state=START and state=COMPLETE logged for every restore (start_cycle/end_cycle fields) |
| 6.2i | Recovery time is recorded | PASS | recovery time recorded: time_cycles = end - start; run 1: MEM-01=11777526, DATA-02=11782616 |
| 6.2j | Success only after correct operation resumes | PASS | COMPLETE success=1 only after 3 consecutive good control cycles with output = law(checkpoint config); the CONTROL records between START and COMPLETE follow the law |

## 6.3

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 6.3a | SW reset: a detected severe fault triggers a controlled software reset | PASS | CPU-03 (fault-handler path, synthetic frame) -> DETECT FAULT_HANDLER -> RECOVERY level=3 action=software_reset in 3/3 runs: EXP=CPU-03_001 attempt=1 level=3 action=software_reset mech=FAULT_HANDLER task=none state=START start_cycle=196205049 det_cycle=193284294 in |
| 6.3b | SW reset: MCU resets successfully | PASS | second BOOT line after the reset (NVIC_SystemReset from the monitor task) in 3/3 runs |
| 6.3c | SW reset: firmware boots normally afterwards | PASS | after the reset: framework ready, 4 tasks started, system_ready, SENSOR/CONTROL records resume |
| 6.3d | SW reset: RCC_CSR is read after reboot | PASS | RCC_CSR is read at every boot and printed (`[RESET] cause=NONE csr=0x00000000`); Wokwi leaves it 0 (docs/SIMULATOR_LIMITATIONS.md section 3) |
| 6.3e | SW reset: reset cause is correctly logged | PASS | cause SOFTWARE logged against the experiment: breadcrumb=SOFTWARE cause_resolved=SOFTWARE source=breadcrumb csr=0x00000000 exp=CPU-03_001 mech=FAULT_HANDLER det_cycle=193284294 latency_cycles=4565 (source=breadcrumb, because RCC_CSR reads 0) |
| 6.3f | SW reset: required application state is restored | PASS | g_config restored from the .noinit checkpoint at boot (`state_restored from=checkpoint`) and the tasks run again |
| 6.3g | SW reset: recovery completion is recorded | PASS | COMPLETE success=1 with start/end cycle and time_cycles across the reset (DWT keeps counting through a reset in Wokwi; on silicon CYCCNT restarts, so this time is simulator-valid) |
| 6.3h | WWDG reset: a hang can cause a WWDG reset | PASS | TIM-01 (hang) and, as a hang, CPU-01 and CPU-02 cause the WWDG shim to reset the MCU: RECOVERY level=3 action=wwdg_reset in 9/9 runs (WWDG results: simulator workaround) |
| 6.3i | WWDG reset: MCU reboots successfully | PASS | reboot after every WWDG reset, 2 BOOT lines, recovery verified success=1 (tasks running) |
| 6.3j | WWDG reset: RCC_CSR identifies the reset | LIMIT | RCC_CSR is read after the reboot (csr=0x00000000) but never contains the WWDG flag in Wokwi, so it cannot identify the reset; the cause comes from the .noinit breadcrumb (cause_resolved=WWDG) |
| 6.3k | WWDG reset: reset cause logged against the experiment | PASS | `[RESET] breadcrumb=WWDG cause_resolved=WWDG ... exp=<fault>_001` for TIM-01, CPU-01, CPU-02 in 9/9 runs |
| 6.3l | WWDG reset: firmware returns to the expected operating state | PASS | after the reset the application is in its expected operating state: 10 CONTROL records follow the law, configuration restored from the checkpoint |

## 6.4

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 6.4a | Repeated faults can be detected | PASS | TIM-01 injected 4 times in a row (FAULT TIM-01 after each recovery): WWDG detections per run [4, 4, 4]; the 4th is the one that ends in the safe state |
| 6.4b | Repeated recovery failure is recognised | PASS | TIM-03 (a fault that survives restart and reset): recovery attempts fail and are recognised - attempt 1 level 1 task_restart_sensor -> FAILED(no_normal_operation_within_timeout), attempt 2 level 3 software_reset -> FAILED(fault_repeated_before_verified), attempt 3 level 1 task_restart_sensor -> FAILED(no_normal_operation_within_timeout) |
| 6.4c | The system enters a defined safe/fallback state | PASS | defined safe state: sensor and control tasks are not started, a safe task holds the actuator output at 100 % (out_max); engineering choice, the project defines no safety policy. state=ENTERED reason=repeated_faults exp=TIM-01_004 level=4 output=100 attempts=4 escalation_to_level_4=1 |
| 6.4d | Safe-state entry is logged | PASS | `[SAFE] state=ENTERING reason=...` before the reset and `[SAFE] state=ENTERED ...` after it, in 6/6 runs |
| 6.4e | The system does not continue normal operation in the safe state | PASS | after ENTERED there is no SENSOR or CONTROL record and every HOLD line says normal_operation=0 |
| 6.4f | Safe state remains stable | PASS | safe state stable: >= 4 HOLD lines (1 s apart, output=100), no further reset or BOOT, in 6/6 runs |
| 6.4g | The experiment records that escalation to Level 4 occurred | PASS | `escalation_to_level_4=1` in the ENTERED line and a RECOVERY level=4 action=safe_state COMPLETE success=1 (state stable for 2 s) in 6/6 runs; reasons: repeated_faults, repeated_recovery_failu |

## 6.5

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 6.5a | After every reset: RCC_CSR is read | PASS | RCC_CSR read and printed after each of the 33 resets observed in the campaign (`[RESET] cause=.. csr=0x...`) |
| 6.5b | Reset cause is identified | PASS | cause identified after each reset: SOFTWARE, WWDG (from the breadcrumb; RCC_CSR itself stays 0 in Wokwi, so hardware-only identification is a LIMIT) |
| 6.5c | Cause is logged | PASS | cause logged on every boot (`[RESET] breadcrumb=.. cause_resolved=.. source=..`) |
| 6.5d | Cause is associated with the experiment ID | PASS | every post-reset RESET line names the experiment (exp=<fault>_001/_00N) that caused it |
| 6.5e | Appropriate recovery path is selected | PASS | boot path selected from the cause/pending state: safe_state, verify_recovery (verify_recovery = check normal operation and close the pending attempt, safe_state = stay out of normal operation) |
| 6.5f | Fault information is preserved where applicable | PASS | fault information preserved across the reset (.noinit): detecting mechanism, det_cycle, latency, pending experiment and level are printed after the reboot |

## 6.6

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 6.6a | No recovery action in a fault-free run | PASS | fault-free recovery build, 3 x 12 s: no DETECT, no recovery attempt, no reset (the recovery layer does not act on its own) |

**44 PASS, 1 LIMIT (simulator), 0 FAIL of 45 criteria.**
