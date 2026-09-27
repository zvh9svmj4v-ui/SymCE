/* Streams the C reference so tools/engine_fuzz.py can diff it against the
 * assembled hook. One entry per line as hex tokens; one answer per line, either
 * PASS or the hex tokens the edit buffer ends up holding. Stays a single process
 * because the fuzzer runs a hundred thousand of these.
 *
 * The answer is what the hook stashes for the OS to draw: minipoly_display's
 * token string, 2X+2X -> 34 58. The entry itself is never touched.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define MAXPOW 8
typedef struct { int32_t coef[MAXPOW]; int var; } poly_t;
int      minipoly(const uint8_t *t, unsigned len, poly_t *p);
unsigned minipoly_emit(const poly_t *p, uint8_t *out, unsigned max);
unsigned minipoly_display(const uint8_t *in, unsigned n, uint8_t *out, unsigned max);
enum { MINI_OK = 0, MINI_PASS = 1 };

int main(void) {
    char line[512];
    while (fgets(line, sizeof line, stdin)) {
        uint8_t in[128], out[128], disp[160];
        unsigned n = 0, olen, dlen, i;
        char *h = line;
        poly_t p;

        while (*h && *h != '\n' && n < sizeof in) {
            unsigned v;
            if (sscanf(h, "%2x", &v) != 1) break;
            in[n++] = (uint8_t)v;
            h += 2;
        }
        if (minipoly(in, n, &p) != MINI_OK) { puts("PASS"); fflush(stdout); continue; }
        olen = minipoly_emit(&p, out, sizeof out);
        if (olen == 0) { puts("PASS"); fflush(stdout); continue; }
        dlen = minipoly_display(out, olen, disp, sizeof disp);
        if (dlen == 0) { puts("PASS"); fflush(stdout); continue; }
        for (i = 0; i < dlen; i++) printf("%02x", disp[i]);
        putchar('\n');
        fflush(stdout);
    }
    return 0;
}
