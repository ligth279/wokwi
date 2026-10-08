/* Detector validation faults (protected build only). The nine study faults
 * cannot exercise every detector: MEM-02 corrupts the saved context at the top
 * of a task stack (found by the context seal), the stack guard at the bottom and the
 * high-water mark need their own faults, and no real CPU fault reaches the
 * fault handlers in Wokwi. These three are labelled as detector tests and are
 * not part of the nine study faults.
 *   MEM-03  overwrites the stack canary of the sensor task
 *   MEM-04  makes the control task use ~88 % of its stack (painting high-water)
 *   CPU-03  synthetic invocation of the fault-handler capture path */
#include "detect.h"

#if PROTECTED

#include "app.h"
#include "board.h"

#define MEM04_TARGET_PCT 88u

/* ---- MEM-03 ---------------------------------------------------------------- */
void fd_mem03_inject(uint32_t *b, uint32_t *a)
{
    uint32_t *base = det_stack_base(DET_TASK_SENSOR);
    *b = base[0];
    base[0] = 0xDEADBEEFu;
    *a = base[0];
}
int fd_mem03_observe(void) { return !det_canary_ok(det_stack_base(DET_TASK_SENSOR)); }
void fd_mem03_cleanup(void) { det_stack_base(DET_TASK_SENSOR)[0] = DET_CANARY_WORD; } /* harness cleanup */
uint32_t fd_mem03_read(void) { return det_stack_base(DET_TASK_SENSOR)[0]; }

/* ---- MEM-04 ---------------------------------------------------------------- */
static void __attribute__((noinline)) burn_stack(unsigned bytes)
{
    volatile uint8_t buf[bytes];
    for (unsigned i = 0; i < bytes; i++) {
        buf[i] = (uint8_t)i;
    }
    if (bytes != 0u && buf[bytes - 1u] != (uint8_t)(bytes - 1u)) { /* keep the buffer observably used */
        return;
    }
}

void fd_mem04_inject(uint32_t *b, uint32_t *a)
{
    *b = det_stack_pct_now(DET_TASK_CONTROL);
    unsigned words = det_stack_words(DET_TASK_CONTROL);
    uint32_t *top = det_stack_base(DET_TASK_CONTROL) + words;
    volatile uint32_t here = 0;
    unsigned depth = (unsigned)(top - (uint32_t *)&here);
    unsigned want = ((words - DET_CANARY_WORDS) * MEM04_TARGET_PCT) / 100u;
    if (__get_IPSR() == 0u && want > depth + 16u) {
        burn_stack((want - depth - 16u) * 4u);
    }
    *a = det_stack_pct_now(DET_TASK_CONTROL);
}
int fd_mem04_observe(void) { return det_stack_pct_now(DET_TASK_CONTROL) > DET_STACK_WARN_PCT; }
void fd_mem04_cleanup(void)
{
    volatile uint32_t here = 0;
    det_stack_repaint_dead(DET_TASK_CONTROL, (const void *)&here); /* harness cleanup */
}
uint32_t fd_mem04_read(void) { return det_stack_pct_now(DET_TASK_CONTROL); }

/* ---- CPU-03 ---------------------------------------------------------------- */
void fd_cpu03_inject(uint32_t *b, uint32_t *a)
{
    *b = 0;
    det_selftest_request = 1; /* the monitor task runs the capture path, after INJECTED is recorded */
    *a = 0x00020000u;         /* the synthetic CFSR it will report */
}
int fd_cpu03_observe(void) { return det_fault_selftest_done != 0u; }
uint32_t fd_cpu03_read(void) { return det_fault_selftest_done; }

#endif /* PROTECTED */
