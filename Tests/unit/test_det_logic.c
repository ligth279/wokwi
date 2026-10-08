/* Host test of the hardware-independent detection logic (FaultDetection/Src/det_logic.c). */
#include "det_logic.h"

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

static void t_crc(void)
{
    /* the smoke probe measured HW CRC == this reference for its test vector */
    uint32_t w[] = {0x12345678u, 0x9ABCDEF0u};
    uint32_t a = det_crc32_sw(w, 2);
    CHECK(a == det_crc32_sw(w, 2), "deterministic");
    w[1] ^= 1u;
    CHECK(a != det_crc32_sw(w, 2), "1-bit change detected");
    uint32_t one[] = {0u};
    CHECK(det_crc32_sw(one, 1) == 0xC704DD7Bu, "CRC-32/MPEG-2 of one zero word: 0x%08X", (unsigned)det_crc32_sw(one, 1));
}

static void t_stack(void)
{
    uint32_t st[64];
    memset(st, 0xFF, sizeof st);
    det_stack_prepare(st, 64, 46);
    CHECK(det_canary_ok(st), "canary set");
    CHECK(det_stack_free_words(st, 64) == 46 - DET_CANARY_WORDS, "free words below the initial frame");
    st[40] = 0x1234; /* 6 words used from the first used word upward */
    CHECK(det_stack_free_words(st, 64) == 40 - DET_CANARY_WORDS, "high-water after use");
    CHECK(det_stack_pct(40 - DET_CANARY_WORDS, 64) == ((60u - 36u) * 100u) / 60u, "pct");
    CHECK(det_stack_pct(1000, 64) == 0, "free clamps to usable");
    st[1] = 0;
    CHECK(!det_canary_ok(st), "canary corruption detected");
    det_stack_prepare(st, 64, 46);
    CHECK(det_canary_ok(st) && det_stack_free_words(st, 64) == 46 - DET_CANARY_WORDS, "re-prepare repaints");
    st[30] = 7;
    det_stack_repaint(st, 40);
    CHECK(det_stack_free_words(st, 64) >= 40 - DET_CANARY_WORDS, "repaint dead region");
}

static void t_frame(void)
{
    uint32_t a[16], b[16];
    for (int i = 0; i < 16; i++) a[i] = b[i] = 0x1000u + (unsigned)i;
    CHECK(det_frame_diff(a, b, 16) == -1, "identical");
    b[13] ^= 1u << 29;
    CHECK(det_frame_diff(a, b, 16) == 13, "saved LR slot (index 13) corruption found");
}

static void t_pair(void)
{
    CHECK(det_pair_ok(2200, (int16_t)~2200), "match");
    CHECK(!det_pair_ok(2200 ^ (1 << 10), (int16_t)~2200), "bit flip in the primary");
    CHECK(!det_pair_ok(15, (int16_t)~100), "overwrite");
}

static void t_hb(void)
{
    det_hb_t h = {0};
    CHECK(det_hb_step(&h, 5, 0, 300) == 0, "first sample initialises");
    CHECK(det_hb_step(&h, 6, 100, 300) == 0, "moving");
    CHECK(det_hb_step(&h, 6, 400, 300) == 0, "300 ms silent: not yet (limit is exclusive)");
    CHECK(det_hb_step(&h, 6, 401, 300) == 1, "missing heartbeat flagged once");
    CHECK(det_hb_step(&h, 6, 500, 300) == 0, "not flagged twice");
    CHECK(det_hb_step(&h, 7, 520, 300) == 0, "recovers when the counter moves");
    CHECK(det_hb_step(&h, 7, 900, 300) == 1, "flagged again after a new stall");
}

static void t_wd(void)
{
    CHECK(det_wd_token_fresh(1000, 900, 150), "fresh");
    CHECK(!det_wd_token_fresh(1000, 800, 150), "stale");
    CHECK(det_wd_token_fresh(50, 0xFFFFFFF0u, 100), "DWT counter wrap");
}

static void t_decode(void)
{
    char b[96];
    CHECK(strcmp(det_decode_cfsr(0, b, sizeof b), "none") == 0, "none");
    CHECK(strcmp(det_decode_cfsr(0x00020000u, b, sizeof b), "INVSTATE") == 0, "INVSTATE: %s", b);
    det_decode_cfsr((1u << 15) | (1u << 9), b, sizeof b);
    CHECK(strstr(b, "PRECISERR") && strstr(b, "BFARVALID"), "bus fault bits: %s", b);
    CHECK(strcmp(det_decode_hfsr(0x40000000u, b, sizeof b), "FORCED") == 0, "FORCED");
    det_decode_cfsr(0xFFFFFFFFu, b, 12);
    CHECK(strlen(b) < 12, "truncates safely");
}

int main(void)
{
    t_crc(); t_stack(); t_frame(); t_pair(); t_hb(); t_wd(); t_decode();
    printf("[UNIT] test_det_logic checks=%d failures=%d\n", checks, failures);
    return failures != 0;
}
