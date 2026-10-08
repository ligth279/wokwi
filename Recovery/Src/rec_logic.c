#include "rec_logic.h"
#include <stddef.h>
#include <string.h>

static const char *const names[REC_ACT_COUNT] = {
    "none", "task_restart_sensor", "task_restart_control", "task_restart_console", "i2c_bus_recovery",
    "config_restore", "sample_restore", "software_reset", "wwdg_reset", "safe_state"};

const char *rec_action_name(rec_action_t a)
{
    return (unsigned)a < REC_ACT_COUNT ? names[a] : "?";
}

int rec_level(rec_action_t a)
{
    switch (a) {
    case REC_ACT_RESTART_SENSOR:
    case REC_ACT_RESTART_CONTROL:
    case REC_ACT_RESTART_CONSOLE:
    case REC_ACT_BUS_RECOVER:
        return 1;
    case REC_ACT_RESTORE_CONFIG:
    case REC_ACT_RESTORE_SAMPLE:
        return 2;
    case REC_ACT_SOFT_RESET:
    case REC_ACT_WWDG_RESET:
        return 3;
    case REC_ACT_SAFE_STATE:
        return 4;
    default:
        return 0;
    }
}

rec_action_t rec_select(det_mech_t mech, int task, int sample_fault)
{
    switch (mech) {
    case DET_M_HEARTBEAT:
    case DET_M_STACK_CANARY:
    case DET_M_STACK_SEAL:
    case DET_M_STACK_PAINT:
        return task == DET_TASK_SENSOR    ? REC_ACT_RESTART_SENSOR
               : task == DET_TASK_CONTROL ? REC_ACT_RESTART_CONTROL
               : task == DET_TASK_CONSOLE ? REC_ACT_RESTART_CONSOLE
                                          : REC_ACT_NONE;
    case DET_M_CRC:
    case DET_M_REDUNDANT:
        return sample_fault ? REC_ACT_RESTORE_SAMPLE : REC_ACT_RESTORE_CONFIG;
    case DET_M_I2C_TIMEOUT:
        return REC_ACT_BUS_RECOVER;
    case DET_M_FAULT_HANDLER:
        return REC_ACT_SOFT_RESET;
    case DET_M_WWDG:
        return REC_ACT_WWDG_RESET;
    default:
        return REC_ACT_NONE;
    }
}

rec_action_t rec_escalate(rec_action_t failed)
{
    switch (rec_level(failed)) {
    case 1:
    case 2:
        return REC_ACT_SOFT_RESET;
    case 3:
        return REC_ACT_SAFE_STATE;
    default:
        return REC_ACT_SAFE_STATE;
    }
}

int rec_may_attempt(uint32_t consecutive)
{
    return consecutive < REC_MAX_CONSEC;
}

uint32_t rec_cp_crc(const rec_checkpoint_t *cp)
{
    const uint32_t *w = (const uint32_t *)cp;
    return det_crc32_sw(w, (unsigned)(offsetof(rec_checkpoint_t, crc) / 4u));
}

int rec_cp_valid(const rec_checkpoint_t *cp)
{
    return cp->seq != 0u && rec_cp_crc(cp) == cp->crc;
}

void rec_cp_seal(rec_checkpoint_t *cp)
{
    cp->crc = rec_cp_crc(cp);
}
