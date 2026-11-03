#include <stdlib.h>
/* Streams symce/src/engine.c on the host, same protocol as minipoly_cli: one
 * entry per line as hex tokens in, one answer per line out, PASS or hex. An
 * entry may be followed by ",HEX", what Ans stands for exactly as the hook keeps
 * it at lastAns: a length and that many tokens, or FF and a TI real. A line
 * that starts with M is answered as for MathPrint, where more fits; with D,
 * as for MODE ANSWERS: DEC; with G, in degrees; with C, an answer is followed
 * by ':' and the column byte symce_engine leaves at out[MAXOUT]; with T the entry
 * is wrapped as steps(...) and a second line S<hex> is the step log (bytes 1.. up
 * to the bytes used: count, flags, entry length, entry, used, steps, answer), or
 * S- when there is none. A line W<hex> answers with width() of those tokens, "W<Classic>,<MathPrint>"
 * (tools/width_check.py). After the
 * entry and its Ans, each ";TT" and 18 hex digits is a stored value: letter
 * token TT holds that TI real, for SOLVE's other letters. Prefixes
 * combine (CMDG) and are capitals; P is the parser hook's mode (MODE_PRGM: no Ans, answers a string if symbolic), the hex lower case. An error screen (a
 * command SymCE cannot do) prints as E and the message bytes through their
 * double NUL.
 *
 *   make -C symce bin/host/engine_cli
 */
#include <stdio.h>
#include <string.h>
#include "../symce/src/engine.c"

static rat_t work[0x5000 / sizeof(rat_t) + 1];   /* the pool, nested()'s two buffers, the step log */
static uint8_t val[NVTOK][9], has[NVTOK];

static const uint8_t *look(uint8_t c) { return has[c - T_VAR0] ? val[c - T_VAR0] : 0; }

int main(void)
{
    static char line[40000];
    static uint8_t in[8200], out[16500];
    while (fgets(line, sizeof line, stdin)) {
        uint8_t ans[1 + 255];
        unsigned n = 0, v, k, len, mode = 0, cols = 0, tw = 0;
        char *h = line;
        if (line[0] == 'J') {                       /* J<offset in the copy> <hex>: the source offset Goto gets, "-" none */
            unsigned t = 0;
            wrapinfo wi;
            for (h = line + 1; *h == ' '; h++) ;
            t = (unsigned)strtoul(h, &h, 10);
            for (; *h == ' '; h++) ;
            while (n < sizeof in && sscanf(h, "%2x", &v) == 1) { in[n++] = (uint8_t)v; h += 2; }
            wi.len = wi.orig = 0; wi.tgt = (uint16_t)t;
            symce_engine(in, n, 0, work, (const uint8_t *)&wi, MODE_WRAP, look);
            if (wi.orig == 0xFFFF) puts("-"); else printf("%u\n", wi.orig);
            fflush(stdout);
            continue;
        }
        if (line[0] == 'W') {                       /* an answer's columns, not an entry */
            for (h++; n < sizeof in && sscanf(h, "%2x", &v) == 1; h += 2) in[n++] = (uint8_t)v;
            printf("W%u,%u\n", width(in, n, 0), width(in, n, 1));
            fflush(stdout);
            continue;
        }
        for (;; h++)
            if (*h == 'M') mode |= MODE_MP;
            else if (*h == 'D') mode |= MODE_DEC;
            else if (*h == 'G') mode |= MODE_DEG;
            else if (*h == 'C') cols = 1;
            else if (*h == 'V') mode |= MODE_EVAL;
            else if (*h == 'P') mode |= MODE_PRGM;     /* the parser hook: a string for expr( */
            else if (*h == 'B') mode = MODE_WRAP;      /* bare command wrap */
            else if (*h == 'T') tw = 1;
            else break;
        while (*h == ' ') h++;
        while (n < sizeof in && sscanf(h, "%2x", &v) == 1) { in[n++] = (uint8_t)v; h += 2; }
        if (mode == MODE_WRAP) {
            wrapinfo wi = {0, 0, 0};
            len = symce_engine(in, n, out, work, (const uint8_t *)&wi, mode, look);
            if (!len) puts("PASS");
            else {
                for (k = 0; k < wi.len; k++) printf("%02x", out[k]);
                printf(":%u\n", wi.orig);
            }
            fflush(stdout);
            continue;
        }
        if (tw) {                                   /* steps( ... ) */
            const char *nm = "STEPS";
            memmove(in + 11, in, n);
            for (k = 0; k < 5; k++) { in[2 * k] = T_BB; in[2 * k + 1] = (uint8_t)(0xB0 + nm[k] - 'A' + (nm[k] >= 'K')); }
            in[10] = T_LPAR;
            in[11 + n] = T_RPAR;
            n += 12;
        }
        ans[0] = 0;
        memset(has, 0, sizeof has);
        if (*h == ',')
            for (h++, k = 0; k < sizeof ans && sscanf(h, "%2x", &v) == 1; h += 2) ans[k++] = (uint8_t)v;
        for (; *h == ';'; h += 21) {            /* stored letters: TT + 9 bytes */
            sscanf(h + 1, "%2x", &v);
            has[v - T_VAR0] = 1;
            for (k = 0; k < 9; k++) { unsigned b; sscanf(h + 3 + 2 * k, "%2x", &b); val[v - T_VAR0][k] = (uint8_t)b; }
        }
        if (getenv("SYMCE_FILL")) memset(work, atoi(getenv("SYMCE_FILL")), sizeof work);   /* debug: leftovers must not matter */
        len = symce_engine(in, n, out, work, ans, mode, look);
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
        if (tw) {
            uint8_t *st = (uint8_t *)work + STOFF;
            if (st[0] != STMAGIC) puts("S-");
            else {
                putchar('S');
                for (k = 1; k < (unsigned)(st[68] | st[69] << 8); k++) printf("%02x", st[k]);
                putchar('\n');
            }
        }
        fflush(stdout);
    }
    return 0;
}
