# Wokwi Simulator Limitations (measured)

All findings below come from the Step-0 probe firmware `Tests/smoke/smoke_main.c`,
run with `make run BUILD=smoke`.
Raw log: `results/raw/smoke/smoke_20261008_011138.log`.

Environment: Wokwi CLI v0.28.1, Wokwi Simulation API 1.0.0-20261006-g9494a200,
part `board-stm32-bluepill`, arm-none-eabi-gcc 16.2.0.

## Probe results

| Check | Result | Observation |
|---|---|---|
| CLOCK | PASS | HSE 8 MHz × PLL9 → SYSCLK = 72 000 000 Hz |
| UART | PASS | USART1 PA9/PA10 at 115200 baud |
| DWT | PASS | 7 208 806 cycles over a HAL_Delay(100) measured as 101 ms (≈ 72 MHz) |
| CRC | PASS | HW CRC = SW CRC-32/MPEG-2 reference (0x7843B1E3) |
| CRC_DETECT | PASS | 1-bit change → different CRC |
| TIM2_IRQ | PASS | 50 update interrupts in 50 ms at 1 kHz |
| I2C_INIT / I2C_SCAN | PASS | I2C1 initialises; empty bus scan completes in 16 ms |
| WWDG_COUNTER | PASS | down-counter decrements (1739 changes in 200 ms) |
| WWDG_FORCED_RESET | PASS | writing CR = WDGA \| 0x3F (T6 = 0) resets the MCU immediately |
| SOFT_RESET | PASS | `NVIC_SystemReset()` resets the MCU |
| NOINIT_WWDG / NOINIT_SOFT | PASS | `.noinit` SRAM contents survive both reset types |
| **WWDG_PRESCALER** | **LIMIT** | WDGTB bits in WWDG_CFR are not retained (wrote 0x37F, read 0x27F) |
| **WWDG_EWI** | **LIMIT** | EWI bit is retained, but the early-wakeup interrupt never fires |
| **WWDG_AUTO_RESET** | **LIMIT** | no reset when the counter passes 0x40→0x3F (no reset after 200 ms; silicon: 58 ms) |
| **CSR_WWDGRSTF** | **LIMIT** | RCC_CSR = 0x00000000 after a WWDG reset |
| **CSR_SFTRSTF** | **LIMIT** | RCC_CSR = 0x00000000 after a software reset |

Also observed: RCC_CSR is 0x00000000 on cold boot. On silicon, PORRSTF and
PINRSTF would be set.

## Consequences and workarounds (peripherals)

These workarounds are engineering choices. Results that rely on them are
labelled in the result tables.

### 1. WWDG timeout does not reset the MCU

* The WWDG hardware counter does run, and a T6=0 write does produce a reset.
* **Workaround (protected build):** a high-priority 1 kHz TIM2 ISR samples
  `WWDG->CR`. If T6 has cleared (counter ≤ 0x3F, so the window to refresh was
  missed), the ISR records a breadcrumb in `.noinit` and writes
  `WWDG->CR = WDGA | 0x3F`, which triggers the reset that the simulator's WWDG
  does not generate by itself.
* On silicon the hardware reset happens first and the ISR never acts, so the
  same code is valid there.
* Limitation: the workaround needs TIM2 interrupts to keep running. A hang with
  interrupts globally disabled (PRIMASK) or inside a higher-priority ISR would
  be caught by real silicon but not in Wokwi. TIM-01 hangs in thread/task
  context, so it is not affected.

### 2. WWDG prescaler ignored

* The counter runs at PCLK1/4096 = 8.79 kHz (113.8 µs per tick), whatever
  WDGTB is set to.
* The effective timeout from refresh to T6 clearing in Wokwi is
  64 × 113.8 µs = **7.28 ms**. With the configured prescaler 8, silicon would
  give **58.25 ms**.
* WWDG detection latencies measured in Wokwi reflect the 7.28 ms timeout. They
  must not be presented as silicon timings.

### 3. RCC_CSR reset flags are never set

* The firmware still reads RCC_CSR on every boot and logs the raw value
  (CLAUDE.md §18).
* Because Wokwi always reads 0, the firmware also writes a **reset breadcrumb**
  to `.noinit` immediately before every deliberate reset (software reset,
  WWDG trigger). After the reset, both values are logged, for example:
  `[RESET] csr=0x00000000 csr_cause=NONE breadcrumb=WWDG`.
* The breadcrumb is a software record, not a hardware reset-cause register.
  The result tables state which of the two each reset cause came from.

### 4. I2C (found in Step 1, `Tests/i2c/i2c_main.c`)

Raw logs: `results/raw/i2ctest/i2ctest_run{1,2,3}.log`. All 3 runs are
identical, cycle counts included.

* **No repeated START.** `HAL_I2C_Mem_Read` fails with
  `HAL_I2C_WRONG_START` (0x200): after the register byte, SB never sets.
  * **Workaround:** register reads are done as a pointer write followed by
    STOP, then a separate read (`App/Src/sensor.c`). The sensor chip latches
    the pointer, so the data read is the same.
* **IDR does not reflect the pin in AF open-drain mode.** An idle bus reads
  SDA = 0.
  * **Workaround:** `i2c_sda_is_low()` samples the line by briefly switching
    SDA to input-with-pull-up, then back to AF.
  * `I2C-IDLE-LINE` (idle reads high) is checked alongside `I2C-STUCK-LINE`
    (stuck reads low), so the stuck check is not a false positive.
* **The `wokwi-resistor` part does not pull a released open-drain net high.**
  The 4.7 kΩ pull-ups are kept in `diagram.json` for realism. Line sensing
  uses the MCU's internal pull-up.
* **What works at pin level:** the `i2c-stuck` chip holding SDA low really
  breaks MCU I2C transfers. A read returns HAL_ERROR (BUS_ERROR) after
  782 480 cycles (10.9 ms at 72 MHz, with a 10 ms HAL timeout). The bus clear
  frees the line after exactly 9 SCL clocks.
* **Chip output interleaving:** custom-chip `printf` output appears in the
  CLI console interleaved with UART text. It is not in `--serial-log-file`,
  so parse the serial log file, not the console.

### 5. Cortex-M3 core deviations (found in Step 2)

Probed by the `CORE_*` checks in `Tests/smoke/smoke_main.c`.

| Check | Result | Silicon behaviour | Wokwi behaviour |
|---|---|---|---|
| CORE_SVC | PASS | SVC taken | same |
| CORE_EXCRET_PSP / _MSP | PASS | EXC_RETURN 0xFFFFFFFD / 0xFFFFFFF9 switch the thread stack | same |
| CORE_PSP | PASS | CONTROL.SPSEL selects PSP | same |
| CORE_STRB_LDRB / _STRH_LDRH | PASS | byte and halfword access | same |
| CORE_PRIMASK_MSR | PASS | `msr primask` writes PRIMASK | same |
| **CORE_PRIMASK** | LIMIT | `cpsid i` sets PRIMASK=1 and masks IRQs | `cpsid` reads back 0 and `cpsie` reads back 1 (inverted); SysTick and NVIC IRQs are never masked |
| **CORE_SVC_PRIMASK** | LIMIT | SVC with PRIMASK=1 escalates to HardFault | the SVC stays pending until PRIMASK is cleared |
| **CORE_BASEPRI** | LIMIT | BASEPRI masks lower-priority IRQs | reads 0 and masks nothing |
| **CORE_SHPR** | LIMIT | SHPR system-handler priorities are kept | read back 0 |
| **CORE_PENDSV** | LIMIT | one PENDSVSET gives one PendSV | the handler is re-entered on every exception return, forever |
| **CORE_IT_STATE** | LIMIT | IT-block state saved in the stacked xPSR | **lost on exception entry/return**: the remaining instructions of an interrupted IT block run unconditionally (27 279 wrong executions in 200 000 iterations with 53 461 IRQs) |

**Consequences and mitigations**

* **IT state loss.** This silently corrupts any compiled Thumb-2 code that is
  interrupted inside an IT block. It was observed in practice: a ring-buffer
  index advanced on an empty buffer. Even `-O0` emits IT instructions, so no
  ARMv7-M compiler setting avoids it.
  * **Mitigation:** every experiment build (`i2ctest`, `uartrx`, `baseline`,
    `protected`) is compiled with `-mcpu=cortex-m0 -mthumb
    -masm-syntax-unified`. That is the ARMv6-M subset, which the Cortex-M3
    executes natively and which has no IT instruction.
  * After linking, the Makefile checks that the image contains **0** IT
    instructions and fails the build otherwise.
  * The smoke probe alone is built for full ARMv7-M, because it needs
    M3-only instructions to characterise the core.
  * Effect on the study: code size and cycle counts are those of
    ARMv6-M-subset code running at 72 MHz on the M3. They are not identical
    to ARMv7-M code (no hardware divide in compiled code, fewer registers in
    16-bit encodings). Baseline and protected builds use the same ISA, so
    the overhead comparison stays like-for-like.
* **PendSV, BASEPRI, SHPR and PRIMASK.** Stock FreeRTOS cannot run.
  * **Mitigation:** a project-specific FreeRTOS port,
    `RTOS/port_wokwi_cm3/`. The FreeRTOS V11.1.0 kernel itself is unmodified.
  * Tasks switch inside SVC (yield) and SysTick (tick preemption).
  * Critical sections gate the SysTick interrupt (TICKINT), and elapsed ticks
    are replayed from DWT, so the tick count stays exact.
  * Yields inside a critical section or an ISR are deferred.
  * PRIMASK is cleared with MSR before every yield SVC, because the HAL's
    `__enable_irq()` leaves PRIMASK=1 in Wokwi.
  * **Rule:** ISRs other than SysTick must not call FreeRTOS APIs. They
    cannot be masked by kernel critical sections here.
  * Preemption therefore happens at the 1 ms tick or at a yield, never
    straight out of a peripheral ISR.
* **Interrupt masking is unavailable.** A fault scenario of the form "hang
  with interrupts disabled" cannot be reproduced in Wokwi.

### 6. USART receive (found in Step 2)

* The STM32 USART model never decodes bits arriving on its RX pin. RXNE never
  sets, and no FE/NE/ORE either, at any baud rate, with polling or
  interrupts.
* The serial-monitor input does reach PA10 electrically, always at
  **9600 baud** (measured about 7 500 cycles per bit at 72 MHz), whatever
  the MCU's USART is set to. No diagram option changes this.
* **Mitigation:** a software UART receiver, `Logging/Src/soft_uart_rx.c`.
  * An EXTI10 falling edge starts TIM3 at a half-bit period.
  * Each bit is sampled at its midpoint; the start and stop bits are checked
    and framing errors are counted.
  * USART1 still transmits (PA9, 115200 baud).
* Verified by `Tests/uart/uart_rx_main.c` + `Tests/uart/uart_rx.yaml`:
  `41 42 0A 43 0A` received exactly, 0 errors.

### 7. Wokwi CLI artefacts

* **CI-minute quota:** the free plan has a monthly CI-minute limit for
  `wokwi-cli`. It was exhausted on 2026-10-08 during Step 3. Test suites
  are designed to use few, short simulations.

* **`wait-serial` treats its text as a pattern.** `'[APP] system_ready'`
  never matches, because `[APP]` is read as a character class. Wait texts
  in scenarios must not contain `[` or `]`.
* **Matched lines lose their line ending in the log.** In
  `--serial-log-file`, the line that satisfied a `wait-serial` step loses
  its trailing `\r\n`, so the next record joins it on the same line. Log
  parsers must split on `[TAG]` tokens, not on newlines. The checker
  `Tests/tools/check_step2.py` does this.

### 8. GDB server (found in Step 3)

Observed with `wokwi-cli -g <port>` and arm-none-eabi-gdb 18.1.

| Behaviour | Consequence and handling |
|---|---|
| Connecting right after the port opens fails: `Bogus trace status reply: S02` / `Unknown remote qXfer reply` | The harness waits a few seconds after the port opens before connecting. |
| `continue` returns at once and later commands fail with "target is running" unless `set non-stop off` is issued **before** `target remote` | The GDB command file sets pagination, confirm and non-stop off, then connects. |
| `detach` is unsupported ("Remote doesn't know how to detach") | Not used. |
| `disconnect` leaves the target **halted**. The simulation never finishes, and its process keeps the GDB port: a stale session. | Resume with `continue &`, then `disconnect`. The harness also kills `wokwi-cli` after a wall-clock timeout and checks that the port is free before and after each run. |
| Memory-mapped peripherals read as 0 from GDB (e.g. `DWT_CYCCNT` at 0xE0001004) | The injection cycle is taken by the firmware at the GDB anchor (`fi_gdb_anchor_cycle`). DWT does not advance while the CPU is halted. |
| With `--scenario`, the simulation does **not** wait for GDB at reset. Without one, it starts halted until GDB connects. | Deterministic GDB runs use no scenario. GDB requests the experiment through a `.noinit` mailbox (`fi_gdb_req_*`) instead of UART input. |
| Helper processes: `pgrep -f`/`pkill -f` patterns match the invoking shell itself | Use `pgrep -x wokwi-cli` or the PID. |

### 9. Other unsupported peripherals (per Wokwi docs)

* DMA, IWDG, PWR and RTC are listed by Wokwi as unsupported, and DBGMCU is
  missing.
* The project does not use DMA, IWDG, PWR or RTC.

### 10. UART glitch on reset

* A stray byte (0x80) can appear before `[BOOT]` after a reset.
* Log parsers must match `[TAG]` tokens anywhere in a line, not only at
  column 0.

### 11. Fault effects (found in Step 4)

Evidence: the probe runs in `results/raw/step4/probes/` (one run each, see the README there) and the 18 campaign
runs in `results/raw/step4/<timestamp>/`.

| Behaviour | Consequence and handling |
|---|---|
| A corrupted PC or SP that leads to an invalid fetch or stack access **ends the simulation**: `wokwi-cli` reports `API Error: Connection to transport closed unexpectedly: code 1006`. Seen for a PC in unmapped space (0x28xxxxxx), in SRAM (0x20002001) and in erased flash (0x0800A9FA), for SP at 0x30001120, 0x20000120 (still inside SRAM) and 0x00000100, and for a saved return address pointing into erased flash. A probe build with a HardFault register dump printed nothing in any of them. | No exception is delivered, so CFSR/HFSR/MMFAR/BFAR and the CPU state after CPU-01, CPU-02 and MEM-02 cannot be observed in the baseline. The campaign records what is observable: `INJECTED` is the last serial record and the run ends with code 1006 (3/3 runs). The fault-handler detection (Step 5) cannot be demonstrated on these faults in Wokwi unless a corruption is found that the simulator turns into a real exception; none was found. |
| `bx` to an even address (Thumb bit cleared) and a return address with bit 0 cleared do **not** fault. | Bit-0 corruption of PC/LR is a silent control-flow error here, not an INVSTATE fault. It was not used as a study fault. |
| A hang (TIM-01) does **not** end the simulation: `wokwi-cli` exits 0 and the scenario completes. | A hang and a crash are told apart by simulator status: exit 0 and `Scenario completed successfully` versus code 1006. |
| The Wokwi service sometimes answers a connection with HTTP 503 (`Service Unavailable`) before any simulation starts, more often with several `wokwi-cli` processes starting at once. | `Tests/run_step4.sh` retries such a run (the attempt is kept as `*.attemptN.txt`) and runs two simulations at a time. It is an infrastructure failure, not an experiment result. |
| **Serial input can desynchronise the I2C sensor stream.** In the Step 3 scenario (about 35 commands in 6 s) the sensor value starts to drift upwards by ~256 centi-C per sample once, at sample 17, a read returns the chip's "unknown register" byte (`sample=255`). The 3 GDB runs (no serial input) are clean. The cause was not determined; the soft-UART receiver interrupts (EXTI10 + TIM3, every half bit during a byte) overlap polled I2C transfers. | Not fixed in Step 4. The Step 4 campaigns send at most 5 commands, 1.5 s apart, and the checker verifies that every sensor sample before each injection is OK, inside 22-28 C and contiguous (`sensor_clean_before`). Keep command traffic sparse during experiments, and investigate before a step needs many serial commands. |

### 12. Study-fault corruption targets (Step 4)

| Fault | Target and corruption | Raw impact observed in Wokwi |
|---|---|---|
| MEM-01 | `g_config.setpoint_centi`, bit 10 flipped (2200 -> 3224) | output drops to 0 (nominal 38 at that input); no crash |
| MEM-02 | LR slot of the sensor task's saved exception frame (`pxTopOfStack[13]`), bit 29 flipped | task resumes with a bad return address; simulation ends (code 1006) |
| CPU-01 | PC := `(pc ^ 2^29) \| 1` by `bx` | simulation ends (code 1006) |
| CPU-02 | SP := `sp ^ 2^28` by `mov sp` | simulation ends (code 1006) |
| TIM-01 | `for(;;){}` in the injecting context (control task) | serial output stops, simulator keeps running |
| TIM-02 | sensor task blocks on a notification that is never sent | sensor_hb and control_hb freeze, console continues |
| DATA-01 | one control input replaced by 8500 (85.00 C) | one control cycle with output 100 |
| DATA-02 | `g_config.kp_pct_per_c` 15 -> 100 | outputs scaled 6.7x until the harness cleanup |
| PERIPH-01 | PB0 -> `i2c-stuck` chip TRIG, chip holds SDA low | every sensor transaction fails (BUS_ERROR), permanent |

These values are engineering choices (the PDF does not fix them). MEM-01, DATA-01 and DATA-02 are cleaned up by the
experiment harness after the effect is observed (so the next chained experiment starts clean); that is not a recovery
mechanism. The GDB mechanism is not available for the nine study faults (`reason=mechanism_unsupported`); the timer
mechanism is wired for them but was exercised only on the host (stubs), not in Wokwi.

### 13. Fault detection (found in Step 5)

Evidence: `results/raw/step5/probes/` (exception probes) and the Step 5 campaign in `results/raw/step5/<timestamp>/`.

| Behaviour | Consequence and handling |
|---|---|
| **No synchronous fault exception is delivered.** An undefined instruction (`UDF`), a read of unmapped 0x60000000 (returns 0), an unaligned load with `CCR.UNALIGN_TRP` and an `sdiv` by zero with `CCR.DIV_0_TRP` all run on without any exception. `SHCSR` reads back 0 after setting MEMFAULTENA/BUSFAULTENA/USGFAULTENA. | MemManage/BusFault/UsageFault/HardFault handlers are linked and the enable bits are written, but no handler can run, so CFSR/HFSR/MMFAR/BFAR are only exercised through a synthetic frame (fault CPU-03, logged `synthetic=1`). Step 5 criteria 5.3a-h, 5.3i-j are reported as LIMIT. |
| In the protected build a PC/SP-corrupting fault (CPU-01, CPU-02) does not end the simulation (the unprotected build did, code 1006): the CPU is stuck, interrupts keep running, the monitor task is starved and the WWDG shim detects it. | CPU-01/CPU-02 are detected only incidentally, as a hang, by the WWDG. The reason for the different outcome was not investigated. |
| **WWDG counter reload is unreliable.** Writes to `WWDG->CR` after the first one did not reload the counter in a cold-boot probe, so T6 cleared ~8 ms after start whatever the firmware did; the prescaler is ignored (section 2) and the counter never resets the MCU by itself (section 1). | The shim in `FaultDetection/Src/det_wwdg.c` decides from the monitor task's progress token (150 ms stale + 8 ms) and forces the reset with `CR = WDGA\|0x3F`. WWDG latencies are shim latencies, not silicon ones. |
| **A reset written from inside an interrupt handler leaves the core inside that exception.** After the reboot SysTick never fired and no FreeRTOS task ran (boot loop of false WWDG detections). | The TIM2 handler returns into `det_reset_thread()` through a synthetic exception frame on MSP, so the reset is issued from thread mode (this also works when the interrupted task's PSP is corrupt, CPU-02). |
| A forced reset restarts the core but leaves NVIC enables/pending bits, SysTick, peripherals and CONTROL.SPSEL as they were. Without cleaning up, half of the reset runs died (code 1006) right after the second BOOT line. | `reset_machine_state()` at the start of `main()` (protected build only). |
| `DWT->CYCCNT = 0` is not honoured after a reset (the counter keeps counting). | Cycle stamps are monotonic across resets; latencies are differences, so they are unaffected. |
| `RCC_CSR` is 0 after a WWDG reset (section 3). | The reset cause comes from the `.noinit` breadcrumb (`source=breadcrumb`). |
| Infrastructure: HTTP 503 on connect, DNS errors (`EAI_AGAIN`), and a connection that dies before any output (code 1006, empty log) occur from time to time; the free CI quota ran out three times on three tokens (about 35-50 simulations each). | `Tests/run_step5_sims.sh` and `Tests/run_step2.sh` retry such runs (kept as `*.attemptN.txt`). A run that never started is not an experiment result. |

Memory cost of the protected build (measured with `make size`): sensor/control stacks 384/320 -> 512/512 words, one monitor task (400 words), heap 8 -> 12 KB, trace facility on: RAM 10 160 -> 14 824 bytes, flash 23 000 -> 29 604 bytes.

### 14. Recovery (found in Step 6)

Evidence: the Step 6 campaign in `results/raw/step6/<timestamp>/` and the single-run probes described below.

| Behaviour | Consequence and handling |
|---|---|
| **After any reset the serial receiver stays deaf.** The soft UART (EXTI10 on PA10 + TIM3) received nothing after a reset (`rx_bytes=0`, no framing errors) although the same init code works on a cold boot. Not caused by the NVIC/RCC clean-up at boot (variants without either did the same). | In the `recovery` build `soft_uart_rx_init()` first points EXTI10 at port B, then back to port A, so the simulator sees a configuration change; commands work after a reset (`PING` -> `PONG` after a software reset). Baseline and protected builds are untouched. |
| Task restart: a restarted higher-priority task runs immediately and would use the stack that the guards are about to be rebuilt on. | The restart (delete + create + guard rebuild) runs with the scheduler suspended (`vTaskSuspendAll`). |
| After a BUS_ERROR next to serial input the sensor stream could desynchronise (section 11). With the `recovery` build the I2C peripheral is re-initialised after the first error of a streak; the 7-command `g1` scenario then ran without any sensor anomaly (0 of 3 runs, 0 anomalies), whereas the same scenario without it desynchronised. Re-initialising on every failure of a stuck bus made the sensor task overrun its period and starve the monitor, hence only the first error of a streak. | Not a proof that the desync cause is gone; it is a mitigation observed on one scenario. |
| DWT `CYCCNT` keeps counting through a reset (section 13). | Level 3 recovery times (start before the reset, end after the reboot) are valid in Wokwi only; on silicon the counter restarts at reset. |
| `RCC_CSR` stays 0 after WWDG and software resets (section 3). | Reset causes come from the `.noinit` breadcrumb; hardware-only identification (criterion 6.3j) is reported as LIMIT. |

Safe state is an engineering choice (the PDF and the project define no safety policy): after `REC_MAX_CONSEC` = 3 recovery attempts without a 4 s healthy window, or when escalation runs out (L1/L2 -> software reset -> safe state), the system resets into a state that does not start the sensor and control tasks and holds the actuator output at its maximum (100 %, cooling at full power, the fail-safe direction for a cooling controller). It persists across resets until power-on.

### 15. GDB attach (found again in the Step 6 follow-up)

* Roughly 1 in 6 GDB attaches still fails with `Unknown remote qXfer reply: OK` although the harness waits 5 s after the port opens; the simulation then runs
  to its timeout with nothing injected. `Tests/run_gdb_study.sh` retries up to 3 times with a longer settle time (6, 8, 10 s) and keeps the failed attempt as
  `*.attemptN.*` (2 of the 12 study runs needed a second attempt). A failed attach is an infrastructure failure, not an experiment result.
* CPU-01/CPU-02 injected by writing `$pc`/`$sp` in GDB end the simulation exactly like the firmware-made versions (`API Error ... code 1006`, harness accepts exit code 1).

### 16. Fault handlers: not demonstrable in Wokwi

Criterion 5.3 (MemManage/BusFault/UsageFault/HardFault handling, CFSR, HFSR, MMFAR, BFAR, real fault results for CPU-01/CPU-02) is **not demonstrable in Wokwi**:
the simulator does not model the Cortex-M fault exception behaviour it requires (section 13). The handlers and the register capture are implemented and linked; the capture and
reporting path is exercised only with a **synthetic** fault frame (fault CPU-03: made-up register values, logged `synthetic=1`), never with a real exception. No other
simulator was used. MemManage additionally cannot occur on the STM32F103 at all (no MPU), so its handler and MMFAR can never be demonstrated on this part.

### 17. Recovery finding: faults before the first checkpoint

The recovery layer takes its first checkpoint 500 ms after boot. A configuration fault injected earlier (first GDB-assisted runs against the protected firmware,
`results/raw/followup/gdb_prot_early`, injection ~175 ms after boot) is detected by CRC and redundant copy, level 2 fails with `reason=checkpoint_invalid` and the system
escalates to a software reset, which succeeds and restores normal operation (6/6 runs). The design therefore degrades correctly, but it does not protect the first 500 ms
at level 2. An initial checkpoint at boot would remove the gap; it was not added because it would change the recovery build that Steps 6 and 7 verified.
The `gdbprot` build lets the debugger act only after 1.2 s so that the level 2 path itself is exercised.
