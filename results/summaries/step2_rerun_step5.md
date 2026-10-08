# Step 2 acceptance - normal application (baseline build)

Runs: results/raw/step2/20261008_135748/baseline_run1.log, results/raw/step2/20261008_135748/baseline_run2.log, results/raw/step2/20261008_135748/baseline_run3.log
ELF: build/baseline/firmware.elf

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 1 | FreeRTOS starts successfully | FAIL | runs passing 2/3; run1: scheduler_start + system_ready present |
| 2 | Sensor task runs | FAIL | runs passing 2/3; run1: started=True, sensor_hb over 9 STATUS lines: 10 -> 90 (strictly increasing=True) |
| 3 | Control task runs | FAIL | runs passing 2/3; run1: started=True, control_hb over 9 STATUS lines: 10 -> 90 (strictly increasing=True) |
| 4 | UART task runs | FAIL | runs passing 2/3; run1: started=True, console_hb over 9 STATUS lines: 90 -> 789 (strictly increasing=True) |
| 5 | Sensor value is continuously produced | FAIL | runs passing 2/3; run1: 100 samples (seq 1..100, contiguous=True), >= 85 expected for t=9016 ms, all status=OK=True, sensor_err=0 |
| 6 | Sample counter increases continuously | FAIL | runs passing 2/3; run1: chip sample counter 1 -> 100, +1 every record=True |
| 7 | Control output continuously follows sensor input | FAIL | runs passing 2/3; run1: 100/100 records: input==sensor value and output==clamp((T-2200)*15/100,0,100); temp 2207..2798 -> output 1..89 |
| 8 | Task periods are stable | FAIL | runs passing 2/3; run1: sensor period (DWT cycles): min=7199899 max=7199901 mean=7199900 target=7200000 worst_dev=101 (0.001%); periodic STATUS: 9 lines, intervals ms [997, 1000, 1003], lateness vs 1000 ms grid ms 0..3 (limit 0..5) |
| 9 | UART logging is stable | FAIL | runs passing 2/3; run1: 229 log records, unknown tags=none, non-printable bytes=0, dropped records=0, SENSOR/CONTROL lines 100/100 |
| 10 | UART command input works | FAIL | runs passing 2/3; run1: responses: {'PING': True, 'STATUS': True, 'HELP': True, 'BOGUS': True, 'FAULT NOSUCH': True}, rx_err=0 |
| 11 | No fault protection is enabled yet | FAIL | BOOT: system_start build=baseline protection=0 sysclk=72000000 clock=HSE_PLL; protection functions linked: none; fault vectors not aliased to Default_Handler: none |
| 12 | No unexpected crashes | FAIL | runs passing 2/3; run1: BOOT lines=1 (1 = no reset), fault records=0 |
| 13 | No unexpected hangs | FAIL | runs passing 2/3; run1: last STATUS t=9016 ms, last sample seq=100 (~10000 ms), all heartbeats increasing to the end |
| 14 | 3/3 repeated runs produce the same expected behavior | FAIL | 2/3 runs pass every per-run criterion; first 0 records (seq,value,sample,DWT cycle) identical across runs: True |
| 15 | Step 0 still passes | PASS | summary failures=0 limits=11; FAIL lines=0 |
| 16 | Step 1 still passes | PASS | summary failures=0; FAIL lines=0 |

**2/16 criteria passed.**
