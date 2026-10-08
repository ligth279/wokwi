#ifndef RECOVERY_H
#define RECOVERY_H

#include "detect.h"
#include "rec_logic.h"
#include <stdint.h>

#ifndef RECOVERY
#define RECOVERY 0
#endif

#if RECOVERY

/* Recovery layer (Step 6, `recovery` build only). Detection (FaultDetection/)
 * reports a fault; this layer picks the level (rec_logic.c), runs the
 * action, verifies that normal operation is actually back and logs
 *   [RECOVERY] EXP=.. attempt=.. level=.. action=.. state=START|COMPLETE|FAILED ...
 * A recovery that is not verified escalates (L1/L2 -> software reset -> safe
 * state); too many attempts in a row end in the safe state (L4), which
 * survives resets until the next power-on.
 *
 * Safe state (engineering choice, the project defines no policy): the
 * sensor and control tasks are not started; a safe task holds the actuator
 * output at its maximum (cooling at 100 %, the fail-safe direction for a
 * cooling controller) and prints [SAFE] lines. */

/* Boot (main.c) */
void rec_boot(uint32_t csr);        /* after det_boot, before det_init: persistence, state restore, boot path */
int  rec_safe_mode(void);           /* 1: main() must call rec_safe_main() instead of starting the application */
void rec_safe_main(void) __attribute__((noreturn));

/* Detection -> recovery (any context) */
void rec_on_detect(det_mech_t m, int task, int sample_fault, const char *exp, uint32_t det_cycle, uint32_t inj_cycle);
int  rec_wwdg_begin(const char *exp, uint32_t det_cycle, uint32_t inj_cycle); /* TIM2 ISR: 1 = reset (hang), safe flag set if the limit was reached */

/* Application hooks */
void rec_note_input(int16_t centi);                     /* control task: last good control input */
int  rec_inline(int16_t *input_centi, int16_t sensor_centi); /* control task: run pending L2 actions; returns 1 if the input was replaced */
void rec_control_done(int16_t input, int16_t output, int cfg_ok); /* control task, end of cycle: verification of L2 */
void rec_step(uint32_t now_ms);                         /* monitor task, every period */
int  rec_active(void);

/* Experiment context kept across resets while an episode is unresolved */
const char *rec_episode_exp(uint32_t *inj_cycle);

/* Shared with the detection layer */
int  det_config_ok(void);
void det_task_rebind(det_task_t t);
void app_restart_task(int which); /* 0 sensor, 1 control, 2 console */

#endif /* RECOVERY */
#endif /* RECOVERY_H */
