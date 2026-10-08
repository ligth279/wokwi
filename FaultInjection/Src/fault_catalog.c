#include "fault_catalog.h"
#include "fi_test.h"
#include <string.h>

/* The nine study faults. With FI_STUDY_FAULTS=1 (baseline/protected
 * builds) each has its real injection routine from fault_study.c. With 0
 * (the `fwtest` build and the host tests) they are registered without one,
 * so a command ends in ERROR reason=not_implemented and touches nothing -
 * that is the Step 3 framework configuration. */
#ifndef FI_STUDY_FAULTS
#define FI_STUDY_FAULTS 1
#endif
#ifndef PROTECTED
#define PROTECTED 0
#endif
#ifndef RECOVERY
#define RECOVERY 0
#endif
#if PROTECTED && FI_STUDY_FAULTS
#include "detect.h" /* detector validation faults MEM-03, MEM-04, CPU-03 */
#endif

#if FI_STUDY_FAULTS
#include "fault_study.h"
/* id, class, name, target, inject, observe, cleanup, read_target, plan, flags */
#define STUDY(id_, cls_, name_, target_, inj_, obs_, clean_, read_, plan_, flags_) \
    {id_, cls_, name_, target_, inj_, obs_, clean_, read_, plan_, (flags_) | FAULT_F_NO_GDB}
#else
#define STUDY(id_, cls_, name_, target_, inj_, obs_, clean_, read_, plan_, flags_) \
    {id_, cls_, name_, target_, 0, 0, 0, 0, 0, 0}
#endif

const fault_desc_t fault_catalog[] = {
    STUDY("MEM-01",    FAULT_CLASS_MEMORY,     "sram_bit_flip",     "g_config.setpoint_centi",
          FS_MEM01_INJECT, FS_MEM01_OBSERVE, FS_MEM01_CLEANUP, FS_MEM01_READ, 0, 0),
    STUDY("MEM-02",    FAULT_CLASS_MEMORY,     "stack_corruption",  "sensor_task_saved_lr",
          FS_MEM02_INJECT, FS_MEM02_OBSERVE, 0, 0, FS_MEM02_PLAN, FAULT_F_FATAL),
    STUDY("CPU-01",    FAULT_CLASS_CPU,        "pc_corruption",     "pc",
          FS_CPU01_INJECT, 0, 0, 0, FS_CPU01_PLAN, FAULT_F_FATAL),
    STUDY("CPU-02",    FAULT_CLASS_CPU,        "sp_corruption",     "sp",
          FS_CPU02_INJECT, 0, 0, 0, FS_CPU02_PLAN, FAULT_F_FATAL),
    STUDY("TIM-01",    FAULT_CLASS_TIMING,     "infinite_loop",     "control_task",
          FS_TIM01_INJECT, 0, 0, 0, FS_TIM01_PLAN, FAULT_F_FATAL),
    STUDY("TIM-02",    FAULT_CLASS_TIMING,     "blocked_task",      "sensor_task",
          FS_TIM02_INJECT, FS_TIM02_OBSERVE, 0, FS_TIM02_READ, 0, FAULT_F_OBS_CONSOLE),
    STUDY("DATA-01",   FAULT_CLASS_DATA,       "sensor_corruption", "sensor_value",
          FS_DATA01_INJECT, FS_DATA01_OBSERVE, FS_DATA01_CLEANUP, FS_DATA01_READ, 0, 0),
    STUDY("DATA-02",   FAULT_CLASS_DATA,       "config_corruption", "g_config.kp_pct_per_c",
          FS_DATA02_INJECT, FS_DATA02_OBSERVE, FS_DATA02_CLEANUP, FS_DATA02_READ, 0, 0),
    STUDY("PERIPH-01", FAULT_CLASS_PERIPHERAL, "i2c_stuck_low",     "i2c_sda",
          FS_PERIPH01_INJECT, FS_PERIPH01_OBSERVE, 0, FS_PERIPH01_READ, 0, 0),
#if PROTECTED && FI_STUDY_FAULTS
    {"MEM-03", FAULT_CLASS_MEMORY, "canary_overwrite", "sensor_stack_canary",
     fd_mem03_inject, fd_mem03_observe, fd_mem03_cleanup, fd_mem03_read, 0, FAULT_F_NO_GDB},
    {"MEM-04", FAULT_CLASS_MEMORY, "stack_overuse", "control_stack",
     fd_mem04_inject, fd_mem04_observe, fd_mem04_cleanup, fd_mem04_read, 0, FAULT_F_NO_GDB},
    {"CPU-03", FAULT_CLASS_CPU, "fault_handler_selftest", "synthetic_fault_frame",
     fd_cpu03_inject, fd_cpu03_observe, 0, fd_cpu03_read, 0, FAULT_F_NO_GDB},
#if RECOVERY
    {"TIM-03", FAULT_CLASS_TIMING, "persistent_blocked_task", "sensor_task_persistent",
     fd_tim03_inject, fd_tim03_observe, 0, fd_tim03_read, 0, FAULT_F_NO_GDB | FAULT_F_OBS_CONSOLE},
#endif
#endif
    {"FI-TEST", FAULT_CLASS_TEST, "framework_selftest", "fi_test_target",
     fi_test_inject, fi_test_observe, fi_test_cleanup, fi_test_read, 0, 0},
};

const unsigned fault_catalog_count = sizeof fault_catalog / sizeof fault_catalog[0];

const fault_desc_t *fault_find(const char *id)
{
    for (unsigned i = 0; i < fault_catalog_count; i++) {
        if (strcmp(fault_catalog[i].id, id) == 0) {
            return &fault_catalog[i];
        }
    }
    return 0;
}

const char *fault_class_str(fault_class_t cls)
{
    switch (cls) {
    case FAULT_CLASS_MEMORY:     return "MEMORY";
    case FAULT_CLASS_CPU:        return "CPU";
    case FAULT_CLASS_TIMING:     return "TIMING";
    case FAULT_CLASS_DATA:       return "DATA";
    case FAULT_CLASS_PERIPHERAL: return "PERIPHERAL";
    case FAULT_CLASS_TEST:       return "TEST";
    }
    return "?";
}
