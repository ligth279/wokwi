# Injection conditions: baseline vs protected campaign

Same fault, same target, same corruption, same trigger rule AND the same time in the run: every study fault comes from a single-fault run in which the command `FAULT <ID>` is sent 2.5 s after system_ready, in both builds (Tests/run_followup.sh; faults whose earlier single-fault run already had this timing were reused). The remaining difference is the boot time of the protected build (~44 ms longer: it initialises the detection layer) and the 100 ms control-cycle quantisation.

| Fault | Injected at, baseline (ms after boot) | Injected at, protected (ms after boot) | Same target and corruption | Same time of run |
|---|---:|---:|---|---|
| MEM-01 | 2631 | 2675 | yes | yes |
| MEM-02 | 2631 | 2675 | yes | yes |
| CPU-01 | 2631 | 2675 | yes | yes |
| CPU-02 | 2631 | 2675 | yes | yes |
| TIM-01 | 2631 | 2675 | yes | yes |
| TIM-02 | 2631 | 2675 | yes | yes |
| DATA-01 | 2631 | 2675 | yes | yes |
| DATA-02 | 2631 | 2675 | yes | yes |
| PERIPH-01 | 2631 | 2675 | yes | yes |
