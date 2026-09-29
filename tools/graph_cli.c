/* Streams symce/src/graph.c's compiler and evaluator on the host, for
 * graph_check.py. One line per equation:
 *
 *   FLAGS HEX VARS X1 X2 ...
 *
 * FLAGS "-" or "G" (degrees); HEX the equation's tokens; VARS "-" or
 * "TT=18hex,TT=18hex" (a letter token and its TI real); then the x values.
 * Answers "E code token" when the equation does not compile (graph.c's
 * E_ codes), else one "nan", "inf", "-inf" or number per x, %.9g.
 *
 *   make -C symce bin/host/graph_cli
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define VRAM fb
#include <stdint.h>
static uint16_t fb[320 * 240];
#include "../symce/src/graph.c"


static struct gw w;

int main(void)
{
    char line[4096], *save, *f;
    while (fgets(line, sizeof line, stdin)) {
        unsigned v, n = 0;
        memset(&w, 0, sizeof w);
        w.deg = line[0] == 'G';
        f = strtok_r(line, " \n", &save);
        f = strtok_r(0, " \n", &save);
        for (const char *h = f; n < 255 && sscanf(h, "%2x", &v) == 1; h += 2, n++)
            if (n < MAXTOK)
                w.tok[0][n] = (uint8_t)v;
        w.len[0] = (uint8_t)n;
        f = strtok_r(0, " \n", &save);
        for (char *s = f; s && *s != '-'; s = strchr(s, ',') ? strchr(s, ',') + 1 : 0) {
            unsigned t;
            sscanf(s, "%2x=", &t);
            for (int k = 0; k < 9; k++) {
                unsigned b;
                sscanf(s + 3 + 2 * k, "%2x", &b);
                w.var[t - 0x41][k] = (uint8_t)b;
            }
        }
        uint8_t e = compile(&w, 0);
        if (e)
            printf("E %d %02x\n", e, w.errtok);
        else {
            while ((f = strtok_r(0, " \n", &save))) {
                float y = run(&w, (float)atof(f));
                if (y != y)
                    printf("nan ");
                else
                    printf("%.9g ", y);
            }
            putchar('\n');
        }
        fflush(stdout);
    }
    (void)fb;
    return 0;
}
