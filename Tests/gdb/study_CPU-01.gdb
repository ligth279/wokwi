# GDB-assisted injection of study fault CPU-01 (build `gdbtest`; Step 3.6 mechanism applied to a Step 4 fault).
# Run by Tests/tools/gdb_inject_run.sh (FAULT_ID=CPU-01). The firmware validates the request like a UART command, SELECTs and ARMs
# the experiment and calls fi_gdb_anchor() from the next control cycle; GDB halts there and does the corruption below.
printf "GDB_ATTACHED pc=0x%08x\n", $pc
set var fi_gdb_req_magic = 0x5EC7FA17
set {char[7]}fi_gdb_req_id = "CPU-01"
printf "GDB_REQUESTED id=%s\n", fi_gdb_req_id
break fi_gdb_anchor
continue
printf "GDB_HIT exp=%s anchor_cycle=%u\n", fi_cur_exp_id, fi_gdb_anchor_cycle
# firmware already printed INJECTED with before = a PC sample and after = (pc ^ 2^29) | 1; load that value into the real PC
printf "GDB_TARGET before pc=0x%08x\n", $pc
set $pc = ('fault_study.c'::cpu01_target & ~1)
printf "GDB_INJECTED exp=%s pc=0x%08x\n", fi_cur_exp_id, $pc
delete
continue &
disconnect
printf "GDB_DISCONNECTED\n"
