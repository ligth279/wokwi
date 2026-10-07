# GDB-assisted injection of FI-TEST (framework self-test fault), Step 3.6.
#
# Run by Tests/tools/gdb_inject_run.sh, which starts Wokwi WITHOUT a scenario
# (so the simulation starts halted at reset until GDB connects), sets
# pagination/confirm/non-stop off, connects, then sources this file.
# non-stop must be off before connecting, and connecting before the stub has
# settled fails ("Bogus trace status reply: S02"), so the harness waits.
#
# Flow (fully deterministic, no UART input):
#  1. at reset, post a request for FI-TEST in the .noinit mailbox
#  2. the firmware validates it like a UART command, SELECTs and ARMs the
#     experiment, then calls fi_gdb_anchor() from the next control cycle
#  3. GDB halts there, flips bit 0 of fi_test_target, writes FI_GDB_DONE to
#     fi_gdb_mailbox, removes the breakpoint and resumes the target
# Wokwi's stub has no `detach`, and `disconnect` alone leaves the target
# halted, so the target is resumed with `continue &` before disconnecting.

printf "GDB_ATTACHED pc=0x%08x\n", $pc
set var fi_gdb_req_magic = 0x5EC7FA17
set {char[8]}fi_gdb_req_id = "FI-TEST"
printf "GDB_REQUESTED id=%s\n", fi_gdb_req_id

break fi_gdb_anchor
continue
printf "GDB_HIT exp=%s anchor_cycle=%u target_before=0x%08x\n", fi_cur_exp_id, fi_gdb_anchor_cycle, fi_test_target
set var fi_test_target = fi_test_target ^ 1
set var fi_gdb_mailbox = 0x6DB0D0E5
printf "GDB_INJECTED exp=%s target_after=0x%08x mailbox=0x%08x\n", fi_cur_exp_id, fi_test_target, fi_gdb_mailbox
delete
continue &
disconnect
printf "GDB_DISCONNECTED\n"
