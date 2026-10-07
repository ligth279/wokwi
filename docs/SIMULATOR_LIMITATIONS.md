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
