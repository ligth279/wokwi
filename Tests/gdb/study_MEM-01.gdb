# GDB-assisted injection of study fault MEM-01 (build `gdbtest`; Step 3.6 mechanism applied to a Step 4 fault).
# Run by Tests/tools/gdb_inject_run.sh (FAULT_ID=MEM-01). The firmware validates the request like a UART command, SELECTs and ARMs
# the experiment and calls fi_gdb_anchor() from the next control cycle; GDB halts there and does the corruption below.
printf "GDB_ATTACHED pc=0x%08x\n", $pc
set var fi_gdb_req_magic = 0x5EC7FA17
set {char[7]}fi_gdb_req_id = "MEM-01"
printf "GDB_REQUESTED id=%s\n", fi_gdb_req_id
break fi_gdb_anchor
continue
printf "GDB_HIT exp=%s anchor_cycle=%u\n", fi_cur_exp_id, fi_gdb_anchor_cycle
printf "GDB_TARGET before setpoint_centi=%d\n", g_config.setpoint_centi
set var g_config.setpoint_centi = g_config.setpoint_centi ^ 0x400
set var fi_gdb_mailbox = 0x6DB0D0E5
printf "GDB_INJECTED exp=%s setpoint_centi=%d\n", fi_cur_exp_id, g_config.setpoint_centi
delete
continue &
disconnect
printf "GDB_DISCONNECTED\n"
