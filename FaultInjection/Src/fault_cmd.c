#include "fault_cmd.h"
#include <stddef.h>

#define MAX_TOKENS 3

typedef struct {
    const char *p;
    size_t      len;
} token_t;

static int is_id_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
}

/* Split on spaces; returns the token count (capped at MAX_TOKENS + 1 so that
 * "too many" is still detectable). */
static unsigned tokenize(const char *s, token_t *tok)
{
    unsigned n = 0;
    for (;;) {
        while (*s == ' ') s++;
        if (*s == '\0' || n > MAX_TOKENS) {
            return n;
        }
        const char *start = s;
        while (*s != '\0' && *s != ' ') s++;
        if (n < MAX_TOKENS) {
            tok[n].p = start;
            tok[n].len = (size_t)(s - start);
        }
        n++;
    }
}

/* Validate the ID token; fills out->id (truncated copy) and out->fault. */
static fault_cmd_status_t check_id(const token_t *t, fault_cmd_t *out)
{
    size_t n = t->len < FAULT_ID_MAX_LEN ? t->len : FAULT_ID_MAX_LEN;
    for (size_t i = 0; i < n; i++) {
        out->id[i] = t->p[i];
    }
    out->id[n] = '\0';
    if (t->len > FAULT_ID_MAX_LEN) {
        return FAULT_CMD_MALFORMED_ID;
    }
    for (size_t i = 0; i < t->len; i++) {
        if (!is_id_char(t->p[i])) {
            return FAULT_CMD_MALFORMED_ID;
        }
    }
    out->fault = fault_find(out->id);
    return out->fault != NULL ? FAULT_CMD_OK : FAULT_CMD_UNKNOWN_ID;
}

static void reset(fault_cmd_t *out)
{
    out->fault = NULL;
    out->id[0] = '\0';
    out->delay_ms = 0;
}

void fault_cmd_parse(const char *args, fault_cmd_t *out)
{
    token_t tok[MAX_TOKENS];
    reset(out);
    unsigned n = tokenize(args, tok);
    if (n == 0) {
        out->status = FAULT_CMD_MISSING_ID;
        return;
    }
    fault_cmd_status_t st = check_id(&tok[0], out);
    if (n > 1) {
        out->fault = NULL;
        out->status = FAULT_CMD_EXTRA_ARGS;
        return;
    }
    out->status = st;
}

void fault_cmd_parse_at(const char *args, fault_cmd_t *out)
{
    token_t tok[MAX_TOKENS];
    reset(out);
    unsigned n = tokenize(args, tok);
    if (n == 0) {
        out->status = FAULT_CMD_MISSING_ID;
        return;
    }
    fault_cmd_status_t st = check_id(&tok[0], out);
    if (n == 1) {
        out->fault = NULL;
        out->status = FAULT_CMD_MISSING_DELAY;
        return;
    }
    if (n > 2) {
        out->fault = NULL;
        out->status = FAULT_CMD_EXTRA_ARGS;
        return;
    }
    if (st != FAULT_CMD_OK) {
        out->status = st;
        return;
    }
    uint32_t v = 0;
    if (tok[1].len == 0 || tok[1].len > 4) {
        st = FAULT_CMD_BAD_DELAY;
    }
    for (size_t i = 0; st == FAULT_CMD_OK && i < tok[1].len; i++) {
        char c = tok[1].p[i];
        if (c < '0' || c > '9') {
            st = FAULT_CMD_BAD_DELAY;
        } else {
            v = v * 10u + (uint32_t)(c - '0');
        }
    }
    if (st == FAULT_CMD_OK && (v < FAULT_AT_MIN_MS || v > FAULT_AT_MAX_MS)) {
        st = FAULT_CMD_BAD_DELAY;
    }
    if (st != FAULT_CMD_OK) {
        out->fault = NULL;
    } else {
        out->delay_ms = v;
    }
    out->status = st;
}

const char *fault_cmd_status_str(fault_cmd_status_t st)
{
    switch (st) {
    case FAULT_CMD_OK:            return "ok";
    case FAULT_CMD_MISSING_ID:    return "missing_id";
    case FAULT_CMD_EXTRA_ARGS:    return "extra_args";
    case FAULT_CMD_MALFORMED_ID:  return "malformed_id";
    case FAULT_CMD_UNKNOWN_ID:    return "unknown_id";
    case FAULT_CMD_MISSING_DELAY: return "missing_delay";
    case FAULT_CMD_BAD_DELAY:     return "bad_delay";
    }
    return "?";
}
