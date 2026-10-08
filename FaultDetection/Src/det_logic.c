#include "det_logic.h"
#include <stddef.h>

uint32_t det_crc32_sw(const uint32_t *w, unsigned n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned i = 0; i < n; i++) {
        crc ^= w[i];
        for (int b = 0; b < 32; b++) {
            crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : (crc << 1);
        }
    }
    return crc;
}

/* Canary at the lowest DET_CANARY_WORDS words, pattern from there up to
 * used_top_words (the part below the initial task frame). */
void det_stack_prepare(uint32_t *base, unsigned words, unsigned used_top_words)
{
    if (used_top_words > words) {
        used_top_words = words;
    }
    for (unsigned i = 0; i < DET_CANARY_WORDS; i++) {
        base[i] = DET_CANARY_WORD;
    }
    for (unsigned i = DET_CANARY_WORDS; i < used_top_words; i++) {
        base[i] = DET_PAINT_WORD;
    }
}

int det_canary_ok(const uint32_t *base)
{
    for (unsigned i = 0; i < DET_CANARY_WORDS; i++) {
        if (base[i] != DET_CANARY_WORD) {
            return 0;
        }
    }
    return 1;
}

unsigned det_stack_free_words(const uint32_t *base, unsigned words)
{
    unsigned i = DET_CANARY_WORDS;
    while (i < words && base[i] == DET_PAINT_WORD) {
        i++;
    }
    return i - DET_CANARY_WORDS;
}

unsigned det_stack_pct(unsigned free_words, unsigned words)
{
    unsigned usable = words - DET_CANARY_WORDS;
    if (free_words > usable) {
        free_words = usable;
    }
    return ((usable - free_words) * 100u) / usable;
}

void det_stack_repaint(uint32_t *base, unsigned upto_words)
{
    for (unsigned i = DET_CANARY_WORDS; i < upto_words; i++) {
        base[i] = DET_PAINT_WORD;
    }
}

int det_frame_diff(const uint32_t *seal, const uint32_t *frame, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        if (seal[i] != frame[i]) {
            return (int)i;
        }
    }
    return -1;
}

int det_hb_step(det_hb_t *h, uint32_t count, uint32_t now_ms, uint32_t timeout_ms)
{
    if (!h->init) {
        h->init = 1;
        h->last = count;
        h->last_change_ms = now_ms;
        return 0;
    }
    if (count != h->last) {
        h->last = count;
        h->last_change_ms = now_ms;
        h->flagged = 0;
        return 0;
    }
    if (!h->flagged && (now_ms - h->last_change_ms) > timeout_ms) {
        h->flagged = 1;
        return 1;
    }
    return 0;
}

int det_wd_token_fresh(uint32_t now_cyc, uint32_t token_cyc, uint32_t limit_cyc)
{
    return (uint32_t)(now_cyc - token_cyc) <= limit_cyc;
}

static unsigned put(char *buf, unsigned n, unsigned pos, const char *s)
{
    if (pos != 0 && pos + 1 < n) {
        buf[pos++] = '|';
    }
    while (*s != '\0' && pos + 1 < n) {
        buf[pos++] = *s++;
    }
    return pos;
}

typedef struct { uint32_t mask; const char *name; } bitname_t;

static const bitname_t cfsr_bits[] = {
    {1u << 0, "IACCVIOL"},  {1u << 1, "DACCVIOL"},  {1u << 3, "MUNSTKERR"}, {1u << 4, "MSTKERR"},
    {1u << 7, "MMARVALID"},
    {1u << 8, "IBUSERR"},   {1u << 9, "PRECISERR"}, {1u << 10, "IMPRECISERR"}, {1u << 11, "UNSTKERR"},
    {1u << 12, "STKERR"},   {1u << 15, "BFARVALID"},
    {1u << 16, "UNDEFINSTR"}, {1u << 17, "INVSTATE"}, {1u << 18, "INVPC"}, {1u << 19, "NOCP"},
    {1u << 24, "UNALIGNED"}, {1u << 25, "DIVBYZERO"},
};
static const bitname_t hfsr_bits[] = {{1u << 1, "VECTTBL"}, {1u << 30, "FORCED"}, {1u << 31, "DEBUGEVT"}};

static const char *decode(uint32_t v, const bitname_t *t, size_t nt, char *buf, unsigned n)
{
    unsigned pos = 0;
    if (n == 0) {
        return buf;
    }
    for (size_t i = 0; i < nt; i++) {
        if (v & t[i].mask) {
            pos = put(buf, n, pos, t[i].name);
        }
    }
    if (pos == 0) {
        pos = put(buf, n, 0, "none");
    }
    buf[pos] = '\0';
    return buf;
}

const char *det_decode_cfsr(uint32_t cfsr, char *buf, unsigned n)
{
    return decode(cfsr, cfsr_bits, sizeof cfsr_bits / sizeof cfsr_bits[0], buf, n);
}

const char *det_decode_hfsr(uint32_t hfsr, char *buf, unsigned n)
{
    return decode(hfsr, hfsr_bits, sizeof hfsr_bits / sizeof hfsr_bits[0], buf, n);
}
