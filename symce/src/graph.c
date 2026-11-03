/* SymCE graph: a full-screen function grapher, drawn straight into VRAM, that
 * behaves like the OS graph screen (docs 11b) but 319x219 instead of 265x165.
 *
 * The OS side (main.asm, `4: Graph` on the settings screen) fills a `struct gw`
 * in RAM from the VAT, then calls graph_key(w, scan) in a loop: scan 0 the
 * first time, then each GetCSC code. It returns 0 to stay, 1 to go home, or an
 * OS key code that main.asm hands to the OS (Y=, WINDOW, MODE ...).
 *
 * The screen: the plot is 319x240 at (0,0); column 319 is the border colour. TI's 265x165 grid (gx 0..264, gy 0..164) maps onto it
 * as px = round(gx*318/264), py = round((164-gy)*239/164); Xres samples every
 * Xres logical columns like the OS does. It draws the FORMAT options the
 * OS keeps in RAM: grid (dots or lines), axes and their ticks, labels, the 15
 * colours, and each Yn's line style (thin, normal, dot-thick, dotted).
 *
 * Per equation, once per draw, the tokens compile to a small RPN program
 * (compile) which run() evaluates in float32. Grammar and precedence are
 * engine.c's (measured on OS 5.8.4): or/xor, under and, under the relations,
 * under + -, under * / with implied multiplication and the n/d bar, under ^
 * (left to right), under negation. Supported tokens: digits . EE, X, A-Z and
 * theta (values from the caller), pi, e, + - * /, ^, negation (B0), ( ), the
 * postfix squared, cubed and ^-1, = != > >= < <=, and or xor not( int( iPart(
 * fPart( sqrt( abs( ln( log( e^( 10^( sin( cos( tan( asin( acos( atan(. An
 * equation with any other token (lists, fnInt(, other Yn ...) is plotted
 * through the OS's parser instead, one sample at a time (symce_osy): slow, but
 * exactly TI's answers; ON interrupts it.
 *
 * Like engine.c it runs from flash: no writable statics, all state in `w`.
 *
 * ponytail: the OS window is taken as it is (an empty one never gets here: the OS's ERR:WINDOW RANGE
 * comes first, graph.py fall); float32
 * samples, libc sinf/powf accuracy as it comes; arrows do nothing yet.
 */
#include <stdint.h>
#include <math.h>
#include <string.h>

#define NEQ    12               /* equation slots: Y1..Y0 (10), X1T..Y6T (12), r1..r6 (6) */
#define MAXTOK 96                /* tokens copied per equation; longer goes to the OS parser */
#define MAXOP  112               /* ops in one program */
#define STK    12                /* evaluation stack */
#define IX     40                /* implicit equations: grid cells across (and IY down) */
#define IY     28
#define MAXDEP 6                 /* nested parentheses */
#define PW     319               /* plot columns */
#define PH     239               /* the y scale, as 318 is the x scale: row = (164 - gy) * PH / 164. Bounds are PW columns, SH rows. */
#define VS     320               /* VRAM stride, pixels */
#define SH     240               /* screen rows */

#ifndef VRAM
#define VRAM ((uint16_t *)0xD40000)               /* 320x240, RGB565 */
#endif

/* OS RAM, read in place (the addresses are the same in every build). */
#ifndef OSB
#define OSB(a) (*(volatile uint8_t *)(a))
#endif
#define OSW(a) (*(volatile uint16_t *)(a))
#define ADDR_MODE   0x0D00082    /* grfModeFlags: Func 10, Pol 20, Par 40, Seq 80 */
#define ADDR_DB     0x0D00084    /* grfDBFlags: 04 grid, 40 labels, 80 grid lines */
#define ADDR_ON     0x0D00089    /* onFlags, bit 4 onInterrupt */
#define ADDR_SG     0x0D00094    /* sGrFlags: 01 horizontal, 06 graph-table */
#define ADDR_ASYM   0x0D000C7    /* asymptoteFlags: bit 0 set = detection OFF */
#define ADDR_XSCL   0x0D01E45
#define ADDR_YSCL   0x0D01E60
#define ADDR_XRES   0x0D01EF0    /* Xres, a TI real */
#define ADDR_STYLE  0x0D024BF    /* y1LineType + n */
#define ADDR_GRIDC  0x0D024F1
#define ADDR_AXESC  0x0D024F2
#define ADDR_BGIMG  0x0D024F3    /* nonzero: a background image is on */
#define ADDR_BORDER 0x0D02A98    /* RGB565 */
#define ADDR_YEDIT  0x0D00097    /* Y= edits since the last plot: bit 0 an equation was added (TI draws it over the picture), bit 1 one was changed, deleted or toggled (TI replots and drops its drawing) */
#define ADDR_SEQF   0x0D0008F    /* seqFlags: 01 Web, 04 uv, 08 vw, 10 uw (none: Time) */
#define ADDR_SEQSEL 0x0D02032    /* the sequences with an equation, bits 0..2 u v w (the OS's own, set by its 0x285AA) */
#define ADDR_SEQV   0x0D01D0F    /* u(n), u(n-1), v(n) ... 18 bytes each, after SeqSolve */
#define ADDR_NMAX   0x0D01EA8
#define ADDR_NMIN   0x0D01EC3
#define ADDR_PSTART 0x0D01E9F
#define ADDR_PSTEP  0x0D01EE7

enum { O_K, O_X, O_Y, O_ADD, O_SUB, O_MUL, O_DIV, O_POW,
       O_EQ, O_LT, O_GT, O_LE, O_GE, O_NE,          /* tokens 6A..6F in order */
       O_AND, O_OR, O_XOR, O_MIN, O_MAX,            /* the last two: AND and OR of an implicit equation */
       O_NEG, O_SQ, O_CU, O_INV, O_SQRT, O_LN, O_EXP, O_LOG, O_E10, O_ABS,
       O_NOT, O_INT, O_IPART, O_FPART,
       O_SIN, O_ASIN, O_COS, O_ACOS, O_TAN, O_ATAN };   /* the last six are tokens C2..C7 in order */

enum { E_OK, E_TOK, E_LONG, E_SYN, E_DEEP };

#include "graph_draw.h"
#include "graph_table.h"

/* The first fields, up to `tok`, are what main.asm fills; their offsets are
   fixed there. All bytes, so the layout is the same on the host. */
struct gw {
    uint8_t win[36];             /*   0 in: Xmin, Xmax, Ymin, Ymax as 9-byte TI reals */
    uint8_t deg;                 /*  36 in: 1 = Degree mode */
    uint8_t col[NEQ];            /*  37 in: line colour index, as the OS keeps y1LineColor (Par, Pol: one a pair, curve) */
    uint8_t len[NEQ];            /*  49 in: token count; 0 = not plotted (empty, or not selected) */
    uint8_t var[27][9];          /*  61 in: A..Z, theta as TI reals; zeros where undefined */
    uint8_t tok[NEQ][MAXTOK];    /* 304 in: the equations' tokens */
    /* state, not the assembler's */
    float x0, x1, y0, y1;        /* the window in view */
    float sx, sy;                /* pixels per unit: 318/(x1-x0), 239/(y1-y0) */
    uint8_t op[MAXOP];           /* the program being compiled or run */
    float k[MAXOP];
    uint8_t n, sp, dep, err, errtok;
    uint8_t uy, yop;             /* the equation has a Y; Y compiles to O_Y (an implicit equation) */
    uint16_t imp;                /* bit i: Yi is drawn as a relation of X and Y (TRACE and CALC skip it) */
    float oy;                    /* O_Y's value */
    const uint8_t *p, *e;        /* parse position, end */
    uint8_t pm, n1, lp;          /* mode (0 Func 1 Par 2 Pol); ops of the first program (Par: X's, the Y's follow); last pair plotted + 1 */
    uint8_t tv[9];               /* Par, Pol: the T or theta of the last point plotted, a TI real */
    uint8_t sec, xres, det, st, brk, last;   /* 2nd pressed; Xres; detect asymptotes; line style; ON pressed; last plotted +1 */
    uint16_t pc;                 /* pen colour */
    uint8_t xr[9], buf[9];       /* a TI real out, a TI real back */
    /* the cursor and TRACE (phase 1b) */
    uint8_t mode, shown, frame, fn, hk, vis, yok, snap, nent;   /* M_*; cursor drawn; blink frame; traced Yn; held arrow; ... */
    int8_t pn;                   /* trace: windows panned so far (+ right) */
    int gx, gy, cx, cy;          /* logical cursor; where it is drawn (pixels, centre) */
    unsigned tb, th, tr;         /* timer 1 stamps: blink, hold start, last repeat */
    uint8_t x0r[9], xv[9], yv[9];/* X at trace position 0; the X and Y shown (Par, Pol: xv is the T or theta) */
    uint8_t pxv[9], prv[9];      /* Par, Pol trace: the X and the R there */
    int tk;                      /* Par, Pol, Seq trace: the step of the T (n) grid */
    uint8_t pt;                  /* trace over a stat plot: its number 1..3, 0 = a function */
    unsigned pi;                 /* ... and the point's place in its lists */
    int sn;                      /* Seq: the n the OS's solver is at, -1 = not started */
    uint8_t ent[24];             /* the typed tokens */
    union {
        uint16_t sv[81];         /* the pixels under the cursor */
        struct {                 /* idraw's two rows of nodes (render drew the cursor away: shown is 0): f, and 0 = NaN / infinite, 1 = f < 0, 2 = f >= 0 */
            float f[2][IX + 1];
            uint8_t c[2][IX + 1];
        } fr;
    };
    /* ZOOM (phase 1c) */
    uint8_t zm, zc;              /* 0 or the zoom the cursor is for (Z_BOX ...); ZBox: first corner set */
    int zx, zy;                  /* ZBox: the first corner, logical */
    int zr[4];                   /* ZBox: x0 y0 x1 y1 of the drawn outline (pixels) */
    uint8_t z1x[9], z1y[9];      /* ZBox: X and Y at the first corner */
    /* 2nd CALC (phase 1d) */
    uint8_t cm, cs, cf, cf2, cres, sr;   /* item (C_*) or 0; stage; first and second curve; 1 result up, 2 cursor gone; label strip rows */
    uint8_t cl[9], cr[9], cd[9]; /* left bound / lower limit, right bound / upper limit, dy/dx or the integral (TI reals) */
    int cmc[2];                  /* the bound markers' columns */
    char ptb[28], ptc[40];       /* the prompt or result line above the bar; the integral's limits in place of X= and Y= */
    uint8_t psv;                 /* the PBR plot rows under ptb are saved in PBUF */
    /* TRACE snap (function mode): the samples seen and the POI label */
    uint8_t tps, hn;             /* a POI label is up; samples kept (0..2) */
    int8_t tpd, hdir;            /* the direction the step to the POI went; the direction the samples were taken in */
    uint16_t hdv;                /* bit j: hd[j] is valid */
    int tga, tgb;                /* the grid gx an arrow in the POI's direction / the other way goes to */
    float hy[2], hd[NEQ];        /* Y at the newest two samples; Yfn - Yj at the newest */
    uint8_t hx[2][9];            /* their X */
    uint8_t pl[9];               /* the last POI's X (pl[0] 0xFF: none) */
    uint16_t pfg;                /* its colour */
    struct dst dr;               /* 2nd DRAW (graph_draw.c) */
    struct tbl tbl;              /* TABLE (graph_table.c) */
};

_Static_assert(sizeof(struct gw) <= 0xC00, "gw past GWN");
_Static_assert(__builtin_offsetof(struct gw, tok) == 304, "main.asm fills tok at 304");
static uint8_t tb_fall(void);
static uint8_t dr_foreign(const struct gw *w);
static void dr_sync(struct gw *w), dr_overlay(void), dr_undo(struct gw *w), dr_prev(struct gw *w), dr_tpaste(struct gw *w);
static void dr_pick(struct gw *w, uint8_t n), dr_enter(struct gw *w), dr_reset(void), dr_home(struct gw *w), dtent(struct gw *w);
static uint8_t mget(int i);
static void gcoord(uint8_t *out, const uint8_t *base, int g, unsigned d);
void symce_stoy(const uint8_t *r);
void symce_stox(const uint8_t *r);
void symce_fp(unsigned fn, const uint8_t *a, const uint8_t *b, uint8_t *out);
void symce_fmt(const uint8_t *r, char *out);
void symce_fmtw(const uint8_t *r, char *out, unsigned width);
void symce_fmtn(const uint8_t *r, char *out, unsigned width);
void symce_fill(uint16_t *p, unsigned n, unsigned c);
void symce_text_(int x, int y, unsigned fg, unsigned bg, const char *s, unsigned big);
void symce_text(int x, int y, unsigned fg, unsigned bg, const char *s, unsigned big);
unsigned symce_eval(const uint8_t *tok, unsigned n, uint8_t *out);
void symce_tok_(const uint8_t *t, char *out);
const uint8_t *symce_sglyph(unsigned c);
static unsigned tkn(uint8_t b)
{
    static const uint8_t pre[] = { 0x5C, 0x5D, 0x5E, 0x60, 0x61, 0x62, 0x63, 0x7E, 0xAA, 0xBB, 0xEF, 0 };
    return b && strchr((const char *)pre, b) ? 2 : 1;  /* the two-byte tokens' first bytes */
}
static unsigned symce_tok(const uint8_t *t, char *out)
{
    symce_tok_(t, out);
    return tkn(*t);
}
void symce_dirty(void);
unsigned symce_calc(unsigned kind, unsigned fn1, const uint8_t *l, const uint8_t *r, const uint8_t *g, unsigned fn2, uint8_t *out);
void symce_stoans(const uint8_t *r);
void symce_rom(unsigned a);
void symce_roma(unsigned a, unsigned n);
void symce_zxy(unsigned in);
void symce_throw(unsigned code, unsigned goto_trace) __attribute__((noreturn));
unsigned symce_seq(unsigned op, const uint8_t *n);

uint8_t symce_osy(uint8_t n, const uint8_t *xr, uint8_t *out, uint8_t v);
void symce_stov(uint8_t v, const uint8_t *r);
#define ADDR_MODE_ 0x0D00082

/* Mode: 0 Function, 1 Parametric, 2 Polar, 3 Sequence (grfModeFlags 40, 20, 80). The equation
   slots: Y1..Y0, X1T Y1T .. X6T Y6T, r1..r6, u v w; a curve is a slot, or a Par pair. */
static uint8_t pmode(void)
{
    uint8_t m = *(volatile uint8_t *)ADDR_MODE_;
    return m & 0x80 ? 3 : m & 0x40 ? 1 : m & 0x20 ? 2 : 0;
}

static uint8_t nu(const struct gw *w)
{
    return w->pm == 3 ? 3 : w->pm ? 6 : 10;
}

/* Yi is plotted and TRACE / CALC may stand on it (not an implicit one). */
static uint8_t lenok(const struct gw *w, uint8_t i)
{
    return w->len[i] && !(w->imp >> i & 1);
}

static uint8_t hv(const struct gw *w, uint8_t i)
{
    return w->pm == 1 ? w->len[2 * i] && w->len[2 * i + 1] : w->len[i];
}

static uint8_t have(const struct gw *w, uint8_t i)
{
    return w->pm == 1 ? hv(w, i) : lenok(w, i);
}

/* The OS's name byte less 0x10 of slot s (symce_osy adds the 0x10), and the variable the equations' X key reads: X, T, theta. */
static uint8_t osn(const struct gw *w, uint8_t s)
{
    return (uint8_t)(s + (w->pm == 1 ? 0x10 : w->pm == 2 ? 0x30 : 0));
}

static uint8_t vtok(const struct gw *w)
{
    return w->pm == 1 ? 0x54 : w->pm == 2 ? 0x5B : 0x58;
}

/* An error inside a CALC: the OS text routines moved the home cursor, TI's error returns it where it was at GRAPH. */
static void __attribute__((noreturn)) cthrow(struct gw *w, unsigned e)
{
    if (w->cm)
        memcpy((void *)0x0D00595, (const void *)0x0D0FB0B, 3);
    symce_throw(e, 1);
}

static float p10(int e)
{
    return powf(10.0f, (float)e);
}

/* A TI real: sign in bit 7 of byte 0, byte 1 is 0x80 + the exponent, then 14
   BCD digits d.ddd...; nine are enough for a float. */
static float real(const uint8_t *r)
{
    uint32_t m = 0;
    for (uint8_t i = 0; i < 9; i++)
        m = m * 10 + ((i & 1) ? r[2 + (i >> 1)] & 15 : r[2 + (i >> 1)] >> 4);
    if (!m)
        return 0.0f;
    float v = (float)m * p10((int)r[1] - 0x80 - 8);
    return (r[0] & 0x80) ? -v : v;
}

/* A float as a TI real of eight digits. */
static void f2r(uint8_t *r, float v)
{
    float a = fabsf(v);
    uint32_t m;
    uint8_t d[8];
    int e;
    memset(r, 0, 9);
    r[1] = 0x80;
    if (!(a < 1e30f) || a < 1e-30f)
        return;
    e = (int)floorf(log10f(a));
    m = (uint32_t)(a * p10(7 - e) + 0.5f);
    if (m >= 100000000UL) {
        m /= 10;
        e++;
    } else if (m < 10000000UL) {
        m *= 10;
        e--;
    }
    for (int8_t i = 7; i >= 0; i--) {
        d[i] = (uint8_t)(m % 10);
        m /= 10;
    }
    r[0] = v < 0 ? 0x80 : 0;
    r[1] = (uint8_t)(0x80 + e);
    for (uint8_t i = 0; i < 4; i++)
        r[2 + i] = (uint8_t)(d[2 * i] << 4 | d[2 * i + 1]);
}

static void emit(struct gw *w, uint8_t op, float k)
{
    if (w->err)
        return;
    if (w->n >= MAXOP) {
        w->err = E_LONG;
        return;
    }
    if (op <= O_Y) {
        if (w->sp >= STK) {
            w->err = E_DEEP;
            return;
        }
        w->sp++;
    } else if (op >= O_ADD && op <= O_MAX)
        w->sp--;
    w->op[w->n] = op;
    w->k[w->n++] = k;
}

static int peek(const struct gw *w)
{
    return w->p < w->e ? *w->p : -1;
}

static void disj(struct gw *w);

static void number(struct gw *w)
{
    uint32_t m = 0;
    int e = 0, t;
    while ((t = peek(w)) >= '0' && t <= '9') {
        if (m < 100000000UL)
            m = m * 10 + (uint32_t)(t - '0');
        else
            e++;
        w->p++;
    }
    if (peek(w) == 0x3A) {                       /* the decimal point */
        w->p++;
        while ((t = peek(w)) >= '0' && t <= '9') {
            if (m < 100000000UL) {
                m = m * 10 + (uint32_t)(t - '0');
                e--;
            }
            w->p++;
        }
    }
    if (peek(w) == 0x3B) {                       /* EE */
        int x = 0, neg = 0;
        w->p++;
        if (peek(w) == 0xB0) {
            neg = 1;
            w->p++;
        }
        while ((t = peek(w)) >= '0' && t <= '9') {
            if (x < 100)
                x = x * 10 + (t - '0');
            w->p++;
        }
        e += neg ? -x : x;
    }
    if (e > 60)
        e = 60;
    if (e < -60)
        e = -60;
    emit(w, O_K, (float)m * p10(e));
}

/* '(' or a function token: the argument, and its ')' unless the entry ends. */
static void group(struct gw *w, uint8_t fn)
{
    if (++w->dep > MAXDEP) {
        w->err = E_DEEP;
        return;
    }
    disj(w);
    if (peek(w) == 0x11)
        w->p++;
    w->dep--;
    if (fn != O_K)
        emit(w, fn, 0);
}

static void primary(struct gw *w)
{
    int t = peek(w);
    if (w->err)
        return;
    if (t < 0) {
        w->err = E_SYN;
        return;
    }
    if ((t >= '0' && t <= '9') || t == 0x3A) {
        number(w);
        return;
    }
    w->p++;
    if (t == 0x11)                               /* a ) with nothing before it */
        w->err = E_SYN;
    else if (t == vtok(w))                       /* X, or T / theta in Par / Pol */
        emit(w, O_X, 0);
    else if (t >= 0x41 && t <= 0x5B) {
        if (t == 0x59) {
            w->uy = 1;
            if (w->yop) {
                emit(w, O_Y, 0);
                return;
            }
        }
        emit(w, O_K, real(w->var[t - 0x41]));
    }
    else if (t == 0xAC)
        emit(w, O_K, 3.14159265f);
    else if (t == 0xBB && peek(w) == 0x31) {     /* e is BB 31 */
        w->p++;
        emit(w, O_K, 2.71828183f);
    } else if (t == 0x10)
        group(w, O_K);
    else if (t >= 0xC2 && t <= 0xC7)
        group(w, (uint8_t)(O_SIN + t - 0xC2));
    else if (t == 0xBC)
        group(w, O_SQRT);
    else if (t == 0xBE)
        group(w, O_LN);
    else if (t == 0xBF)
        group(w, O_EXP);
    else if (t == 0xC0)
        group(w, O_LOG);
    else if (t == 0xC1)
        group(w, O_E10);
    else if (t == 0xB2)
        group(w, O_ABS);
    else if (t == 0xB8)
        group(w, O_NOT);
    else if (t == 0xB1)
        group(w, O_INT);
    else if (t == 0xB9)
        group(w, O_IPART);
    else if (t == 0xBA)
        group(w, O_FPART);
    else {
        w->err = E_TOK;
        w->errtok = (uint8_t)t;
    }
}

static void postfix(struct gw *w)
{
    int t;
    primary(w);
    while ((t = peek(w)) == 0x0D || t == 0x0F || t == 0x0C) {
        w->p++;
        emit(w, t == 0x0D ? O_SQ : t == 0x0F ? O_CU : O_INV, 0);
    }
}

/* Any number of negations, then a postfix. (Recursive: the toggling loop it
   replaces was compiled wrongly for the eZ80, the NEG went missing.) */
static void negpost(struct gw *w)
{
    if (!w->err && peek(w) == 0xB0) {
        w->p++;
        negpost(w);
        emit(w, O_NEG, 0);
    } else
        postfix(w);
}

static void power(struct gw *w)
{
    postfix(w);
    while (!w->err && peek(w) == 0xF0) {
        w->p++;
        negpost(w);
        emit(w, O_POW, 0);
    }
}

static void unary(struct gw *w)
{
    if (!w->err && peek(w) == 0xB0) {
        w->p++;
        unary(w);
        emit(w, O_NEG, 0);
    } else
        power(w);
}

/* Does the next token begin a primary, so that it multiplies what came before? */
static int starts(const struct gw *w)
{
    int t = peek(w);
    return (t >= '0' && t <= '9') || t == 0x3A || (t >= 0x41 && t <= 0x5B) || t == 0x10 ||
           t == 0xAC || t == 0xBB || t == 0xB1 || t == 0xB2 || t == 0xB8 || t == 0xB9 ||
           t == 0xBA || (t >= 0xBC && t <= 0xC7);
}

static void term(struct gw *w)
{
    int t;
    unary(w);
    while (!w->err) {
        t = peek(w);
        if (t == 0x82 || t == 0x83) {
            w->p++;
            unary(w);
            emit(w, t == 0x82 ? O_MUL : O_DIV, 0);
        } else if (t == 0xEF && w->p + 1 < w->e && w->p[1] == 0x2E) {    /* the n/d bar */
            w->p += 2;
            unary(w);
            emit(w, O_DIV, 0);
        } else if (starts(w)) {
            unary(w);
            emit(w, O_MUL, 0);
        } else
            break;
    }
}

static void expr(struct gw *w)
{
    int t;
    term(w);
    while (!w->err && ((t = peek(w)) == 0x70 || t == 0x71)) {
        w->p++;
        term(w);
        emit(w, t == 0x70 ? O_ADD : O_SUB, 0);
    }
}

static void rel(struct gw *w)
{
    int t;
    expr(w);
    while (!w->err && (t = peek(w)) >= 0x6A && t <= 0x6F) {
        w->p++;
        expr(w);
        if (w->yop) {                            /* implicit: left - right, or right - left for < <=: >= 0 where it holds */
            emit(w, O_SUB, 0);
            if (t == 0x6B || t == 0x6D)
                emit(w, O_NEG, 0);
        } else
            emit(w, (uint8_t)(O_EQ + t - 0x6A), 0);
    }
}

static void conj(struct gw *w)
{
    rel(w);
    while (!w->err && peek(w) == 0x40) {
        w->p++;
        rel(w);
        emit(w, w->yop ? O_MIN : O_AND, 0);
    }
}

static void disj(struct gw *w)
{
    int t;
    conj(w);
    while (!w->err && ((t = peek(w)) == 0x3C || t == 0x3D)) {
        w->p++;
        conj(w);
        emit(w, t == 0x3C ? (w->yop ? O_MAX : O_OR) : O_XOR, 0);
    }
}

/* Equation i to w->op / w->k from op b on (Par: Y follows X); returns 0 or an E_ code (w->errtok: the token). */
static uint8_t compilet(struct gw *w, const uint8_t *t, uint8_t l, uint8_t b)
{
    w->n = b;
    w->sp = w->dep = w->err = w->uy = 0;
    w->errtok = 0;
    if (l > MAXTOK)
        return E_LONG;
    w->p = t;
    w->e = t + l;
    disj(w);
    if (!w->err && w->p < w->e) {                /* something disj() does not take */
        w->err = *w->p == 0x11 ? E_SYN : E_TOK;
        w->errtok = *w->p;
    }
    return w->err;
}

static uint8_t compileb(struct gw *w, uint8_t i, uint8_t b)
{
    return compilet(w, w->tok[i], w->len[i], b);
}

static uint8_t compile(struct gw *w, uint8_t i)
{
    return compileb(w, i, 0);
}

/* ops b0..b1 at x */
__attribute__((noinline)) static float runr(const struct gw *w, uint8_t b0, uint8_t b1, float x)
{
    float s[STK + 1];
    uint8_t sp = 0;
    float a, b, dg = w->deg ? 0.01745329252f : 1.0f;
    for (uint8_t i = b0; i < b1; i++) {
        uint8_t o = w->op[i];
        if (o <= O_Y) {
            s[sp++] = o == O_K ? w->k[i] : o == O_X ? x : w->oy;
            continue;
        }
        a = s[sp - 1];
        if (o <= O_MAX) {                        /* binary */
            b = a;
            a = s[--sp - 1];
            switch (o) {
            case O_ADD: a = a + b; break;
            case O_SUB: a = a - b; break;
            case O_MUL: a = a * b; break;
            case O_DIV: a = a / b; break;
            case O_POW: a = powf(a, b); break;
            case O_EQ:  a = a == b ? 1.0f : 0.0f; break;
            case O_NE:  a = a != b ? 1.0f : 0.0f; break;
            case O_LT:  a = a < b ? 1.0f : 0.0f; break;
            case O_GT:  a = a > b ? 1.0f : 0.0f; break;
            case O_LE:  a = a <= b ? 1.0f : 0.0f; break;
            case O_GE:  a = a >= b ? 1.0f : 0.0f; break;
            case O_AND: a = a != 0 && b != 0 ? 1.0f : 0.0f; break;
            case O_OR:  a = a != 0 || b != 0 ? 1.0f : 0.0f; break;
            case O_MIN: a = a < b ? a : b; break;
            case O_MAX: a = a > b ? a : b; break;
            default:    a = (a != 0) != (b != 0) ? 1.0f : 0.0f; break;
            }
        } else switch (o) {
        case O_NEG:   a = 0.0f - a; break;      /* (-a was __fneg, which came back unchanged here) */
        case O_SQ:    a = a * a; break;
        case O_CU:    a = a * a * a; break;
        case O_INV:   a = 1.0f / a; break;
        case O_SQRT:  a = sqrtf(a); break;
        case O_LN:    a = logf(a); break;
        case O_EXP:   a = expf(a); break;
        case O_LOG:   a = log10f(a); break;
        case O_E10:   a = powf(10.0f, a); break;
        case O_ABS:   a = fabsf(a); break;
        case O_NOT:   a = a == 0 ? 1.0f : 0.0f; break;
        case O_INT:   a = floorf(a); break;
        case O_IPART: a = truncf(a); break;
        case O_FPART: a = a - truncf(a); break;
        case O_SIN:   a = sinf(a * dg); break;
        case O_COS:   a = cosf(a * dg); break;
        case O_TAN:   a = tanf(a * dg); break;
        case O_ASIN:  a = asinf(a) / dg; break;
        case O_ACOS:  a = acosf(a) / dg; break;
        default:      a = atanf(a) / dg; break;
        }
        if (!(fabsf(a) < 1e30f))                 /* 1/0, ln(0) and the like: not 0 later on */
            return __builtin_nanf("");
        s[sp - 1] = a;
    }
    return s[0];
}

static inline float run(const struct gw *w, float x)
{
    return runr(w, 0, w->n, x);
}

/* (A comparison is written as ?: above: the compiler's bool -> float on the eZ80
   gave -1 for true.) */

/* ---- drawing ---- */

/* The OS's 15 colours, by palette index (measured: 1..7 are the defaults of
   Y1..Y7). Index 0 and anything else draw black. */
static uint16_t pal(uint8_t i)
{
    static const uint16_t c[16] = { 0, 0x001F, 0xF800, 0x0000, 0xF81F, 0x04E0, 0xFC64, 0xB100,
                                    0x0010, 0x049F, 0xFFE0, 0xFFFF, 0xE71C, 0xC618, 0x8C51, 0x52AA };
    return i < 16 ? c[i] : 0;
}

/* A TI real in OS RAM as a float. */
static float real_os(uint32_t a)
{
    return real((const uint8_t *)(uintptr_t)a);
}

/* TI's grid <-> the screen, for the cursor (phase 1b). */
__attribute__((unused)) static int gx2px(int gx)
{
    return (gx * 318 + 132) / 264;
}

__attribute__((unused)) static int gy2py(int gy)
{
    return ((164 - gy) * PH + 82) / 164;
}

/* floor(v + 0.5), v clamped: a truncating convert, minus one below zero if it cut something off
   (cheaper than libm's floorf on the eZ80). */
static int rnd(float v)
{
    int i;
    v = v < -30000.0f ? -30000.0f : v > 30000.0f ? 30000.0f : v;
    v += 0.5f;
    i = (int)v;
    return v < 0 && (float)i != v ? i - 1 : i;
}

/* fabsf(y) < 1e30f by its bits (1e30f is 0x7149F2CA; NaN is above): the top byte settles all but
   the last few binades. */
static inline __attribute__((always_inline)) int fin1e30(float y)
{
    volatile float v = y;                        /* in memory, so the byte is one load (not shifts) */
    uint32_t u;
    if ((((const volatile uint8_t *)&v)[3] & 0x7F) < 0x71)
        return 1;
    memcpy(&u, &y, 4);
    return (u & 0x7FFFFFFFu) < 0x7149F2CAu;
}

/* rnd(r) for curve() off r's bytes (no float add, compare or convert). Positive: floor(r + 0.5)
   for r < 256 is the top bits of the mantissa plus the bit below them (the add itself never
   rounds across an integer, bar r = 0.5 - 2^-25, which it takes to 1); off the screen either
   way as +-30000 (nothing below tells 30000 from 256 or 20000). A small negative: rnd(). */
static inline __attribute__((always_inline)) int rowof(float r)
{
    volatile float v = r;                        /* likewise */
    const volatile uint8_t *b = (const volatile uint8_t *)&v;
    uint8_t e = (uint8_t)(b[3] << 1 | b[2] >> 7), t, u;
    if (b[3] & 0x80)
        return e >= 129 ? -30000 : rnd(r);
    if (e >= 135)
        return 30000;
    if (e < 126)
        return e == 125 && b[2] == 0xFF && b[1] == 0xFF && b[0] == 0xFF;
    t = b[2] | 0x80;                             /* 1.xxxxxxx, the mantissa's top byte */
    if (e == 134)
        return t + (b[1] >> 7);
    u = t >> (133 - e);                          /* r's integer part and the half bit */
    return (u >> 1) + (u & 1);
}

static void put(int x, int y, uint16_t c)
{
    if ((unsigned)x < PW && (unsigned)y < SH)
        VRAM[y * VS + x] = c;
}

static void hline(int y, uint16_t c)
{
    if ((unsigned)y < SH)
        for (int x = 0; x < PW; x++)
            VRAM[y * VS + x] = c;
}

static void vline(int x, uint16_t c)
{
    if ((unsigned)x < PW)
        for (int y = 0; y < SH; y++)
            VRAM[y * VS + x] = c;
}

static void block(int x, int y, int r, uint16_t c)
{
    for (int j = -r; j <= r; j++)
        for (int i = -r; i <= r; i++)
            put(x + i, y + j, c);
}

/* Column x, rows ya..yb, clipped to the plot. */
static inline __attribute__((always_inline)) void fillcol(int x, int ya, int yb, uint16_t c)
{
    uint16_t *p;
    if ((unsigned)x >= PW)
        return;
    if (ya < 0)
        ya = 0;
    if (yb >= SH)
        yb = SH - 1;
    for (p = VRAM + ya * VS + x; ya <= yb; ya++, p += VS)
        *p = c;
}

/* Pixel (x, y) with no multiply (y * 320 = y * 256 + y * 64), n pixels down from p. */
#define PX(x, y) (VRAM + (((y) << 8) + ((y) << 6)) + (x))

static inline __attribute__((always_inline)) void vfill(uint16_t *p, int n, uint16_t c)
{
    for (; n > 0; n--, p += VS)
        *p = c;
}

/* line() for x1 = x0 + 1, which Bresenham plots as a run of (d - 1) / 2 + 1 pixels
   (d = |dy|; one if d is 0) in column x0 and the rest in column x1. The pen adds a
   pixel right of and below each. a..b are the rows of the first run, c..e of the
   second. */
static void step(const struct gw *w, int x, int y0, int y1)
{
    int d = y1 > y0 ? y1 - y0 : y0 - y1, k = d ? (d - 1) >> 1 : 0, a, b, c, e;
    uint16_t pc = w->pc;
    if (y1 >= y0) {
        a = y0, b = y0 + k, c = d ? b + 1 : y0, e = y1;
        if (w->st == 1 && a >= 0 && e + 1 < SH && (unsigned)x < PW - 2) {   /* all on the plot: no clips */
            uint16_t *p = PX(x, a);
            vfill(p, k + 2, pc);                 /* x: a..b+1 */
            vfill(p + 1, d + 2, pc);             /* x+1: a..e+1 */
            vfill(PX(x + 2, c), d ? d - k : 1, pc);   /* x+2: c..e */
            return;
        }
        if (w->st != 1) {
            fillcol(x, a, b, pc);
            fillcol(x + 1, c, e, pc);
            return;
        }
        fillcol(x, a, b + 1, pc);
        fillcol(x + 1, a, e + 1, pc);
        fillcol(x + 2, c, e, pc);
    } else {
        a = y0 - k, b = y0, c = y1, e = a - 1;
        if (w->st == 1 && c >= 0 && b + 1 < SH && (unsigned)x < PW - 2) {
            uint16_t *p = PX(x, c);
            vfill(PX(x, a), k + 2, pc);          /* x: a..b+1 */
            vfill(p + 1, d + 1, pc);             /* x+1: c..b */
            vfill(p + 2, d - k, pc);             /* x+2: c..e */
            return;
        }
        if (w->st != 1) {
            fillcol(x, a, b, pc);
            fillcol(x + 1, c, e, pc);
            return;
        }
        fillcol(x, a, b + 1, pc);
        fillcol(x + 1, c, b, pc);
        fillcol(x + 2, c, e, pc);
    }
}

/* Bresenham with the pen: thin is a pixel, normal adds the one right and the one
   below (each clipped on its own). One column or two (all curve() draws unless Xres
   is up) are the column runs the loop below would plot: at dx = 1 the first run is
   (d - 1) / 2 + 1 rows long (d = |dy|), the rest is in the next column. */
static void line(const struct gw *w, int x0, int y0, int x1, int y1)
{
    if (x1 == x0 + 1) {
        step(w, x0, y0, y1);
        return;
    }
    if (x1 == x0) {                              /* a point or a vertical line: no right pixel in the way */
        int a = y1 < y0 ? y1 : y0, b = y1 < y0 ? y0 : y1;
        fillcol(x0, a, w->st == 1 ? b + 1 : b, w->pc);
        if (w->st == 1)
            fillcol(x0 + 1, a, b, w->pc);
        return;
    }
    {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy, e2;
    int ro = y0 * VS, rs = sy * VS;
    uint16_t c = w->pc;
    uint8_t th = w->st == 1;
    for (;;) {
        uint8_t inx = (unsigned)x0 < PW, iny = (unsigned)y0 < SH;
        if (inx && iny)
            VRAM[ro + x0] = c;
        if (th) {
            if (inx && (unsigned)(y0 + 1) < SH)
                VRAM[ro + VS + x0] = c;
            if (iny && (unsigned)(x0 + 1) < PW)
                VRAM[ro + x0 + 1] = c;
        }
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * e;
        if (e2 >= dy) {
            e += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            e += dx;
            y0 += sy;
            ro += rs;
        }
    }
    }
}

/* A segment in pixel coordinates, rows as floats that may be far off screen:
   clipped to a few rows around the plot, then drawn. */
static void seg(const struct gw *w, float xa, float ya, float xb, float yb)
{
    const float lo = -4.0f, hi = (float)PH + 3.0f;
    float ta = 0, tb = 1, dy = yb - ya, dx = xb - xa;
    if ((ya < lo && yb < lo) || (ya > hi && yb > hi))
        return;
    if (ya < lo)
        ta = (lo - ya) / dy;
    else if (ya > hi)
        ta = (hi - ya) / dy;
    if (yb < lo)
        tb = (lo - ya) / dy;
    else if (yb > hi)
        tb = (hi - ya) / dy;
    line(w, rnd(xa + dx * ta), rnd(ya + dy * ta), rnd(xa + dx * tb), rnd(ya + dy * tb));
}

/* How many multiples of s lie in lo..hi (0 if s is 0 or there would be more
   than a column's worth), and the first one. */
static int mult(float lo, float hi, float s, float *k0)
{
    float a, b;
    if (!(s > 0))
        return 0;
    a = ceilf(lo / s);
    b = floorf(hi / s);
    if (b < a || b - a > (float)PW)
        return 0;
    *k0 = a;
    return (int)(b - a) + 1;
}

static int colat(const struct gw *w, float k, int j, float s)
{
    return rnd(((k + (float)j) * s - w->x0) * w->sx);
}

static int rowat(const struct gw *w, float k, int j, float s)
{
    return rnd((w->y1 - (k + (float)j) * s) * w->sy);
}

static void grid(const struct gw *w, uint8_t lines, uint16_t c)
{
    float xs = real_os(ADDR_XSCL), ys = real_os(ADDR_YSCL), kx = 0, ky = 0;
    int nx = mult(w->x0, w->x1, xs, &kx), ny = mult(w->y0, w->y1, ys, &ky);
    for (int i = 0; i < nx; i++) {
        int cx = colat(w, kx, i, xs);
        if (lines)
            vline(cx, c);
        else
            for (int j = 0; j < ny; j++)
                block(cx, rowat(w, ky, j, ys), 1, c);
    }
    if (lines)
        for (int j = 0; j < ny; j++)
            hline(rowat(w, ky, j, ys), c);
}

static void axes(const struct gw *w, uint16_t c)
{
    float xs = real_os(ADDR_XSCL), ys = real_os(ADDR_YSCL), kx = 0, ky = 0;
    int nx = mult(w->x0, w->x1, xs, &kx), ny = mult(w->y0, w->y1, ys, &ky);
    int ax = -1, ay = -1;
    if (w->y0 <= 0 && w->y1 >= 0) {
        ay = rnd(w->y1 * w->sy);
        hline(ay, c);
    }
    if (w->x0 <= 0 && w->x1 >= 0) {
        ax = rnd(-w->x0 * w->sx);
        vline(ax, c);
    }
    if (ay >= 0)
        for (int i = 0; i < nx; i++) {
            int cx = colat(w, kx, i, xs);
            for (int d = -4; d <= 4; d++)
                put(cx, ay + d, c);
        }
    if (ax >= 0)
        for (int j = 0; j < ny; j++) {
            int cy = rowat(w, ky, j, ys);
            for (int d = -4; d <= 4; d++)
                put(ax + d, cy, c);
        }
}

/* TI's axis labels: a light gray box with the letter cut out in black. */
static void label(int x0, int y0, int wd, const uint16_t *rows)
{
    for (int j = 0; j < 14; j++)
        for (int i = 0; i < wd; i++)
            put(x0 + i, y0 + j, (rows[j] >> (wd - 1 - i)) & 1 ? 0 : 0xE71C);
}

static void labels(void)
{
    static const uint16_t ly[14] = { 0, 0, 0, 0, 0xCC, 0xCC, 0xCC, 0x4C, 0x38, 0x30, 0xE0, 0xC0, 0, 0 };
    static const uint16_t lx[14] = { 0, 0, 0, 0, 0x18C, 0x18C, 0xD8, 0x70, 0x70, 0xD8, 0x18C, 0x18C, 0, 0 };
    label(1, 10, 10, ly);
    label(304, 194, 11, lx);
}

static float osy(struct gw *w, uint8_t i, float x)
{
    f2r(w->xr, x);
    if (symce_osy(osn(w, i), w->xr, w->buf, vtok(w)))
        return __builtin_nanf("");
    return real(w->buf);
}

/* Equation i across the plot: from the compiled program, or (slow) the OS's
   parser. The pen and style are in w->pc and w->st. */
__attribute__((noinline)) static void curve(struct gw *w, uint8_t i, uint8_t slow)
{
    float pv = 0, pr = 0, xspan = w->x1 - w->x0;
    int pcol = 0, prr = 0, have = 0, step = w->xres > 1 ? w->xres : 0;
    uint8_t ins, pins = 0;
    /* Rows -3..221 are inside seg()'s clip band (-4..222), so a segment between two of
       them is drawn unclipped: no NaN rows (an absurd window) for that. */
    uint8_t fast = fabsf(w->y1) < 1e30f && w->sy < 1e30f;
    for (int s = 0;; s++) {
        int col, rr;
        float x, y, r;
        if (step) {
            int g = s * step;
            if (g > 264)
                break;
            col = (g * 318 + 132) / 264;
            x = w->x0 + (float)g * xspan / 264.0f;
        } else {
            if (s >= PW)
                break;
            col = s;
            x = w->x0 + (float)s * xspan / 318.0f;
        }
        if (slow) {
            if (OSB(ADDR_ON) & 0x10) {
                w->brk = 1;
                return;
            }
            y = osy(w, i, x);
        } else
            y = run(w, x);
        if (!fin1e30(y)) {                        /* NaN, infinity: the line breaks */
            have = 0;
            continue;
        }
        r = (w->y1 - y) * w->sy;
        if (fast)
            rr = rowof(r);
        else
            rr = rnd(r);
        if (w->st == 6) {
            block(col, rr, 1, w->pc);
            continue;
        }
        if (w->st == 7) {
            if (!(col & 1))
                put(col, rr, w->pc);
            continue;
        }
        ins = fast && (unsigned)(rr - 1) < 217;      /* r in 0.5..217.5: y is inside the window */
        if (have && w->det && !(ins && pins) && ((pv > w->y1 && y < w->y0) || (pv < w->y0 && y > w->y1)))
            have = 0;                            /* across an asymptote */
        if (fast && (unsigned)(rr + 3) < 225) {
            if (have && (unsigned)(prr + 3) < 225)   /* seg(): ya + (yb - ya) is its far end */
                /* pr + (r - pr) is r when r - pr is exact (Sterbenz: within a factor 2, and the rows
                   say so with room to spare) */
                line(w, pcol, prr, col, rr <= 2 * prr - 2 && prr <= 2 * rr - 2 ? rr : rnd(pr + (r - pr)));
            else if (have)
                seg(w, (float)pcol, pr, (float)col, r);
            else
                line(w, col, rr, col, rr);
        } else if (have)
            seg(w, (float)pcol, pr, (float)col, r);
        else
            seg(w, (float)col, r, (float)col, r);
        pv = y;
        pr = r;
        pcol = col;
        prr = rr;
        pins = ins;
        have = 1;
    }
}


/* ---- implicit equations and inequalities (docs: implicit) ----
   A Yn that has a Y and is one relation (= < > <= >=), or relations joined by and / or: TI's graph says
   ERR:INVALID for any Y in an equation, so this replaces nothing it draws. Recompiled with w->yop, every
   relation is left - right (right - left for < <=) so that f >= 0 where it holds, and/or are min/max, Y is
   O_Y. f is run at the nodes of an IX x IY grid, two rows at a time; per cell, marching squares draws
   f = 0 from the linear crossing on each edge (continuous: a shared edge gives the same point to both
   cells), and an inequality tints the pen colour (1/4 of it over what is there, so overlaps and the axes
   show through) where f >= 0, the cell's boundary pixels by f's bilinear value, a row at a time. A strict
   relation (< >) makes the boundary dashed (5 x 4 pixel checks, by segment). */
static void tint(uint16_t *p, int n, uint16_t q)
{
    for (; n > 0; n--, p++) {
        uint16_t d = *p;
        *p = (uint16_t)(d - ((d >> 2) & 0x39E7) + q);
    }
}

#define I_SH 1                                   /* inequality: tint */
#define I_SO 2                                   /* solid boundary */

static void icell(struct gw *w, int i, int j, float f0, float f1, float f2, float f3, uint8_t fl)
{
    const float dxp = 318.0f / IX, dyp = (float)PH / IY;
    float x0 = (float)i * dxp, x1 = (float)(i + 1) * dxp, y0 = (float)j * dyp, y1 = (float)(j + 1) * dyp;
    uint8_t sa = f0 >= 0, sb = f1 >= 0, sc = f2 >= 0, sd = f3 >= 0, n = 0;
    float qx[4], qy[4], t;
    if (fl & I_SH) {
        uint8_t in = sa + sb + sc + sd;
        int c0 = rnd(x0), c1 = i == IX - 1 ? PW : rnd(x1), r0 = rnd(y0), r1 = j == IY - 1 ? SH : rnd(y1);
        uint16_t q = (w->pc >> 2) & 0x39E7;
        if (in == 4)
            for (int r = r0; r < r1; r++)
                tint(PX(c0, r), c1 - c0, q);
        else if (in)
            for (int r = r0; r < r1; r++) {
                float v = ((float)r - y0) * (IY / (float)PH), a = f0 + v * (f3 - f0), b = (f1 - f0) + v * ((f2 - f3) - (f1 - f0));
                int k0 = c0, k1 = c1, k;
                if (b == 0) {
                    if (a < 0)
                        continue;
                } else {
                    k = rnd(x0 - a / b * dxp);
                    if (b > 0)
                        k0 = k > k0 ? k : k0;
                    else
                        k1 = k + 1 < k1 ? k + 1 : k1;
                }
                if (k0 < k1)
                    tint(PX(k0, r), k1 - k0, q);
            }
    }
    if (sa != sb) {
        t = f0 / (f0 - f1);
        qx[n] = x0 + t * dxp, qy[n++] = y0;
    }
    if (sb != sc) {
        t = f1 / (f1 - f2);
        qx[n] = x1, qy[n++] = y0 + t * dyp;
    }
    if (sd != sc) {
        t = f3 / (f3 - f2);
        qx[n] = x0 + t * dxp, qy[n++] = y1;
    }
    if (sa != sd) {
        t = f0 / (f0 - f3);
        qx[n] = x0, qy[n++] = y0 + t * dyp;
    }
    for (uint8_t k = 0; k < n; k += 2) {         /* two points, or four: round the corner the centre does not join */
        uint8_t a = k, b = k + 1;
        if (n == 4) {
            if (((f0 + f1 + f2 + f3) >= 0) == sa) {   /* a and c joined: cut off b and d */
                a = k;
                b = k + 1;
            } else {
                a = k ? 1 : 0;
                b = k ? 2 : 3;
            }
        }
        if (!(fl & I_SO) && (rnd((qx[a] + qx[b]) * 0.1f) + (rnd((qy[a] + qy[b]) * 0.5f) >> 2) & 1))
            continue;
        line(w, rnd(qx[a]), rnd(qy[a]), rnd(qx[b]), rnd(qy[b]));
    }
}

/* After compile(): 0 if the equation is not implicit, else I_SH (an inequality) | I_SO (no strict relation) | 4. */
static uint8_t imptype(const struct gw *w)
{
    uint8_t fl = 4 | I_SO, rel = 0, eq = 0, last = w->n ? w->op[w->n - 1] : 0;
    if (!w->uy || (uint8_t)(last - O_EQ) > 4 && last != O_AND && last != O_OR)
        return 0;
    for (uint8_t i = 0; i < w->n; i++) {
        uint8_t o = w->op[i];
        if (o == O_NE || o == O_XOR || o == O_NOT)
            return 0;
        if (o == O_LT || o == O_GT)
            fl &= ~I_SO;
        eq += o == O_EQ;
        rel += o >= O_EQ && o <= O_NE || o == O_AND || o == O_OR;
    }
    if (eq && (eq != 1 || rel != 1))
        return 0;                                /* a lone = is a curve; = beside anything else has no inside */
    return eq ? fl : fl | I_SH;
}

__attribute__((noinline)) static void idraw(struct gw *w, uint8_t fl)
{
    float dxu = (w->x1 - w->x0) / IX, dyu = (w->y1 - w->y0) / IY;
    for (int j = 0; j <= IY; j++) {
        float *cur = w->fr.f[j & 1], *prv = w->fr.f[~j & 1];
        uint8_t *cc = w->fr.c[j & 1], *pc = w->fr.c[~j & 1];
        if (OSB(ADDR_ON) & 0x10) {
            w->brk = 1;
            return;
        }
        w->oy = w->y1 - (float)j * dyu;
        for (int i = 0; i <= IX; i++) {
            float f = run(w, w->x0 + (float)i * dxu);
            volatile float v = f;
            cur[i] = f;
            cc[i] = !fin1e30(f) ? 0 : ((volatile uint8_t *)&v)[3] & 0x80 ? 1 : 2;
        }
        for (int i = 0; j && i < IX; i++) {
            uint8_t m = (uint8_t)(pc[i] | pc[i + 1] | cc[i] | cc[i + 1]);
            if (m == 2 && fl & I_SH && pc[i] && pc[i + 1] && cc[i] && cc[i + 1]) {   /* wholly inside: no boundary, integer bounds (icell's rnd of them) */
                int c0 = (i * 318 + IX / 2) / IX, c1 = i == IX - 1 ? PW : ((i + 1) * 318 + IX / 2) / IX;
                int r0 = ((j - 1) * PH + IY / 2) / IY, r1 = j == IY ? SH : (j * PH + IY / 2) / IY;
                for (int r = r0; r < r1; r++)
                    tint(PX(c0, r), c1 - c0, (w->pc >> 2) & 0x39E7);
            } else if (pc[i] && pc[i + 1] && cc[i] && cc[i + 1] && ((fl & I_SH && (m & 2)) || m == 3))
                icell(w, i, j - 1, prv[i], prv[i + 1], cur[i + 1], cur[i], fl);
        }
    }
}

static void pcurve(struct gw *w, uint8_t p);
static void pfinish(struct gw *w);
static void sdraw(struct gw *w);
static void wdraw(struct gw *w);
static int webone(const struct gw *w);
static void splots(struct gw *w);
static uint8_t sp_state(void);
static int sp_degen(void);
static uint8_t sp_trok(void);

static uint8_t style(const struct gw *w, uint8_t i)
{
    return OSB(w->pm == 0 ? ADDR_STYLE + i : w->pm == 1 ? 0x0D024C9 + i : w->pm == 2 ? 0x0D024CF + i : 0x0D024D5 + i);
}

static void render(struct gw *w)
{
    uint16_t *v = VRAM;
    uint8_t fl = OSB(ADDR_DB), xr = OSB(ADDR_XRES + 1) == 0x80 ? OSB(ADDR_XRES + 2) >> 4 : 1;
    w->x0 = real(w->win);
    w->x1 = real(w->win + 9);
    w->y0 = real(w->win + 18);
    w->y1 = real(w->win + 27);
    if (!(w->x1 > w->x0) || !(w->y1 > w->y0)) {  /* an empty or NaN window */
        w->x0 = w->y0 = -10.0f;
        w->x1 = w->y1 = 10.0f;
    }
    w->sx = 318.0f / (w->x1 - w->x0);
    w->sy = (float)PH / (w->y1 - w->y0);
    w->xres = xr;
    w->det = !(OSB(ADDR_ASYM) & 1);
    w->brk = 0;
    w->last = w->lp = 0;
    w->pm = pmode();
    w->shown = 0;
    w->psv = 0;
    dr_sync(w);
    memset(v, 0xFF, (size_t)VS * SH * 2);        /* white; libc's is an ldir */
    if (fl & 4)
        grid(w, fl & 0x80, pal(OSB(ADDR_GRIDC)));
    axes(w, pal(OSB(ADDR_AXESC)));
    if (fl & 0x40)
        labels();
    if (w->pm == 3) {
        sdraw(w);
        return;
    }
    splots(w);                                   /* stat plots, under the functions */
    w->imp = 0;
    for (uint8_t pass = 0; pass < 2; pass++)     /* pass 0: the implicit equations, under the curves */
        for (uint8_t i = 0; i < nu(w) && !w->brk; i++) {
            uint8_t e, r;
            if (!hv(w, i))
                continue;
            w->pc = pal(w->col[i]);
            w->st = style(w, i);
            if (w->pm) {
                if (!pass)
                    continue;
                pcurve(w, i);
                w->lp = (uint8_t)(i + 1);
                continue;
            }
            e = compile(w, i);
            r = e ? 0 : imptype(w);
            if (r && !pass) {
                w->imp |= (uint16_t)(1 << i);
                w->yop = 1;
                compile(w, i);
                w->yop = 0;
                idraw(w, r);
            } else if (!r && pass) {
                curve(w, i, e != 0);
                w->last = (uint8_t)(i + 1);
            }
        }
    if (w->brk)
        return;
    dr_overlay();
    if (w->pm) {
        pfinish(w);
        return;
    }
    {
        uint8_t x[9];
        gcoord(x, (const uint8_t *)0x0D01E33, 264, 0x0D01FAE);
        {   /* ZFrac's flagged DeltaX flags X too, unless X is a whole number (measured, 1/2 and 1/3 vs 1/4..1/10) */
            unsigned i, e = x[1] - 0x80, nz = 0;
            for (i = e + 1; i < 14; i++)
                nz |= (x[2 + i / 2] >> (i & 1 ? 0 : 4)) & 15;
            if (nz)
                x[0] |= *(const uint8_t *)0x0D01FAE & 0x18;
        }
        symce_stox(x);                           /* X = Xmin + rnd10(264 DeltaX), Y = the last function there */
    }
    if (w->last) {
        if (!symce_osy((uint8_t)(w->last - 1), 0, w->buf, 0x58))
            symce_stoy(w->buf);
    }
}

/* Web: sequence i's formula in b with its own u(n-1) read as X; its length, or 0xFF if there is anything else of the sequences (n, u(n-2), v(n-1)). */
static uint8_t websub(const struct gw *w, uint8_t i, uint8_t *b)
{
    const uint8_t *t = w->tok[i];
    uint8_t n = 0, l = w->len[i] > MAXTOK ? MAXTOK : w->len[i];
    for (uint8_t j = 0; j < l;) {
        if (t[j] == 0x5E && j + 8 <= l && t[j + 1] == 0x80 + i && !memcmp(t + j + 2, "\x10\x62\x21\x71\x31\x11", 6)) {
            b[n++] = 0x58;
            j += 8;
        } else if (t[j] == 0x5E && t[j + 1] >= 0x80 && t[j + 1] < 0x83 || t[j] == 0x62 && t[j + 1] == 0x21)
            return 0xFF;
        else {
            for (uint8_t k = tkn(t[j]); k--;)
                b[n++] = t[j++];
        }
    }
    return n;
}

/* Does the OS have to draw this graph? Mode other than Function, a split
   screen, a stat plot, a background image, or a line style this does not draw. */
uint8_t graph_fall(const struct gw *w)
{
    uint8_t pm = pmode();
    if (OSB(0x0D0EF00 + 0xC0A) == 0x20)          /* GWK: the table */
        return tb_fall();
    if (dr_foreign(w))
        return 1;
    if (OSB(0x0D0EF00 + 0xC0A) == 0xF5) {        /* DRAW Tangent(: SymCE's in Function mode with a function and no stat plot, else TI's */
        uint8_t i = 0;
        while (i < 10 && !have(w, i))
            i++;
        if (pm || sp_state() || i == 10)
            return 1;
    }
    if ((OSB(ADDR_SG) & 7) || OSB(ADDR_BGIMG))
        return 1;
    if (sp_state() > 3 || (pm == 3 && sp_state()))   /* a plot TI draws; in Seq mode every plot is TI's */
        return 1;
    {   /* stat plots: TI's TRACE walks them; ZoomStat with none on is TI's error */
        uint8_t s = sp_state();
        if ((s && !sp_trok() && OSB(0x0D0EF00 + 0xC07) == 0x5A) || (!s && OSB(0x0D0EF00 + 0xC0A) == 0xD9) ||   /* 0xD0 + Z_STAT */
            (s && OSB(0x0D0EF00 + 0xC0A) == 0xD9 && sp_degen()))
            return 1;
    }
    if (pm == 3) {                               /* Seq: Time format, explicit terms, dots (slice 1) */
        uint8_t f = OSB(ADDR_SEQF);
        if (f & 1) {                             /* Web: TI says INVALID for n, u(n-2), another sequence: its screen */
            uint8_t b[MAXTOK];
            for (uint8_t i = 0; i < 3; i++) {
                uint8_t l = w->len[i] ? websub(w, i, b) : 0;
                /* compilet clobbers w->op/k/n..., which no one reads across calls: render, wdraw, CALC and DRAW each compile before they run */
                if (w->len[i] && ((OSB(0x0D0EF00 + 0xC07) == 0x5A && !webone(w)) || OSB(0x0D0EF00 + 0xC0A) == 0xFE || l == 0xFF || compilet((struct gw *)w, b, l, 0)))
                    return 1;                    /* (a handed menu key: TI's TRACE and zooms; ponytail) */
            }
        }
        if (f & 0x1C) {                          /* uv, vw, uw: both terms, and TI's own TRACE and menus */
            uint8_t a = f & 4 ? 0 : 1, b = f & 16 ? 2 : f & 4 ? 1 : 2;
            if (!w->len[a] || !w->len[b] || OSB(0x0D0EF00 + 0xC07) == 0x5A)
                return 1;
        }
        for (uint8_t i = 0; i < 3; i++)
            if (w->len[i]) {
                if (OSB(0x0D024D5 + i) != 6)
                    return 1;                    /* ponytail: TI's line styles */
                for (uint8_t j = 0; !(f & 1) && j + 1 < w->len[i] && j < MAXTOK; j++)
                    if (w->tok[i][j] == 0x5E && (uint8_t)(w->tok[i][j + 1] - 0x80) < 3 &&
                        (j + 8 > w->len[i] || memcmp(w->tok[i] + j + 2, "\x10\x62\x21\x71\x31\x11", 6)))
                        return 1;                /* ponytail: only u(n-1) (u(n-2) hangs the OS's SeqSolve step) */
            }
    }
    if (pm == 3 && OSB(0x0D0EF00 + 0xC0A) == 0xDA)    /* GWK: ZoomFit (0xD0 + Z_FIT; ponytail: not done for Seq) */
        return 1;
    for (uint8_t i = 0; i < (pm == 3 ? 0 : pm ? 6 : 10); i++)
        if (pm == 1 ? w->len[2 * i] && w->len[2 * i + 1] : w->len[i]) {
            uint8_t s = OSB(pm == 0 ? ADDR_STYLE + i : pm == 1 ? 0x0D024C9 + i : 0x0D024CF + i);
            if (s >= 2 && s <= 5)
                return 1;
            if (pm && s > 1)
                return 1;                        /* ponytail: Par and Pol draw thin and normal lines; TI the dot styles */
        }
    return 0;
}

/* ---- the free cursor, TRACE and the X= box (phase 1b) ----
   Everything runs on TI's logical grid (gx 0..264, gy 0..164); the picture is the
   physical mapping of it. X and Y are TI reals made with the OS's arithmetic, so the
   stored values are TI's to the last digit (docs 11c). */
enum { M_PLAIN, M_FREE, M_TRACE, M_ENTRY };
#define ADDR_XMIN 0x0D01E33
#define ADDR_XMAX 0x0D01E3C
#define ADDR_YMIN 0x0D01E4E
#define ADDR_DX   0x0D01FAE
#define ADDR_DY   0x0D01FB7
#define ADDR_TS   0x0D0203D
#define ADDR_ZPRV 0x0D01FFC      /* ZPrevious: Xmin Xmax Xscl Ymin Ymax Yscl, as the window vars are */
#define ADDR_TRF  0x0D00098      /* traceFlags: bit 0 = ExprOff */
#define ADDR_TMR  0x0F20000      /* timer 1, 32768 Hz while the grapher runs (main.asm) */
#define ADDR_KB7  0x0F5001E      /* keypad data, group 7: Down 1, Left 2, Right 4, Up 8 */
#define FP_ADD 0x00201C0
#define FP_SUB 0x00201BC
#define FP_MUL 0x00201D8
#define FP_DIV 0x00201F4
#define BARY   226               /* the X= / Y= text, glyph top, as TI's */
#define BOXY   220               /* the X= box: black line, 17 white rows, black line (TI's is at 216, over the plot's foot) */
#define STRIP  14                /* rows under the trace label */
#define SMAX   28                /* ... and under a CALC title and label */
#define BLINK  10500             /* ticks: 320 ms a frame */
#define HOLD   16384             /* a held arrow repeats after 0.5 s ... */
#define REPT   3277              /* ... every 100 ms (TI's, measured) */

static unsigned now(void)
{
    volatile uint8_t *t = (volatile uint8_t *)ADDR_TMR;
    unsigned a, b;
    do {                                         /* two equal reads: no torn byte */
        a = t[0] | t[1] << 8 | (unsigned)t[2] << 16;
        b = t[0] | t[1] << 8 | (unsigned)t[2] << 16;
    } while (a != b);
    return a;
}

/* An integer (|n| < 10^8) as a TI real. */
static void itor(int n, uint8_t *r)
{
    uint8_t d[14], t[9];
    unsigned m = n < 0 ? (unsigned)-n : (unsigned)n;
    int k = 0, i;
    memset(r, 0, 9);
    memset(d, 0, sizeof d);
    r[1] = 0x80;
    for (; m; m /= 10)
        t[k++] = (uint8_t)(m % 10);
    if (!k)
        return;
    for (i = 0; i < k; i++)
        d[i] = t[k - 1 - i];
    r[0] = n < 0 ? 0x80 : 0;
    r[1] = (uint8_t)(0x80 + k - 1);
    for (i = 0; i < 7; i++)
        r[2 + i] = (uint8_t)(d[2 * i] << 4 | d[2 * i + 1]);
}

/* A TI real rounded (half up in magnitude) to 10 significant digits: what TI does to
   the product that becomes a cursor's X or Y, and to the window after a pan. */
static void rnd10(uint8_t *r)
{
    uint8_t d[14];
    int i;
    for (i = 0; i < 7; i++) {
        d[2 * i] = r[2 + i] >> 4;
        d[2 * i + 1] = r[2 + i] & 15;
    }
    if (!d[0])
        return;
    if (d[10] >= 5) {
        for (i = 9; i >= 0 && ++d[i] == 10; i--)
            d[i] = 0;
        if (i < 0) {
            d[0] = 1;
            r[1]++;
        }
    }
    for (i = 10; i < 14; i++)
        d[i] = 0;
    for (i = 0; i < 7; i++)
        r[2 + i] = (uint8_t)(d[2 * i] << 4 | d[2 * i + 1]);
}

/* out = base + rnd10(g * d), d at an OS address: the cursor's X (base Xmin, d DeltaX)
   or Y (Ymin, DeltaY). */
static void gcoord(uint8_t *out, const uint8_t *base, int g, unsigned d)
{
    uint8_t t[9];
    itor(g, t);
    symce_fp(FP_MUL, (const uint8_t *)d, t, t);  /* DeltaX first: the product keeps its flag bits, as TI's does after ZFrac */
    rnd10(t);
    symce_fp(FP_ADD, t, base, out);               /* the product first: its flag bits win, as in TI's sum */
}

static int gpx(int gx) { return (gx * 318 + 132) / 264; }
static int gpy(int gy) { return ((164 - gy) * PH + 82) / 164; }

/* gpx and gpy for points off the grid (a Par or Pol curve leaves the window): floor, not truncation. */
static int fdiv(int n, int d)
{
    return n >= 0 ? n / d : -((-n + d - 1) / d);
}

/* A segment between pixel points that may be far off the plot: Liang-Barsky to a few pixels around it, then line(). */
static void pseg(const struct gw *w, int xa, int ya, int xb, int yb)
{
    const int lx = -3, hx = PW + 2, ly = -3, hy = SH + 2;
    float t0 = 0, t1 = 1, dx = (float)(xb - xa), dy = (float)(yb - ya);
    float p[4], q[4];
    if ((xa < lx && xb < lx) || (xa > hx && xb > hx) || (ya < ly && yb < ly) || (ya > hy && yb > hy))
        return;
    if (xa >= lx && xa <= hx && xb >= lx && xb <= hx && ya >= ly && ya <= hy && yb >= ly && yb <= hy) {
        line(w, xa, ya, xb, yb);
        return;
    }
    p[0] = -dx; q[0] = (float)(xa - lx);
    p[1] = dx;  q[1] = (float)(hx - xa);
    p[2] = -dy; q[2] = (float)(ya - ly);
    p[3] = dy;  q[3] = (float)(hy - ya);
    for (int i = 0; i < 4; i++) {
        float r;
        if (p[i] == 0) {
            if (q[i] < 0)
                return;
            continue;
        }
        r = q[i] / p[i];
        if (p[i] < 0) {
            if (r > t1)
                return;
            if (r > t0)
                t0 = r;
        } else {
            if (r < t0)
                return;
            if (r < t1)
                t1 = r;
        }
    }
    line(w, rnd((float)xa + dx * t0), rnd((float)ya + dy * t0), rnd((float)xa + dx * t1), rnd((float)ya + dy * t1));
}

#include "graph_stat.c"

/* The T / theta of step k as TI makes it: min + rnd10(k step), a TI real (a = the window var of min; max, step follow). */
static void tstepr(uint8_t *out, uint32_t a, int k);

/* A Par pair or a Pol curve over the T / theta window: TI's points (min + k step while <= max), each rounded to
   the 265x165 grid like the cursor's, joined by lines in the plot's pixels. The OS parser has it if the
   compiler cannot. */
/* Seq window values are whole numbers. */
static int ival(uint32_t a)
{
    return (int)(real_os(a) + 0.5f);
}

/* The first n a Seq trace (and the draw's dots) reach. */
static int sstart(void)
{
    return ival(ADDR_NMIN) + (ival(ADDR_PSTART) > 1 ? ival(ADDR_PSTART) - 1 : 0);   /* PlotStart counts terms from nMin */
}

/* Seq, Web format: the trace is the cobweb. */
static int webt(const struct gw *w)
{
    return w->pm == 3 && (OSB(ADDR_SEQF) & 1);
}

/* One sequence defined, nMin..: the only Web TRACE SymCE does (more sequences, a PlotStart or PlotStep: TI's, measured only for this). */
static int webone(const struct gw *w)
{
    return (w->len[0] > 0) + (w->len[1] > 0) + (w->len[2] > 0) == 1 && sstart() == ival(ADDR_NMIN) && ival(ADDR_PSTEP) == 1;
}

/* The last step k with min + k step <= max, by the OS's own division (powf is not exact enough for 360 / 7.5). */
static int tn(uint32_t a)
{
    uint8_t q[9];
    symce_fp(FP_SUB, (const uint8_t *)(a + 9), (const uint8_t *)a, q);
    symce_fp(FP_DIV, q, (const uint8_t *)(a + 18), q);
    return (int)(real(q) * 1.000001f + 0.000001f);
}

__attribute__((noinline)) static void pcurve(struct gw *w, uint8_t p)
{
    uint8_t pol = w->pm == 2, sx = pol ? p : (uint8_t)(2 * p), slow = 0, haveprev = 0;
    uint32_t a = pol ? 0x0D01E69 : 0x0D01E84;
    float t0 = real_os(a), t1 = real_os(a + 9), ts = real_os(a + 18);
    float kx = 264.0f / (w->x1 - w->x0), ky = 164.0f / (w->y1 - w->y0);
    int n, pxo = 0, pyo = 0;
    if (!(ts > 0) || t1 < t0)
        return;
    n = tn(a);
    if (compileb(w, sx, 0))
        slow = 1;
    w->n1 = w->n;
    if (!pol && !slow && compileb(w, (uint8_t)(sx + 1), w->n1))
        slow = 1;
    for (int k = 0; k <= n; k++) {
        float t = t0 + (float)k * ts, x, y;
        int px, py;
        if (!(k & 63) && k && (OSB(ADDR_ON) & 0x10)) {
            w->brk = 1;
            return;
        }
        if (slow) {
            tstepr(w->xr, a, k);
            x = symce_osy(osn(w, sx), w->xr, w->buf, vtok(w)) ? __builtin_nanf("") : real(w->buf);
            y = 0;
            if (!pol)
                y = symce_osy(osn(w, (uint8_t)(sx + 1)), w->xr, w->buf, vtok(w)) ? __builtin_nanf("") : real(w->buf);
        } else {
            x = runr(w, 0, w->n1, t);
            y = pol ? 0 : runr(w, w->n1, w->n, t);
        }
        if (pol && fin1e30(x)) {
            float th = w->deg ? t * 0.01745329252f : t, r = x;
            x = r * cosf(th);
            y = r * sinf(th);
        }
        if (!fin1e30(x) || !fin1e30(y)) {        /* undefined: the line breaks */
            haveprev = 0;
            continue;
        }
        x = (x - w->x0) * kx;
        y = (y - w->y0) * ky;
        px = fdiv(rnd(x) * 318 + 132, 264);
        py = fdiv((164 - rnd(y)) * PH + 82, 164);
        if (haveprev)
            pseg(w, pxo, pyo, px, py);
        else
            line(w, px, py, px, py);
        pxo = px;
        pyo = py;
        haveprev = 1;
    }
    tstepr(w->tv, a, n);
}

static void tstepr(uint8_t *out, uint32_t a, int k)
{
    gcoord(out, (const uint8_t *)a, k, a + 18);
}

static const uint8_t pcs[2][5] = { { 0x52, 0x82, 0xC4, 0x5B, 0x11 }, { 0x52, 0x82, 0xC2, 0x5B, 0x11 } };   /* R*cos(theta), R*sin(theta) */

/* After a Par or Pol draw TI leaves the last point in the variables: X, Y, and T or theta (R too), as the OS's
   own arithmetic has them. */
static void pfinish(struct gw *w)
{
    uint8_t x[9], y[9], r[9], sx;
    if (!w->lp)
        return;
    sx = (uint8_t)(w->pm == 2 ? w->lp - 1 : 2 * (w->lp - 1));
    if (w->pm == 1) {
        if (symce_osy(osn(w, sx), w->tv, x, 0x54) || symce_osy(osn(w, (uint8_t)(sx + 1)), 0, y, 0x54))
            return;
        symce_stox(x);
        symce_stoy(y);
        symce_stov(0x54, w->tv);
    } else {
        symce_stov(0x5B, w->tv);
        if (symce_osy(osn(w, sx), w->tv, r, 0x5B))
            return;
        symce_stov(0x52, r);
        if (symce_eval(pcs[0], 5, x) || symce_eval(pcs[1], 5, y))
            return;
        symce_stox(x);
        symce_stoy(y);
    }
}

/* Seq, Time format: u, v, w for n = nMin..nMax, the OS's own SeqSet / SeqSolve have the terms (u(n-1), initial
   values and all). A 3x3 dot on every PlotStep-th term from PlotStart, on the 265x165 grid like a Par point. */
/* The OS's terms at n: restart for a lower n, else step on from where it is. 0, or the OS error code. */
static unsigned sterm(struct gw *w, int n)
{
    uint8_t r[9];
    unsigned e;
    if (n < w->sn || w->sn < 0) {
        w->sn = -1;
        if ((e = symce_seq(0, 0)))
            return e;
        w->sn = ival(ADDR_NMIN);
    }
    while (n > w->sn) {                          /* Web: one term at a time (the recursion has u(n-1) from the call before); else the OS runs up to n */
        int t = webt(w) ? w->sn + 1 : n;
        itor(t, r);
        if ((e = symce_seq(1, r))) {
            w->sn = -1;
            return e;
        }
        w->sn = t;
    }
    return 0;
}

/* Seq, Web format: each sequence's formula as a curve of X (u(n-1) is X), in its colour, and after each Y = X in black. After it X is
   the column before the last, Y the last curve's value there (as for a function, 263 and not 264 DeltaX). */
__attribute__((noinline)) static void wdraw(struct gw *w)
{
    uint8_t b[MAXTOK], l, last = 0, x[9], r[9];
    for (uint8_t i = 0; i < 3 && !w->brk; i++)
        if (have(w, i) && (l = websub(w, i, b)) != 0xFF && !compilet(w, b, l, 0)) {
            w->pc = pal(w->col[i]);
            curve(w, i, 0);
            w->op[0] = O_X;                      /* Y = X after each curve, over it (TI's, measured: u over v's crossing shows v) */
            w->n = 1;
            w->pc = pal(3);
            w->st = 0;
            curve(w, 0, 0);
            last = (uint8_t)(i + 1);
        }
    if (w->brk)
        return;
    gcoord(x, (const uint8_t *)0x0D01E33, 263, 0x0D01FAE);
    symce_stox(x);
    if (last && (l = websub(w, (uint8_t)(last - 1), b)) != 0xFF && !symce_eval(b, l, r))
        symce_stoy(r);
}

__attribute__((noinline)) static void sdraw(struct gw *w)
{
    int nmin = ival(ADDR_NMIN), nmax = ival(ADDR_NMAX), p0 = sstart(), ps = ival(ADDR_PSTEP);
    float kx = 264.0f / (w->x1 - w->x0), ky = 164.0f / (w->y1 - w->y0);
    uint8_t r[9], ly[9], lx[9];
    int ln = -1;
    unsigned e = 0;
    uint8_t f = OSB(ADDR_SEQF) & 0x1C;
    if (OSB(ADDR_SEQF) & 1) {
        wdraw(w);
        return;
    }
    const uint8_t *ax = f == 4 ? (const uint8_t *)"\0\1" : f == 8 ? (const uint8_t *)"\1\2" : f == 16 ? (const uint8_t *)"\0\2" : 0;
    w->sn = -1;
    if (ps < 1 || nmax < nmin)
        return;
    for (int n = nmin; n <= nmax; n++) {
        if ((e = sterm(w, n)))
            break;
        if (!(n & 15) && (OSB(ADDR_ON) & 0x10)) {
            w->brk = 1;
            return;
        }
        if (n < p0 || (n - p0) % ps)
            continue;
        if (ax) {                                /* uv, vw, uw: a dot at (first term, second term), in the second one's colour */
            uint32_t a0 = ADDR_SEQV + 18 * ax[0], a1 = ADDR_SEQV + 18 * ax[1];
            float x, y;
            if ((OSB(a0) & 0x1F) || (OSB(a1) & 0x1F))
                continue;
            x = real((const uint8_t *)a0);
            y = real((const uint8_t *)a1);
            if (!fin1e30(x) || !fin1e30(y))
                continue;
            ln = n;
            memcpy(lx, (const void *)a0, 9);
            memcpy(ly, (const void *)a1, 9);
            w->pc = pal(w->col[ax[1]]);
            block(fdiv(rnd((x - w->x0) * kx) * 318 + 132, 264), fdiv((164 - rnd((y - w->y0) * ky)) * PH + 82, 164), 1, w->pc);
            continue;
        }
        for (uint8_t i = 0; i < 3; i++) {
            uint32_t a = ADDR_SEQV + 18 * i;
            float y;
            if (!have(w, i) || (OSB(a) & 0x1F))
                continue;
            ln = n;
            memcpy(ly, (const void *)a, 9);
            y = real((const uint8_t *)a);
            if (!fin1e30(y))
                continue;
            w->pc = pal(w->col[i]);
            block(fdiv(rnd(((float)n - w->x0) * kx) * 318 + 132, 264), fdiv((164 - rnd((y - w->y0) * ky)) * PH + 82, 164), 1, w->pc);
        }
    }
    if (ln >= 0) {                               /* TI leaves X = the last n plotted, Y = its term (the last sequence's) */
        if (ax)
            memcpy(r, lx, 9);                    /* uv, vw, uw: X is the first term */
        else
            itor(ln, r);
        symce_stox(r);
        symce_stoy(ly);
    }
    if (e)
        symce_throw(e, 1);                       /* a term the OS cannot get (1/0 ...): its error screen, after the points before it */
}

/* The cursor sprites, 9x9 (docs 11c): '#' black, '.' white, ' ' what is under it. Free
   frames, then trace frames. */
static const char spr[4][9][10] = {
    { "  #####  ", " ##...## ", "###...###", "#...#...#", "#..###..#", "#...#...#", "###...###", " ##...## ", "  #####  " },
    { "  .###.  ", " ..###.. ", "...###...", "####.####", "###...###", "####.####", "...###...", " ..###.. ", "  .###.  " },
    { "###   ###", "#..# #..#", "#...#...#", " #..#..# ", "  #####  ", " #..#..# ", "#...#...#", "#..# #..#", "###   ###" },
    { "###   ###", "#### ####", "#########", " ###.### ", "  #...#  ", " ###.### ", "#########", "#### ####", "###   ###" },
};

static void cdraw(struct gw *w)
{
    const char (*s)[10] = spr[(w->mode == M_TRACE ? 2 : 0) + w->frame];
    for (int j = 0; j < 9; j++)
        for (int i = 0; i < 9; i++)
            if (s[j][i] != ' ')
                put(w->cx - 4 + i, w->cy - 4 + j, s[j][i] == '#' ? 0 : 0xFFFF);
}

static void cshow(struct gw *w)
{
    for (int j = 0; j < 9; j++)
        for (int i = 0; i < 9; i++) {
            int x = w->cx - 4 + i, y = w->cy - 4 + j;
            w->sv[j * 9 + i] = (unsigned)x < PW && (unsigned)y < SH ? VRAM[y * VS + x] : 0xFFFF;
        }
    cdraw(w);
    w->shown = 1;
}

static void chide(struct gw *w)
{
    if (!w->shown)
        return;
    for (int j = 0; j < 9; j++)
        for (int i = 0; i < 9; i++)
            put(w->cx - 4 + i, w->cy - 4 + j, w->sv[j * 9 + i]);
    w->shown = 0;
}

static void fill(int y0, int y1, uint16_t c)
{
    for (int i = y0 * VS; i < y1 * VS; i++)
        VRAM[i] = c;
}

/* ---- 2nd CALC (phase 1d): the items, the titles ---- */
enum { C_VAL = 1, C_ZERO, C_MIN, C_MAX, C_ISECT, C_DYDX, C_INT, C_DYT, C_DXT, C_DRT };   /* the last three: Par dy/dt, dx/dt, Pol dr/dtheta */
static const char *const ctit[11] = { "", "", "CALC ZERO", "CALC MINIMUM", "CALC MAXIMUM", "CALC INTERSECT",
                                      "CALC DERIVATIVE AT POINT", "CALC INTEGRAL OVER INTERVAL",
                                      "CALC DERIVATIVE AT POINT", "CALC DERIVATIVE AT POINT", "CALC DERIVATIVE AT POINT" };

/* Back to the plain graph: no cursor, no zoom, no CALC. (The picture is the caller's.) */
static void plain(struct gw *w)
{
    w->mode = M_PLAIN;
    w->zm = w->zc = 0;
    w->dr.dm = 0;
    w->cm = w->cs = w->cres = 0;
    w->ptb[0] = w->ptc[0] = 0;
    w->psv = w->tps = 0;
}

/* The prompt or result line, TI's, over the foot of the plot (PBR rows, 12 = the small font's cell), saved in PBUF a
   byte a pixel: a palette index, or 16 + the index for that colour's shade over white. shortcut: a pixel that is
   neither (two shades overlapping, a shade over a curve) comes back white; upgrade: a second byte plane.
   Redrawn by bar(), so it survives the cursor moving. */
#define PBUF ((uint8_t *)0x0D0FB20)
#define PBX 20
#define PBW 168
#define PBR 12
#define PY 214
_Static_assert(0x0D0FB20 + PBR * PBW <= 0x0D10300, "PBUF overflows into SB");
static uint16_t pshade(uint8_t i) { return (uint16_t)(0xC618 + ((pal(i) >> 2) & 0x39E7)); }
static void pbuf(int save)
{
    uint16_t t[32];                              /* the 16 colours, then their shades over white (built once: a pass is 2016 pixels) */
    for (uint8_t i = 0; i < 16; i++) {
        t[i] = pal(i);
        t[16 + i] = pshade(i);
    }
    for (int y = 0; y < PBR; y++) {
        uint16_t *v = VRAM + (PY + y) * VS + PBX;
        uint8_t *u = PBUF + y * PBW;
        for (int x = 0; x < PBW; x++) {
            if (!save)
                v[x] = t[u[x] & 31];
            else {
                uint8_t q = 11;
                if (v[x] != 0xFFFF)
                    for (uint8_t i = 0; i < 32; i++)
                        if (t[i] == v[x]) {
                            q = i;
                            break;
                        }
                u[x] = q;
            }
        }
    }
}
static void pdraw(struct gw *w)
{
    if (!w->ptb[0])
        return;
    pbuf(w->psv == 0);
    w->psv = 1;
    symce_text(28, PY, w->pfg, 0xFFFF, w->ptb, 0);
}

/* The bar: X= and Y=, in the function's colour while tracing, black for the free cursor. */
static void bar(struct gw *w)
{
    char s[24];
    uint16_t b = 0xFFFF, fg = w->mode == M_TRACE ? (w->pt ? sp_color(w) : pal(w->col[w->fn])) : 0;
    const uint8_t *xs = w->xv, *ys = w->yv;
    uint8_t polar = OSB(ADDR_DB) & 8;
    if (polar && w->mode == M_FREE) {            /* PolarGC free cursor: R= and theta= */
        xs = w->prv;
        ys = w->pxv;
    }
    if (w->pm == 3 && w->mode == M_TRACE) {      /* Seq: n= over X= Y= */
        static const uint8_t nt[2] = { 0x62, 0x21 };
        symce_tok_(nt, w->ptb);
        w->ptb[1] = '=';
        symce_fmt(w->xv, w->ptb + 2);
        w->pfg = fg;
    } else if (w->pm && w->mode == M_TRACE) {           /* Par, Pol: T= (theta=) over X= Y=, or R= theta= for Pol with PolarGC */
        xs = w->pxv;
        if (polar && w->pm == 2) {
            xs = w->prv;
            ys = w->xv;
        } else if (w->cres != 3) {
            w->ptb[0] = w->pm == 2 ? 0x5B : 'T';
            w->ptb[1] = '=';
            symce_fmt(w->xv, w->ptb + 2);
            w->pfg = fg;
        }
    }
    if (w->mode == M_TRACE && webt(w))
        xs = w->pxv;                             /* Web: n= over the cobweb's X= Y= */
    pdraw(w);
    if (w->pm && w->cres == 3)                   /* a Par, Pol derivative: its line alone (TI) */
        return;
    if (OSB(ADDR_DB) & 0x10)                     /* CoordOff */
        return;
    if (w->ptc[0]) {                             /* the integral: its limits instead */
        char tc[40];
        unsigned l = strlen(w->ptc);
        memcpy(tc, w->ptc, l + 1);
        while (l < 30 && l < sizeof(tc) - 1)
            tc[l++] = ' ';
        tc[l] = 0;
        symce_text(26, BARY, fg, b, tc, 0);   /* TI starts this line 2 px left (measured) */
        return;
    }
    s[0] = polar && (w->mode == M_FREE || (w->pm == 2 && w->mode == M_TRACE)) ? 'R' : 'X';
    s[1] = '=';
    symce_fmt(xs, s + 2);
    {
        unsigned l = strlen(s);
        while (l < 15 && l < sizeof(s) - 1)
            s[l++] = ' ';
        s[l] = 0;
    }
    symce_text(28, BARY, fg, b, s, 0);
    s[0] = polar && (w->mode == M_FREE || (w->pm == 2 && w->mode == M_TRACE)) ? 0x5B : 'Y';
    s[2] = 0;
    if (w->mode == M_FREE || w->yok)
        symce_fmt(ys, s + 2);
    {
        unsigned l = strlen(s);
        while (l < 15 && l < sizeof(s) - 1)
            s[l++] = ' ';
        s[l] = 0;
    }
    symce_text(162, BARY, fg, b, s, 0);
}

/* The strip of the plot under the trace label, kept in plotSScreen (the OS graph is
   marked dirty when the trace starts). */
/* ... and since the draw layer (graph_draw.c) lives in plotSScreen, in 4 bits a pixel at 0xD10300 (the plot
   only has the 16 palette colours): SB, then ZB for the ZBox outline. */
#define SB ((uint8_t *)0x0D10300)
#define SBW ((PW + 1) / 2)
#define ZB (SB + SMAX * SBW)
static uint8_t cidx(uint16_t c)
{
    for (uint8_t i = 0; i < 16; i++)
        if (pal(i) == c)
            return i;
    return 11;
}
static void nset(uint8_t *b, int i, uint16_t c)
{
    uint8_t v = cidx(c);
    b += i >> 1;
    *b = i & 1 ? (*b & 0xF0) | v : (*b & 0x0F) | (uint8_t)(v << 4);
}
static uint16_t nget(const uint8_t *b, int i)
{
    b += i >> 1;
    return pal(i & 1 ? *b & 15 : *b >> 4);
}
static void sbrest(int y, int xa, int xb)       /* columns xa..xb-1 of row y back from SB */
{
    for (int x = xa; x < xb && x < PW; x++)
        VRAM[y * VS + x] = nget(SB, y * PW + x);
}
/* The title line over the label: a CALC's, or DRAW Tangent('s (TI's header). */
static const char *ttit(const struct gw *w)
{
    return w->cm >= 2 ? ctit[w->cm] : w->dr.dm == D_TANG ? "DRAW TANGENT" : 0;
}

static void labsave(struct gw *w)
{
    w->sr = ttit(w) ? SMAX : STRIP;
    for (int y = 0; y < w->sr; y++)
        for (int x = 0; x < PW; x++)
            nset(SB, y * PW + x, VRAM[y * VS + x]);
}

/* ---- Par and Pol TRACE: T or theta steps along min + k step, as the draw does ---- */
static void ptlabel(struct gw *w);
static void ptshow(struct gw *w);

static uint32_t tbase(const struct gw *w)
{
    return w->pm == 2 ? 0x0D01E69 : 0x0D01E84;
}

/* The last step k with min + k step <= max (the draw's end rule). */
static int tcount(const struct gw *w)
{
    uint32_t a = tbase(w);
    float t0, t1, ts;
    if (webt(w))
        return 1 << 20;                          /* no nMax: TI's cobweb walks on */
    if (w->pm == 3) {
        int p = ival(ADDR_PSTEP);
        return p < 1 || ival(ADDR_NMAX) < sstart() ? 0 : (ival(ADDR_NMAX) - sstart()) / p;
    }
    t0 = real_os(a);
    t1 = real_os(a + 9);
    ts = real_os(a + 18);
    if (!(ts > 0) || t1 < t0)
        return 0;
    return tn(a);
}

/* The trace grid's step k: T = Tmin + k step, Seq n = max(nMin, PlotStart) + k PlotStep. */
static void tset(struct gw *w, int k)
{
    if (w->pm == 3) {
        itor(sstart() + k * ival(ADDR_PSTEP), w->xv);
    } else
        tstepr(w->xv, tbase(w), k);
}

/* ---- TRACE over stat plots (the point list, in TI's order) ---- */
/* The label "Plot1:L1,L2" top left in the plot's colour, over the saved strip. */
static void plabel(struct gw *w)
{
    char s[40], q[16];
    uint32_t rec = SPREC(w->pt - 1);
    int n = 0;
    for (int y = 0; y < w->sr; y++)
        sbrest(y, 0, PW);
    memcpy(s, "Plot", 4);
    s[4] = (char)('0' + w->pt);
    s[5] = ':';
    n = 6;
    for (int k = 0; k < 2; k++) {
        uint8_t t[2] = { 0x5D, OSB(rec + (k ? SP_YL : SP_XL)) };
        if (k)
            s[n++] = ',';
        symce_tok_(t, q);
        for (char *p = q; *p && n < 38; p++)
            s[n++] = *p;
    }
    s[n] = 0;
    symce_text(3, 1, sp_color(w), 0xFFFF, s, 0);
}

/* The point pi of the plot: X, Y stored, shown; the cursor on its grid cell if that is on the grid. */
static void pshow(struct gw *w)
{
    const uint8_t *x, *y;
    chide(w);
    w->vis = 0;
    unsigned n = splists(SPREC(w->pt - 1), &x, &y);
    if (!n)
        return;
    if (w->pi >= n)
        w->pi = n - 1; /* TI clamps to the shorter list and keeps it */
    memcpy(w->xv, x + 9 * w->pi, 9);
    memcpy(w->yv, y + 9 * w->pi, 9);
    symce_stox(w->xv);
    symce_stoy(w->yv);
    w->yok = 1;
    w->gx = sgrid(w->xv, ADDR_XMIN, ADDR_DX);
    w->gy = sgrid(w->yv, ADDR_YMIN, ADDR_DY);
    w->vis = w->gx >= 0 && w->gx <= 264 && w->gy >= 0 && w->gy <= 164;
    bar(w);
    if (w->vis) {
        w->cx = gpx(w->gx);
        w->cy = gpy(w->gy);
        cshow(w);
    }
}

static void pmove(struct gw *w, int dir)
{
    const uint8_t *x, *y;
    unsigned n = splists(SPREC(w->pt - 1), &x, &y);
    if (dir < 0 ? w->pi : w->pi + 1 < n)
        w->pi += dir;
    pshow(w);
}

/* UP / DOWN with plots on: the ring is the plots that are on, then the selected functions. */
static int pswap(struct gw *w, int dir)
{
    uint8_t ids[16], n = 0, cur = 0, id, plots = 0;
    for (uint8_t p = 0; p < 3; p++)
        if (OSB(SPREC(p) + SP_ON) & 1) {
            ids[n++] = p;
            plots = 1;
        }
    if (!plots)
        return 0;
    for (uint8_t i = 0; i < NEQ; i++)
        if (lenok(w, i))
            ids[n++] = (uint8_t)(3 + i);
    for (uint8_t i = 0; i < n; i++)
        if (ids[i] == (w->pt ? w->pt - 1 : 3 + w->fn))
            cur = i;
    id = ids[(cur + n + dir) % n];
    if (id < 3) {
        w->pt = (uint8_t)(id + 1);                 /* the place in the lists is kept, even across a function */
    } else {
        if (w->pt) {                             /* from a plot: the function starts at the centre */
            w->gx = 132;
            w->pn = 0;
            w->snap = 0;
            memcpy(w->x0r, (const void *)ADDR_XMIN, 9);
        }
        w->pt = 0;
        w->fn = (uint8_t)(id - 3);
    }
    return 1;
}

/* "Y1=sin(X)" top left on white; ExprOff: just "Y1" top right. */
static void tlabel(struct gw *w)
{
    char s[120];
    int n = 3, i, l = w->len[w->fn], ly = 1;
    if (w->pt) {
        plabel(w);
        return;
    }
    if (w->pm) {
        ptlabel(w);
        return;
    }
    for (int y = 0; y < w->sr; y++)
        sbrest(y, 0, PW);
    if (ttit(w)) {
        symce_text(3, 1, 0, 0xFFFF, ttit(w), 0);
        ly = 15;
    }
    OSB(0x0D01474) = (uint8_t)(w->fn + 1);       /* the OS's traced function: CALC and TI's own screens read it */
    s[0] = 'Y';
    s[1] = (char)('1' + w->fn);
    if (w->fn == 9)
        s[1] = '0';
    s[2] = '=';
    if (OSB(ADDR_TRF) & 1) {
        s[2] = 0;
        symce_text(PW - 19, ly, pal(w->col[w->fn]), 0xFFFF, s, 0);
        return;
    }
    if (l > MAXTOK)
        l = MAXTOK;
    for (i = 0; i < l && n < 100;) {
        char t[16];
        i += symce_tok(w->tok[w->fn] + i, t);
        for (char *p = t; *p && n < 119; p++)
            s[n++] = *p;
    }
    s[n] = 0;
    symce_text(3, ly, pal(w->col[w->fn]), 0xFFFF, s, 0);
}

/* The OS window vars and the grapher's copy of them. */
static void setwin(struct gw *w)
{
    memcpy(w->win, (const void *)ADDR_XMIN, 18);
    memcpy(w->win + 18, (const void *)ADDR_YMIN, 18);
}

/* Free cursor: X and Y from gx, gy, stored. */
static void zhide(struct gw *w);
static void zdraw(struct gw *w);

static void fshow(struct gw *w)
{
    zhide(w);
    gcoord(w->xv, (const uint8_t *)ADDR_XMIN, w->gx, ADDR_DX);
    gcoord(w->yv, (const uint8_t *)ADDR_YMIN, w->gy, ADDR_DY);
    symce_stox(w->xv);
    symce_stoy(w->yv);
    if (OSB(ADDR_DB) & 8) {                      /* PolarGC: R and theta stored too (R>Pr(X,Y), R>Ptheta(X,Y)) */
        static const uint8_t rp[2][5] = { { 0x1B, 0x58, 0x2B, 0x59, 0x11 }, { 0x1C, 0x58, 0x2B, 0x59, 0x11 } };
        if (!symce_eval(rp[0], 5, w->prv) && !symce_eval(rp[1], 5, w->pxv)) {
            symce_stov(0x5B, w->pxv);
            symce_stov(0x52, w->prv);
        }
    }
    bar(w);
    w->cx = gpx(w->gx);
    w->cy = gpy(w->gy);
    zdraw(w);
}

/* Trace: w->xv is the X; find Y there, store both, show. */
static void tshow(struct gw *w)
{
    if (w->pt) {
        pshow(w);
        return;
    }
    if (w->pm) {
        ptshow(w);
        return;
    }
    chide(w);
    w->vis = 0;
    w->yok = !symce_osy(osn(w, w->fn), w->xv, w->yv, vtok(w));    /* stores X too */
    if (w->yok) {
        float f = (real(w->yv) - real_os(ADDR_YMIN)) / real_os(ADDR_DY);
        symce_stoy(w->yv);
        if (f > -0.5f && f < 164.5f && w->gx >= 0 && w->gx <= 264) { /* ponytail: the rounded row must be 0..164 */
            w->vis = 1;
            w->gy = rnd(f);
        }
    }
    bar(w);
    if (w->vis) {
        w->cx = gpx(w->gx);
        w->cy = gpy(w->gy);
        cshow(w);
    }
}

static void tcalc(struct gw *w)
{
    if (!w->pm && !w->pt)
        gcoord(w->xv, w->x0r, w->gx + 16 * w->pn, ADDR_DX);
    tshow(w);
}

/* ---- TRACE snap: a step that passes a zero, Y-intercept, intersection, minimum or maximum stops on it ----
   Settings item 6 (asm_Flag2 bit 4 set = off). Function mode only. The samples are the grid points the cursor has stood on. */
static uint8_t snapon(const struct gw *w)
{
    return !w->pm && !w->pt && !w->cm && w->dr.dm != D_TANG && !(OSB(0x0D000A2) & 0x10);
}

/* No samples, no last POI. */
static void thist0(struct gw *w)
{
    w->hn = 0;
    w->pl[0] = 0xFF;
}

/* Y of Yf at x; 0 if undefined. */
static uint8_t tyat(struct gw *w, uint8_t f, const uint8_t *x, float *y)
{
    uint8_t t[9];
    if (symce_osy(osn(w, f), x, t, vtok(w)))
        return 0;
    *y = real(t);
    return 1;
}

/* The POI label off: the plot rows under it back. */
static void tunpoi(struct gw *w)
{
    if (!w->tps)
        return;
    chide(w);
    if (w->psv)
        pbuf(0);
    w->psv = w->tps = 0;
    w->ptb[0] = 0;
}

/* The OS solver (kind 1 zero, 2 min, 3 max, 4 intersect with Yj) between the TI reals a and b; the root in out. */
static uint8_t tsolve(struct gw *w, unsigned kind, unsigned j, const uint8_t *a, const uint8_t *b, uint8_t *out)
{
    uint8_t g[9];
    const uint8_t *l = real(a) < real(b) ? a : b, *r = l == a ? b : a;
    float lo = real(l), hi = real(r), x, e = 1e-3f * real_os(ADDR_DX);
    memcpy(g, w->xv, 9);
    if (symce_calc(kind, w->fn, l, r, g, j, out))
        return 0;
    x = real(out);
    return x >= lo - e && x <= hi + e;           /* (the minimum / maximum solver runs off along a pole) */
}

/* The step to the new X (w->xv, w->yv are there) in direction dir: look for the POI nearest the old sample, show it if there is one. */
static void tpoi(struct gw *w, int dir)
{
    uint8_t br[9], r[9], zero[9], bn = 0, sol = 0;
    const char *nm = 0;
    float xn = real(w->xv), xo, yn, ym, y2, yr, d[NEQ], best = 1e30f, dx = real_os(ADDR_DX), dy = real_os(ADDR_DY);
    uint16_t dm = 0;
    if (w->hn && (xn - real(w->hx[0])) * dir <= 0)
        return;                                  /* walking back over samples already seen (after a POI that was behind the cursor) */
    if (!w->yok) {
        w->hn = 0;
        return;
    }
    yn = real(w->yv);
    for (uint8_t j = 0; j < NEQ; j++)
        if (j != w->fn && have(w, j) && tyat(w, j, w->xv, &y2)) {
            d[j] = yn - y2;
            dm |= 1 << j;
        }
    if (w->hn && w->hdir != dir && w->hn > 1)
        w->hn = 1;                               /* a reversal: the older sample is on the other side */
    if (w->hn) {
        xo = real(w->hx[0]);
        ym = w->hy[0];
#define TCAND(root, name, id)                                                         \
        do {                                                                          \
            float dd = fabsf(real(root) - xo);                                        \
            if (dd < best - 1e-3f * dx && (w->pl[0] == 0xFF || fabsf(real(root) - real(w->pl)) > dx)) { \
                best = dd;                                                            \
                memcpy(br, root, 9);                                                  \
                nm = name;                                                            \
                bn = id;                                                              \
            }                                                                         \
        } while (0)
        if (ym * yn < 0 || yn == 0) {            /* a zero; an asymptote jumps over the axis: Y at the root is far from 0 */
            if (yn == 0)
                memcpy(r, w->xv, 9);
            else
                sol = 1;
            if ((yn == 0 || tsolve(w, 1, 0, w->hx[0], w->xv, r)) && tyat(w, w->fn, r, &yr) && fabsf(yr) <= dy)
                TCAND(r, "Zero", 1);
        }
        if (dir > 0 ? xo < 0 && xn >= 0 : xn <= 0 && xo > 0) {   /* the Y axis */
            itor(0, zero);
            if (tyat(w, w->fn, zero, &yr))
                TCAND(zero, "Y-intercept", 2);
        }
        for (uint8_t j = 0; j < NEQ; j++)
            if ((dm & w->hdv) >> j & 1 && (w->hd[j] * d[j] < 0 || d[j] == 0)) {
                float y1;
                if (d[j] == 0)
                    memcpy(r, w->xv, 9);
                else
                    sol = 1;
                if ((d[j] == 0 || tsolve(w, 4, j, w->hx[0], w->xv, r)) && tyat(w, w->fn, r, &y1) && tyat(w, j, r, &yr) && fabsf(y1 - yr) <= dy)
                    TCAND(r, "Intersection", 3);
            }
        if (w->hn > 1 && (w->hy[0] - w->hy[1]) * (yn - ym) < 0) {   /* the slope changed sign over three samples */
            float hi = yn > ym ? yn : ym, lo = yn < ym ? yn : ym;
            uint8_t mx = yn < ym;
            hi = hi > w->hy[1] ? hi : w->hy[1];
            lo = lo < w->hy[1] ? lo : w->hy[1];
            sol = 1;
            if (tsolve(w, mx ? 3 : 2, 0, w->hx[1], w->xv, r) && tyat(w, w->fn, r, &yr) && yr <= hi + (hi - lo) && yr >= lo - (hi - lo) &&
                (mx ? yr >= hi - dy : yr <= lo + dy) && yr >= real_os(ADDR_YMIN) && yr <= real_os(ADDR_YMIN + 9))   /* (a pole's "extremum" runs off the window) */
                TCAND(r, mx ? "Maximum" : "Minimum", 4);
        }
    }
    w->hn += w->hn < 2;
    w->hdir = (int8_t)dir;
    memcpy(w->hx[1], w->hx[0], 9);
    memcpy(w->hx[0], w->xv, 9);
    w->hy[1] = w->hy[0];
    w->hy[0] = yn;
    memcpy(w->hd, d, sizeof d);
    w->hdv = dm;
    if (bn) {
        float u = (real(br) - real_os(ADDR_XMIN)) / dx, k = u / 2;
        int c = (int)floorf(k + 0.5f);
        if (fabsf(k - (float)c) < 1e-4f) {       /* on a grid point: the next one on either side */
            w->tga = 2 * (c + dir);
            w->tgb = 2 * (c - dir);
        } else {
            c = (int)floorf(k);                  /* (k is no integer: the grid points around it are c and c + 1) */
            w->tga = 2 * (dir > 0 ? c + 1 : c);
            w->tgb = 2 * (dir > 0 ? c : c + 1);
        }
        w->tpd = (int8_t)dir;
        memcpy(w->xv, br, 9);
        w->gx = rnd(u);
        w->gx = w->gx < 0 ? 0 : w->gx > 264 ? 264 : w->gx;
        strcpy(w->ptb, nm);
        w->pfg = pal(w->col[w->fn]);
        w->tps = 1;
        memcpy(w->pl, br, 9);
    }
    if (sol || bn)
        tshow(w);                                /* the solver left its X in X; the POI, or the grid point, put back */
}

/* The label's text s[0..n) at x on the strip, TI's way: characters while the pen stays within 123 of x, else the ellipsis (measured). */
static void ltext(int x, int y, uint16_t fg, const char *s, int n)
{
    int pen[60], px = x, m = n, cap = x + 123;
    char c[2] = { 0, 0 };
    for (int i = 0; i < n; i++) {
        c[0] = s[i];
        symce_text(px, y, fg, 0xFFFF, c, 0);
        px = pen[i] = (int)OSW(0x0D008D2);
    }
    if (px <= cap)
        return;
    for (int r = y; r < y + 12 && r < SH; r++)
        sbrest(r, x, px + 8);
    for (m = 0; m < n && pen[m] <= cap; m++)
        ;
    for (int i = 0, p = x; i <= m; i++) {
        c[0] = i < m ? s[i] : (char)0xCE;
        symce_text(p, y, fg, 0xFFFF, c, 0);
        p = (int)OSW(0x0D008D2);
    }
}

/* One Par or Pol label: the equation's name (5E nm), =, its tokens. ExprOff: the name alone. */
static void plab(struct gw *w, int x, uint8_t nm, uint8_t slot, int y)
{
    char s[60], q[16];
    uint8_t t[2] = { 0x5E, nm };
    int n = 0, l = w->len[slot];
    symce_tok_(t, q);
    for (char *p = q; *p && n < 8; p++)
        s[n++] = *p;
    if (!(OSB(ADDR_TRF) & 1)) {
        s[n++] = '=';
        if (l > MAXTOK)
            l = MAXTOK;
        for (int i = 0; i < l && n < 40;) {
            i += symce_tok(w->tok[slot] + i, q);
            for (char *p = q; *p && n < 56; p++)
                s[n++] = *p;
        }
    }
    ltext(x, y, pal(w->col[w->fn]), s, n);
}

/* Par: X1T= left, Y1T= from TI's x 160; Pol: r1= left. */
static void ptlabel(struct gw *w)
{
    int ly = 1;
    for (int y = 0; y < w->sr; y++)
        sbrest(y, 0, PW);
    if (w->cm >= 2) {                            /* the CALC title, the label below it */
        symce_text(3, 1, 0, 0xFFFF, ctit[w->cm], 0);
        ly = 15;
    }
    if (w->pm == 3) {
        plab(w, 3, (uint8_t)(0x80 + w->fn), w->fn, ly);
        return;
    }
    if (w->pm == 2) {
        plab(w, 3, (uint8_t)(0x40 + w->fn), w->fn, ly);
        return;
    }
    plab(w, 3, (uint8_t)(0x20 + 2 * w->fn), (uint8_t)(2 * w->fn), ly);
    plab(w, 135, (uint8_t)(0x21 + 2 * w->fn), (uint8_t)(2 * w->fn + 1), ly);
}

/* Web step s (the cobweb): 0 is (u(nMin), 0) at n = nMin; 2j-1 is (u(nMin+j-1), u(nMin+j)), on the curve; 2j is (u(nMin+j), u(nMin+j)), on
   y = x; both at n = nMin + j. 1 if the terms are defined. */
static int webvals(struct gw *w, int s, int *n, uint8_t *x, uint8_t *y)
{
    uint32_t a = ADDR_SEQV + 18 * w->fn;
    int j = (s + 1) / 2, m = ival(ADDR_NMIN), ok;
    *n = m + j;
    ok = !sterm(w, s ? m + j - (s & 1) : m) && !(OSB(a) & 0x1F);
    memcpy(x, (const void *)a, 9);
    if (!s)
        itor(0, y);
    else if (s & 1) {
        ok = ok && !sterm(w, m + j) && !(OSB(a) & 0x1F);
        memcpy(y, (const void *)a, 9);
    } else
        memcpy(y, x, 9);
    return ok;
}

/* The pixel of Web step s on the plot (the cursor's grid rounding; far off the window is clamped by rnd), and its n, X, Y. */
static int webpix(struct gw *w, int s, int *px, int *py, int *n, uint8_t *x, uint8_t *y)
{
    if (!webvals(w, s, n, x, y))
        return 0;
    *px = fdiv(rnd((real(x) - real_os(ADDR_XMIN)) / real_os(ADDR_DX)) * 318 + 132, 264);
    *py = fdiv((164 - rnd((real(y) - real_os(ADDR_YMIN)) / real_os(ADDR_DY))) * PH + 82, 164);
    return 1;
}

/* The cobweb's segments up to step tk, from where zx (the last step drawn) left off: 1 px, in the sequence's colour (the cursor is hidden).
   They stay. The terms only go up, so the solver is not restarted; the last step's n, X, Y come back. */
static int webseg(struct gw *w, int *n, uint8_t *x, uint8_t *y)
{
    int ax = w->zr[0], ay = w->zr[1], bx, by, a = w->zr[2], ok = 0;   /* (zr: the pixel of step zx, if known) */
    w->pc = pal(w->col[w->fn]);
    w->st = 0;
    w->sn = -1;                                  /* (the OS forgets u(n-1) between keys: start again, the terms then go up) */
    if (!a)
        a = webpix(w, w->zx, &ax, &ay, n, x, y);
    for (int s = w->zx + 1; s <= w->tk; s++) {
        ok = webpix(w, s, &bx, &by, n, x, y);
        if (a == 2) {                            /* from the x axis under this step */
            a = ok;
            ax = bx;
            ay = fdiv((164 - rnd((0 - real_os(ADDR_YMIN)) / real_os(ADDR_DY))) * PH + 82, 164);
        }
        if (ok && a)
            pseg(w, ax, ay, bx, by);
        a = ok;
        ax = bx;
        ay = by;
    }
    w->zx = w->tk;
    w->zr[0] = ax;
    w->zr[1] = ay;
    w->zr[2] = a;
    return ok;
}

/* The point at xv (the T or theta): X, Y (and R) by the OS parser, stored as TI stores them; the cursor on its pixel. */
static void ptshow(struct gw *w)
{
    uint8_t sx = w->pm == 2 ? w->fn : (uint8_t)(2 * w->fn), x[9], y[9];
    chide(w);
    w->vis = 0;
    if (webt(w)) {                               /* Web step tk: n, X, Y; the segments not yet drawn */
        int n;
        w->yok = w->tk > w->zx ? webseg(w, &n, x, y) : webvals(w, w->tk, &n, x, y);   /* (the OS's solver leaves X and Y of its own: before they are stored) */
        itor(n, w->xv);
    } else if (w->pm == 3) {                     /* Seq: the OS's term (X = n) */
        uint32_t a = ADDR_SEQV + 18 * w->fn;
        memcpy(x, w->xv, 9);
        w->yok = !sterm(w, (int)(real(x) + 0.5f)) && !(OSB(a) & 0x1F);
        memcpy(y, (const void *)a, 9);
    } else if (w->pm == 1)
        w->yok = !symce_osy(osn(w, sx), w->xv, x, 0x54) && !symce_osy(osn(w, (uint8_t)(sx + 1)), 0, y, 0x54);
    else {
        w->yok = !symce_osy(osn(w, sx), w->xv, w->prv, 0x5B);
        if (w->yok) {
            symce_stov(0x52, w->prv);
            w->yok = !symce_eval(pcs[0], 5, x) && !symce_eval(pcs[1], 5, y);
        }
    }
    if (w->yok) {
        float fx = (real(x) - real_os(ADDR_XMIN)) / real_os(ADDR_DX), fy = (real(y) - real_os(ADDR_YMIN)) / real_os(ADDR_DY);
        memcpy(w->pxv, x, 9);
        memcpy(w->yv, y, 9);
        symce_stox(x);
        symce_stoy(y);
        if (fx > -0.5f && fx < 264.5f && fy > -0.5f && fy < 164.5f) {
            w->vis = 1;
            w->gx = rnd(fx);
            w->gy = rnd(fy);
        }
    }
    bar(w);
    if (w->vis) {
        w->cx = gpx(w->gx);
        w->cy = gpy(w->gy);
        cshow(w);
    }
}

/* LEFT / RIGHT: the next T on the grid (no panning); after a typed T, a step from it. */
static void ptmove(struct gw *w, int dir)
{
    int n = tcount(w), k = w->tk + dir;
    if (w->snap) {                               /* a typed T: the step nearest it, then one more */
        uint8_t t[9];
        if (webt(w)) {                           /* TI's measured step after a typed n: twice the step on the right, one back on the left */
            k = dir > 0 ? 2 * w->tk + !w->tk : w->tk - 1;   /* (n = nMin: ponytail, unmeasured) */
            if (dir > 0 && k > 0) {              /* it draws only the last two segments, the first from the x axis up (measured) */
                w->zx = k - 2;
                w->zr[2] = 2;
            }
        }
        else if (w->pm == 3)
            k = (int)floorf((real(w->xv) - sstart()) / ival(ADDR_PSTEP) + 0.5f) + dir;
        else {
            symce_fp(FP_SUB, w->xv, (const uint8_t *)tbase(w), t);
            symce_fp(FP_DIV, t, (const uint8_t *)(tbase(w) + 18), t);
            k = (int)floorf(real(t) + 0.5f) + dir;
        }
        w->snap = 0;
    }
    k = k < 0 ? 0 : k > n ? n : k;
    w->tk = k;
    tset(w, k);
    ptshow(w);
}

static void cdecor(struct gw *w);

/* The window moved one pan (16 columns): Xmin' = rnd10(Xmin +- rnd10(16 DeltaX)) and
   Xmax' = rnd10(Xmin' + rnd10(264 DeltaX)), as measured on TI. */
static void tpan(struct gw *w, int dir)
{
    uint8_t a[9], b[9], c[9];
    itor(16, a);
    symce_fp(FP_MUL, a, (const uint8_t *)ADDR_DX, a);
    rnd10(a);
    symce_fp(dir > 0 ? FP_ADD : FP_SUB, (const uint8_t *)ADDR_XMIN, a, b);
    rnd10(b);
    b[0] |= 0x40;                                /* TI leaves this bit in the new Xmin (not in Xmax) */
    itor(264, a);
    symce_fp(FP_MUL, a, (const uint8_t *)ADDR_DX, a);
    rnd10(a);
    symce_fp(FP_ADD, b, a, c);
    rnd10(c);
    memcpy((void *)ADDR_XMIN, b, 9);
    memcpy((void *)ADDR_XMAX, c, 9);
    setwin(w);
    w->gx += dir > 0 ? -16 : 16;
    w->pn += dir;
    if (w->cm) {                                 /* CALC: its markers pan with the picture */
        w->cmc[0] += dir > 0 ? -16 : 16;
        if (w->cm != C_ISECT)
            w->cmc[1] += dir > 0 ? -16 : 16;
    }
    render(w);
    labsave(w);
    tlabel(w);
    cdecor(w);
}

/* TRACE: the lowest selected function, the centre column. */
static void tstart(struct gw *w)
{
    uint8_t i = 0, n = w->pm ? nu(w) : NEQ, p = 0;
    while (i < n && !have(w, i))
        i++;
    while (!w->pm && p < 3 && !(OSB(SPREC(p) + SP_ON) & 1))
        p++;
    if (i == n && (w->pm || p == 3))
        return;
    zhide(w);
    w->zm = w->zc = 0;
    symce_dirty();                               /* plotSScreen is ours now */
    labsave(w);
    w->mode = M_TRACE;
    w->fn = i == n ? 0 : i;
    w->pt = (uint8_t)(!w->pm && p < 3 ? p + 1 : 0);
    w->pi = 0;
    w->gx = 132;
    w->pn = 0;
    w->snap = 0;
    w->tps = 0;
    thist0(w);
    w->hx[0][0] = 0xFF;                          /* (no sample is the next X) */
    w->tk = w->zx = w->zr[2] = 0;
    if (w->pm)
        tset(w, 0);
    memcpy(w->x0r, (const void *)ADDR_XMIN, 9);
    tlabel(w);
    tcalc(w);
}

/* One step of TRACE: dir +1 right, -1 left. A typed X first snaps to the trace grid. */
static void tmove(struct gw *w, int dir)
{
    if (w->pt) {
        pmove(w, dir);
        return;
    }
    if (w->pm) {
        ptmove(w, dir);
        return;
    }
    if (w->tps) {                                /* standing on a POI: the grid point next to it, in the direction pressed */
        w->gx = dir == w->tpd ? w->tga : w->tgb;
        if (dir != w->tpd)
            thist0(w);                           /* (the samples are on the other side) */
        tunpoi(w);
    } else {
        if (w->snap) {
            uint8_t t[9];
            float u;
            symce_fp(FP_SUB, w->xv, (const uint8_t *)ADDR_XMIN, t);
            symce_fp(FP_DIV, t, (const uint8_t *)ADDR_DX, t);
            u = real(t) / 2.0f;                  /* in TraceStep units (= 2 DeltaX) */
            w->gx = 2 * (int)floorf(u + 0.5f);
            w->pn = 0;
            w->snap = 0;
            memcpy(w->x0r, (const void *)ADDR_XMIN, 9);
            thist0(w);
        }
        w->gx += 2 * dir;
    }
    if ((w->gx > 264 || w->gx < 0) && w->dr.dm != D_TANG)   /* Tangent(: TI never pans, X runs on past the edge */
        tpan(w, dir);
    tcalc(w);
    if (snapon(w))
        tpoi(w, dir);
}

/* UP / DOWN: the previous / next selected function, wrapping. X stays. */
static void tswap(struct gw *w, int dir)
{
    uint8_t i = w->fn, n = w->pm ? nu(w) : NEQ;
    tunpoi(w);
    thist0(w);
    if (!w->pm && pswap(w, dir)) {
        tlabel(w);
        cdecor(w);
        tcalc(w);
        return;
    }
    do
        i = (uint8_t)((i + n + dir) % n);
    while (!have(w, i));
    w->fn = i;
    tlabel(w);
    cdecor(w);
    tshow(w);
}

/* DeltaX, DeltaY (and TraceStep = 2 DeltaX) from the window. */
static void deltas(void)
{
    uint8_t h[9], n[9], two[9];
    itor(2, two);
    for (int k = 0; k < 2; k++) {
        const uint8_t *mn = (const uint8_t *)(k ? ADDR_YMIN : ADDR_XMIN);
        itor(k ? 164 : 264, n);
        symce_fp(FP_SUB, mn + 9, mn, h);
        symce_fp(FP_DIV, h, n, h);
        memcpy((void *)(k ? ADDR_DY : ADDR_DX), h, 9);
        if (!k)
            symce_fp(FP_MUL, h, two, (uint8_t *)ADDR_TS);
    }
}

/* ENTER: QuickZoom. The window moves to centre on (X, Y) with the same size; the old one
   becomes ZPrevious; the trace goes on at the centre column with X, Y as they were. */
static void tzoom(struct gw *w)
{
    uint8_t h[9], lo[9], hi[9], two[9];
    if (!w->yok)
        return;                                  /* ponytail: TI on an undefined point untested */
    tunpoi(w);
    thist0(w);
    symce_rom(0x6EDDA);                          /* the window to ZPrevious, and ZPrevious is valid */
    itor(2, two);
    for (int k = 0; k < 2; k++) {
        const uint8_t *mn = (const uint8_t *)(k ? ADDR_YMIN : ADDR_XMIN), *c = k ? w->yv : w->pm && (w->pm < 3 || webt(w)) ? w->pxv : w->xv;
        symce_fp(FP_SUB, mn + 9, mn, h);
        symce_fp(FP_DIV, h, two, h);
        symce_fp(FP_SUB, c, h, lo);
        symce_fp(FP_ADD, c, h, hi);
        rnd10(lo);
        rnd10(hi);
        memcpy((void *)mn, lo, 9);
        memcpy((void *)(mn + 9), hi, 9);
    }
    deltas();
    setwin(w);
    render(w);
    symce_dirty();
    labsave(w);
    w->gx = 132;
    w->pn = 0;
    memcpy(w->x0r, (const void *)ADDR_XMIN, 9);
    tlabel(w);
    if (w->pm) {                                 /* Par, Pol: TI starts the trace over at Tmin */
        w->tk = w->zx = w->zr[2] = 0;
        w->snap = 0;
        w->sn = -1;
        tset(w, 0);
    }
    w->pi = 0;                                   /* a plot: TI restarts at point 1 */
    tshow(w);
}

/* ---- ZOOM and ZOOM MEMORY (phase 1c) ----
   The menu hook (main.asm) hands the pick over as the grapher's first key, 0xC0 + the
   action below. The window maths is the OS's own wherever it has a routine (ZmDecml ...,
   so the bytes are TI's); ZBox, ZoomFit, ZStandard, ZQuadrant1 and ZFrac are done here
   with the OS's arithmetic. */
enum { Z_BOX = 1, Z_IN, Z_OUT, Z_DEC, Z_SQR, Z_STD, Z_TRIG, Z_INT, Z_STAT, Z_FIT, Z_Q1,
       Z_F2, Z_F3, Z_F4, Z_F5, Z_F8, Z_F10, Z_PREV, Z_STO, Z_RCL };
#define ADDR_ZSTO 0x0D01D61      /* ZXmin ... ZYscl, laid out as the window block is */

static uint16_t vpix(int x, int y)
{
    return (unsigned)x < PW && (unsigned)y < SH ? VRAM[y * VS + x] : 0xFFFF;
}

/* ZBox: the outline between the first corner and the cursor (1 px), and the 7x7 marker on the
   corner, drawn over the plot; the pixels under them are in ZB (outline, then marker). */
static void zbdraw(struct gw *w)
{
    int ax = gpx(w->zx), ay = gpy(w->zy), bx = gpx(w->gx), by = gpy(w->gy);
    int x0 = ax < bx ? ax : bx, x1 = ax < bx ? bx : ax, y0 = ay < by ? ay : by, y1 = ay < by ? by : ay, k = 0;
    w->zr[0] = x0; w->zr[1] = y0; w->zr[2] = x1; w->zr[3] = y1;
    for (int x = x0; x <= x1; x++) {
        nset(ZB, k++, vpix(x, y0)); put(x, y0, 0);
        if (y1 != y0) { nset(ZB, k++, vpix(x, y1)); put(x, y1, 0); }
    }
    for (int y = y0 + 1; y < y1; y++) {
        nset(ZB, k++, vpix(x0, y)); put(x0, y, 0);
        if (x1 != x0) { nset(ZB, k++, vpix(x1, y)); put(x1, y, 0); }
    }
    for (int j = -3; j <= 3; j++)
        for (int i = -3; i <= 3; i++) { nset(ZB, k++, vpix(ax + i, ay + j)); put(ax + i, ay + j, 0); }
}

static void zbhide(struct gw *w)
{
    int x0 = w->zr[0], y0 = w->zr[1], x1 = w->zr[2], y1 = w->zr[3];
    int ax = gpx(w->zx), ay = gpy(w->zy), n = (x1 - x0 + 1) * (y1 != y0 ? 2 : 1) + (y1 - y0 - 1 > 0 ? y1 - y0 - 1 : 0) * (x1 != x0 ? 2 : 1);
    int k = n + 49;                              /* back to front: the marker was drawn last */
    for (int j = 3; j >= -3; j--)
        for (int i = 3; i >= -3; i--) put(ax + i, ay + j, nget(ZB, --k));
    for (int y = y1 - 1; y > y0; y--) {
        if (x1 != x0) put(x1, y, nget(ZB, --k));
        put(x0, y, nget(ZB, --k));
    }
    for (int x = x1; x >= x0; x--) {
        if (y1 != y0) put(x, y1, nget(ZB, --k));
        put(x, y0, nget(ZB, --k));
    }
}

static void zhide(struct gw *w)
{
    chide(w);
    if (w->zm == Z_BOX && w->zc)
        zbhide(w);
    dr_undo(w);
}

static void zdraw(struct gw *w)
{
    if (w->zm == Z_BOX && w->zc)
        zbdraw(w);
    dr_prev(w);
    cshow(w);
}

/* The window the OS routines left: new deltas are theirs (they store through the window-variable
   routine); redraw, and TI's graph is out of date. */
static void zdone(struct gw *w)
{
    uint8_t two[9];
    itor(2, two);
    if (!(*(const uint8_t *)ADDR_DX & 0x18))
        symce_fp(FP_MUL, (const uint8_t *)ADDR_DX, two, (uint8_t *)ADDR_TS);   /* TI's graph draw: TraceStep = 2 DeltaX */
    else {   /* ZFrac: measured, TS = (Xmax-Xmin)/132 (2 DeltaX off the rounded DeltaX is 1 ulp high for 1/3) */
        uint8_t t[9], n[9];
        symce_fp(FP_SUB, (const uint8_t *)ADDR_XMAX, (const uint8_t *)ADDR_XMIN, t);
        itor(132, n);
        symce_fp(FP_DIV, t, n, (uint8_t *)ADDR_TS);
    }
    *(uint8_t *)ADDR_TS |= *(const uint8_t *)ADDR_DX & 0x18;   /* and TS takes DeltaX's 0x18 bits */
    setwin(w);
    plain(w);
    render(w);
    symce_dirty();
}

/* The cursor of an interactive zoom at the centre of the picture (X and Y stay as they were). */
static void zcentre(struct gw *w, int fresh)
{
    w->mode = M_FREE;
    w->zc = 0;
    w->gx = 132;
    w->gy = 82;
    w->hk = 0;
    if (fresh) {
        fshow(w);
        return;
    }
    w->cx = gpx(132);
    w->cy = gpy(82);
    bar(w);
    cshow(w);
}

/* A real from a fraction. */
static void rat(uint8_t *r, int n, int d)
{
    uint8_t a[9], b[9];
    itor(n, a);
    itor(d, b);
    symce_fp(FP_DIV, a, b, r);
}

static void wvar(int k, const uint8_t *r)
{
    memcpy((void *)(ADDR_XMIN + 9 * k), r, 9);
}

/* ZoomFit: Ymin and Ymax from the 265 columns of every selected function, the OS parser's answers. */
static void zfit(struct gw *w)
{
    uint8_t x[9], y[9], lo[9], hi[9];
    int have = 0;
    for (int k = 0; k <= 264; k++) {
        gcoord(x, (const uint8_t *)ADDR_XMIN, k, ADDR_DX);
        for (uint8_t i = 0; i < NEQ; i++) {
            if (!w->len[i] || symce_osy(osn(w, i), x, y, vtok(w)))
                continue;
            if (!have || real(y) < real(lo))
                memcpy(lo, y, 9);
            if (!have || real(y) > real(hi))
                memcpy(hi, y, 9);
            have = 1;
        }
    }
    if (!have)
        return;
    symce_rom(0x6EDDA);
    rnd10(lo);
    rnd10(hi);
    wvar(3, lo);
    wvar(4, hi);
    deltas();
}

/* ZoomFit in Par and Pol: Xmin Xmax and Ymin Ymax from the points of every selected curve at T = min + k step
   (measured on TI: Xscl, Yscl and the T window stay; Pol's X, Y are R cos(theta), R sin(theta) in the OS's arithmetic). */
static void zfitp(struct gw *w)
{
    uint8_t x[9], y[9], lo[2][9], hi[2][9];
    uint8_t pol = w->pm == 2;
    uint32_t a = pol ? 0x0D01E69 : 0x0D01E84;
    int got = 0, n;
    if (!(real_os(a + 18) > 0) || real_os(a + 9) < real_os(a))
        return;
    n = tn(a);
    for (uint8_t i = 0; i < nu(w); i++) {
        uint8_t sx = pol ? i : (uint8_t)(2 * i);
        if (!have(w, i))
            continue;
        for (int k = 0; k <= n; k++) {
            tstepr(w->tv, a, k);
            if (pol) {
                uint8_t r[9];
                symce_stov(0x5B, w->tv);
                if (symce_osy(osn(w, sx), w->tv, r, 0x5B))
                    continue;
                symce_stov(0x52, r);
                if (symce_eval(pcs[0], 5, x) || symce_eval(pcs[1], 5, y))
                    continue;
            } else if (symce_osy(osn(w, sx), w->tv, x, 0x54) || symce_osy(osn(w, (uint8_t)(sx + 1)), w->tv, y, 0x54))
                continue;
            for (uint8_t j = 0; j < 2; j++) {
                const uint8_t *v = j ? y : x;
                if (!got || real(v) < real(lo[j]))
                    memcpy(lo[j], v, 9);
                if (!got || real(v) > real(hi[j]))
                    memcpy(hi[j], v, 9);
            }
            got = 1;
        }
    }
    if (!got)
        return;
    symce_rom(0x6EDDA);
    for (uint8_t j = 0; j < 2; j++) {
        rnd10(lo[j]);
        rnd10(hi[j]);
        wvar(3 * j, lo[j]);
        wvar(3 * j + 1, hi[j]);
    }
    deltas();
}

/* A window of whole numbers: Xmin Xmax Xscl Ymin Ymax Yscl. */
static void zints(const int8_t *v)
{
    uint8_t r[9];
    symce_rom(0x6EDDA);
    for (int k = 0; k < 6; k++) {
        itor(v[k], r);
        wvar(k, r);
    }
    deltas();
}

static void ztitle(struct gw *w, const char *t)
{
    labsave(w);
    symce_text(3, 1, 0, 0xFFFF, t, 0);
    (void)w;
}

/* The pick from the menu hook: 1..21, see the enum (Z_STAT and SetFactors never come). */
static void zpick(struct gw *w, uint8_t a)
{
    static const int8_t std[] = { -10, 10, 1, -10, 10, 1 };
    plain(w);
    if (a != Z_STO)
        dr_reset();                              /* TI's replot empties the drawing */
    if (a == Z_BOX || a == Z_IN || a == Z_OUT || a == Z_INT) {
        render(w);
        symce_dirty();                           /* plotSScreen is ours now */
        w->zm = a;
        ztitle(w, a == Z_BOX ? "ZOOM BOX" : a == Z_IN ? "ZOOM IN" : a == Z_OUT ? "ZOOM OUT" : "");
        zcentre(w, 1);
        return;
    }
    switch (a) {
    case Z_DEC: symce_rom(0x20C0C); break;
    case Z_SQR: symce_rom(0x20C20); deltas(); break;     /* the OS leaves the deltas to TI's own graph screen */
    case Z_TRIG: symce_rom(0x20C24); break;
    case Z_STD:
        zints(std);
        if (pmode() == 3) {                      /* Seq: nMin nMax PlotStart PlotStep 1 10 1 1 */
            static const uint32_t a[4] = { ADDR_NMIN, ADDR_NMAX, ADDR_PSTART, ADDR_PSTEP };
            static const int8_t v[4] = { 1, 10, 1, 1 };
            uint8_t r[9];
            for (int k = 0; k < 4; k++) {
                itor(v[k], r);
                memcpy((void *)a[k], r, 9);
            }
        } else if (pmode()) {                    /* (w->pm is set by the first render) Par, Pol: T or theta 0..2pi (360) step pi/24 (7.5), as TI's measured */
            static const uint8_t rad[27] = { 0, 0x80, 0, 0, 0, 0, 0, 0, 0,  0, 0x80, 0x62, 0x83, 0x18, 0x53, 7, 0, 0,  0, 0x7F, 0x13, 8, 0x99, 0x69, 0x38, 0x99, 0x57 },
                                      deg[27] = { 0, 0x80, 0, 0, 0, 0, 0, 0, 0,  0, 0x82, 0x36, 0, 0, 0, 0, 0, 0,  0, 0x80, 0x75, 0, 0, 0, 0, 0, 0 };
            memcpy((void *)(pmode() == 2 ? 0x0D01E69 : 0x0D01E84), w->deg ? deg : rad, 27);
        }
        break;
    case Z_STAT: symce_rom(0x20B28); deltas(); break;    /* the OS's ZoomStat (graph_fall has seen to its errors) */
    case Z_FIT:
        w->pm = pmode();                         /* (the first render has not set it yet) */
        if (w->pm) zfitp(w);
        else zfit(w);
        break;
    case Z_Q1: symce_rom(0xBD0F0); break;               /* the OS's ZQuadrant1 */
    case Z_PREV: symce_rom(0x20C10); deltas(); break;
    case Z_STO: symce_rom(0x20C18); break;
    case Z_RCL: symce_rom(0x20C14); deltas(); break;
    default:
        if (a >= Z_F2 && a <= Z_F10)
            symce_roma(0xBD2C6, "\2\3\4\5\10\12"[a - Z_F2]);   /* the OS's ZFrac, A = the denominator */
    }
    zdone(w);
}

/* ENTER with a zoom cursor. */
static void zenter(struct gw *w)
{
    if (w->zm == Z_BOX) {
        if (!w->zc) {
            zhide(w);
            w->zc = 1;
            w->zx = w->gx;
            w->zy = w->gy;
            memcpy(w->z1x, w->xv, 9);
            memcpy(w->z1y, w->yv, 9);
            fshow(w);
            return;
        }
        if (w->zx == w->gx || w->zy == w->gy)
            return;                              /* ponytail: TI's empty box not measured */
        symce_rom(0x6EDDA);
        wvar(0, w->zx < w->gx ? w->z1x : w->xv);
        wvar(1, w->zx < w->gx ? w->xv : w->z1x);
        wvar(3, w->zy < w->gy ? w->z1y : w->yv);
        wvar(4, w->zy < w->gy ? w->yv : w->z1y);
        deltas();
        setwin(w);
        w->zc = 0;
        render(w);
        symce_dirty();
        zcentre(w, 1);
        return;
    }
    if (w->zm == Z_INT) {
        symce_rom(0x9AE22);
        symce_rom(0x7FA0E);
        symce_rom(0x9AE13);
        symce_rom(0x20C08);
    } else {
        symce_zxy(w->zm == Z_IN);
    }
    deltas();
    setwin(w);
    render(w);
    symce_dirty();
    zcentre(w, w->zm != Z_INT);
}

/* ---- 2nd CALC (phase 1d) ----
   The menu hook (main.asm) hands the pick over as the grapher's first key, 0xE0 + the item (C_*).
   The numbers are the OS's: symce_calc runs its solvers (zero, minimum, maximum, intersect, dy/dx),
   the integral is the parser's fnInt( on the limits. TI's text, colours, small font. The stages
   (cs) are TRACE with a prompt; a result is TRACE's picture plus the result line, and an arrow
   after it is the free cursor on the result's pixel. */
static void ebox(struct gw *w);

static void cprompt(struct gw *w)
{
    static const char *const s[3][3] = { { "Left Bound?", "Right Bound?", "Guess?" },
                                         { "First curve?", "Second curve?", "Guess?" },
                                         { "Lower Limit?", "Upper Limit?", "" } };
    strcpy(w->ptb, w->cm == C_DYDX || w->cm > C_INT ? "" : s[w->cm == C_ISECT ? 1 : w->cm == C_INT ? 2 : 0][w->cs]);
    w->pfg = 0;
    w->ptc[0] = 0;
}

/* A bound: the dotted line down the plot and TI's little arrow on it, the left one pointing right. */
static void cmark(struct gw *w, int g, int right)
{
    static const uint8_t wd[9] = { 1, 2, 3, 5, 5, 5, 3, 2, 1 };
    int c = gpx(g);
    uint16_t k = pal(w->col[w->cf]);
    for (int y = SMAX; y < SH; y += 2)
        put(c, y, k);
    for (int j = 0; j < 9; j++)
        for (int i = 0; i < wd[j]; i++)
            put(right ? c + 4 - i : c - 4 + i, SMAX + 3 + j, k);
}

/* Intersect's first curve: the black trace sprite stays on the point. */
static void cfirst(struct gw *w)
{
    int cx = gpx(w->cmc[0]), cy = gpy(w->cmc[1]);
    for (int j = 0; j < 9; j++)
        for (int i = 0; i < 9; i++)
            if (spr[2][j][i] != ' ')
                put(cx - 4 + i, cy - 4 + j, spr[2][j][i] == '#' ? 0 : 0xFFFF);
}

static void cdecor(struct gw *w)
{
    if (!w->cm || w->cres)
        return;
    if (w->cm == C_ISECT) {
        if (w->cs >= 1)
            cfirst(w);
    } else if (w->cm != C_DYDX && w->cm != C_VAL && w->cm < C_DYT) {
        if (w->cs >= 1)
            cmark(w, w->cmc[0], 0);
        if (w->cs >= 2)
            cmark(w, w->cmc[1], 1);
    }
}

/* render() for a Par or Pol point: the draw steps T (theta), X, Y, R to their ends, TI's leave them where the trace was. */
static void replot(struct gw *w)
{
    uint8_t t[9], x[9], y[9], r[9], pm = w->pm, ok = w->yok;
    memcpy(t, w->xv, 9);
    memcpy(x, w->pxv, 9);
    memcpy(y, w->yv, 9);
    memcpy(r, w->prv, 9);
    render(w);
    if (pm && pm < 3 && ok) {
        symce_stov(pm == 1 ? 0x54 : 0x5B, t);
        symce_stox(x);
        symce_stoy(y);
        if (pm == 2)
            symce_stov(0x52, r);
    }
}

/* Out of CALC: the plain graph. In a CALC the cursor's X and Y were never re-plotted, so they stay. */
static void cleave(struct gw *w)
{
    uint8_t x[9], y[9], had = w->cm, yo = w->yok || w->mode == M_FREE, cr = w->cres;
    memcpy(x, webt(w) ? w->pxv : w->xv, 9);
    memcpy(y, w->yv, 9);
    plain(w);
    replot(w);
    if ((had && !w->pm) || (w->pm == 3 && (!had || cr))) {         /* (Par, Pol: X, Y, T were stored by the trace; Seq: TI keeps the trace's X, Y even without a CALC, but not from a CALC prompt cleared before its result) */
        symce_stox(x);
        if (yo)
            symce_stoy(y);
    }
}

/* The integral's shading: the function's colour from the x axis to the curve, the limits' columns. */
static void cshade(struct gw *w)
{
    float a = real(w->cl), b = real(w->cr), xm = real_os(ADDR_XMIN), dx = real_os(ADDR_DX), t;
    int ay, c0, c1;
    if (a > b) {
        t = a;
        a = b;
        b = t;
    }
    c0 = gpx(rnd((a - xm) / dx));
    c1 = gpx(rnd((b - xm) / dx));
    if (compile(w, w->cf))
        return;
    w->pc = pal(w->col[w->cf]);
    ay = rnd(w->y1 * w->sy);
    for (int c = c0 < 0 ? 0 : c0; c <= c1 && c < PW; c++) {
        float y = run(w, w->x0 + (float)c * (w->x1 - w->x0) / 318.0f);
        int r;
        if (!fin1e30(y))
            continue;
        r = rnd((w->y1 - y) * w->sy);
        fillcol(c, r < ay ? r : ay, r < ay ? ay : r, w->pc);
    }
}

/* The cursor and the label of a result: X is xv, Y the function there; Ans is r. */
static void cshowres(struct gw *w, const uint8_t *r, const char *what, uint16_t fg, uint8_t cur)
{
    float x = real(w->xv);
    render(w);
    if (w->cm == C_INT)
        cshade(w);
    symce_stoans(r);
    labsave(w);
    tlabel(w);
    strcpy(w->ptb, what);
    w->ptc[0] = 0;
    w->pfg = fg;
    w->cs = 3;
    w->cres = 1;
    w->mode = M_TRACE;
    w->pn = 0;
    w->snap = 0;
    memcpy(w->x0r, (const void *)ADDR_XMIN, 9);
    w->gx = rnd((x - real_os(ADDR_XMIN)) / real_os(ADDR_DX));
    w->gx = w->gx < 0 ? 0 : w->gx > 264 ? 264 : w->gx;
    tshow(w);                                    /* Y, both stored */
    if (w->cm == C_INT) {                        /* TI: X the pixel of the larger limit, Y at the second limit typed */
        symce_osy(osn(w, w->fn), w->cr, w->yv, vtok(w));
        symce_stoy(w->yv);
        gcoord(w->xv, (const uint8_t *)ADDR_XMIN, w->gx, ADDR_DX);
        symce_stox(w->xv);
        bar(w);
    }
    if (!cur)
        chide(w);
}

/* A real as tokens: sign, digits, the point; EE only when the number is too big or small to write out. */
static unsigned rtok(const uint8_t *r, uint8_t *o)
{
    uint8_t d[14], n = 0, k = 14;
    int e = (int)r[1] - 0x80, i, pt;
    for (i = 0; i < 7; i++) {
        d[2 * i] = r[2 + i] >> 4;
        d[2 * i + 1] = r[2 + i] & 15;
    }
    if (r[0] & 0x80)
        o[n++] = 0xB0;
    if (!d[0]) {
        o[n++] = 0x30;
        return n;
    }
    while (k > 1 && !d[k - 1])
        k--;
    if (e < -9 || e > 13)
        pt = 1;                                  /* d.ddd EE e */
    else if (e < 0) {
        o[n++] = 0x3A;
        for (i = -1; i > e; i--)
            o[n++] = 0x30;
        pt = 0;
        for (i = 0; i < k; i++)
            o[n++] = (uint8_t)(0x30 + d[i]);
        return n;
    } else
        pt = e + 1;                              /* digits before the point */
    for (i = 0; i < (pt > k ? pt : k); i++) {
        if (i == pt && i < k && pt >= 1)
            o[n++] = 0x3A;
        o[n++] = (uint8_t)(0x30 + (i < k ? d[i] : 0));
    }
    if (e < -9 || e > 13) {
        o[n++] = 0x3B;
        if (e < 0) {
            o[n++] = 0xB0;
            e = -e;
        }
        if (e >= 100)
            o[n++] = (uint8_t)(0x30 + e / 100);
        if (e >= 10)
            o[n++] = (uint8_t)(0x30 + e / 10 % 10);
        o[n++] = (uint8_t)(0x30 + e % 10);
    }
    return n;
}

/* ENTER at a stage: the cursor's X is the bound, limit, curve or guess. */
static void center(struct gw *w)
{
    uint8_t r[9], x[9];
    uint16_t fg;
    unsigned e;
    char s[28];
    if (w->cm != C_ISECT)
        w->cf = w->fn;
    fg = pal(w->col[w->fn]);
    switch (w->cm) {
    case C_DYDX:
    case C_DYT:
    case C_DXT:
    case C_DRT:
        /* Par dy/dx 6, dy/dt 7, dx/dt 8; Pol dr/dtheta 9, dy/dx 10: the OS's own routines (symce_calc) */
        e = symce_calc(!w->pm ? 5 : w->cm == C_DYDX ? (w->pm == 1 ? 6 : 10) : w->cm == C_DYT ? 7 : w->cm == C_DXT ? 8 : 9,
                       w->fn, w->cl, w->cr, w->xv, 0, r);
        if (e)
            cthrow(w, e);
        symce_stoans(r);
        chide(w);
        strcpy(w->ptb, w->cm == C_DYT ? "dy/dt=" : w->cm == C_DXT ? "dx/dt=" : w->cm == C_DRT ? "dr/d\x5B=" : "dy/dx=");
        symce_fmt(r, w->ptb + 6);
        w->pfg = fg;
        w->cs = 3;
        w->cres = 3;
        tshow(w);
        return;
    case C_INT:
        if (w->cs == 0) {
            memcpy(w->cl, w->xv, 9);
            w->cmc[0] = w->gx;
            w->cs = 1;
            chide(w);
            cmark(w, w->gx, 0);
            cprompt(w);
            tshow(w);
            return;
        } else {
            uint8_t tk[72], n = 0;
            memcpy(w->cr, w->xv, 9);
            tk[n++] = 0x24;                      /* fnInt( Yn, X, lo, hi ) */
            tk[n++] = 0x5E;
            tk[n++] = (uint8_t)(0x10 + w->cf);
            tk[n++] = 0x2B;
            tk[n++] = 0x58;
            tk[n++] = 0x2B;
            n += (uint8_t)rtok(w->cl, tk + n);
            tk[n++] = 0x2B;
            n += (uint8_t)rtok(w->cr, tk + n);
            tk[n++] = 0x11;
            e = symce_eval(tk, n, r);
            if (e)
                cthrow(w, e == 1 ? 0x8F : e);
            memcpy(w->xv, real(w->cl) > real(w->cr) ? w->cl : w->cr, 9);   /* X is the larger limit (measured) */
            cshowres(w, r, "", fg, 0);
            w->cres = 2;
            cshade(w);
            s[0] = 8;                            /* Lintegral */
            strcpy(s + 1, "f(x)dx=");
            symce_fmt(r, s + 8);
            strcpy(w->ptb, s);
            w->ptc[0] = (char)0xC1;              /* LlBrack: '[' would draw theta */
            symce_fmt(w->cl, w->ptc + 1);
            strcat(w->ptc, ",");
            symce_fmt(w->cr, w->ptc + strlen(w->ptc));
            strcat(w->ptc, "]");
            bar(w);
            return;
        }
    case C_ISECT:
        if (w->cs == 0) {
            uint8_t i = w->fn;
            if (!w->vis)
                return;
            memcpy(w->cl, w->xv, 9);
            w->cf = w->fn;
            w->cmc[0] = w->gx;
            w->cmc[1] = w->gy;
            do
                i = (uint8_t)((i + 1) % NEQ);
            while (!lenok(w, i));
            chide(w);
            w->cs = 1;
            w->fn = i;
            tlabel(w);
            cdecor(w);
            cprompt(w);
            tshow(w);
            return;
        }
        if (w->cs == 1) {
            if (w->fn == w->cf && w->gx == w->cmc[0])
                return;                          /* the same curve at the same place: ignored */
            w->cf2 = w->fn;
            w->cs = 2;
            cprompt(w);
            chide(w);
            tshow(w);
            return;
        }
        e = symce_calc(4, w->cf, w->cl, w->cr, w->xv, w->fn, r);
        if (e)
            cthrow(w, e);
        memcpy(w->xv, r, 9);
        cshowres(w, r, "Intersection", 0, 1);
        return;
    default:                                     /* zero, minimum, maximum */
        if (w->cs < 2) {
            memcpy(w->cs ? w->cr : w->cl, w->xv, 9);
            w->cmc[w->cs] = w->gx;
            w->cs++;
            chide(w);
            cmark(w, w->gx, w->cs - 1);
            cprompt(w);
            tshow(w);
            return;
        }
        e = symce_calc(w->cm - 1, w->fn, w->cl, w->cr, w->xv, 0, r);
        if (e)
            cthrow(w, e);
        memcpy(w->xv, r, 9);
        cshowres(w, r, w->cm == C_ZERO ? "Zero" : w->cm == C_MIN ? "Minimum" : "Maximum", fg, 1);
    }
}

/* The pick from the menu hook. */
static void cpick(struct gw *w, uint8_t item)
{
    uint8_t i = OSB(0x0D01474), n = w->pm ? nu(w) : NEQ;
    plain(w);
    render(w);
    symce_dirty();
    if (w->pm) {                                 /* the menu: Par 1 value 2 dy/dx 3 dy/dt 4 dx/dt, Pol 1 value 2 dy/dx 3 dr/dtheta */
        if (item > (w->pm == 1 ? 4 : w->pm == 2 ? 3 : 1))
            return;
        item = item < 3 ? (item == 1 ? C_VAL : C_DYDX) : w->pm == 2 ? C_DRT : item == 3 ? C_DYT : C_DXT;
    }
    i = i ? (uint8_t)(i - 1) : 0;
    if (i >= n || !have(w, i))
        for (i = 0; i < n && !have(w, i); i++)
            ;
    if (i == n)
        return;
    w->cm = item;
    w->cs = 0;
    w->fn = w->cf = i;
    w->hk = 0;
    if (item == C_VAL) {                         /* the X= box at once */
        w->mode = M_ENTRY;
        w->nent = 0;
        ebox(w);
        return;
    }
    labsave(w);
    w->mode = M_TRACE;
    w->gx = 132;
    w->pn = 0;
    w->snap = 0;
    w->tk = 0;
    if (w->pm)
        tset(w, 0);
    memcpy(w->x0r, (const void *)ADDR_XMIN, 9);
    tlabel(w);
    cprompt(w);
    tcalc(w);
}

/* The X= box: a white strip with the typed line in the large font, 12 px a character,
   and a block cursor. */
static void ebox(struct gw *w)
{
    char s[40], t[16];
    int n = 2, x = 2;
    fill(BOXY, BOXY + 19, 0xFFFF);
    for (int i = 0; i < VS; i++)
        VRAM[BOXY * VS + i] = VRAM[(BOXY + 18) * VS + i] = 0;
    s[0] = w->pm == 1 ? 'T' : w->pm == 2 ? 0x5B : 'X';
    if (w->pm == 3) {
        static const uint8_t nt[2] = { 0x62, 0x21 };
        symce_tok_(nt, t);
        s[0] = t[0];
    }
    s[1] = '=';
    for (int i = 0; i < w->nent;) {
        i += symce_tok(w->ent + i, t);
        for (char *p = t; *p && n < 39; p++)
            s[n++] = *p;
    }
    for (int i = 0; i < n; i++, x += 12) {
        char c[2] = { s[i], 0 };
        symce_text(x, BOXY + 3, 0, 0xFFFF, c, 1);
    }
    for (int j = BOXY + 3; j < BOXY + 17; j++)
        for (int i = x; i < x + 10 && i < PW; i++)
            VRAM[j * VS + i] = 0;
}

/* Keys the X= box takes: scan code, token. (2nd ^ is pi, 2nd , is EE.) ponytail: digits,
   the point, (-), + - * / ^, parentheses, X, pi, EE; TI's box takes any expression key. */
static const uint8_t etab[] = { 0x21, 0x30, 0x22, 0x31, 0x1A, 0x32, 0x12, 0x33, 0x23, 0x34, 0x1B, 0x35,
                                0x13, 0x36, 0x24, 0x37, 0x1C, 0x38, 0x14, 0x39, 0x19, 0x3A, 0x11, 0xB0,
                                0x0A, 0x70, 0x0B, 0x71, 0x0C, 0x82, 0x0D, 0x83, 0x0E, 0xF0, 0x1D, 0x10,
                                0x15, 0x11, 0x28, 0x58, 0 };
static const uint8_t etab2[] = { 0x0E, 0xAC, 0x25, 0x3B, 0 };

static uint8_t etok(uint8_t key, uint8_t sec)
{
    for (const uint8_t *t = sec ? etab2 : etab; *t; t += 2)
        if (*t == key)
            return t[1];
    return 0;
}

/* The X key types the T or theta in Par and Pol. */
static uint8_t etokw(const struct gw *w, uint8_t key, uint8_t sec)
{
    uint8_t t = etok(key, sec);
    return t == 0x58 ? vtok(w) : t;
}

/* ENTER in the box: the typed line through the OS parser (no Ans), then TI's range check. */
static void eenter(struct gw *w)
{
    uint8_t r[9];
    unsigned e = symce_eval(w->ent, w->nent, r);
    float x;
    tunpoi(w);
    thist0(w);
    if (e)
        cthrow(w, e == 1 ? 0x8F : e);       /* ERROR: <what the parser said> */
    x = real(r);
    if (w->pm) {                                 /* Par, Pol: T within Tmin..Tmax (theta within its window) */
        if (!(w->pm == 3 ? x > sstart() - 1e-5f && (webt(w) || x < ival(ADDR_NMAX) + 1e-5f) : x >= real_os(tbase(w)) && x <= real_os(tbase(w) + 9)))
            cthrow(w, 0x8F);
        if (w->pm == 3 && fabsf(x - floorf(x + 0.5f)) > 1e-5f * (1 + x))   /* (real() is a float: 8 comes out 8.00001) */
            cthrow(w, 0x84);                     /* ERROR: DOMAIN (TI: INVALID first, outside sstart..nMax) */
        memcpy(w->xv, r, 9);
        w->mode = M_TRACE;
        w->snap = 1;
        replot(w);
        labsave(w);                              /* (the plot was redrawn under the box: the label strip again) */
        tlabel(w);
        if (webt(w)) {                           /* Web: n = nMin is step 0, else the step on the curve; the cobweb is drawn up to it */
            int n = rnd(x), m = ival(ADDR_NMIN);
            w->tk = n > m ? 2 * (n - m) - 1 : 0;
            w->sn = -1;                          /* (the OS parser on the typed n leaves the solver's terms stale) */
        }
        tshow(w);
        if (w->cm == C_VAL) {                    /* CALC value: the point, and the result is up */
            tshow(w);
            w->cs = 3;
            w->cres = 1;
        } else if (w->cm)
            center(w);
        return;
    }
    if (!(x >= real_os(ADDR_XMIN) && x <= real_os(ADDR_XMAX)))
        cthrow(w, 0x8F);                    /* ERROR: INVALID */
    memcpy(w->xv, r, 9);
    w->mode = M_TRACE;
    w->gx = rnd((x - real_os(ADDR_XMIN)) / real_os(ADDR_DX));
    w->pn = 0;
    w->snap = 1;
    memcpy(w->x0r, (const void *)ADDR_XMIN, 9);
    replot(w);
    labsave(w);                                  /* (the plot was redrawn under the box: the label strip again) */
    tlabel(w);
    if (w->cm == C_VAL) {                        /* CALC value: the point, and the result is up */
        tshow(w);
        w->cs = 3;
        w->cres = 1;
        return;
    }
    tshow(w);
    if (w->cm)                                   /* a typed bound, limit, curve or guess */
        center(w);
    else if (w->dr.dm == D_TANG)                 /* Tangent(: ENTER in the box draws at once */
        dtent(w);
}

/* An arrow (GetCSC 1 down, 2 left, 3 right, 4 up) n times for the free cursor, once for TRACE. */
static void arrow(struct gw *w, uint8_t key, int rep)
{
    if (w->cres && w->pm == 3 && (key == 1 || key == 4)) {   /* Seq, after the value: up and down go on tracing (TI) */
        tswap(w, key == 4 ? -1 : 1);
        return;
    }
    if (w->cres) {                               /* after a CALC result: a free cursor where the result is */
        int gx = w->gx, gy = w->gy;
        uint8_t tg = w->dr.dm == D_TANG;
        chide(w);
        plain(w);
        replot(w);
        w->mode = M_FREE;
        w->gx = gx;
        w->gy = gy;
        if (tg) {
            dr_home(w);                          /* after Tangent( the free cursor is where it was left (measured) */
            w->dr.dm = D_TANG;                   /* ... and goes on remembering it (dr_prev) */
        }
        w->cm = w->cs = w->cres = 0;             /* the CALC is over: a plain free cursor from now on */
    }
    if (w->mode == M_FREE || w->mode == M_PLAIN) {
        int n = rep ? 5 : 1;
        if (w->mode == M_PLAIN) {
            w->zm = w->zc = 0;
            w->mode = M_FREE;
            w->gx = 132;
            w->gy = 82;
            if (mget(40) == 1) {                 /* after a Tangent( session: from the spot it kept, and keeps it */
                dr_home(w);
                w->dr.dm = D_TANG;
            }
        }
        if (key == 1) w->gy -= n;
        if (key == 2) w->gx -= n;
        if (key == 3) w->gx += n;
        if (key == 4) w->gy += n;
        w->gx = w->gx < 0 ? 0 : w->gx > 264 ? 264 : w->gx;
        w->gy = w->gy < 0 ? 0 : w->gy > 164 ? 164 : w->gy;
        if (w->dr.dm == D_TEXT) {                /* Text(: the cell stays on the grid */
            w->gx = w->gx > 263 ? 263 : w->gx;
            w->gy = w->gy < 16 ? 16 : w->gy;
        }
        fshow(w);
    } else if (key == 2 || key == 3) {
        tmove(w, key == 3 ? 1 : -1);
    } else {
        tswap(w, key == 4 ? -1 : 1);
    }
}

/* Between keys: the blink, and a held arrow's repeats. */
static void tb_ttick(struct gw *w);
static void tick(struct gw *w)
{
    unsigned t;
    if (w->tbl.on) {
        tb_ttick(w);
        return;
    }
    if (w->mode != M_FREE && w->mode != M_TRACE) {
        w->hk = 0;
        return;
    }
    t = now();
    if (w->hk) {
        if (!(*(volatile uint8_t *)ADDR_KB7 & (1 << (w->hk - 1))))
            w->hk = 0;
        else if (((t - w->th) & 0xFFFFFF) >= HOLD && ((t - w->tr) & 0xFFFFFF) >= REPT) {
            w->tr = t;
            arrow(w, w->hk, 1);
        }
    }
    if (w->shown && ((t - w->tb) & 0xFFFFFF) >= BLINK) {
        w->tb = t;
        for (int j = 0; j < 9; j++)              /* back to what was under it, then the other frame */
            for (int i = 0; i < 9; i++)
                put(w->cx - 4 + i, w->cy - 4 + j, w->sv[j * 9 + i]);
        w->frame ^= 1;
        cdraw(w);
    }
}

#include "graph_draw.c"

/* GetCSC scan code -> the OS key the same key sends (Y= ... and the 2nd menus). */
static const uint8_t hand1[] = { 0x35, 0x49, 0x34, 0x48, 0x33, 0x2E, 0x32, 0x5A, 0x37, 0x45,
                                 0x2F, 0x32, 0x1F, 0x2D, 0x17, 0x35, 0x20, 0x31, 0x27, 0x2C, 0 };
static const uint8_t hand2[] = { 0x32, 0x3B, 0x33, 0x57, 0x31, 0x4A, 0x34, 0x4B, 0x35, 0x30,
                                 0x1F, 0x2F, 0x0A, 0x36, 0x20, 0x3A, 0x2F, 0x33, 0x27, 0x39,
                                 0x17, 0x38, 0x2E, 0x37, 0x21, 0x3E, 0 };

#include "graph_table.c"

/* Key 0 draws, 0xFF is "no key yet" (the blink and a held arrow's repeats), 0xFE starts
   TRACE. A GetCSC code afterwards: CLEAR and 2nd QUIT (2nd MODE) return 1 to go home
   (CLEAR only from the plain graph; with a cursor it is the plain graph again), GRAPH
   redraws, the arrows move the cursor, TRACE traces, in TRACE ENTER zooms and a digit
   opens the X= box, the keys that leave the graph (hand1, and hand2 after 2nd) return the
   OS key for main.asm to send, the rest do nothing. */
uint8_t graph_key(struct gw *w, uint8_t key)
{
    uint8_t sec = w->sec, m = w->mode, tk;
    const uint8_t *t;
    if (key == 0xFF) {
        tick(w);
        return 0;
    }
    if (!key) {
        plain(w);
        render(w);
        return 0;
    }
    if (w->tbl.on)
        return tb_tkey(w, key);
    if (key == 0xFE) {
        tstart(w);
        return 0;
    }
    if (key == 0xE0) {                           /* 2nd GRAPH from home */
        tb_topen(w);
        return 0;
    }
    if (key == 0xFC) {                           /* back from an OS menu opened in Text(: its token, at the cursor */
        dr_tpaste(w);
        return 0;
    }
    if (key >= 0xF1 && key <= 0xFD) {            /* a 2nd DRAW pick */
        dr_pick(w, key - 0xF0);
        return 0;
    }
    if (key >= 0xE1) {
        cpick(w, key - 0xE0);
        return 0;
    }
    if (key >= 0xC1) {
        zpick(w, key - 0xC0);
        return 0;
    }
    if (key == 0x36) {                           /* 2nd */
        w->sec = 1;
        return 0;
    }
    if (key == 0x30) {                           /* alpha */
        if (m == M_FREE && w->dr.dm == D_TEXT) {
            w->dr.ds = sec ? 2 : 1;              /* ALPHA once, 2nd ALPHA locked */
            w->sec = 0;
        }
        return 0;
    }
    w->sec = 0;
    if (sec && key == 0x37)
        return 1;
    if (m == M_FREE && w->dr.dm == D_TEXT && dr_text(w, key, sec))
        return 0;
    if (!sec && key >= 1 && key <= 4) {
        if (w->hk == key && m != M_ENTRY && ((now() - w->tr) & 0xFFFFFF) < 5000) {   /* GetCSC repeating a held key (a tap of the same arrow comes 150 ms or more after the last): tick() does TI's repeats (0.5 s, then 5 px a 100 ms) */
            if (((now() - w->th) & 0xFFFFFF) < HOLD)
                w->tr = now();                   /* (after HOLD tr is tick()'s: its repeats need it) */
            return 0;
        }
        w->hk = key;
        w->th = w->tr = now();
        if (m != M_ENTRY)
            arrow(w, key, 0);
        return 0;
    }
    if (!sec && key == 0x32 && m == M_FREE && w->dr.dm == D_TEXT) {   /* TRACE in Text(: TI's plain graph, the text kept */
        cleave(w);
        return 0;
    }
    if (!sec && key == 0x32) {                   /* TRACE */
        if ((sp_state() && !sp_trok()) || (w->pm == 3 && ((OSB(ADDR_SEQF) & 0x1C) || (webt(w) && !webone(w)))))
            return 0x5A;                         /* stat plots on, Seq uv/vw/uw: TI's TRACE walks them (graph_fall hands the OS the graph) */
        if (w->cm)
            return 0;                            /* ponytail: TRACE inside CALC untested */
        if (m == M_ENTRY) {                      /* back to the start of the trace */
            w->mode = M_TRACE;
            w->gx = 132;
            w->pn = 0;
            w->snap = 0;
            if (w->pm) {
                w->tk = 0;
                tset(w, 0);
            }
            memcpy(w->x0r, (const void *)ADDR_XMIN, 9);
            replot(w);
            tcalc(w);
        } else if (m != M_TRACE)
            tstart(w);
        return 0;
    }
    if (m == M_ENTRY) {
        if (!sec && key == 0x09) {
            if (w->nent)
                eenter(w);
            return 0;
        }
        if (!sec && key == 0x38) {               /* DEL */
            if (w->nent)
                w->nent--;
            ebox(w);
            return 0;
        }
        if (!sec && key == 0x0F) {               /* CLEAR: the line, then the trace */
            if (w->nent) {
                w->nent = 0;
                ebox(w);
                return 0;
            }
            cleave(w);
            return 0;
        }
        tk = etokw(w, key, sec);
        if (tk) {
            if (w->nent < sizeof w->ent)
                w->ent[w->nent++] = tk;
            ebox(w);
            return 0;
        }
    }
    if (m == M_FREE && w->dr.dm && w->dr.dm != D_TEXT && w->dr.dm != D_TANG && !sec && key == 0x09) {
        dr_enter(w);
        return 0;
    }
    if (m == M_FREE && w->zm && !sec && key == 0x09) {
        zenter(w);
        return 0;
    }
    if (m == M_TRACE) {
        if (w->pt && !(!sec && key == 0x09)) {   /* plot TRACE: a digit goes to HOME typed there, as TI; other entry keys do nothing */
            uint8_t d = etok(key, sec) - 0x30;
            if (etok(key, sec))
                return d < 10 ? 0x8E + d : 0;   /* k0 = 0x8E, k1..k9 = 0x8F..0x97 */
        }
        if (!sec && key == 0x09) {
            if (w->dr.dm == D_TANG) {
                if (w->cres)
                    cleave(w);
                else
                    dtent(w);
            } else if (!w->cm)
                tzoom(w);
            else if (w->cres)
                cleave(w);
            else
                center(w);
            return 0;
        }
        tk = w->cres ? 0 : etokw(w, key, sec);
        if (tk) {                                /* a digit ...: the X= box opens, the cursor is gone */
            chide(w);
            w->mode = M_ENTRY;
            w->nent = 1;
            w->ent[0] = tk;
            ebox(w);
            return 0;
        }
    }
    if (!sec && (key == 0x0F || (m != M_PLAIN && key == 0x09))) {
        if (m == M_PLAIN)
            return 1;
        if (key == 0x0F && m == M_TRACE && webt(w) && !w->cm) {   /* Web CLEAR: the cursor, label and bar go, the cobweb stays */
            uint8_t x[9], y[9];
            int n;
            chide(w);
            plain(w);
            replot(w);                           /* the bar sat on the plot's foot: the plot again, and the cobweb redrawn up to the step */
            if (w->tk) {
                w->zx = w->zr[2] = 0;
                webseg(w, &n, x, y);
            }
            symce_stox(w->pxv);
            if (w->yok)
                symce_stoy(w->yv);
            return 0;
        }
        cleave(w);                               /* the cursor is forgotten */
        return 0;
    }
    if (!sec && key == 0x31) {
        cleave(w);
        return 0;
    }
    if (sec && key == 0x31) {                    /* 2nd GRAPH: SymCE's table, or TI's for what it does not do */
        if (tb_fall())
            return 0x4A;
        tb_topen(w);
        return 0;
    }
    for (t = sec ? hand2 : hand1; *t; t += 2)
        if (*t == key) {
            if (m == M_FREE && w->dr.dm == D_TEXT && (t[1] == 0x32 || t[1] == 0x33 || t[1] == 0x35 || t[1] == 0x37 || t[1] == 0x39 || t[1] == 0x3A))
                *(volatile uint16_t *)0x0D0FB12 = 0x5AA5;   /* MATH, TEST, MATRIX, ANGLE, LIST (not DISTR: its items open a wizard and never reach the menu hook): an item is written at the cursor (main.asm, menuKey) */
            if (m == M_FREE && w->dr.dm == D_TEXT && t[1] == 0x3E)
                *(volatile uint16_t *)0x0D0FB16 = 0x5AA5;   /* CATALOG: its pick is the raw key hook's (main.asm, .cat) */
            return t[1];
        }
    return 0;
}

#ifdef __ez80__
/* The entry points, first in the image (src/engine.ld): +0 is symce_graph(w,
   key), +4 is symce_gfall(w). */
__asm__(
"	.section	.text._symce_graph,\"ax\",@progbits\n"
"	.globl	_symce_graph\n"
"_symce_graph:\n"
"	jp	_graph_key\n"
"	jp	_graph_fall\n"
);

/* The OS's parser on Yn (n 0..9 = Y1..Y0), as os_EvalVar does it but without
   the StoAns, so Ans stays. xr a TI real: stored in X first; 0 leaves X alone.
   The result real goes to out. Returns 0, or the OS error code, or 1 if the
   answer was not a real number. The error handler is what libce uses. */
__asm__(
"	.section	.text._symce_osy,\"ax\",@progbits\n"
"	.globl	_symce_osy\n"
"_symce_osy:\n"
"	push	ix\n"
"	ld	ix, 0\n"
"	add	ix, sp\n"                /* ix+6 n, +9 xr, +12 out */
"	ld	iy, 0x0D00080\n"
"	ld	hl, (ix + 9)\n"
"	ld	bc, 0\n"
"	or	a, a\n"
"	sbc	hl, bc\n"
"	jr	z, .Lnox\n"
"	call	0x0020320\n"          /* Mov9ToOP1 */
"	ld	a, (ix + 15)\n"
"	call	.Lstva\n"             /* StoX, StoT, or theta */
".Lnox:\n"
"	ld	hl, 0x0D005F8\n"         /* OP1 = the equation's name, 03 5E 1n 00 */
"	ld	(hl), 3\n"
"	inc	hl\n"
"	ld	(hl), 0x5E\n"
"	inc	hl\n"
"	ld	a, (ix + 6)\n"
"	add	a, 0x10\n"
"	ld	(hl), a\n"
"	inc	hl\n"
"	ld	(hl), 0\n"
"	push	ix\n"
"	ld	hl, (0x0D02596)\n"
"	push	hl\n"
"	ld	hl, .Lerr\n"
"	call	0x0020798\n"          /* PushErrorHandler */
"	call	0x0020510\n"          /* FindSym */
"	ld	a, 0x8D\n"
"	jr	c, .Lerr\n"
"	call	0x0020F00\n"          /* ParseInp */
"	call	0x002079C\n"          /* PopErrorHandler */
"	xor	a, a\n"
".Lerr:\n"
"	pop	de\n"
"	push	af\n"
"	call	0x0020E80\n"          /* FixTempCnt */
"	pop	af\n"
"	pop	ix\n"
"	ld	iy, 0x0D00080\n"
"	res	0, (iy + 7)\n"
"	or	a, a\n"
"	jr	nz, .Lout\n"
"	ld	hl, 0x0D005F8\n"
"	ld	a, (hl)\n"
"	and	a, 0x1F\n"
"	ld	a, 1\n"
"	jr	nz, .Lout\n"
"	ld	de, (ix + 12)\n"
"	ld	bc, 9\n"
"	ldir\n"
"	xor	a, a\n"
".Lout:\n"
"	pop	ix\n"
"	ret\n"
"	.section	.text._symce_stoy,\"ax\",@progbits\n"
"	.globl	_symce_stoy\n"
"_symce_stoy:\n"                 /* (real): Y = it */
"	pop	de\n"
"	pop	hl\n"
"	push	hl\n"
"	push	de\n"
"	push	ix\n"
"	ld	iy, 0x0D00080\n"
"	call	0x0020320\n"
"	call	0x0020F3C\n"          /* StoY */
"	pop	ix\n"
"	ret\n"
/* stov(v, real): variable v (token 58 X, 54 T, 5B theta, 52 R ...) = it. .Lstva: OP1 = the value, A = v. */
"	.section	.text._symce_stov,\"ax\",@progbits\n"
"	.globl	_symce_stov\n"
"_symce_stov:\n"
"	push	ix\n"
"	ld	ix, 0\n"
"	add	ix, sp\n"               /* ix+6 v, +9 r */
"	ld	iy, 0x0D00080\n"
"	ld	hl, (ix + 9)\n"
"	call	0x0020320\n"
"	ld	a, (ix + 6)\n"
"	call	.Lstva\n"
"	pop	ix\n"
"	ret\n"
".Lstva:\n"
"	cp	a, 0x58\n"
"	jp	z, 0x0020F48\n"
"	cp	a, 0x54\n"
"	jp	z, 0x0020F44\n"
"	ld	hl, -10\n"           /* frame: 9 bytes of value, then the name byte */
"	add	hl, sp\n"
"	ld	sp, hl\n"
"	ld	bc, 9\n"
"	add	hl, bc\n"
"	ld	(hl), a\n"
"	ld	hl, 0\n"
"	add	hl, sp\n"
"	ex	de, hl\n"
"	ld	hl, 0x0D005F8\n"
"	ldir\n"
"	ld	hl, 9\n"
"	add	hl, sp\n"
"	ld	a, (hl)\n"
"	ld	hl, 0x0D005F8\n"
"	ld	(hl), 0\n"
"	inc	hl\n"
"	ld	(hl), a\n"
"	inc	hl\n"
"	ld	(hl), 0\n"
"	inc	hl\n"
"	ld	(hl), 0\n"            /* the name is 3 bytes: letter, 0, 0 */
"	call	0x002050C\n"          /* ChkFindSym */
"	jr	nc, .Lsf\n"
"	call	0x0020534\n"          /* CreateReal: DE = its data */
".Lsf:\n"
"	ld	hl, 0\n"
"	add	hl, sp\n"
"	ld	bc, 9\n"
"	ldir\n"
"	ld	hl, 10\n"
"	add	hl, sp\n"
"	ld	sp, hl\n"
"	ret\n"
);


/* stox(real): X = it. fp(fn, a, b, out): out = a <op> b with the OS routine at fn
   (FPAdd, FPSub, FPMult, FPDiv); the result is rounded to the OS's 14 digits. */
__asm__(
"	.section	.text._symce_stox,\"ax\",@progbits\n"
"	.globl	_symce_stox\n"
"_symce_stox:\n"
"	pop	de\n"
"	pop	hl\n"
"	push	hl\n"
"	push	de\n"
"	push	ix\n"
"	ld	iy, 0x0D00080\n"
"	call	0x0020320\n"
"	call	0x0020F48\n"          /* StoX */
"	pop	ix\n"
"	ret\n"
"	.section	.text._symce_fp,\"ax\",@progbits\n"
"	.globl	_symce_fp\n"
"_symce_fp:\n"
"	push	ix\n"
"	ld	ix, 0\n"
"	add	ix, sp\n"                /* ix+6 fn, +9 a, +12 b, +15 out */
"	ld	iy, 0x0D00080\n"
"	ld	hl, (ix + 9)\n"
"	call	0x0020320\n"          /* Mov9ToOP1 */
"	ld	hl, (ix + 12)\n"
"	call	0x0020328\n"          /* Mov9ToOP2 */
"	ld	hl, (ix + 6)\n"
"	call	.Lfpj\n"
"	ld	hl, 0x0D005F8\n"
"	ld	de, (ix + 15)\n"
"	ld	bc, 9\n"
"	ldir\n"
"	pop	ix\n"
"	ret\n"
".Lfpj:\n"
"	jp	(hl)\n"
/* fmt(real, out): the 9-character number field TI's graph screen prints (OS 0x9968B, which
   calls FormEReal), as a string in out. */
"	.section	.text._symce_fmt,\"ax\",@progbits\n"
"	.globl	_symce_fmt\n"
"_symce_fmt:\n"
"	push	ix\n"
"	ld	ix, 0\n"
"	add	ix, sp\n"                /* ix+6 real, +9 out */
"	ld	iy, 0x0D00080\n"
"	ld	hl, (ix + 6)\n"
"	call	0x0020320\n"
"	set	6, (iy + 29)\n"
"	ld	a, 9\n"
"	call	0x009968B\n"          /* HL = OP3, the text */
"	ld	de, (ix + 9)\n"
".Lfc:\n"
"	ld	a, (hl)\n"
"	ld	(de), a\n"
"	inc	hl\n"
"	inc	de\n"
"	or	a, a\n"
"	jr	nz, .Lfc\n"
"	pop	ix\n"
"	ret\n"
/* fill(p, n, c): n 16-bit pixels at p set to c, by LDIR over the first pixel (n >= 1). */
"	.section	.text._symce_fill,\"ax\",@progbits\n"
"	.globl	_symce_fill\n"
"_symce_fill:\n"
"	push	ix\n"
"	ld	ix, 0\n"
"	add	ix, sp\n"                /* ix+6 p, +9 n, +12 c */
"	ld	hl, (ix + 6)\n"
"	ld	bc, (ix + 12)\n"
"	ld	(hl), c\n"
"	inc	hl\n"
"	ld	(hl), b\n"
"	dec	hl\n"
"	ld	bc, (ix + 9)\n"
"	dec	bc\n"
"	ld	a, b\n"
"	or	a, c\n"
"	jr	z, .Lfd\n"
"	sla	c\n"
"	rl	b\n"
"	push	hl\n"
"	pop	de\n"
"	inc	de\n"
"	inc	de\n"
"	ldir\n"
".Lfd:\n"
"	pop	ix\n"
"	ret\n"
/* fmtn(real, out, width): symce_fmt with a width, the OS's 0x9968B number field (the table's bottom line). */
"	.section	.text._symce_fmtn,\"ax\",@progbits\n"
"	.globl	_symce_fmtn\n"
"_symce_fmtn:\n"
"	push	ix\n"
"	ld	ix, 0\n"
"	add	ix, sp\n"
"	ld	iy, 0x0D00080\n"
"	ld	hl, (ix + 6)\n"
"	call	0x0020320\n"
"	set	6, (iy + 29)\n"
"	ld	a, (ix + 12)\n"
"	call	0x009968B\n"
"	ld	de, (ix + 9)\n"
".Lfn:\n"
"	ld	a, (hl)\n"
"	ld	(de), a\n"
"	inc	hl\n"
"	inc	de\n"
"	or	a, a\n"
"	jr	nz, .Lfn\n"
"	pop	ix\n"
"	ret\n"
/* fmtw(real, out, width): the number TI's table prints in a cell of that many characters (OS 0x98F2B, as
   the table's 0xB62B8 calls it for a real), and in the bottom line for the selected cell. The text is
   at OP3's 0xD0060E and is copied to out. */
"	.section	.text._symce_fmtw,\"ax\",@progbits\n"
"	.globl	_symce_fmtw\n"
"_symce_fmtw:\n"
"	push	ix\n"
"	ld	ix, 0\n"
"	add	ix, sp\n"                /* ix+6 real, +9 out, +12 width */
"	ld	iy, 0x0D00080\n"
"	ld	hl, (ix + 6)\n"
"	call	0x0020320\n"
"	ld	a, (ix + 12)\n"
"	call	0x0098F2B\n"
"	ld	hl, 0x0D0060E\n"
"	ld	de, (ix + 9)\n"
".Lfw:\n"
"	ld	a, (hl)\n"
"	ld	(de), a\n"
"	inc	hl\n"
"	inc	de\n"
"	or	a, a\n"
"	jr	nz, .Lfw\n"
"	pop	ix\n"
"	ret\n"
/* text(x, y, fg, bg, s, big): s at the pen (x, y) with VPutMap in the small font, or the
   large one if big. */
"	.section	.text._symce_text_,\"ax\",@progbits\n"
"	.globl	_symce_text_\n"
"_symce_text_:\n"
"	push	ix\n"
"	ld	ix, 0\n"
"	add	ix, sp\n"                /* ix+6 x, +9 y, +12 fg, +15 bg, +18 s, +21 big */
"	ld	iy, 0x0D00080\n"
"	ld	a, (iy + 0x32)\n"
"	push	af\n"
"	ld	hl, (ix + 6)\n"
"	ld	a, l\n"
"	ld	(0x0D008D2), a\n"
"	ld	a, h\n"
"	ld	(0x0D008D3), a\n"
"	ld	a, (ix + 9)\n"
"	ld	(0x0D008D5), a\n"
"	ld	hl, (ix + 12)\n"
"	ld	a, l\n"
"	ld	(0x0D026AC), a\n"
"	ld	a, h\n"
"	ld	(0x0D026AD), a\n"
"	ld	hl, (ix + 15)\n"
"	ld	a, l\n"
"	ld	(0x0D026AA), a\n"
"	ld	a, h\n"
"	ld	(0x0D026AB), a\n"
"	ld	a, (ix + 21)\n"
"	or	a, a\n"
"	jr	z, .Lsm\n"
"	set	2, (iy + 0x32)\n"
"	jr	.Lgo\n"
".Lsm:\n"
"	res	2, (iy + 0x32)\n"
".Lgo:\n"
"	ld	hl, (ix + 18)\n"
".Lch:\n"
"	ld	a, (hl)\n"
"	or	a, a\n"
"	jr	z, .Ldn\n"
"	inc	hl\n"
"	push	hl\n"
"	call	0x0020830\n"          /* VPutMap */
"	pop	hl\n"
"	jr	.Lch\n"
".Ldn:\n"
"	pop	af\n"
"	ld	iy, 0x0D00080\n"
"	ld	(iy + 0x32), a\n"
"	pop	ix\n"
"	ret\n"
/* sglyph(c): the OS's small-font record of display char c (Load_Sfont: A = c, HL = c*25), byte 0 = advance. */
"	.section	.text._symce_sglyph,\"ax\",@progbits\n"
"	.globl	_symce_sglyph\n"
"_symce_sglyph:\n"
"	push	ix\n"
"	ld	ix, 0\n"
"	add	ix, sp\n"                /* ix+6 c */
"	ld	iy, 0x0D00080\n"
"	ld	a, (ix + 6)\n"
"	ld	hl, 0\n"
"	ld	h, 25\n"
"	ld	l, a\n"
"	mlt	hl\n"
"	call	0x00A55D3\n"
"	pop	ix\n"
"	ret\n"
/* tok(t, out): the text of the token at t (GetTokString) into out. */
"	.section	.text._symce_tok_,\"ax\",@progbits\n"
"	.globl	_symce_tok_\n"
"_symce_tok_:\n"
"	push	ix\n"
"	ld	ix, 0\n"
"	add	ix, sp\n"                /* ix+6 t, +9 out */
"	ld	iy, 0x0D00080\n"
"	ld	hl, (ix + 6)\n"
"	call	0x005E43B\n"          /* DE = the token, carry if it has a prefix */
"	call	0x00A2C17\n"          /* HL = length, then the characters */
"	ld	bc, 0\n"
"	ld	c, (hl)\n"
"	inc	hl\n"
"	ld	de, (ix + 9)\n"
"	ld	a, c\n"
"	or	a, a\n"
"	jr	z, .Lt0\n"
"	ldir\n"
".Lt0:\n"
"	xor	a, a\n"
"	ld	(de), a\n"
"	pop	ix\n"
"	ret\n"
/* eval(tok, n, out): the OS parser on n tokens, as os_Eval does it but without the StoAns.
   0 and the real in out, or the OS error code, or 1 if the answer is not a real. */
"	.section	.text._symce_eval,\"ax\",@progbits\n"
"	.globl	_symce_eval\n"
"_symce_eval:\n"
"	push	ix\n"
"	ld	ix, 0\n"
"	add	ix, sp\n"                /* ix+6 tok, +9 n, +12 out */
"	ld	iy, 0x0D00080\n"
"	push	ix\n"
"	ld	hl, (0x0D02596)\n"
"	push	hl\n"
"	ld	hl, .Lee\n"
"	call	0x0020798\n"          /* PushErrorHandler */
"	ld	hl, (ix + 9)\n"
"	inc	hl\n"
"	inc	hl\n"
"	call	0x002055C\n"          /* CreatetEqu */
"	inc	de\n"
"	inc	de\n"
"	xor	a, a\n"
"	ld	(de), a\n"
"	inc	de\n"
"	ld	(de), a\n"
"	inc	de\n"
"	ld	bc, (ix + 9)\n"
"	ld	hl, (ix + 6)\n"
"	ldir\n"
"	call	0x00202C8\n"          /* OP4ToOP1 */
"	call	0x0020F00\n"          /* ParseInp */
"	call	0x002079C\n"          /* PopErrorHandler */
"	xor	a, a\n"
".Lee:\n"
"	pop	de\n"
"	push	af\n"
"	call	0x0020E80\n"          /* FixTempCnt */
"	pop	af\n"
"	pop	ix\n"
"	ld	iy, 0x0D00080\n"
"	res	0, (iy + 7)\n"
"	or	a, a\n"
"	jr	nz, .Leo\n"
"	ld	hl, 0x0D005F8\n"
"	ld	a, (hl)\n"
"	and	a, 0x1F\n"
"	ld	a, 1\n"
"	jr	nz, .Leo\n"
"	ld	de, (ix + 12)\n"
"	ld	bc, 9\n"
"	ldir\n"
"	xor	a, a\n"
".Leo:\n"
"	pop	ix\n"
"	ld	hl, 0\n"
"	ld	l, a\n"
"	ret\n"
/* rom(addr): call an OS routine (no arguments), keeping ix; iy is the OS's. zxy(in): the OS's ZoomIn
   (in = 1) or ZoomOut about X, Y with XFact, YFact (0xB18E7, as the cursor's ENTER calls it). */
"	.section	.text._symce_rom,\"ax\",@progbits\n"
"	.globl	_symce_rom\n"
"_symce_rom:\n"
"	pop	de\n"
"	pop	hl\n"
"	push	hl\n"
"	push	de\n"
"	push	ix\n"
"	ld	iy, 0x0D00080\n"
"	ld	de, .Lrb\n"
"	push	de\n"
"	jp	(hl)\n"
".Lrb:\n"
"	pop	ix\n"
"	ret\n"
"	.section	.text._symce_roma,\"ax\",@progbits\n"
"	.globl	_symce_roma\n"
"_symce_roma:\n"
"	pop	de\n"
"	pop	hl\n"
"	pop	bc\n"
"	push	bc\n"
"	push	hl\n"
"	push	de\n"
"	ld	a, c\n"
"	push	ix\n"
"	ld	iy, 0x0D00080\n"
"	ld	de, .Lrc\n"
"	push	de\n"
"	jp	(hl)\n"
".Lrc:\n"
"	pop	ix\n"
"	ret\n"
"	.section	.text._symce_zxy,\"ax\",@progbits\n"
"	.globl	_symce_zxy\n"
"_symce_zxy:\n"
"	pop	de\n"
"	pop	hl\n"
"	push	hl\n"
"	push	de\n"
"	push	ix\n"
"	ld	iy, 0x0D00080\n"
"	ld	a, l\n"
"	sub	a, 1\n"                 /* carry if out ... */
"	ccf\n"                        /* ... set for in */
"	ld	hl, 0x0D01FA4\n"
"	ld	de, 0x0D01F9B\n"
"	call	0x00B18E7\n"
"	pop	ix\n"
"	ret\n"
/* lst(nm): the list whose 5 name bytes (as a stat plot record keeps them) are at nm, its data (count word, then the
   9-byte reals), or 0. ChkFindSym, so an archived list is read in place. */
"	.section	.text._symce_lst,\"ax\",@progbits\n"
"	.globl	_symce_lst\n"
"_symce_lst:\n"
"	push	ix\n"
"	ld	ix, 0\n"
"	add	ix, sp\n"
"	ld	iy, 0x0D00080\n"
"	ld	hl, (ix + 6)\n"
"	ld	de, 0x0D005FA\n"
"	ld	bc, 5\n"
"	ldir\n"
"	xor	a, a\n"
"	ld	(de), a\n"
"	ld	hl, 0x0D005F8\n"
"	ld	(hl), 1\n"                   /* ListObj */
"	inc	hl\n"
"	ld	(hl), 0x5D\n"
"	call	0x002050C\n"              /* ChkFindSym */
"	ld	hl, 0\n"
"	jr	c, .Llnf\n"
"	ex	de, hl\n"
".Llnf:\n"
"	pop	ix\n"
"	ret\n"
/* dirty(): the OS graph has to redraw (graphDraw, iy + graphFlags bit 0). */
"	.section	.text._symce_dirty,\"ax\",@progbits\n"
"	.globl	_symce_dirty\n"
"_symce_dirty:\n"
"	ld	iy, 0x0D00080\n"
"	set	0, (iy + 3)\n"
"	ret\n"
/* throw(code, to_graph): leave the grapher the way an OS error does. main.asm's timer is put
   back first (its saved control word is at GW + 0xC04), then the OS error screen. */
"	.section	.text._symce_throw,\"ax\",@progbits\n"
"	.globl	_symce_throw\n"
"_symce_throw:\n"
"	ld	hl, (0x0D0FB04)\n"
"	ld	(0x0F20030), hl\n"
"	ld	iy, 0x0D00080\n"
"	ld	hl, 3\n"
"	add	hl, sp\n"
"	ld	a, (hl)\n"
"	push	af\n"
"	call	0x0021A3C\n"          /* DrawStatusBar: the big graph covered it, TI's error screen keeps it */
"	pop	af\n"
/* ponytail: Goto goes home (TI re-enters its own graph with TRACE at the centre; ours is not a context the OS can re-enter) */
"	jp	0x0020790\n"           /* JError */
);

/* calc(kind, fn1, l, r, g, fn2, out): the OS's own solvers, as 2nd CALC calls them (docs 4):
   kind 1 zero (GRITSolver), 2 minimum, 3 maximum (DFMin), 4 intersect (ITSolver on Yfn1 - Yfn2),
   5 dy/dx (nDeriv). g is the guess, or the point for dy/dx; out gets the X found (or the
   derivative). Returns 0 or the OS error code; Ans, Y and the screen are the caller's. */
__asm__(
"	.section	.text._symce_calc,\"ax\",@progbits\n"
"	.globl	_symce_calc\n"
"_symce_calc:\n"
"	push	ix\n"
"	ld	ix, 0\n"
"	add	ix, sp\n"                /* ix+6 kind, +9 fn1, +12 l, +15 r, +18 g, +21 fn2, +24 out */
"	ld	iy, 0x0D00080\n"
"	ld	hl, (0x0D0258D)\n"       /* the FPS, put back at the end */
"	push	hl\n"
"	push	ix\n"
"	ld	hl, (0x0D02596)\n"
"	push	hl\n"
"	ld	hl, .Lcerr\n"
"	call	0x0020798\n"          /* PushErrorHandler */
"	ld	a, (ix + 6)\n"
"	cp	a, 6\n"
"	jr	nc, .Lnsx\n"            /* Par, Pol: T or theta is stored already, X and Y stay */
"	ld	hl, (ix + 18)\n"
"	call	0x0020320\n"          /* Mov9ToOP1 */
"	call	0x0020F48\n"          /* StoX = the guess (the point) */
".Lnsx:\n"
"	ld	a, (ix + 9)\n"
"	inc	a\n"
"	ld	(0x0D01474), a\n"      /* the OS's traced function (1-based) */
"	ld	a, (ix + 6)\n"
"	cp	a, 6\n"
"	jp	nc, .Lcpar\n"
"	cp	a, 5\n"
"	jp	z, .Lcdy\n"
"	cp	a, 4\n"
"	jp	z, .Lcis\n"
"	ld	hl, (ix + 12)\n"
"	call	0x0082A15\n"          /* PushReal L, then R */
"	ld	hl, (ix + 15)\n"
"	call	0x0082A15\n"
"	call	0x0091FA6\n"          /* GEQNameA: OP1 = Yn */
"	ld	a, (ix + 6)\n"
"	cp	a, 1\n"
"	jr	nz, .Lcmm\n"
"	call	0x00A77AC\n"          /* GRITSolver */
"	jr	.Lcx\n"
".Lcmm:\n"
"	push	af\n"
"	call	0x007FA0E\n"          /* OP1ToOP5 */
"	call	0x00829A6\n"          /* PopRealO3 = R */
"	call	0x00829B2\n"          /* PopRealO1 = L */
"	call	0x007F9AA\n"          /* OP1ToOP2 */
"	call	0x0080077\n"          /* XName */
"	call	0x007F952\n"          /* OP1ToOP4 */
"	call	0x007FAF6\n"          /* OP1Set1 */
"	ld	a, 0x7B\n"
"	ld	(0x0D005F9), a\n"      /* tolerance 1e-5 */
"	call	0x007FDA2\n"          /* OP1ExOP5 */
"	pop	af\n"
"	cp	a, 3\n"                  /* carry: minimum */
"	ld	a, 0x6D\n"
"	jr	c, .Lcmn\n"
"	ld	a, 0x7B\n"
"	scf\n"                         /* carry: maximum, as 6D453 sets it */
"	jr	.Lcgo\n"
".Lcmn:\n"
"	or	a, a\n"
".Lcgo:\n"
"	ld	b, 0\n"
"	call	0x005D688\n"          /* DFMin */
".Lcx:\n"
"	call	0x009A75C\n"          /* StoX */
"	call	0x009AE22\n"          /* RclX */
"	jp	.Lcok\n"
".Lcis:\n"
"	ld	hl, 7\n"
"	call	0x002055C\n"          /* CreatetEqu: Yfn1 - Yfn2 */
"	inc	de\n"
"	inc	de\n"
"	xor	a, a\n"
"	ld	(de), a\n"
"	inc	de\n"
"	ld	(de), a\n"
"	inc	de\n"
"	ld	a, 0x5E\n"
"	ld	(de), a\n"
"	inc	de\n"
"	ld	a, (ix + 9)\n"
"	add	a, 0x10\n"
"	ld	(de), a\n"
"	inc	de\n"
"	ld	a, 0x71\n"
"	ld	(de), a\n"
"	inc	de\n"
"	ld	a, 0x5E\n"
"	ld	(de), a\n"
"	inc	de\n"
"	ld	a, (ix + 21)\n"
"	add	a, 0x10\n"
"	ld	(de), a\n"
"	call	0x00202C8\n"          /* OP4ToOP1 */
"	call	0x0082A11\n"          /* PushRealO1 x2 */
"	call	0x0082A11\n"
"	call	0x0080077\n"          /* XName */
"	call	0x0082A11\n"
"	call	0x00A7792\n"          /* ITSolver */
"	jr	.Lcx\n"
".Lcpar:\n"                    /* 6 Par dy/dx, 7 dy/dt, 8 dx/dt, 9 Pol dr/dtheta, 10 Pol dy/dx: the CALC item's own calls (0x6D2E9..) */
"	call	0x0091FA6\n"          /* GEQNameA: X1T, or r1 */
"	ld	a, (ix + 6)\n"
"	cp	a, 8\n"
"	jr	z, .Lcdt\n"
"	jr	nc, .Lcpo\n"
"	call	0x0080177\n"          /* OP1 = Y1T */
"	cp	a, 7\n"
"	jr	z, .Lcdt\n"
"	push	hl\n"
"	call	0x0082A11\n"          /* PushRealO1: the Y1T name */
"	pop	hl\n"
"	res	0, (hl)\n"             /* OP1 = X1T again */
"	call	0x005E2D6\n"          /* Par dy/dx */
"	jr	.Lcpk\n"
".Lcdt:\n"
"	call	0x005E23B\n"          /* d/dT */
"	jr	.Lcpk\n"
".Lcpo:\n"
"	cp	a, 9\n"
"	jr	nz, .Lcpd\n"
"	call	0x005E231\n"          /* dr/dtheta */
"	jr	.Lcpk\n"
".Lcpd:\n"
"	call	0x005E25A\n"          /* Pol dy/dx = (dr sin + r cos) / (dr cos - r sin), then the divide */
"	call	0x007CB69\n"
".Lcpk:\n"
"	call	0x007F86D\n"          /* CkOP1Real */
"	jr	.Lcok\n"
".Lcdy:\n"
"	call	0x0091FA6\n"          /* GEQNameA */
"	call	0x0082A11\n"
"	call	0x0080077\n"
"	call	0x0082A11\n"
"	call	0x009AE26\n"          /* RclVarSym: X's value */
"	ld	a, 0x25\n"
"	call	0x0067EBE\n"          /* ThreeExec: nDeriv */
"	call	0x007F86D\n"          /* CkOP1Real */
".Lcok:\n"
"	call	0x002079C\n"          /* PopErrorHandler */
"	xor	a, a\n"
".Lcerr:\n"
"	pop	de\n"
"	push	af\n"
"	call	0x0020E80\n"          /* FixTempCnt */
"	pop	af\n"
"	pop	ix\n"
"	pop	hl\n"
"	ld	(0x0D0258D), hl\n"
"	ld	iy, 0x0D00080\n"
"	res	0, (iy + 7)\n"
"	or	a, a\n"
"	jr	nz, .Lcout\n"
"	ld	hl, 0x0D005F8\n"
"	ld	de, (ix + 24)\n"
"	ld	bc, 9\n"
"	ldir\n"
"	xor	a, a\n"
".Lcout:\n"
"	pop	ix\n"
"	ld	hl, 0\n"
"	ld	l, a\n"
"	ret\n"

/* stoans(real): Ans = it. */
"	.section	.text._symce_stoans,\"ax\",@progbits\n"
"	.globl	_symce_stoans\n"
"_symce_stoans:\n"
"	pop	de\n"
"	pop	hl\n"
"	push	hl\n"
"	push	de\n"
"	push	ix\n"
"	ld	iy, 0x0D00080\n"
"	call	0x0020320\n"
"	call	0x009A6F5\n"          /* StoAns */
"	pop	ix\n"
"	ret\n"
);

/* seq(op, n): the OS's sequence solver. op 0: the flags (0x285AA) and SeqSet, n = nMin and its terms; op 1: SeqSolve
   up to the real n. 0, or the OS error code. The terms are left at 0xD01D0F... */
__asm__(
"	.section	.text._symce_seq,\"ax\",@progbits\n"
"	.globl	_symce_seq\n"
"_symce_seq:\n"
"	push	ix\n"
"	ld	ix, 0\n"
"	add	ix, sp\n"                /* ix+6 op, +9 n */
"	ld	iy, 0x0D00080\n"
"	ld	hl, (0x0D0258D)\n"       /* the FPS, put back at the end */
"	push	hl\n"
"	push	ix\n"
"	ld	hl, (0x0D02596)\n"
"	push	hl\n"
"	ld	hl, .Lsqe\n"
"	call	0x0020798\n"          /* PushErrorHandler */
"	ld	a, (ix + 6)\n"
"	or	a, a\n"
"	jr	nz, .Lsq1\n"
"	xor	a, a\n"
"	ld	(0x0D02FE6), a\n"      /* not Web */
"	call	0x00285AA\n"          /* the selected sequences */
"	call	0x007D633\n"          /* SeqSet */
"	jr	.Lsq2\n"
".Lsq1:\n"
"	ld	hl, (ix + 9)\n"
"	call	0x0020320\n"          /* Mov9ToOP1 */
"	call	0x007D683\n"          /* SeqSolve */
".Lsq2:\n"
"	call	0x002079C\n"          /* PopErrorHandler */
"	xor	a, a\n"
".Lsqe:\n"
"	pop	de\n"
"	push	af\n"
"	call	0x0020E80\n"          /* FixTempCnt */
"	pop	af\n"
"	pop	ix\n"
"	pop	hl\n"
"	ld	(0x0D0258D), hl\n"
"	ld	iy, 0x0D00080\n"
"	res	0, (iy + 7)\n"
"	pop	ix\n"
"	ld	hl, 0\n"
"	ld	l, a\n"
"	ret\n"
);

/* libc's sinf and friends set errno. It is a global there, and this image is
   read-only, so it is a fixed address in the free scratch instead (docs
   section 2: 0xD0EEF0 is past ansBuf's last byte, and the graph runs from
   the app, never at A=2). */
__asm__(
"	.globl	_errno\n"
"	.equ	_errno, 0x0D0EEF0\n"
);

/* And libc's rounding-mode byte: read-only here, so it stays 0, to nearest. */
__asm__(
"	.section	.rodata.___fe_cur_env,\"a\",@progbits\n"
"	.globl	___fe_cur_env\n"
"___fe_cur_env:\n"
"	.byte	0\n"
);
/* VPutMap also records the glyphs it draws in the OS's two home pixel shadows (0xD031F6, 0xD07396: 40 bytes a
   row, row = y - 30, a bit set where the pixel is not white), which a later redraw of home replays: the
   grapher's text (the X= box, the bar under a CALC result) would come back over the home screen after it.
   So the rows a text can touch are put back as they were, parked in 0xD13800 (free; 1360 bytes). Text above
   row 30 is not recorded. */
void symce_text(int x, int y, unsigned fg, unsigned bg, const char *s, unsigned big)
{
    uint8_t *a = (uint8_t *)0x0D031F6, *b = (uint8_t *)0x0D07396, *v = (uint8_t *)0x0D13800;
    int r = y - 30, n = 17;
    if (r < 0 || r >= 210) {
        symce_text_(x, y, fg, bg, s, big);
        return;
    }
    n = r + n > 210 ? 210 - r : n;
    memcpy(v, a + r * 40, n * 40);
    memcpy(v + n * 40, b + r * 40, n * 40);
    symce_text_(x, y, fg, bg, s, big);
    memcpy(a + r * 40, v, n * 40);
    memcpy(b + r * 40, v + n * 40, n * 40);
}
#else
/* The host build (tools/graph_cli.c) only compiles and runs equations. */
uint8_t symce_osy(uint8_t n, const uint8_t *xr, uint8_t *out, uint8_t v) { (void)n; (void)xr; (void)out; (void)v; return 1; }
void symce_stov(uint8_t v, const uint8_t *r) { (void)v; (void)r; }
void symce_stoy(const uint8_t *r) { (void)r; }
void symce_stox(const uint8_t *r) { (void)r; }
void symce_fp(unsigned fn, const uint8_t *a, const uint8_t *b, uint8_t *out) { (void)fn; (void)a; (void)b; (void)out; }
void symce_fmt(const uint8_t *r, char *out) { (void)r; *out = 0; }
void symce_fmtw(const uint8_t *r, char *out, unsigned width) { (void)r; (void)width; *out = 0; }
void symce_fmtn(const uint8_t *r, char *out, unsigned width) { (void)r; (void)width; *out = 0; }
void symce_fill(uint16_t *p, unsigned n, unsigned c) { while (n--) *p++ = c; }
void symce_text(int x, int y, unsigned fg, unsigned bg, const char *s, unsigned big) { (void)x; (void)y; (void)fg; (void)bg; (void)s; (void)big; }
unsigned symce_eval(const uint8_t *tok, unsigned n, uint8_t *out) { (void)tok; (void)n; (void)out; return 1; }
void symce_tok_(const uint8_t *t, char *out) { (void)t; *out = 0; }
const uint8_t *symce_sglyph(unsigned c) { (void)c; return 0; }
void symce_dirty(void) {}
unsigned symce_calc(unsigned kind, unsigned fn1, const uint8_t *l, const uint8_t *r, const uint8_t *g, unsigned fn2, uint8_t *out) { (void)kind; (void)fn1; (void)l; (void)r; (void)g; (void)fn2; (void)out; return 1; }
void symce_stoans(const uint8_t *r) { (void)r; }
void symce_rom(unsigned a) { (void)a; }
void symce_roma(unsigned a, unsigned n) { (void)a; (void)n; }
void symce_zxy(unsigned in) { (void)in; }
const uint8_t *symce_lst(const uint8_t *nm) { (void)nm; return 0; }
void symce_throw(unsigned code, unsigned to_graph) { (void)code; (void)to_graph; for (;;) ; }
unsigned symce_seq(unsigned op, const uint8_t *n) { (void)op; (void)n; return 1; }
#endif
