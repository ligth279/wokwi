# GDB-assisted injection of study fault DATA-02 (build `gdbtest`; Step 3.6 mechanism applied to a Step 4 fault).
# Run by Tests/tools/gdb_inject_run.sh (FAULT_ID=DATA-02). The firmware validates the request like a UART command, SELECTs and ARMs
# the experiment and calls fi_gdb_anchor() from the next control cycle; GDB halts there and does the corruption below.
printf "GDB_ATTACHED pc=0x%08x\n", $pc
set var fi_gdb_req_magic = 0x5EC7FA17
set {char[8]}fi_gdb_req_id = "DATA-02"
printf "GDB_REQUESTED id=%s\n", fi_gdb_req_id
break fi_gdb_anchor
continue
printf "GDB_HIT exp=%s anchor_cycle=%u\n", fi_cur_exp_id, fi_gdb_anchor_cycle
printf "GDB_TARGET before kp_pct_per_c=%d\n", g_config.kp_pct_per_c
set var g_config.kp_pct_per_c = 100
set var fi_gdb_mailbox = 0x6DB0D0E5
printf "GDB_INJECTED exp=%s kp_pct_per_c=%d\n", fi_cur_exp_id, g_config.kp_pct_per_c
delete
continue &
disconnect
printf "GDB_DISCONNECTED\n"
