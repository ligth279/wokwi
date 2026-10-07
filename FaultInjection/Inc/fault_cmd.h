#ifndef FAULT_CMD_H
#define FAULT_CMD_H

#include "fault_catalog.h"
#include <stdint.h>

/* Parsers for the argument part of the UART fault commands.
 *
 *   FAULT <ID>              fault_cmd_parse()     (also FAULT_GDB <ID>)
 *   FAULT_AT <ID> <DELAY>   fault_cmd_parse_at()
 *
 * Grammar (after the command word; tokens separated by one or more spaces):
 *   ID      := 1..FAULT_ID_MAX_LEN characters from [A-Z0-9-]
 *   DELAY   := 1..4 decimal digits, value FAULT_AT_MIN_MS..FAULT_AT_MAX_MS
 * Rejections, checked in this order:
 *   missing_id     no token at all
 *   missing_delay  FAULT_AT with only an ID
 *   extra_args     more tokens than the command takes
 *   malformed_id   ID too long, or contains a character outside [A-Z0-9-]
 *   unknown_id     well-formed ID, but not in fault_catalog
 *   bad_delay      DELAY not all digits, or out of range
 *
 * Parsing is pure (no hardware, no logging) so it can be unit-tested on the
 * host (Tests/unit/test_fault_cmd.c). Accepting a command does NOT inject a
 * fault; injection is a separate, later step. */

typedef enum {
    FAULT_CMD_OK = 0,
    FAULT_CMD_MISSING_ID,
    FAULT_CMD_EXTRA_ARGS,
    FAULT_CMD_MALFORMED_ID,
    FAULT_CMD_UNKNOWN_ID,
    FAULT_CMD_MISSING_DELAY,
    FAULT_CMD_BAD_DELAY,
} fault_cmd_status_t;

#define FAULT_AT_MIN_MS 1u
#define FAULT_AT_MAX_MS 6000u

typedef struct {
    fault_cmd_status_t  status;
    const fault_desc_t *fault;                  /* set when status == FAULT_CMD_OK */
    char                id[FAULT_ID_MAX_LEN + 1]; /* first token, truncated, for logging */
    uint32_t            delay_ms;               /* FAULT_AT only */
} fault_cmd_t;

void fault_cmd_parse(const char *args, fault_cmd_t *out);
void fault_cmd_parse_at(const char *args, fault_cmd_t *out);
const char *fault_cmd_status_str(fault_cmd_status_t st);

#endif /* FAULT_CMD_H */
