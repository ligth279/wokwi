# CLAUDE.md

## Project: Simulation-Based Study of Software Fault Detection and Self-Recovery in STM32 Microcontrollers

This file defines the implementation, testing, logging, and evaluation requirements for the Wokwi-based STM32 fault-injection project.

The project is based on the provided project PDF. Do not silently add unrelated functionality or claim that a feature has been implemented/tested unless it is actually present in the project.

---

# 1. Project Objective

Build a Wokwi simulation of an STM32F103C8 Blue Pill and use software fault injection to study:

1. Fault injection
2. Fault detection
3. Fault recovery
4. Detection coverage
5. Detection latency
6. Recovery success rate
7. Recovery time
8. Flash/RAM/CPU overhead

The central comparison is:

- **Baseline firmware:** no fault-detection/recovery protection; show the raw impact of injected faults.
- **Protected firmware:** the same faults are injected with detection and recovery mechanisms enabled.

The same fault conditions should be reproducible between baseline and protected runs.

---

# 2. Source-of-Truth Requirements

The project PDF specifies:

- Target MCU: STM32F103C8 Blue Pill
- Cortex-M3
- 72 MHz
- 64 KB flash
- 20 KB RAM
- Simulator: Wokwi
- Toolchain: STM32CubeIDE or arm-none-eabi-gcc
- STM32 HAL
- GDB
- Test application:
  - I2C sensor read
  - control loop
  - UART output
  - optionally FreeRTOS

The PDF identifies five fault classes:

1. Memory
2. CPU
3. Timing
4. Data
5. Peripheral

The PDF specifies the fault injection workflow:

1. Select fault
2. Inject fault
3. Observe behavior
4. Log result and timing
5. Repeat

The PDF also requires baseline and protected runs.

---

# 3. Hard Constraints

## 3.1 MCU

Use:

- STM32F103C8
- Cortex-M3
- 72 MHz target configuration

Do not replace the MCU with another STM32 unless the project requirements are explicitly changed.

## 3.2 Simulator

Use Wokwi for the simulation.

The project should be reproducible in Wokwi.

## 3.3 Application

The application must provide enough normal activity to demonstrate faults:

- I2C sensor read
- Control loop
- UART output

FreeRTOS may be used when required for:

- task heartbeat
- blocked-task faults
- task restart recovery

If FreeRTOS is used, keep task responsibilities explicit and deterministic.

---

# 4. Recommended Project Structure

Use a structure similar to:

```text
project/
├── CLAUDE.md
├── README.md
├── wokwi.toml
├── diagram.json
├── Makefile
├── Core/
│   ├── Inc/
│   └── Src/
├── Drivers/
│   ├── Inc/
│   └── Src/
├── App/
│   ├── Inc/
│   └── Src/
├── FaultInjection/
│   ├── Inc/
│   └── Src/
├── FaultDetection/
│   ├── Inc/
│   └── Src/
├── Recovery/
│   ├── Inc/
│   └── Src/
├── Logging/
│   ├── Inc/
│   └── Src/
├── Tests/
│   ├── baseline/
│   └── protected/
└── results/
    ├── raw/
    ├── summaries/
    └── tables/
```

The exact directory names may be adapted to the existing STM32 project.

Do not restructure a working project unnecessarily.

---

# 5. Normal Application

Before any fault injection exists, establish a working baseline application.

Implement:

## 5.1 I2C sensor

Requirements:

- Initialize I2C.
- Periodically read a sensor value.
- Store the value in a known variable.
- Make the value visible through UART.
- Feed the value into the control loop.

The exact sensor is not mandated by the PDF. Choose a Wokwi-supported I2C sensor that makes the experiment reproducible.

## 5.2 Control loop

Implement a deterministic control loop.

It should:

- periodically execute,
- consume sensor data,
- produce a control/output result,
- provide observable UART status.

The control loop must be simple enough that corrupted input produces an observable behavioral difference.

## 5.3 UART

UART is the primary human-readable experiment log.

Print at minimum:

```text
BOOT
SYSTEM_READY
SENSOR value=...
CONTROL value=...
FAULT_INJECT ...
FAULT_DETECTED ...
RECOVERY_START ...
RECOVERY_SUCCESS ...
RESET_CAUSE ...
```

Do not rely exclusively on UART for timing measurements.

---

# 6. Fault Injection Framework

Create one central fault-injection interface.

Conceptually:

```text
fault type
target
injection point/time
fault parameters
```

The injection mechanism should support the mechanisms identified by the PDF:

- timer-triggered injection
- UART-triggered injection
- GDB-assisted injection

The implementation can expose a single command interface.

Example conceptual commands:

```text
FAULT MEM_VAR
FAULT STACK
FAULT PC
FAULT SP
FAULT LOOP
FAULT BLOCK_TASK
FAULT SENSOR
FAULT CONFIG
FAULT I2C
```

The exact command syntax is not mandated by the PDF.

Every injected fault must generate a unique experiment ID.

Example:

```text
EXP=MEM_VAR_001
```

---

# 7. Fault Model

## 7.1 Memory faults

The PDF specifies:

> Firmware flips bits in SRAM variables and the stack.

Required tests:

### MEM-01 — SRAM variable bit flip

Inject a bit flip into an SRAM application variable.

Record:

- variable/target
- original value
- injected value
- bit position
- injection time
- observed behavior
- detection mechanism
- recovery result

### MEM-02 — Stack corruption

Corrupt stack data.

Record:

- injection point
- corruption type
- fault observed
- detection mechanism
- recovery result

Do not claim arbitrary memory corruption coverage. Only report the specific locations/conditions actually tested.

---

# 8. CPU Faults

The PDF specifies:

- corrupted program counter
- corrupted stack pointer

Expected symptoms:

- HardFault
- silent control-flow error

## CPU-01 — Program Counter corruption

Inject PC corruption using an appropriate debugging/fault-injection method.

Record:

- PC before injection
- injected/target condition where observable
- fault exception
- fault registers
- detection latency
- recovery

## CPU-02 — Stack Pointer corruption

Inject SP corruption.

Record:

- SP before injection where available
- fault type
- exception
- fault registers
- recovery

Important:

The Cortex-M3/STM32F103C8 fault architecture must be respected. Do not invent fault status values.

---

# 9. Timing Faults

The PDF specifies:

- forced infinite loops
- blocked tasks

Expected symptom:

- system hang

## TIM-01 — Infinite loop

Force a relevant execution path into:

```c
while (1) {
}
```

The fault should be deterministic.

Verify:

- normal progress stops
- watchdog behavior
- detection
- reset/recovery
- time to detection
- time to recovery

## TIM-02 — Blocked task

If FreeRTOS is used:

- deliberately block/stall a task
- stop its normal heartbeat
- verify heartbeat detection
- attempt task restart

Record:

- task name
- last heartbeat
- detection time
- recovery level
- recovery result

---

# 10. Data Faults

The PDF specifies corruption of:

- sensor values
- configuration data

Expected symptom:

- silent wrong behavior

## DATA-01 — Sensor data corruption

Inject a deliberately incorrect sensor value.

Test:

- normal value
- corrupted value
- resulting control-loop behavior
- integrity detection
- recovery

## DATA-02 — Configuration corruption

Corrupt a configuration value.

Examples may include:

- threshold
- control parameter
- mode/configuration variable

The exact configuration variables must be chosen from the actual implementation.

Record:

- original value
- corrupted value
- detection result
- recovery result

---

# 11. Peripheral Fault

The PDF specifically requires:

> Custom Wokwi chip holds the I2C line low.

## PERIPH-01 — I2C stuck-low

Implement a custom Wokwi peripheral/fault mechanism that forces the relevant I2C line LOW.

Test:

1. Normal I2C operation
2. Activate stuck-low fault
3. Attempt I2C transaction
4. Observe bus behavior
5. Detect timeout/bus hang
6. Trigger recovery
7. Verify post-recovery operation

Record:

- I2C state
- transaction attempted
- timeout/detection
- recovery method
- recovery success

Do not substitute a generic software sensor error for this test; the PDF specifically identifies the I2C-line fault.

---

# 12. Fault Detection

Implement the detection mechanisms specified by the PDF.

## DET-01 — Window Watchdog (WWDG)

Purpose:

Detect software hangs or missed timing windows.

Normal behavior:

```text
task/application executes
        ↓
watchdog is serviced
        ↓
system continues
```

Fault behavior:

```text
infinite loop / hang
        ↓
watchdog is not serviced correctly
        ↓
WWDG reset
```

Test:

- normal WWDG operation
- intentional hang
- watchdog timeout/reset
- reset cause logging

---

# 13. Fault Handlers

The PDF specifies enabling:

- MemManage
- BusFault
- UsageFault

and reading:

- CFSR
- HFSR
- MMFAR
- BFAR

Implement fault handlers that:

1. identify the exception,
2. capture available fault information,
3. log the fault,
4. select an appropriate recovery path.

Example UART output:

```text
FAULT exception=HardFault
CFSR=0x........
HFSR=0x........
MMFAR=0x........
BFAR=0x........
```

Only print registers that are meaningful for the actual fault.

---

# 14. Stack Protection

The PDF specifies:

- stack canaries
- stack painting

The STM32F103C8 does not provide an MPU, so do not implement an MPU-based requirement as if it were part of this project.

## DET-STACK-01 — Stack canary

Implement:

```text
known canary
     ↓
stack usage
     ↓
check canary
```

Inject stack corruption and verify detection.

## DET-STACK-02 — Stack painting

Initialize a known stack region/pattern.

Use it to estimate stack usage and detect abnormal consumption.

Test:

- normal stack usage
- increased usage
- overflow/corruption condition

---

# 15. Data Integrity

The PDF specifies:

- hardware CRC
- redundant variable copies

## DET-DATA-01 — CRC

Use the STM32 hardware CRC peripheral where appropriate.

Protect selected critical data.

Test:

```text
valid data → CRC passes
corrupted data → CRC mismatch
```

Record:

- original data
- corruption
- CRC result
- detection
- recovery

## DET-DATA-02 — Redundant variable copies

Maintain redundant copies of selected critical variables.

Concept:

```text
variable_A
variable_A_copy
       ↓
compare
       ↓
match / mismatch
```

Inject corruption into one copy and verify detection.

Do not claim redundancy protects every variable unless every variable is actually protected.

---

# 16. Task Heartbeat

For each protected RTOS task:

```text
task
 ↓
periodic heartbeat
 ↓
monitor
```

Normal:

```text
heartbeat received
```

Fault:

```text
task stalls
 ↓
heartbeat missing
 ↓
fault detected
```

Test:

- healthy task
- stalled task
- missing heartbeat
- detection
- task restart
- escalation if restart fails

---

# 17. Recovery Levels

The PDF specifies four recovery levels.

## Level 1 — Task restart

Use for task-local failures.

Sequence:

```text
fault
 ↓
detect
 ↓
restart faulty task
 ↓
verify system continues
```

Test with:

- blocked task
- heartbeat failure

Record recovery time.

---

## Level 2 — Checkpoint restore

The PDF specifies restoring saved state from a `.noinit` SRAM section.

Sequence:

```text
normal state
 ↓
checkpoint saved
 ↓
runtime state corrupted
 ↓
fault detected
 ↓
checkpoint restored
 ↓
operation resumes
```

Verify that the restored state is actually the saved state.

Do not claim persistence across power loss; `.noinit` SRAM is not equivalent to nonvolatile storage.

---

## Level 3 — System reset

Use:

- controlled software reset
- WWDG reset

Sequence:

```text
fault
 ↓
detect
 ↓
reset
 ↓
boot
 ↓
read reset cause
 ↓
log
 ↓
resume/recover
```

Test both reset paths separately.

---

## Level 4 — Safe state

If faults repeatedly occur:

```text
fault
 ↓
recovery
 ↓
same fault again
 ↓
recovery failure/repetition
 ↓
safe state
```

The safe state should be explicitly observable.

Examples of safe behavior must be tied to the actual control application. Do not invent a safety policy not defined by the project.

---

# 18. Reset Cause

After every reset:

- read `RCC_CSR`
- determine reset cause
- log the cause
- choose the appropriate recovery path

At minimum distinguish the reset causes that are actually generated by the implementation.

Do not hard-code a reset cause without reading the relevant hardware register.

---

# 19. Experiment IDs

Use stable identifiers.

Recommended:

```text
BASE-T0
MEM-01
MEM-02
CPU-01
CPU-02
TIM-01
TIM-02
DATA-01
DATA-02
PERIPH-01
DET-WWDG
DET-FAULT
DET-CANARY
DET-PAINT
DET-CRC
DET-REDUNDANT
DET-HEARTBEAT
REC-TASK
REC-CHECKPOINT
REC-SOFTRESET
REC-WWDG
REC-SAFE
```

Every experiment should produce machine-readable or clearly structured logs.

---

# 20. Baseline Campaign

The baseline firmware must have fault detection/recovery protections disabled as required for the raw-impact comparison.

Run:

1. MEM-01
2. MEM-02
3. CPU-01
4. CPU-02
5. TIM-01
6. TIM-02 if RTOS is used
7. DATA-01
8. DATA-02
9. PERIPH-01

For each run record:

```text
experiment ID
fault class
fault type
injection condition
observed symptom
crash/hang/wrong-output status
time information if available
```

Expected baseline behavior from the PDF:

- crash
- hang
- silent wrong output/behavior

Do not force a particular result if the actual simulation produces a different result. Report the observed result.

---

# 21. Protected Campaign

Enable the detection and recovery mechanisms.

Repeat the same fault campaign:

1. MEM-01
2. MEM-02
3. CPU-01
4. CPU-02
5. TIM-01
6. TIM-02
7. DATA-01
8. DATA-02
9. PERIPH-01

For every run record:

```text
experiment ID
fault
detection mechanism
detected: yes/no
detection timestamp/cycles
recovery level
recovery started: yes/no
recovery successful: yes/no
recovery time
final system state
reset cause if reset occurred
```

The protected campaign must use the same or equivalent injection conditions as the baseline campaign.

---

# 22. Repeated Runs

Each fault type must be repeated.

For every experiment:

```text
Run 1
Run 2
Run 3
...
```

Use a fixed number of repetitions for the main comparison.

If the project uses a different number, document it explicitly.

Do not mix results from different fault conditions without labeling them.

---

# 23. Timing Measurement

The PDF specifies detection latency in DWT cycles.

Implement DWT cycle measurement where supported by the STM32F103C8/Cortex-M3 environment.

Conceptually:

```text
DWT_start
    ↓
inject fault
    ↓
fault detected
    ↓
DWT_stop
```

Calculate:

```text
detection_latency_cycles =
    detection_cycle - injection_cycle
```

For recovery:

```text
recovery_time =
    recovery_complete_cycle - recovery_start_cycle
```

If Wokwi/debugging limitations prevent a particular measurement from being trustworthy, document the limitation instead of inventing a value.

---

# 24. Required Evaluation Metrics

## 24.1 Detection coverage

Formula:

```text
Detection Coverage =
(number of detected injected faults /
 total injected faults) × 100
```

Calculate:

- per fault class
- overall

Suggested classes:

```text
Memory
CPU
Timing
Data
Peripheral
```

---

## 24.2 Detection latency

Report:

- minimum
- maximum
- average

when enough repeated measurements exist.

Primary unit:

```text
DWT cycles
```

Do not silently convert to milliseconds without documenting the clock frequency and conversion.

---

## 24.3 Recovery success rate

Formula:

```text
Recovery Success Rate =
successful recoveries /
recovery attempts × 100
```

Report:

- per fault
- per recovery level
- overall

---

## 24.4 Recovery time

Measure:

```text
recovery start → recovery complete
```

Report the unit and measurement method.

---

## 24.5 Overhead

Compare baseline/protected builds.

Measure:

- Flash usage
- RAM usage
- CPU-cycle overhead

At minimum provide:

```text
baseline flash
protected flash
difference
percentage overhead
```

and equivalent RAM measurements.

---

# 25. Required Result Table

Produce a table like:

| ID | Fault | Baseline | Detection | Recovery | Detected | Recovered | Detection Latency | Recovery Time |
|---|---|---|---|---|---|---|---:|---:|
| MEM-01 | SRAM bit flip | | | | | | | |
| MEM-02 | Stack corruption | | | | | | | |
| CPU-01 | PC corruption | | | | | | | |
| CPU-02 | SP corruption | | | | | | | |
| TIM-01 | Infinite loop | | | | | | | |
| TIM-02 | Blocked task | | | | | | | |
| DATA-01 | Sensor corruption | | | | | | | |
| DATA-02 | Config corruption | | | | | | | |
| PERIPH-01 | I2C stuck-low | | | | | | | |

Do not fill cells with assumed results.

---

# 26. Detection Coverage Table

Produce:

| Fault Class | Injected | Detected | Coverage |
|---|---:|---:|---:|
| Memory | | | |
| CPU | | | |
| Timing | | | |
| Data | | | |
| Peripheral | | | |
| **Overall** | | | |

---

# 27. Recovery Table

Produce:

| Recovery Level | Faults Tested | Attempts | Successful | Success Rate | Avg. Recovery Time |
|---|---|---:|---:|---:|---:|
| Task restart | | | | | |
| Checkpoint restore | | | | | |
| System reset | | | | | |
| Safe state | | | | | |

---

# 28. Overhead Table

Produce:

| Resource | Baseline | Protected | Difference | Overhead % |
|---|---:|---:|---:|---:|
| Flash | | | | |
| RAM | | | | |
| CPU cycles | | | | |

---

# 29. UART Log Format

Use a consistent format.

Example:

```text
[BOOT] system_start
[APP] system_ready
[SENSOR] value=25
[CONTROL] output=...
[FAULT] id=MEM-01
[FAULT] injected=1
[FAULT] detected=1
[FAULT] mechanism=CRC
[RECOVERY] level=2
[RECOVERY] started=1
[RECOVERY] success=1
[RECOVERY] time_cycles=...
[RESET] cause=...
```

Keep logs easy to parse.

---

# 30. Fault Record Format

Each experiment should produce something equivalent to:

```text
Experiment ID:
Fault class:
Fault type:
Target:
Injection method:
Injection time/cycle:
Expected symptom:
Observed symptom:
Detection mechanism:
Detected:
Detection latency:
Recovery level:
Recovery attempted:
Recovery successful:
Recovery time:
Reset occurred:
Reset cause:
Final state:
Notes:
```

---

# 31. What Counts as a Successful Test

A fault test is not complete just because a fault was injected.

A complete test must establish:

```text
FAULT
  ↓
OBSERVATION
  ↓
DETECTION
  ↓
RECOVERY
  ↓
FINAL STATE
```

For baseline:

```text
FAULT
  ↓
RAW IMPACT
```

For protected:

```text
FAULT
  ↓
DETECTION
  ↓
RECOVERY
  ↓
CONTINUED/SAFE OPERATION
```

---

# 32. Test Completion Criteria

Do not declare the project complete until:

- [ ] Wokwi STM32F103C8 runs normally
- [ ] I2C sensor works
- [ ] Control loop works
- [ ] UART logging works
- [ ] Fault injection framework works
- [ ] Memory fault tested
- [ ] Stack fault tested
- [ ] PC fault tested
- [ ] SP fault tested
- [ ] Infinite-loop fault tested
- [ ] Blocked-task fault tested if RTOS is used
- [ ] Sensor-data corruption tested
- [ ] Configuration corruption tested
- [ ] I2C stuck-low tested
- [ ] WWDG tested
- [ ] Fault handlers tested
- [ ] Stack canary tested
- [ ] Stack painting tested
- [ ] CRC tested
- [ ] Redundant variables tested
- [ ] Task heartbeat tested
- [ ] Task restart tested
- [ ] Checkpoint restore tested
- [ ] Software reset tested
- [ ] WWDG reset tested
- [ ] Safe state tested
- [ ] RCC_CSR reset-cause logging tested
- [ ] Baseline campaign completed
- [ ] Protected campaign completed
- [ ] Repeated runs completed
- [ ] Detection coverage calculated
- [ ] Detection latency measured
- [ ] Recovery success calculated
- [ ] Recovery time measured
- [ ] Flash overhead measured
- [ ] RAM overhead measured
- [ ] CPU overhead measured
- [ ] Final result tables generated

---

# 33. Important Implementation Discipline

## Do not

- invent experimental results
- mark a fault as detected without evidence
- mark recovery as successful merely because the MCU restarted
- claim 100% coverage from a small number of tests
- replace the I2C stuck-low fault with a generic sensor failure
- claim PC/SP corruption was tested if it was only simulated through an unrelated software exception
- claim DWT timing values if they were not actually measured
- mix baseline and protected results
- change the injection conditions between comparison runs without documenting it

## Do

- keep experiments reproducible
- use unique experiment IDs
- log every fault
- repeat each fault
- compare baseline and protected behavior
- record actual observed symptoms
- record actual recovery behavior
- preserve raw logs
- calculate metrics from raw results
- clearly document simulator limitations

---

# 34. Final Deliverables

The completed project should contain:

1. **Wokwi simulation**
2. **STM32 firmware**
3. **Fault-injection implementation**
4. **Fault-detection implementation**
5. **Recovery implementation**
6. **UART experiment logs**
7. **Baseline results**
8. **Protected results**
9. **Detection-coverage results**
10. **Detection-latency results**
11. **Recovery results**
12. **Resource-overhead results**
13. **Final comparison tables**
14. **README describing how to reproduce every test**

---

# 35. Minimal Required Fault Matrix

The core fault matrix is:

```text
MEMORY
 ├── SRAM variable bit flip
 └── Stack corruption

CPU
 ├── Program Counter corruption
 └── Stack Pointer corruption

TIMING
 ├── Infinite loop
 └── Blocked task

DATA
 ├── Sensor data corruption
 └── Configuration data corruption

PERIPHERAL
 └── I2C line held LOW
```

Then apply:

```text
DETECTION
 ├── WWDG
 ├── Fault handlers
 ├── Stack canary
 ├── Stack painting
 ├── CRC
 ├── Redundant variables
 └── Task heartbeat

RECOVERY
 ├── Task restart
 ├── Checkpoint restore
 ├── Software/system reset
 ├── WWDG reset
 └── Safe state

MEASUREMENT
 ├── Detection coverage
 ├── Detection latency
 ├── Recovery success
 ├── Recovery time
 └── Flash/RAM/CPU overhead
```

This is the implementation/test plan to follow. The fault classes, injection workflow, detection mechanisms, recovery levels, and evaluation metrics above are derived from the supplied project PDF; implementation details not explicitly specified by the PDF should be treated as engineering choices and documented as such.
