/* Streams symce/src/engine.c on the host, same protocol as minipoly_cli: one
 * entry per line as hex tokens in, one answer per line out, PASS or hex. An
 * entry may be followed by ",HEX", what Ans stands for exactly as the hook keeps
 * it at lastAns: a length and that many tokens, or FF and a TI real. A line
 * that starts with M is answered as for MathPrint, where more fits; with D,
 * as for MODE ANSWERS: DEC; with G, in degrees; with C, an answer is followed
 * by ':' and the column byte symce_engine leaves at out[MAXOUT]. Prefixes
 * combine (CMDG) and are capitals, the hex lower case. An error screen (a
 * command SymCE cannot do) prints as E and the message bytes through their
 * double NUL.
 *
 *   make -C symce bin/host/engine_cli
 */
#include <stdio.h>
#include "../symce/src/engine.c"

static rat_t work[NPOOL];

int main(void)
{
    char line[512];
    while (fgets(line, sizeof line, stdin)) {
        uint8_t in[128], out[MAXOUT + 1], ans[1 + 255];
        unsigned n = 0, v, k, len, mode = 0, cols = 0;
        char *h = line;
        for (;; h++)
            if (*h == 'M') mode |= MODE_MP;
            else if (*h == 'D') mode |= MODE_DEC;
            else if (*h == 'G') mode |= MODE_DEG;
            else if (*h == 'C') cols = 1;
            else break;
        while (n < sizeof in && sscanf(h, "%2x", &v) == 1) { in[n++] = (uint8_t)v; h += 2; }
        ans[0] = 0;
        if (*h == ',')
            for (h++, k = 0; k < sizeof ans && sscanf(h, "%2x", &v) == 1; h += 2) ans[k++] = (uint8_t)v;
        len = symce_engine(in, n, out, work, ans, mode);
        if (!len) puts("PASS");
        else if (len == SYMCE_ERR) {        /* E, then "MESSAGE",0,"LINE",0,0 */
            putchar('E');
            for (k = 0; out[k] || out[k + 1]; k++) printf("%02x", out[k]);
            puts("0000");
        }
        else {
            for (k = 0; k < len; k++) printf("%02x", out[k]);
            if (cols) printf(":%02x", out[MAXOUT]);
            putchar('\n');
        }
        fflush(stdout);
    }
    return 0;
}
