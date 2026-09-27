/* Reference implementation of the small in-hook engine.
 *
 * This is the algorithm the PIC eZ80 hook body will run. It is written in C
 * only so it can be tested exhaustively on the host; the compiler cannot be
 * used to produce the shipped version, because ez80-clang emits absolute calls
 * to its runtime helpers (__frameset, __lmulu, __ladd) and absolute jp to its
 * own basic blocks, neither of which survives being relocated into an appvar.
 * The asm port must match this behaviour case for case.
 *
 * SCOPE, deliberately small: a sum of terms, where each term is a product of
 * factors, and each factor is an integer or a variable with an optional integer
 * power. That covers what a student actually types on the home screen:
 *
 *     2X+2X -> 4X      X*X -> X^2      3X+3X -> 6X     2X-5X -> -3X
 *     X+X+X -> 3X      X-X -> 0        X*X*X -> X^3    2X+3X -> 5X
 *
 * Anything else - parentheses, division, functions, a second variable, a power
 * above MAXPOW - returns MINI_PASS and the hook falls through to stock OS
 * behaviour. Rejecting is always safe; guessing is not.
 *
 * No allocation. Coefficients are int32 and overflow is checked, because on the
 * calculator this runs on the OS's own stack with no room to be clever.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define MAXPOW   8          /* powers 0..7; X^8 and up pass through */
#define COEF_LIM 999999L    /* keep products well inside int32 */
#define MINI_MAXLEN 64      /* the hook flattens the entry into a stack buffer */

enum { MINI_OK = 0, MINI_PASS = 1 };

/* TI tokens we accept. Everything else means pass. */
#define T_MUL   0x82
#define T_ADD   0x70
#define T_SUB   0x71
#define T_POW   0xF0
#define T_NEG   0xB0        /* NEGATE, which is a different token from SUBTRACT */
#define T_SQR   0x0D        /* the squared token */
#define T_CUBE  0x0F

#define SCR_WIDTH 26        /* home screen columns; a wider answer is refused */

typedef struct {
    int32_t coef[MAXPOW];   /* coef[n] is the coefficient of var^n */
    int     var;            /* which letter, 0 if none seen yet */
} poly_t;

static int is_digit(uint8_t t) { return t >= 0x30 && t <= 0x39; }
static int is_var(uint8_t t)   { return t >= 0x41 && t <= 0x5B; }

/* Reads one term: a run of factors joined by explicit or implicit
   multiplication. Returns MINI_PASS on anything out of scope. */
static int read_term(const uint8_t *t, unsigned len, unsigned *ip,
                     poly_t *p, int32_t *out_coef, int *out_pow) {
    int32_t coef = 1;
    int pow = 0;
    int any = 0;
    unsigned i = *ip;

    while (i < len) {
        uint8_t c = t[i];

        if (is_digit(c)) {
            int32_t v = 0;
            while (i < len && is_digit(t[i])) {
                v = v * 10 + (t[i] - 0x30);
                if (v > COEF_LIM) return MINI_PASS;
                i++;
            }
            {   /* 999999 * 999999 overflows int32, which is undefined, so
                       the product is formed wide and range-checked before it is
                       narrowed. The hook does the same thing by refusing any
                       multiply that carries out of 24 bits. */
                long long prod = (long long)coef * v;
                if (prod > COEF_LIM) return MINI_PASS;
                coef = (int32_t)prod;
            }
            any = 1;
            continue;
        }

        if (is_var(c)) {
            if (p->var == 0) p->var = c;
            else if (p->var != c) return MINI_PASS;   /* second variable */
            i++;
            /* optional power: ^n, squared, or cubed */
            if (i < len && t[i] == T_POW) {
                int32_t e = 0;
                i++;
                if (i >= len || !is_digit(t[i])) return MINI_PASS;
                while (i < len && is_digit(t[i])) {
                    e = e * 10 + (t[i] - 0x30);
                    /* checked inside the loop: e only grows, and an unbounded
                       accumulator overflows on a long run of digits */
                    if (e >= MAXPOW) return MINI_PASS;
                    i++;
                }
                pow += (int)e;
            } else if (i < len && t[i] == T_SQR)  { pow += 2; i++; }
            else if (i < len && t[i] == T_CUBE)   { pow += 3; i++; }
            else                                   { pow += 1; }
            if (pow >= MAXPOW) return MINI_PASS;
            any = 1;
            continue;
        }

        if (c == T_MUL) {                 /* explicit * between factors */
            if (!any) return MINI_PASS;   /* leading *, e.g. "*A" - symcore
                                             refuses this and so must we */
            i++;
            /* A * has to be followed by another factor. "5*+Y", "1*-X" and
               "559**A" are all rejected by symcore, so they must be rejected
               here too or we would answer where the oracle will not. */
            if (i >= len) return MINI_PASS;
            if (!is_digit(t[i]) && !is_var(t[i])) return MINI_PASS;
            continue;
        }

        break;                            /* + or - ends the term */
    }

    if (!any) return MINI_PASS;             /* empty term, e.g. "+ +" */
    *ip = i;
    *out_coef = coef;
    *out_pow = pow;
    return MINI_OK;
}

int minipoly(const uint8_t *t, unsigned len, poly_t *p) {
    unsigned i = 0;
    int sign = 1;

    memset(p, 0, sizeof *p);
    if (len == 0 || len > MINI_MAXLEN) return MINI_PASS;

    /* A typed leading minus is NEGATE (0xB0), not SUBTRACT (0x71). They are
       different tokens and symcore rejects a leading SUBTRACT, so we must too
       or we would answer where the oracle will not. */
    if (t[0] == T_NEG) { sign = -1; i = 1; }

    for (;;) {
        int32_t coef;
        int pow;

        if (read_term(t, len, &i, p, &coef, &pow) != MINI_OK) return MINI_PASS;

        p->coef[pow] += sign * coef;
        if (p->coef[pow] > COEF_LIM || p->coef[pow] < -COEF_LIM) return MINI_PASS;

        if (i >= len) break;
        if (t[i] == T_ADD)      sign =  1;
        else if (t[i] == T_SUB) sign = -1;
        else return MINI_PASS;              /* anything else is out of scope */
        i++;
        if (i >= len) return MINI_PASS;     /* trailing operator */
        if (t[i] == T_NEG) {                /* e.g. 2X+-3X */
            sign = -sign;
            i++;
            if (i >= len) return MINI_PASS;
        }
    }

    if (p->var == 0) return MINI_PASS;      /* purely numeric - leave it to the OS */
    return MINI_OK;
}

/* Emits TI tokens in symcore's order, because the hook has to produce the same
   bytes the full engine would - see tools/minipoly_test.c.
   The rule, found by differential fuzzing rather than read off the source:
   descending by power, EXCEPT when the highest-power coefficient is negative
   and some other coefficient is positive - then ascending, which is what puts
   the positive term first and avoids an avoidable leading NEGATE.

       3-9Y        not  -9Y+3     (positive term exists, so lead with it)
       -2421A-9    not  -9-2421A  (all negative, nothing to lead with)
       X^2+2X+1                   (top already positive, plain descending) */
unsigned minipoly_emit(const poly_t *p, uint8_t *out, unsigned max) {
    unsigned n = 0;
    int k, step, stop, first = 1, top = -1;

    for (k = MAXPOW - 1; k >= 0; k--)
        if (p->coef[k] != 0) { top = k; break; }

    if (top < 0) {                        /* everything cancelled */
        if (n >= max) return 0;
        out[n++] = 0x30;                  /* "0" */
        return n;
    }

    {
        int any_positive = 0;
        for (k = 0; k <= top; k++) if (p->coef[k] > 0) { any_positive = 1; break; }

        if (p->coef[top] < 0 && any_positive) { k = 0;   step =  1; stop = top + 1; }
        else                                  { k = top; step = -1; stop = -1; }
    }

    for (; k != stop; k += step) {
        int32_t c = p->coef[k];
        int32_t a;
        uint8_t digits[8];
        int nd = 0;

        if (c == 0) continue;

        if (!first) {
            if (n >= max) return 0;
            out[n++] = (c < 0) ? T_SUB : T_ADD;
            a = (c < 0) ? -c : c;
        } else {
            if (c < 0) { if (n >= max) return 0; out[n++] = T_NEG; a = -c; }
            else a = c;
        }
        first = 0;

        /* the coefficient, unless it is a bare 1 in front of a variable */
        if (!(a == 1 && k > 0)) {
            while (a > 0) { digits[nd++] = (uint8_t)(0x30 + (a % 10)); a /= 10; }
            if (nd == 0) digits[nd++] = 0x30;
            while (nd > 0) { if (n >= max) return 0; out[n++] = digits[--nd]; }
        }

        if (k >= 1) {
            if (n >= max) return 0;
            out[n++] = (uint8_t)p->var;
            if (k >= 2) {
                if (n + 1 >= max) return 0;
                out[n++] = T_POW;
                out[n++] = (uint8_t)(0x30 + k);   /* k < MAXPOW = 8, one digit */
            }
        }
    }
    return n;
}

/* Turns the canonical answer into what should actually go on the screen.
 * Kept separate from minipoly_emit because that one has to stay byte-identical
 * to symcore -- this is presentation, not algebra, and symcore has no opinion.
 *
 * The answer is DISPLAYED, never parsed. The hook leaves the entry exactly as
 * typed, and at the OS's display-result call hands this text to the routine
 * the OS uses for its own real-number results, so it is right-aligned like a
 * number and the history holds what the user actually typed. That routine
 * files and draws a TOKEN string (it converts its own LFont digits to tokens
 * first, and leaves tokens alone), so this stays tokens. The one change:
 *
 *   ^2 and ^3 become the SQUARE and CUBE tokens, which draw as raised digits.
 *   There is no such token for 4 and up, so those keep the caret.
 *
 * Wider than one screen line is refused: the OS's own one-line result path
 * stops at 26 characters too, and what it does past that is unmeasured.
 */
unsigned minipoly_display(const uint8_t *in, unsigned n, uint8_t *out, unsigned max)
{
    unsigned i, m = 0;

    for (i = 0; i < n; i++) {
        uint8_t c = in[i];
        if (m >= max) return 0;
        if (c == T_POW && i + 1 < n && in[i + 1] == 0x32)      { c = T_SQR;  i++; }
        else if (c == T_POW && i + 1 < n && in[i + 1] == 0x33) { c = T_CUBE; i++; }
        out[m++] = c;
    }
    return m > SCR_WIDTH ? 0 : m;
}
