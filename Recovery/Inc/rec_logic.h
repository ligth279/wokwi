#ifndef REC_LOGIC_H
#define REC_LOGIC_H

#include "detect.h"
#include <stdint.h>

/* Hardware-independent recovery policy (host-testable). The firmware glue is
 * Recovery/Src/recovery.c.
 *
 * Levels (CLAUDE.md section 17): 1 task restart, 2 checkpoint restore,
 * 3 system reset (software or WWDG), 4 safe state. A recovery attempt that is
 * not verified escalates to the next level; too many attempts in a row end in
 * the safe state. */

typedef enum {
    REC_ACT_NONE = 0,
    REC_ACT_RESTART_SENSOR,   /* L1 */
    REC_ACT_RESTART_CONTROL,  /* L1 */
    REC_ACT_RESTART_CONSOLE,  /* L1 */
    REC_ACT_BUS_RECOVER,      /* L1: stop the sensor task, clock the I2C bus clear, restart the task */
    REC_ACT_RESTORE_CONFIG,   /* L2: g_config from the .noinit checkpoint */
    REC_ACT_RESTORE_SAMPLE,   /* L2: last good control input from the checkpoint */
    REC_ACT_SOFT_RESET,       /* L3: controlled software reset */
    REC_ACT_WWDG_RESET,       /* L3: reset by the watchdog (hang) */
    REC_ACT_SAFE_STATE,       /* L4 */
    REC_ACT_COUNT
} rec_action_t;

#define REC_MAX_CONSEC   3u     /* recovery attempts in a row (no healthy window between) before the safe state */
#define REC_HEALTHY_MS   4000u  /* quiet time that ends an episode */

const char *rec_action_name(rec_action_t a);
int         rec_level(rec_action_t a);

/* First response to a detection; REC_ACT_NONE when no recovery applies.
 * task: det_task_t of the affected task, or -1. */
rec_action_t rec_select(det_mech_t mech, int task, int sample_fault);

/* The action to try when the verification of `failed` did not succeed. */
rec_action_t rec_escalate(rec_action_t failed);

/* Policy gate before a recovery attempt: 1 = attempt it, 0 = enter the safe state instead. */
int rec_may_attempt(uint32_t consecutive);

/* Checkpoint of the application state kept in .noinit SRAM. */
typedef struct {
    int16_t  cfg[4];      /* g_config fields */
    int16_t  last_input;  /* last good control input (centi-C) */
    int16_t  pad;
    uint32_t seq;         /* checkpoint number */
    uint32_t crc;         /* CRC-32/MPEG-2 of the words above */
} rec_checkpoint_t;

uint32_t rec_cp_crc(const rec_checkpoint_t *cp);
int      rec_cp_valid(const rec_checkpoint_t *cp);
void     rec_cp_seal(rec_checkpoint_t *cp);

#endif /* REC_LOGIC_H */
