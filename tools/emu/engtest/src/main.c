/* Runs the SHIPPED engine -- symce/obj/engine/engine.s, the ez80-clang output
 * the app carries, linked in unchanged -- over every vector in vectors.h, and
 * compares each answer with what the host build of the same engine.c gave.
 * The two differ in int width (24 bits here) and in the runtime's 32-bit
 * helpers; this is where that would show.
 *
 * vectors.h, from tools/emu/engine_device.py: { Ans, mode, entry length,
 * entry tokens, answer length (0 = refused, FF = an error screen, its
 * message's length next), answer tokens } repeated, then
 * 0xFE, which no Ans starts with. The mode is the byte the hook passes:
 * 0x20 MathPrint, 0x01 MODE ANSWERS: DEC, 0x04 MODE DEGREE.
 * Ans is laid out as the hook keeps lastAns -- a length and that many tokens,
 * or 0xFF and a 9-byte TI real -- and passed the way the hook passes it.
 *
 * The report goes to fixed RAM for the autotester to read. It never returns:
 * spinning keeps the OS from reusing that RAM before it is read.
 */
#include <stdint.h>
#include <string.h>
#include "vectors.h"

uint8_t symce_engine(const uint8_t *in, unsigned len, uint8_t *out, void *work,
                     const uint8_t *ans, uint8_t mode);

static uint8_t work[0x4000];        /* engine.c asserts its pool fits in this */

typedef struct {
    char     magic[4];              /* "RUN!" while going, "DONE" at the end */
    uint24_t n, bad, first;         /* vectors run, mismatches, first mismatch */
    uint8_t  len, out[26];          /* what the engine gave for that one */
    uint24_t deep, deepat;          /* most stack any call took, below main's frame; which */
} report_t;
#define REPORT ((volatile report_t *)0xD0EE40)
/* Below main's frame, painted before each call and scanned after: the
   deepest byte the engine wrote. The CE's stack is 4 KB. */
#define PAINT 3900

int main(void)
{
    const uint8_t *p = vectors;
    uint8_t out[65];                /* 64 tokens, then the column count */
    volatile uint8_t mark;          /* volatile: out of any object, so no access is elided */
    volatile uint8_t *lo = (volatile uint8_t *)((uint24_t)&mark - PAINT), *q;

    memcpy((void *)REPORT->magic, "RUN!", 4);
    REPORT->n = REPORT->bad = 0;
    REPORT->first = 0xFFFFFF;
    REPORT->deep = 0;
    while (*p != 0xFE) {
        const uint8_t *ans = p;
        uint8_t il, wl, got, mode;
        const uint8_t *in;
        p += *p == 0xFF ? 10 : 1 + *p;
        mode = *p++;
        il = *p++;
        in = p;
        p += il;
        wl = *p++;
        for (q = lo; q < lo + PAINT - 256; q++) *q = 0xA5;
        got = symce_engine(in, il, out, work, ans, mode);
        for (q = lo; q < lo + PAINT - 256 && *q == 0xA5; q++) ;
        if ((uint24_t)&mark - (uint24_t)q > REPORT->deep) {
            REPORT->deep = (uint24_t)&mark - (uint24_t)q;
            REPORT->deepat = REPORT->n;
        }
        if (wl == 0xFF ? got != wl || memcmp(out, p + 1, *p) : got != wl || memcmp(out, p, wl)) {
            if (!REPORT->bad) {
                REPORT->first = REPORT->n;
                REPORT->len = got;
                memcpy((void *)REPORT->out, out, got > 26 ? 26 : got);
            }
            REPORT->bad++;
        }
        p += wl == 0xFF ? 1 + *p : wl;
        REPORT->n++;
    }
    memcpy((void *)REPORT->magic, "DONE", 4);
    for (;;)
        ;
}
