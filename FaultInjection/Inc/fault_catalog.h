#ifndef FAULT_CATALOG_H
#define FAULT_CATALOG_H

#include <stdint.h>

/* The fault matrix of the study (CLAUDE.md section 35) plus the framework
 * self-test fault FI-TEST. This table is the single source of truth for
 * which IDs the FAULT commands accept and which injection routine runs.
 *
 * A fault whose `inject` is NULL is registered (its ID is valid) but has no
 * injection routine yet; triggering it ends the experiment with
 * ERROR reason=not_implemented, without touching the system. */

typedef enum {
    FAULT_CLASS_MEMORY = 0,
    FAULT_CLASS_CPU,
    FAULT_CLASS_TIMING,
    FAULT_CLASS_DATA,
    FAULT_CLASS_PERIPHERAL,
    FAULT_CLASS_TEST, /* framework self-test, not part of the study */
} fault_class_t;

typedef struct {
    const char   *id;    /* e.g. "MEM-01"                        */
    fault_class_t cls;
    const char   *name;  /* short token, no spaces (log-friendly) */
    const char   *target; /* what the injection modifies (log)   */
    /* Perform the fault. Must be short and safe to call from a task or an
     * ISR. Reports the target value before and after injection. */
    void (*inject)(uint32_t *before, uint32_t *after);
    /* Non-zero once the fault's effect is visible in the system. */
    int (*observe)(void);
    /* Undo the fault after observation (may be NULL). */
    void (*cleanup)(void);
    /* Current value of the target (used to log before/after for GDB
     * injections, which modify the target from outside the firmware). */
    uint32_t (*read_target)(void);
    /* FATAL faults only: report the target's value before and after the
     * injection WITHOUT performing it. The framework records and prints the
     * INJECTED event from these values and only then calls `inject`, which
     * may never return (crash, hang). */
    void (*plan)(uint32_t *before, uint32_t *after);
    uint8_t flags; /* FAULT_F_* */
} fault_desc_t;

/* inject() takes the system (or the injecting task) down: INJECTED must be
 * printed before the injection is performed. Requires `plan`. */
#define FAULT_F_FATAL       0x01u
/* observe() runs from the console task (fi_poll) instead of the control
 * cycle - for faults after which the control task no longer runs. */
#define FAULT_F_OBS_CONSOLE 0x02u
/* Not injectable through the GDB mechanism (the GDB script only knows how to
 * modify the FI-TEST target). */
#define FAULT_F_NO_GDB      0x04u

#define FAULT_ID_MAX_LEN 12u

extern const fault_desc_t fault_catalog[];
extern const unsigned     fault_catalog_count;

const fault_desc_t *fault_find(const char *id); /* exact, case-sensitive; NULL if absent */
const char *fault_class_str(fault_class_t cls);

#endif /* FAULT_CATALOG_H */
