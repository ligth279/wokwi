# Follow-up campaigns: timer and GDB mechanisms on study faults, equal-timing comparison

Root: results/raw/followup

| # | Criterion | Result | Evidence |
|---|---|---|---|
| F1a | Timer mechanism injects the study faults | PASS | `FAULT_AT <ID> 800` injects all nine study faults with mech=TIMER in 3/3 runs each |
| F1b | Timer trigger accuracy | PASS | the injection happens in the TIM4 interrupt within 8979 cycles of the programmed instant (<= 1 ms) (trigger_error_cycles on every INJECTED line) |
| F1c | Timer-injected faults have the same effect as UART-injected ones | PASS | the effect equals the UART-triggered one: wrong control output (MEM-01, DATA-01, DATA-02), failing sensor reads (PERIPH-01), frozen sensor heartbeat (TIM-02), total silence (TIM-01; a loop in the ISR stops everything), simulation ends with code 1006 (MEM-02, CPU-01, CPU-02, injected from the interrupt) |
| F1d | Timer runs repeat identically | PASS | injection cycle identical in the 3 runs of every fault (deterministic) |
| F2a | GDB-assisted injection runs cleanly | PASS | GDB attaches, halts at fi_gdb_anchor(), performs the corruption and detaches cleanly in 12/12 runs (harness result=PASS) |
| F2b | GDB injections are logged with mech=GDB and the experiment ID | PASS | the INJECTED line carries mech=GDB and the experiment ID EXP=<fault>_001 |
| F2c | GDB corruptions equal the Step 4 corruptions | PASS | before/after are the Step 4 corruptions: setpoint 2200 -> 3224, kp 15 -> 100, PC -> (pc^2^29)/1, SP -> sp^2^28 (12/12) |
| F2d | GDB writes the real PC/SP registers (CPU-01/CPU-02) | PASS | CPU-01/CPU-02: the debugger wrote the real PC / SP register (GDB output shows the register after the write equal to the logged `after`) |
| F2e | Behaviour after GDB injection | PASS | MEM-01/DATA-02: experiment OBSERVED and COMPLETED (wrong control output); CPU-01/CPU-02: no STATUS/CONTROL record after INJECTED (the CPU cannot continue), as with the firmware-made fault |
| F2f | GDB runs repeat identically | PASS | injection cycle identical in the 3 runs of every fault |
| F4a | GDB injects study faults into the protected firmware | FAIL | gdb_prot runs missing |
| F4b | GDB corruptions into the protected firmware equal the Step 4 corruptions | FAIL | same corruptions as in the baseline GDB runs (setpoint 2200 -> 3224, kp 15 -> 100, PC and SP corrupted by the debugger) |
| F4c | Detected after GDB injection | FAIL | detected in 12/12 runs: MEM-01=, DATA-02=, CPU-01=, CPU-02= |
| F4d | Recovered after GDB injection | FAIL | recovery after the debugger's injection succeeds in 12/12 runs:  |
| F4e | Normal operation after GDB-injected faults | FAIL | normal operation after the recovery: 6 following CONTROL records follow the control law, no safe state (12/12) |
| F4f | GDB runs on the protected firmware repeat identically | FAIL | injection cycle and recovery time identical in the 3 runs of every fault:  |
| F5a | GDB injection before the first checkpoint: level 2 fails and escalates | PASS | injected by the debugger ~75 ms after the first control cycle (before the first 500 ms checkpoint), level 2 correctly fails with reason=checkpoint_invalid and escalates to a software reset that succeeds (6/6 runs: MEM-01, DATA-02) |
| F5b | The escalation restores normal operation | PASS | the escalation ends in verified normal operation (tasks running after the reset), no safe state - the recovery design degrades correctly; an initial checkpoint at boot would avoid the escalation but would change the verified recovery build |
| F3a | Equal-timing single-fault runs exist for both builds | PASS | single-fault runs with the command at the same time exist for all nine faults in both builds |
| F3b | Baseline and protected faults are injected at the same time of the run | PASS | injection times (ms after boot) baseline / protected: MEM-01 2631/2675, MEM-02 2631/2675, CPU-01 2631/2675, CPU-02 2631/2675, TIM-01 2631/2675, TIM-02 2631/2675, DATA-01 2631/2675, DATA-02 2631/2675, PERIPH-01 2631/2675 - within one control cycle (the protected build boots ~44 ms later) |

**14/20 criteria passed.**
