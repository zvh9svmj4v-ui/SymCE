/* Draws one implicit equation with graph.c's idraw on the host into a raw 320x240 RGB565 file:
 *   implicit_cli HEXTOKENS COLOUR_RGB565 OUT.raw [WINDOW x0 x1 y0 y1]
 * (graph_check.py's implicit case compares it with Python's own sign test; implicit.py's host mode makes PNGs.) */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define VRAM fb
static uint8_t osb_d;
#define OSB(a) (osb_d)                    /* no OS RAM: ON is never pressed */
#include <stdint.h>
static uint16_t fb[320 * 240];
#include "../symce/src/graph.c"

static struct gw w;

int main(int argc, char **argv)
{
    unsigned v, n = 0;
    if (argc < 4)
        return 2;
    for (const char *h = argv[1]; n < MAXTOK && sscanf(h, "%2x", &v) == 1; h += 2, n++)
        w.tok[0][n] = (uint8_t)v;
    w.len[0] = (uint8_t)n;
    w.x0 = argc > 7 ? (float)atof(argv[4]) : -10.0f;
    w.x1 = argc > 7 ? (float)atof(argv[5]) : 10.0f;
    w.y0 = argc > 7 ? (float)atof(argv[6]) : -10.0f;
    w.y1 = argc > 7 ? (float)atof(argv[7]) : 10.0f;
    w.sx = 318.0f / (w.x1 - w.x0);
    w.sy = (float)PH / (w.y1 - w.y0);
    w.pc = (uint16_t)strtoul(argv[2], 0, 0);
    w.st = 1;
    memset(fb, 0xFF, sizeof fb);
    uint8_t e = compile(&w, 0), r = e ? 0 : imptype(&w);
    if (!r) {
        printf("not implicit\n");
        return 1;
    }
    w.yop = 1;
    compile(&w, 0);
    w.yop = 0;
    idraw(&w, r);
    FILE *f = fopen(argv[3], "wb");
    fwrite(fb, 2, 320 * 240, f);
    fclose(f);
    return 0;
}
