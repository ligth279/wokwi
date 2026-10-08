/* Host test of the recovery policy (Recovery/Src/rec_logic.c). */
#include "rec_logic.h"

#include <stdio.h>
#include <string.h>

static int failures, checks;
#define CHECK(c, ...)                                     \
    do {                                                  \
        checks++;                                         \
        if (!(c)) {                                       \
            failures++;                                   \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)

int main(void)
{
    /* selection per mechanism */
    CHECK(rec_select(DET_M_HEARTBEAT, DET_TASK_SENSOR, 0) == REC_ACT_RESTART_SENSOR, "heartbeat sensor");
    CHECK(rec_select(DET_M_STACK_SEAL, DET_TASK_SENSOR, 0) == REC_ACT_RESTART_SENSOR, "seal sensor");
    CHECK(rec_select(DET_M_STACK_CANARY, DET_TASK_SENSOR, 0) == REC_ACT_RESTART_SENSOR, "canary sensor");
    CHECK(rec_select(DET_M_STACK_PAINT, DET_TASK_CONTROL, 0) == REC_ACT_RESTART_CONTROL, "paint control");
    CHECK(rec_select(DET_M_HEARTBEAT, DET_TASK_CONSOLE, 0) == REC_ACT_RESTART_CONSOLE, "heartbeat console");
    CHECK(rec_select(DET_M_HEARTBEAT, -1, 0) == REC_ACT_NONE, "unknown task: no action");
    CHECK(rec_select(DET_M_CRC, -1, 0) == REC_ACT_RESTORE_CONFIG, "crc config");
    CHECK(rec_select(DET_M_REDUNDANT, -1, 0) == REC_ACT_RESTORE_CONFIG, "redundant config");
    CHECK(rec_select(DET_M_CRC, -1, 1) == REC_ACT_RESTORE_SAMPLE, "crc sample");
    CHECK(rec_select(DET_M_I2C_TIMEOUT, -1, 0) == REC_ACT_BUS_RECOVER, "i2c");
    CHECK(rec_select(DET_M_FAULT_HANDLER, -1, 0) == REC_ACT_SOFT_RESET, "fault handler -> soft reset");
    CHECK(rec_select(DET_M_WWDG, -1, 0) == REC_ACT_WWDG_RESET, "wwdg -> wwdg reset");
    CHECK(rec_select(DET_M_NONE, -1, 0) == REC_ACT_NONE, "none");

    /* levels */
    CHECK(rec_level(REC_ACT_RESTART_SENSOR) == 1 && rec_level(REC_ACT_BUS_RECOVER) == 1, "L1");
    CHECK(rec_level(REC_ACT_RESTORE_CONFIG) == 2 && rec_level(REC_ACT_RESTORE_SAMPLE) == 2, "L2");
    CHECK(rec_level(REC_ACT_SOFT_RESET) == 3 && rec_level(REC_ACT_WWDG_RESET) == 3, "L3");
    CHECK(rec_level(REC_ACT_SAFE_STATE) == 4 && rec_level(REC_ACT_NONE) == 0, "L4/none");

    /* escalation chain: L1/L2 -> software reset -> safe state */
    CHECK(rec_escalate(REC_ACT_RESTART_SENSOR) == REC_ACT_SOFT_RESET, "L1 fail -> L3");
    CHECK(rec_escalate(REC_ACT_RESTORE_CONFIG) == REC_ACT_SOFT_RESET, "L2 fail -> L3");
    CHECK(rec_escalate(REC_ACT_SOFT_RESET) == REC_ACT_SAFE_STATE, "L3 fail -> L4");
    CHECK(rec_escalate(REC_ACT_WWDG_RESET) == REC_ACT_SAFE_STATE, "wwdg fail -> L4");

    /* attempt gate */
    CHECK(rec_may_attempt(0) && rec_may_attempt(REC_MAX_CONSEC - 1u), "attempts allowed");
    CHECK(!rec_may_attempt(REC_MAX_CONSEC) && !rec_may_attempt(REC_MAX_CONSEC + 5u), "safe state after the limit");

    /* names */
    CHECK(strcmp(rec_action_name(REC_ACT_RESTORE_CONFIG), "config_restore") == 0, "name");
    CHECK(strcmp(rec_action_name((rec_action_t)99), "?") == 0, "unknown name");

    /* checkpoint */
    rec_checkpoint_t cp;
    memset(&cp, 0, sizeof cp);
    CHECK(!rec_cp_valid(&cp), "zero checkpoint (seq 0) is invalid");
    cp.cfg[0] = 2200; cp.cfg[1] = 15; cp.cfg[2] = 0; cp.cfg[3] = 100; cp.last_input = 2345; cp.seq = 7;
    rec_cp_seal(&cp);
    CHECK(rec_cp_valid(&cp), "sealed checkpoint valid");
    cp.cfg[1] ^= 1 << 3;
    CHECK(!rec_cp_valid(&cp), "corrupted checkpoint rejected");
    cp.cfg[1] ^= 1 << 3;
    CHECK(rec_cp_valid(&cp), "valid again");
    cp.last_input = 8500;
    CHECK(!rec_cp_valid(&cp), "last_input is covered by the CRC");

    printf("[UNIT] test_rec_logic checks=%d failures=%d\n", checks, failures);
    return failures != 0;
}
