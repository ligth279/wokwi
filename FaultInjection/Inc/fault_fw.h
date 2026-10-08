#ifndef FAULT_FW_H
#define FAULT_FW_H

#include "fault_catalog.h"
#include <stdint.h>

/* Fault-injection framework: experiment IDs, lifecycle, trigger mechanisms.
 *
 * Lifecycle of one experiment (one active experiment at a time):
 *
 *   SELECTED -> ARMED -> INJECTED -> OBSERVED -> COMPLETED
 *       \          \          \
 *        +----------+----------+--> ERROR (terminal, with a reason)
 *
 * Experiment ID: EXP=<FAULT-ID>_<seq>, seq = per-fault counter, 3 digits,
 * starting at 001, kept in .noinit so it continues across system resets
 * within one simulation run.
 *
 * Mechanisms and their injection points:
 *   UART   `FAULT <ID>`           injected at the start of the next control
 *                                 cycle (fi_site_control, control task)
 *   TIMER  `FAULT_AT <ID> <ms>`   injected in the TIM4 one-shot ISR, <ms>
 *                                 after arming
 *   GDB    `FAULT_GDB <ID>`, or   GDB halts at fi_gdb_anchor() (called from
 *          a GDB request          the control cycle while armed), modifies
 *          (fi_gdb_req_*, set     the target and writes FI_GDB_DONE to
 *          by GDB at reset)       fi_gdb_mailbox (Tests/gdb/). The request
 *                                 path needs no UART input, so a run started
 *                                 paused under GDB is fully deterministic.
 *
 * The injection cycle is the DWT cycle count at the moment the target is
 * modified (for GDB: when the CPU halted at the anchor; DWT does not advance
 * while halted). Every experiment reaches COMPLETED or ERROR; an experiment
 * cut short by a reset is closed at the next boot with
 * ERROR reason=interrupted_by_reset.
 *
 * Lifecycle events are recorded by whichever context performs them (task or
 * ISR) and printed by the console task (fi_log_pending), so ISRs never
 * block on the UART. */

typedef enum { FI_MECH_UART = 0, FI_MECH_TIMER, FI_MECH_GDB } fi_mech_t;

typedef enum {
    FI_ST_NONE = 0,
    FI_ST_SELECTED,
    FI_ST_ARMED,
    FI_ST_INJECTED,
    FI_ST_OBSERVED,
    FI_ST_COMPLETED,
    FI_ST_ERROR,
} fi_state_t;

typedef enum {
    FI_SELECT_OK = 0,
    FI_SELECT_BUSY, /* another experiment is still running */
} fi_select_t;

#define FI_GDB_DONE    0x6DB0D0E5u
#define FI_GDB_REQ_MAGIC 0x5EC7FA17u

/* Written by GDB while the target is halted at reset (.noinit, so startup
 * code does not clear them): request an experiment by fault ID. */
extern volatile uint32_t fi_gdb_req_magic;
extern char fi_gdb_req_id[16];

/* Written by GDB (Tests/gdb/fi_inject.gdb). */
extern volatile uint32_t fi_gdb_mailbox;
/* Read by GDB to associate its action with the experiment. */
extern char fi_cur_exp_id[24];

void fi_boot(void);       /* before the scheduler starts (logs directly) */
fi_select_t fi_select(const fault_desc_t *f, fi_mech_t mech, uint32_t delay_ms);
void fi_site_control(void);
void fi_poll(void); /* console task: observation of FAULT_F_OBS_CONSOLE faults */
void fi_log_pending(void);
const char *fi_active_exp(void); /* "none" when idle */
/* Detection layer (FaultDetection/): the experiment whose fault was injected
 * most recently. Unlike fi_active_exp() it stays valid after the experiment
 * has completed, until the next fi_select(). Returns an injection serial that
 * changes with every injection (0 = nothing injected since boot). Read-only. */
uint32_t fi_last_injection(const char **exp_id, uint32_t *inject_cycle);
/* If GDB has posted a request, consume it (once) and return its ID text,
 * else NULL. Called by the console task. */
const char *fi_take_gdb_request(void);
void fi_gdb_anchor(void);

#endif /* FAULT_FW_H */
