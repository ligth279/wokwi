/* Host unit test for the FAULT command parser (FaultInjection/Src/fault_cmd.c).
 * Build + run: make unit */
#include "fault_cmd.h"
#include <stdio.h>
#include <string.h>

static int failures, total;

static void check(const char *args, const fault_cmd_t *r_, fault_cmd_status_t want, const char *want_id,
                  uint32_t want_delay);

static void expect(const char *args, fault_cmd_status_t want, const char *want_id)
{
    fault_cmd_t r;
    fault_cmd_parse(args, &r);
    check(args, &r, want, want_id, 0);
}

static void expect_at(const char *args, fault_cmd_status_t want, const char *want_id, uint32_t want_delay)
{
    fault_cmd_t r;
    fault_cmd_parse_at(args, &r);
    check(args, &r, want, want_id, want_delay);
}

static void check(const char *args, const fault_cmd_t *r_, fault_cmd_status_t want, const char *want_id,
                  uint32_t want_delay)
{
    fault_cmd_t r = *r_;
    int ok = r.status == want;
    if (want == FAULT_CMD_OK) {
        ok = ok && r.fault != NULL && strcmp(r.fault->id, want_id) == 0 && r.delay_ms == want_delay;
    } else {
        ok = ok && r.fault == NULL;
    }
    total++;
    if (!ok) {
        failures++;
        printf("FAIL args=\"%s\": got %s (id=\"%s\"), want %s\n", args, fault_cmd_status_str(r.status), r.id,
               fault_cmd_status_str(want));
    }
}

int main(void)
{
    /* every catalog ID is accepted, with any amount of surrounding spaces */
    for (unsigned i = 0; i < fault_catalog_count; i++) {
        char buf[40];
        const char *id = fault_catalog[i].id;
        expect(id, FAULT_CMD_OK, id);
        snprintf(buf, sizeof buf, " %s", id);
        expect(buf, FAULT_CMD_OK, id);
        snprintf(buf, sizeof buf, "   %s   ", id);
        expect(buf, FAULT_CMD_OK, id);
    }

    /* missing ID */
    expect("", FAULT_CMD_MISSING_ID, NULL);
    expect(" ", FAULT_CMD_MISSING_ID, NULL);
    expect("     ", FAULT_CMD_MISSING_ID, NULL);

    /* extra arguments */
    expect("MEM-01 X", FAULT_CMD_EXTRA_ARGS, NULL);
    expect("MEM-01 MEM-02", FAULT_CMD_EXTRA_ARGS, NULL);
    expect(" MEM-01  now ", FAULT_CMD_EXTRA_ARGS, NULL);
    expect("BOGUS extra", FAULT_CMD_EXTRA_ARGS, NULL);

    /* malformed IDs */
    expect("mem-01", FAULT_CMD_MALFORMED_ID, NULL);    /* lower case */
    expect("MEM_01", FAULT_CMD_MALFORMED_ID, NULL);    /* underscore */
    expect("MEM-01;", FAULT_CMD_MALFORMED_ID, NULL);
    expect("MEM\t01", FAULT_CMD_MALFORMED_ID, NULL);   /* tab is not a separator */
    expect("MEM-01\x01", FAULT_CMD_MALFORMED_ID, NULL);
    expect("ABCDEFGHIJKLM", FAULT_CMD_MALFORMED_ID, NULL); /* 13 chars > 12 */

    /* well-formed but unknown */
    expect("MEM-03", FAULT_CMD_UNKNOWN_ID, NULL);
    expect("MEM-1", FAULT_CMD_UNKNOWN_ID, NULL);
    expect("MEM01", FAULT_CMD_UNKNOWN_ID, NULL);
    expect("XYZ", FAULT_CMD_UNKNOWN_ID, NULL);
    expect("-", FAULT_CMD_UNKNOWN_ID, NULL);
    expect("ABCDEFGHIJKL", FAULT_CMD_UNKNOWN_ID, NULL); /* exactly 12 chars */
    expect("PERIPH-011", FAULT_CMD_UNKNOWN_ID, NULL);

    /* FI-TEST is a registered (self-test) fault */
    expect("FI-TEST", FAULT_CMD_OK, "FI-TEST");

    /* FAULT_AT <ID> <DELAY> */
    expect_at("FI-TEST 500", FAULT_CMD_OK, "FI-TEST", 500);
    expect_at("  MEM-01   1  ", FAULT_CMD_OK, "MEM-01", 1);
    expect_at("FI-TEST 6000", FAULT_CMD_OK, "FI-TEST", 6000);
    expect_at("", FAULT_CMD_MISSING_ID, NULL, 0);
    expect_at("FI-TEST", FAULT_CMD_MISSING_DELAY, NULL, 0);
    expect_at("FI-TEST 500 X", FAULT_CMD_EXTRA_ARGS, NULL, 0);
    expect_at("FI-TEST 0", FAULT_CMD_BAD_DELAY, NULL, 0);
    expect_at("FI-TEST 6001", FAULT_CMD_BAD_DELAY, NULL, 0);
    expect_at("FI-TEST 12345", FAULT_CMD_BAD_DELAY, NULL, 0);
    expect_at("FI-TEST -5", FAULT_CMD_BAD_DELAY, NULL, 0);
    expect_at("FI-TEST 5ms", FAULT_CMD_BAD_DELAY, NULL, 0);
    expect_at("FI-TEST 0x10", FAULT_CMD_BAD_DELAY, NULL, 0);
    expect_at("XYZ 500", FAULT_CMD_UNKNOWN_ID, NULL, 0);
    expect_at("mem-01 500", FAULT_CMD_MALFORMED_ID, NULL, 0);
    expect_at("A B C D E", FAULT_CMD_EXTRA_ARGS, NULL, 0);

    /* the id buffer never overflows and is always terminated */
    fault_cmd_t r;
    fault_cmd_parse("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", &r);
    total++;
    if (strlen(r.id) != FAULT_ID_MAX_LEN || r.status != FAULT_CMD_MALFORMED_ID) {
        failures++;
        printf("FAIL long token: id len %zu status %s\n", strlen(r.id), fault_cmd_status_str(r.status));
    }

    printf("[UNIT] test_fault_cmd total=%d failures=%d\n", total, failures);
    return failures != 0;
}
