#include "fault_catalog.h"
#include "fi_test.h"
#include <string.h>

/* Study faults have no injection routine yet (next step). */
#define STUDY(id_, cls_, name_, target_) {id_, cls_, name_, target_, 0, 0, 0, 0}

const fault_desc_t fault_catalog[] = {
    STUDY("MEM-01",    FAULT_CLASS_MEMORY,     "sram_bit_flip",     "sram_variable"),
    STUDY("MEM-02",    FAULT_CLASS_MEMORY,     "stack_corruption",  "task_stack"),
    STUDY("CPU-01",    FAULT_CLASS_CPU,        "pc_corruption",     "pc"),
    STUDY("CPU-02",    FAULT_CLASS_CPU,        "sp_corruption",     "sp"),
    STUDY("TIM-01",    FAULT_CLASS_TIMING,     "infinite_loop",     "control_task"),
    STUDY("TIM-02",    FAULT_CLASS_TIMING,     "blocked_task",      "sensor_task"),
    STUDY("DATA-01",   FAULT_CLASS_DATA,       "sensor_corruption", "sensor_value"),
    STUDY("DATA-02",   FAULT_CLASS_DATA,       "config_corruption", "control_config"),
    STUDY("PERIPH-01", FAULT_CLASS_PERIPHERAL, "i2c_stuck_low",     "i2c_sda"),
    {"FI-TEST", FAULT_CLASS_TEST, "framework_selftest", "fi_test_target",
     fi_test_inject, fi_test_observe, fi_test_cleanup, fi_test_read},
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
