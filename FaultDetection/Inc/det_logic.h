#ifndef DET_LOGIC_H
#define DET_LOGIC_H

#include <stdint.h>

/* Hardware-independent detection logic (host-testable). The firmware glue is
 * in detect.c / det_monitor.c / det_wwdg.c / det_fault.c. */

#define DET_PAINT_WORD    0xA5A5A5A5u /* stack painting pattern (free stack)  */
#define DET_CANARY_WORD   0xC0DEC0DEu /* stack canary at the lowest addresses */
#define DET_CANARY_WORDS  4u
#define DET_FRAME_WORDS   16u         /* R4-R11 + R0-R3,R12,LR,PC,xPSR saved by the port */

/* CRC-32/MPEG-2 as computed by the STM32 hardware CRC unit for 32-bit words
 * (poly 0x04C11DB7, init 0xFFFFFFFF, no reflection, no final XOR). */
uint32_t det_crc32_sw(const uint32_t *w, unsigned n);

/* ---- stack canary / painting ---- */
void     det_stack_prepare(uint32_t *base, unsigned words, unsigned used_top_words);
int      det_canary_ok(const uint32_t *base);
/* Free (still painted) words between the canary and the first used word. */
unsigned det_stack_free_words(const uint32_t *base, unsigned words);
unsigned det_stack_pct(unsigned free_words, unsigned words);
/* Repaint the free region [canary end, upto_words) of a stack. */
void     det_stack_repaint(uint32_t *base, unsigned upto_words);

/* ---- saved-context seal ---- */
/* Index of the first word that differs, or -1 when identical. */
int      det_frame_diff(const uint32_t *seal, const uint32_t *frame, unsigned n);

/* ---- redundant variable pair ---- */
static inline int det_pair_ok(int16_t v, int16_t inverted_copy) { return v == (int16_t)~inverted_copy; }

/* ---- task heartbeat ---- */
typedef struct {
    uint32_t last;
    uint32_t last_change_ms;
    uint8_t  flagged;
    uint8_t  init;
} det_hb_t;
/* Returns 1 exactly once, when the heartbeat counter has not changed for
 * more than timeout_ms; re-armed when the counter moves again. */
int det_hb_step(det_hb_t *h, uint32_t count, uint32_t now_ms, uint32_t timeout_ms);

/* ---- WWDG progress token ---- */
int det_wd_token_fresh(uint32_t now_cyc, uint32_t token_cyc, uint32_t limit_cyc);

/* ---- fault status decoding (ARMv7-M CFSR/HFSR) ---- */
#define DET_CFSR_MMARVALID (1u << 7)
#define DET_CFSR_BFARVALID (1u << 15)
/* Writes "NAME|NAME..." for the set CFSR bits (or "none"). Returns buf. */
const char *det_decode_cfsr(uint32_t cfsr, char *buf, unsigned n);
const char *det_decode_hfsr(uint32_t hfsr, char *buf, unsigned n);

#endif /* DET_LOGIC_H */
