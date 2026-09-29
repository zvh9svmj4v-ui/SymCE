/* SymCE graph: a full-screen function grapher, drawn straight into VRAM.
 *
 * The OS graph is 265x165 px and takes 1.5-7.5 s (docs 11b); this draws
 * Y1..Y0 at 320x220 in float32. The OS side (main.asm, `4: Graph` on the
 * settings screen) fills a `struct gw` in RAM from the VAT, and calls
 * symce_graph(w, key) in a loop: key 0 the first time, then each GetKey code.
 * It returns 1 to leave, and leaves the line for the bottom bar in w->info.
 *
 * Per equation, once per draw: the tokens compile to a small RPN program
 * (compile), which run() evaluates at each of the 320 columns. Grammar and
 * precedence are engine.c's (measured on OS 5.8.4): implied multiplication and
 * the n/d bar (EF 2E) bind like * and /, left to right, under ^ (left to
 * right), which is under negation. Supported tokens:
 *   digits . EE, X, A-Z and theta (values from the caller), pi, e, + - * /,
 *   ^, negation (B0), ( ), the postfix squared, cubed and ^-1, and
 *   sqrt( abs( ln( log( e^( 10^( sin( cos( tan( asin( acos( atan(.
 * Anything else makes that equation "unsupported": it is skipped and named.
 * Degree mode scales the trig. A NaN or infinity breaks the line.
 *
 * Like engine.c it runs from flash: no writable statics, all state in `w`.
 *
 * ponytail: no grid, ticks, TRACE or CALC; one sample per column, so a
 * function that wiggles faster than a pixel aliases; libc sinf/powf accuracy
 * is taken as it comes.
 */
#include <stdint.h>
#include <math.h>
#include <string.h>

#define NEQ    10
#define MAXTOK 96                /* tokens copied per equation; longer is refused */
#define MAXOP  112               /* ops in one program */
#define STK    12                /* evaluation stack */
#define MAXDEP 6                 /* nested parentheses */
#define PW     320               /* plot columns */
#define PH     220               /* plot rows; the bar below holds the info line */
#define SW     240               /* screen rows */

#ifndef VRAM
#define VRAM ((uint16_t *)0xD40000)               /* 320x240, RGB565 */
#endif

/* GetKey codes */
enum { K_RIGHT = 1, K_LEFT = 2, K_UP = 3, K_DOWN = 4, K_CLEAR = 9, K_QUIT = 0x40,
       K_ADD = 0x80, K_SUB = 0x81, K_0 = 0x8E };

enum { O_K, O_X, O_ADD, O_SUB, O_MUL, O_DIV, O_POW, O_NEG, O_SQ, O_CU, O_INV,
       O_SQRT, O_LN, O_EXP, O_LOG, O_E10, O_ABS,
       O_SIN, O_ASIN, O_COS, O_ACOS, O_TAN, O_ATAN };   /* the last six are tokens C2..C7 in order */

enum { E_OK, E_TOK, E_LONG, E_SYN, E_DEEP };

/* The first fields, up to `tok`, are what main.asm fills; their offsets are
   fixed there. All bytes, so the layout is the same on the host. */
struct gw {
    uint8_t win[36];             /*   0 in: Xmin, Xmax, Ymin, Ymax as 9-byte TI reals */
    uint8_t deg;                 /*  36 in: 1 = Degree mode */
    uint8_t col[NEQ];            /*  37 in: line colour index, as the OS keeps y1LineColor */
    uint8_t len[NEQ];            /*  47 in: token count; 0 = not plotted (empty, or not selected) */
    uint8_t var[27][9];          /*  57 in: A..Z, theta as TI reals; zeros where undefined */
    uint8_t tok[NEQ][MAXTOK];    /* 300 in: the equations' tokens */
    char info[72];               /* 1260 out: the bar's text, NUL-terminated */
    /* state, not the assembler's */
    float x0, x1, y0, y1;        /* the window in view */
    float o[4];                  /* the OS window, for key 0 */
    uint8_t op[MAXOP];           /* the program being compiled or run */
    float k[MAXOP];
    uint8_t n, sp, dep, err, errtok;
    const uint8_t *p, *e;        /* parse position, end */
};

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

static void emit(struct gw *w, uint8_t op, float k)
{
    if (w->err)
        return;
    if (w->n >= MAXOP) {
        w->err = E_LONG;
        return;
    }
    if (op == O_K || op == O_X) {
        if (w->sp >= STK) {
            w->err = E_DEEP;
            return;
        }
        w->sp++;
    } else if (op >= O_ADD && op <= O_POW)
        w->sp--;
    w->op[w->n] = op;
    w->k[w->n++] = k;
}

static int peek(const struct gw *w)
{
    return w->p < w->e ? *w->p : -1;
}

static void expr(struct gw *w);

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
    expr(w);
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
    else if (t == 0x58)
        emit(w, O_X, 0);
    else if (t >= 0x41 && t <= 0x5B)
        emit(w, O_K, real(w->var[t - 0x41]));
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

static void power(struct gw *w)
{
    postfix(w);
    while (!w->err && peek(w) == 0xF0) {
        uint8_t neg = 0;
        w->p++;
        while (peek(w) == 0xB0) {
            neg ^= 1;
            w->p++;
        }
        postfix(w);
        if (neg)
            emit(w, O_NEG, 0);
        emit(w, O_POW, 0);
    }
}

static void unary(struct gw *w)
{
    uint8_t neg = 0;
    while (peek(w) == 0xB0) {
        neg ^= 1;
        w->p++;
    }
    power(w);
    if (neg)
        emit(w, O_NEG, 0);
}

/* Does the next token begin a primary, so that it multiplies what came before? */
static int starts(const struct gw *w)
{
    int t = peek(w);
    return (t >= '0' && t <= '9') || t == 0x3A || (t >= 0x41 && t <= 0x5B) || t == 0x10 ||
           t == 0xAC || t == 0xBB || t == 0xB2 || (t >= 0xBC && t <= 0xC7);
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

/* Equation i to w->op / w->k; returns 0 or an E_ code (w->errtok: the token). */
static uint8_t compile(struct gw *w, uint8_t i)
{
    w->n = w->sp = w->dep = w->err = 0;
    w->errtok = 0;
    if (w->len[i] > MAXTOK)
        return E_LONG;
    w->p = w->tok[i];
    w->e = w->p + w->len[i];
    expr(w);
    if (!w->err && w->p < w->e) {                /* something expr() does not take */
        w->err = *w->p == 0x11 ? E_SYN : E_TOK;
        w->errtok = *w->p;
    }
    return w->err;
}

static float run(const struct gw *w, float x)
{
    float s[STK + 1];
    uint8_t sp = 0;
    float a, b, dg = w->deg ? 0.01745329252f : 1.0f;
    for (uint8_t i = 0; i < w->n; i++) {
        uint8_t o = w->op[i];
        if (o == O_K || o == O_X) {
            s[sp++] = o == O_K ? w->k[i] : x;
            continue;
        }
        a = s[sp - 1];
        if (o <= O_POW) {                        /* binary */
            b = a;
            a = s[--sp - 1];
            a = o == O_ADD ? a + b : o == O_SUB ? a - b : o == O_MUL ? a * b :
                o == O_DIV ? a / b : powf(a, b);
        } else switch (o) {
        case O_NEG:  a = -a; break;
        case O_SQ:   a = a * a; break;
        case O_CU:   a = a * a * a; break;
        case O_INV:  a = 1.0f / a; break;
        case O_SQRT: a = sqrtf(a); break;
        case O_LN:   a = logf(a); break;
        case O_EXP:  a = expf(a); break;
        case O_LOG:  a = log10f(a); break;
        case O_E10:  a = powf(10.0f, a); break;
        case O_ABS:  a = fabsf(a); break;
        case O_SIN:  a = sinf(a * dg); break;
        case O_COS:  a = cosf(a * dg); break;
        case O_TAN:  a = tanf(a * dg); break;
        case O_ASIN: a = asinf(a) / dg; break;
        case O_ACOS: a = acosf(a) / dg; break;
        default:     a = atanf(a) / dg; break;
        }
        if (!(fabsf(a) < 1e30f))                 /* 1/0, ln(0) and the like: not 0 later on */
            return __builtin_nanf("");
        s[sp - 1] = a;
    }
    return s[0];
}

/* What the OS keeps in y1LineColor (measured: 1..7 by default, in the pixels
   the OS draws them in). ponytail: 8 and up (Y= can pick them) draw black. */
static uint16_t colour(uint8_t i)
{
    static const uint16_t c[] = { 0, 0x001F, 0xF800, 0x0000, 0xF81F, 0x04E0, 0xFC64, 0xB100 };
    return i < 8 ? c[i] : 0;
}

static void plot(const struct gw *w, uint16_t c)
{
    uint16_t *v = VRAM;
    float sy = (float)(PH - 1) / (w->y1 - w->y0), dx = (w->x1 - w->x0) / (float)(PW - 1);
    int rp = 0, have = 0;
    for (int col = 0; col < PW; col++) {
        float y = run(w, w->x0 + (float)col * dx), r;
        int ri, lo, hi;
        if (!(fabsf(y) < 1e30f)) {               /* NaN, infinity: the line breaks */
            have = 0;
            continue;
        }
        r = (w->y1 - y) * sy;
        r = r < -30000.0f ? -30000.0f : r > 30000.0f ? 30000.0f : r;
        ri = (int)floorf(r + 0.5f);
        lo = hi = ri;
        if (have) {
            lo = rp < ri ? rp : ri;
            hi = rp < ri ? ri : rp;
        }
        rp = ri;
        have = 1;
        if (hi < 0 || lo >= PH)
            continue;
        lo = lo < 0 ? 0 : lo;
        hi = hi >= PH ? PH - 1 : hi;
        for (uint16_t *q = v + lo * PW + col; lo <= hi; lo++, q += PW)
            *q = c;
    }
}

static char *str(char *o, const char *s)
{
    while (*s)
        *o++ = *s++;
    return o;
}

/* A float to at most three decimals, trailing zeros off; "big" past a million. */
static char *fmt(char *o, float v)
{
    uint32_t i, f;
    char d[10];
    uint8_t n = 0;
    if (v < 0) {
        *o++ = '-';
        v = -v;
    }
    if (!(v < 1e6f))
        return str(o, "big");
    i = (uint32_t)v;
    f = (uint32_t)((v - (float)i) * 1000.0f + 0.5f);
    if (f >= 1000) {
        i++;
        f -= 1000;
    }
    do
        d[n++] = (char)('0' + i % 10);
    while (i /= 10);
    while (n)
        *o++ = d[--n];
    if (f) {
        *o++ = '.';
        *o++ = (char)('0' + f / 100);
        if (f % 100) {
            *o++ = (char)('0' + f / 10 % 10);
            if (f % 10)
                *o++ = (char)('0' + f % 10);
        }
    }
    return o;
}

static void render(struct gw *w)
{
    static const char *const msg[] = { 0, ": unsupported token ", ": too long", ": syntax", ": too deep" };
    static const char hex[] = "0123456789ABCDEF";
    uint16_t *v = VRAM;
    char *o = w->info;
    uint8_t fail = 0, fe = 0, ft = 0, e;
    memset(v, 0xFF, (size_t)PW * SW * 2);        /* white; libc's is an ldir */
    if (w->y0 < 0 && w->y1 > 0) {                /* the axes */
        uint16_t *q = v + (int)(w->y1 * (float)(PH - 1) / (w->y1 - w->y0) + 0.5f) * PW;
        for (int c = 0; c < PW; c++)
            q[c] = 0;
    }
    if (w->x0 < 0 && w->x1 > 0) {
        uint16_t *q = v + (int)(-w->x0 * (float)(PW - 1) / (w->x1 - w->x0) + 0.5f);
        for (int r = 0; r < PH; r++, q += PW)
            *q = 0;
    }
    for (uint8_t i = 0; i < NEQ; i++) {
        if (!w->len[i])
            continue;
        e = compile(w, i);
        if (!e)
            plot(w, colour(w->col[i]));
        else if (!fe) {                          /* the first one that fails is named */
            fail = (uint8_t)((i + 1) % 10);
            fe = e;
            ft = w->errtok;
        }
    }
    o = str(o, "x ");
    o = fmt(o, w->x0);
    o = str(o, "..");
    o = fmt(o, w->x1);
    o = str(o, "  y ");
    o = fmt(o, w->y0);
    o = str(o, "..");
    o = fmt(o, w->y1);
    if (fe) {
        o = str(o, "  Y");
        *o++ = (char)('0' + fail);
        o = str(o, msg[fe]);
        if (fe == E_TOK) {
            *o++ = hex[ft >> 4];
            *o++ = hex[ft & 15];
        }
    }
    *o = 0;
}

/* Key 0 draws the OS window; the arrows pan by 1/8 of the span, + and - halve
   and double it about the centre, 0 goes back to the OS window. Returns 1 to
   leave (CLEAR, 2nd QUIT). Any other key changes nothing. */
uint8_t symce_graph(struct gw *w, uint8_t key)
{
    float sx = w->x1 - w->x0, sy = w->y1 - w->y0;
    float cx = (w->x0 + w->x1) / 2, cy = (w->y0 + w->y1) / 2;
    if (key == K_CLEAR || key == K_QUIT)
        return 1;
    if (!key) {
        for (uint8_t i = 0; i < 4; i++)
            w->o[i] = real(w->win + i * 9);
        key = K_0;
    }
    switch (key) {
    case K_RIGHT: w->x0 += sx / 8; w->x1 += sx / 8; break;
    case K_LEFT:  w->x0 -= sx / 8; w->x1 -= sx / 8; break;
    case K_UP:    w->y0 += sy / 8; w->y1 += sy / 8; break;
    case K_DOWN:  w->y0 -= sy / 8; w->y1 -= sy / 8; break;
    case K_ADD:   w->x0 = cx - sx / 4; w->x1 = cx + sx / 4; w->y0 = cy - sy / 4; w->y1 = cy + sy / 4; break;
    case K_SUB:   w->x0 = cx - sx; w->x1 = cx + sx; w->y0 = cy - sy; w->y1 = cy + sy; break;
    case K_0:     w->x0 = w->o[0]; w->x1 = w->o[1]; w->y0 = w->o[2]; w->y1 = w->o[3]; break;
    default:      return 0;
    }
    if (!(w->x1 > w->x0) || !(w->y1 > w->y0)) {  /* an empty or NaN window, or zoomed to nothing */
        w->x0 = w->y0 = -10.0f;
        w->x1 = w->y1 = 10.0f;
    }
    render(w);
    return 0;
}

/* libc's sinf and friends set errno. It is a global there, and this image is
   read-only, so it is a fixed address in the free scratch instead (docs
   section 2: 0xD0EEF0 is past ansBuf's last byte, and the graph runs from
   the app, never at A=2). */
#ifdef __ez80__
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
#endif
