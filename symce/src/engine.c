/* SymCE engine: a home-screen entry in, the simplified polynomial or fraction out.
 *
 * Runs on the calculator from inside the SymCE flash app: the hook body
 * (hook.c) calls this after ENTER with the entry as the OS parsed it, MathPrint
 * boxes already flattened to plain tokens. Unlike hook.c it is
 * ordinary compiled C, because the makefile links it on its own and turns every
 * absolute address in the result into an app relocation (tools/relocs.py), so
 * it can live anywhere in flash. It runs FROM flash, so no writable statics:
 * all state is on the stack or in `work`, fixed RAM the hook hands in.
 *
 * The same file builds on the host (tools/engine_cli.c) and is fuzzed against
 * sympy, and against tools/minipoly.c -- the single-variable engine this
 * replaces -- which it must match byte for byte wherever that one answers.
 *
 * SCOPE: polynomials in up to NV variables with rational coefficients, and
 * fractions of them, built from + - * / ^ squared cubed, negation,
 * parentheses, implied multiplication, and the n/d fraction bar:
 *
 *     2X+3Y+X -> 3X+3Y     (X+1)^2 -> X²+2X+1     2(X+3) -> 2X+6
 *     6X/3 -> 2X           X/2+X/3 -> 5X/6        (X+Y)(X-Y) -> X²-Y²
 *     1/X+1/X -> 2/X       X^-2 -> 1/X²           (X²+5X+6)/(X²-4) -> (X+3)/(X-2)
 *
 * Ans is the last answer shown, read as if in parentheses, so typing an
 * operator first on the line (the OS puts Ans in front of it) carries on from
 * that answer: 4X, then *3, gives 12X. The hook passes it in: SymCE's own
 * answer as tokens, or, when the OS showed its own result, that result as the
 * OS keeps a real -- 2+2, then *X, gives 4X. A real is exact when it is a
 * decimal the engine could have been typed (4, -0.25); anything else is
 * refused. SymCE's own Ans is never left to the OS, whose Ans was not set:
 * POLYDEGREE(X³), then +1, is 4; a list or an equation as Ans is ERROR:
 * SYMCE LIMIT, ANS.
 *
 * A decimal number is exact, 2.5 is 5/2, and an entry with a decimal point
 * in it is answered in decimals where a coefficient ends and fractions where
 * it does not: .1X+.2X -> 0.3X, X/3+.5 -> X/3+0.5. An Ans the OS keeps as a
 * real does not count: MathPrint may have shown it as 1/4, so 1/4, then *X,
 * is X/4. MODE ANSWERS: DEC answers every entry that way: X/4 -> 0.25X.
 *
 * A fraction is kept as numerator and denominator and answered in lowest
 * terms, which reduce() has to prove, not hope for: a common monomial, a gcd
 * when either side is in one variable (Euclid over the rationals), a side that
 * cannot be factored (X+Y, XY+1), one side dividing the other, or a side
 * homogeneous in two variables ((X²-Y²)/(X²-2XY+Y²) -> (X+Y)/(X-Y),
 * (X²-Y²+X-Y)/(X²-Y²) -> (X+Y+1)/(X+Y)).
 *
 * Square roots of one term: √8 -> 2√(2), √(12X)+√(27X) -> 5√(3X),
 * √(X³) -> X√(X), 1/(1+√2) -> √(2)-1, X/(X+√2) -> (X²-X√(2))/(X²-2). A term
 * is a coefficient times a monomial times √(n·V), V at most one variable,
 * since √X√Y is not √(XY) when both are negative; √(X²) is |X| and is
 * refused. The denominator is kept free of roots (rationalize()), so equal
 * values print alike. An entry with no variable is answered only when its
 * answer keeps a root, and never under MODE ANSWERS: DEC.
 *
 * Commands, typed from the SymCE menu (menu.c) or with ALPHA letters, one
 * expression then an optional variable, X by default, else the first one.
 * SOLVE takes an equation and the variable to solve for, always:
 * SOLVE(A=B,V); anything short of that is ERROR: SYNTAX.
 *
 *     EXPAND((X+1)²) -> X²+2X+1        DERIV(X³,X) -> 3X²
 *     FACTOR(X²-4) -> (X-2)(X+2)       FACTOR(X²+X-1) -> (2X+1-√(5))(2X+1+√(5))/4
 *     FACTOR(12) -> 2²*3               SOLVE(X²=2,X) -> X=-√(2) or X=√(2)
 *     SOLVE(X+2=4,X) -> X=2            SOLVE(X+2Y=3,Y) -> Y=(3-X)/2
 *
 * FACTOR finds rational roots, then quadratic factors from the values at 0 and
 * ±1 (Kronecker), then groups by a variable's content; two variables are
 * factored when homogeneous, through Y=1. A quadratic left over is split over
 * √ when that fits the screen. SOLVE factors and answers the factors of
 * degree 1 and 2 in its variable, and a command it cannot finish is an error
 * the hook throws (SYMCE_ERR), not an OS evaluation: ERROR: TOO WIDE,
 * SYMCE LIMIT, NO SOLUTION, ALWAYS TRUE, SYNTAX, DATA TYPE (not a polynomial
 * in V), ARGUMENT (two polynomials wanted), DIVIDE BY 0.
 *
 * The Algebra menu's, the TI-Nspire's names; a list is { , } (0x08 0x09,
 * the comma as glyph 0x2C, which the hook files as token 0x2B) and i is glyph
 * 0xD7 (token 0x2C), both measured on the ROM:
 *
 *     POLYROOTS(X²-2) -> {-√(2),√(2)}       CPOLYROOTS(X²+1) -> {-i,i}
 *     NROOTS(X²-2) -> {-1.414213562,1.414213562}   (floats; 10 digits down to 4)
 *     CZEROS(X²+2X+5) -> {-1-2i,-1+2i}      CSOLVE(X²+2X+5=0,X) -> X=-1-2i or X=-1+2i
 *     CFACTOR(X²+1) -> (X-i)(X+i)           COMDENOM(1/X+1/Y) -> (X+Y)/(XY)
 *     POLYREMAINDER(X³+2X+1,X²+1) -> X+1    POLYQUOTIENT(X²,2X+1) -> X/2-1/4
 *     POLYGCD(4X+4,6X+6) -> 2X+2            POLYCOEFFS(AX²+BX+C) -> {A,B,C}
 *     POLYDEGREE(XY²,Y) -> 2                LEFT(X²+1=3X) -> X²+1, RIGHT: 3X
 *
 * Exact roots are those FACTOR finds, rational or over √; a factor it cannot
 * split makes POLYROOTS SYMCE LIMIT rather than a short list. NROOTS is
 * Durand-Kerner in 62-bit floats (fp_t), degree 7 at most, and refuses
 * roots it cannot tell apart.
 *
 * Convert Expression and Trigonometry read functions -- sin( cos( tan( ln(
 * log( logBASE( e^( and e -- each value of one a kernel: a variable of its
 * own to the polynomial code (kern()), written back out by putk():
 *
 *     TEXPAND(sin(2X)) -> 2sin(X)cos(X)     TEXPAND(cos(A+B)) -> cos(A)cos(B)-sin(A)sin(B)
 *     TCOLLECT(cos(X)²) -> (cos(2X)+1)/2    TCOLLECT(sin(A)cos(B)) -> (sin(A-B)+sin(A+B))/2
 *     TOSIN(cos(X)²) -> 1-sin(X)²           TOCOS(sin(X)³) -> sin(X)-sin(X)cos(X)²
 *     TOEXP(sin(X)) -> (e^(Xi)-e^(-Xi))/(2i)   TOLN(log(X)) -> ln(X)/ln(10)
 *     TOLOGBASE(ln(X),5) -> logBASE(X,5)/logBASE(e,5)
 *
 * cos( goes out as the letters c o s and '(': the OS's result routine reads
 * its token, 0xC4, as the pi glyph (table 0xB1AB2, docs §7). TOEXP wants
 * radians (MODE_DEG: ERROR: MODE); the identities of the others hold in
 * degrees too.
 *
 * When none of those settles it -- (X²+X-Y²+Y)/(X²+2XY+Y²+X+Y) -- the entry
 * is refused. So is anything else out of scope -- E notation, functions,
 * a zero divisor, fractional powers, a root of more than one term, a power over 7 anywhere on the way, a
 * numerator or denominator over 999999, an answer wider than the screen (see
 * width()), an entry with no variable at all: 0, and the OS evaluates the
 * entry as usual.
 * Rejecting is always safe; guessing is not.
 *
 * Grammar, with TI-84 precedence. Implied multiplication binds exactly like *,
 * left to right, so 1/2X is X/2 -- what the calculator itself computes.
 *   expr    := term (('+'|'-') term)*
 *   term    := unary (('*'|'/'|n/d|implied) unary)*  implied: before 0-9, '.', A-θ, (, √(
 *   unary   := '~'* power
 *   power   := postfix ('^' '~'* postfix)*          left to right
 *   postfix := primary ('²'|'³'|'⁻¹')*
 *   primary := number | variable | Ans | ('(' | '√(') expr [')']   ')' may be left off at the end
 *   number  := digits ['.' digits] | '.' digits           up to 999999 over up to 10^5
 *
 * OUTPUT, canonical: terms by total degree, highest first, ties alphabetical
 * (X² before XY before Y²); variables alphabetical within a term. If the first
 * coefficient is negative and another is positive the order is reversed, 3-X
 * rather than -X+3: minipoly.c found that rule in symcore by fuzzing, and the
 * single-variable answers must not change. A fraction prints as 3X/2, X/2,
 * 1/2. Powers 2 and 3 use the squared and cubed tokens, which draw raised;
 * there are none for 4 and up, so those keep the caret. With a variable below
 * the bar the answer is N/D, both expanded, whole coefficients with no common
 * factor, D's first term positive; a side is in parentheses unless it is one
 * term, and D unless it is one variable too, since 1/2X would be X/2:
 * 2/X, 3/(2X), (X+3)/(X-2), -1/(X-1).
 */
#include <stdint.h>

#define NV        6           /* distinct variables, 4 bits of power each */
#define OVF       0x888888u   /* a power over 7 sets its nibble's top bit */
#define MAXT      24          /* terms in one polynomial */
#define NPOOL     24          /* values in `work`; a scratch polynomial takes one */
#define MAXDEPTH  6           /* nested parentheses; each level holds 2 of NPOOL */
#define MAXPOW    31          /* largest exponent operand; X^8 fails on OVF */
#define LIM       999999L     /* any stored numerator or denominator */
#define BIG       1000000000L /* intermediates: two still add inside int32 */
#define MAXLEN    64          /* longer entries are left to the OS */
#define SCR_WIDTH 26          /* home screen columns; the answer must fit */
#define MAXOUT    64          /* answer tokens; MathPrint fits more than 26 */
#define MODE_DEC  0x01        /* symce_engine's mode: MODE ANSWERS: DEC */
#define MODE_MP   0x20        /* and MathPrint, bit 5 as in the OS's flags */
#define MODE_DEG  0x04        /* and degrees, bit 2 as in trigFlags */
/* ponytail: -Oz inlines a static function into its one caller, and inside a
   big one (symce_engine, command) the frame is past IX's reach and the code
   bloats: NOINLINE saved 4.2 KB for the int64 five, 8.2 KB for command,
   ipfactor, putfl, rsplit, rok. Stack, measured on the calculator by
   engine_device.py: 1,451 bytes at most, 1,689 with those five inlined. */
#define NOINLINE __attribute__((noinline))

#define T_LPAR 0x10
#define T_RPAR 0x11
#define T_INV  0x0C           /* the x⁻¹ key, a postfix like ² */
#define T_SQR  0x0D
#define T_CUBE 0x0F
#define T_ADD  0x70
#define T_SUB  0x71
#define T_MUL  0x82
#define T_DIV  0x83
#define T_NEG  0xB0
#define T_ANS  0x72
#define T_DOT  0x3A           /* the decimal point */
#define T_POW  0xF0
#define T_EXT  0xEF           /* first byte of EF 2E, the n/d fraction bar */
#define T_FRAC 0x2E
#define T_VAR0 0x41           /* A..Z then theta at 0x5B */
#define T_SQRT 0xBC           /* √( */
#define T_EQ   0x6A
#define T_COMMA 0x2B
#define T_OR   0x3C           /* " or " */
/* Lists and i go out as the GLYPHS the OS result routine maps to their tokens
   (table 0xB1AB2, docs §7): the comma token 0x2B would be read as glyph '+'
   and filed as +, and the i token 0x2C as a comma. */
#define T_LBRACE 0x08
#define T_RBRACE 0x09
#define T_LCOMMA 0x2C         /* glyph ',': filed and drawn as token 0x2B */
#define T_I      0xD7         /* glyph i: filed and drawn as token 0x2C */
/* Functions, in and out; none of these is in that table. Two-byte: logBASE(
   is EF 34, e BB 31, small letters BB B0.. */
#define T_SIN  0xC2
#define T_TAN  0xC6
#define T_LN   0xBE
#define T_EXP  0xBF           /* e^( */
#define T_LOG  0xC0
#define T_BB   0xBB
#define T_ABS  0xB2           /* abs(: ln(|u|) is ln( abs( u ) ), drawn |u| in MathPrint */
#define T_EE   0x3B           /* the E of 1E99: infinity, as typed (calc()) */
#define IMAG   0x80000000UL   /* rad: times i. i·i is -1 (rmul) */
#define NVTOK  27
#define ANS_REAL 0xFF         /* ans[0]: Ans is a TI real at ans + 1, not tokens */
#define RNUM   0xFFFFFUL      /* rad: the whole number under the root, */
#define RVAR   20             /* and from this bit up, the variable of nibble k at bit RVAR + k */

typedef struct {
    int32_t  num, den;        /* lowest terms, den > 0 */
    unsigned mono;            /* one power per nibble, first variable highest */
    uint32_t rad;             /* times the square root of: a squarefree whole
                                 number, times one variable at most; 1 = none */
} term_t;

typedef struct {
    uint8_t n;
    term_t  t[MAXT];          /* unordered until emit; no zero coefficients */
} poly_t;

typedef struct {
    poly_t n, d;              /* n/d; reduce() leaves d 1, or monic */
} rat_t;

/* The hook reserves this much at its work pointer. */
#define SYMCE_WORK (NPOOL * sizeof(rat_t))
typedef char work_fits[SYMCE_WORK <= 0x4F00 ? 1 : -1];   /* 0xD0EF00..0xD13E00 */
typedef char mono_fits[4 * NV <= 24 ? 1 : -1];   /* unsigned is 24 bits on the eZ80 */

typedef struct {
    const uint8_t *in;
    unsigned len, i;
    const uint8_t *ans;       /* the last answer, length or ANS_REAL first; 0 when none */
    rat_t   *pool;
    uint8_t  top, depth, nv;
    uint8_t  dec;             /* a decimal number was read: the answer shows decimals */
    uint8_t  mp;              /* MathPrint: width() */
    uint8_t  rank[NVTOK];     /* variable token - T_VAR0 -> alphabetical rank */
    uint8_t  var[NV];         /* rank -> token */
    uint8_t  lim;             /* get() takes slots below it; kernels' arguments sit above */
    uint8_t  fn;              /* TOLN..TCOLLECT: functions are read */
    uint8_t  deg;             /* MODE_DEG */
    uint8_t  inans;           /* reading SymCE's own Ans: a comma is glyph 0x2C */
    uint8_t  nk;              /* kernels, at ranks nv.. after the variables: */
    uint8_t  kf[NV];          /* K_SIN.., */
    rat_t   *ka[NV], *kb[NV]; /* argument, and a logarithm's base (0: e) */
    rat_t   *tb;              /* TOLN's and TOLOGBASE's base (0: e) */
} ps_t;

static int is_digit(int c) { return c >= 0x30 && c <= 0x39; }
static int is_var(int c)   { return c >= T_VAR0 && c < T_VAR0 + NVTOK; }

/* ---- rationals ---- */

/* Speed: the eZ80 divides 32 bits in ~3300 cycles and 24 bits in ~1600, and
   these run for every coefficient. So Euclid drops to 24 bits once both fit,
   stops at a remainder of 1, and dv() skips dividing by 1; mulc() skips its
   overflow test's division when both are at most 31622 (31622² < BIG). The
   answers are the same numbers, only sooner. */
/* |v| as a branch. clang's own abs is (v + (v >> 31)) ^ (v >> 31), and the
   eZ80 shifts a bit at a time; the empty asm keeps it a branch. */
static int32_t iabs(int32_t v)
{
    if (v < 0) { __asm__ volatile (""); v = -v; }
    return v;
}

static int32_t gcd(int32_t a, int32_t b)     /* b > 0 */
{
    unsigned x, y, r;
    a = iabs(a);
    while (b && ((uint32_t)a > 0xFFFFFF || (uint32_t)b > 0xFFFFFF)) { int32_t r = a % b; a = b; b = r; }
    if (!b) return a;
    for (x = a, y = b; y > 1; x = y, y = r) r = x % y;
    return y ? 1 : x;
}

/* a / g, g > 0 */
static int32_t dv(int32_t a, int32_t g)
{
    if (g == 1) return a;
    if (a >= -0x7FFFFF && a <= 0x7FFFFF && g <= 0x7FFFFF) return (int)a / (int)g;
    return a / g;
}

static int mulc(int32_t a, int32_t b, int32_t *r)
{
    int32_t x = iabs(a), y = iabs(b);
    if ((x > 31622 || y > 31622) && x && y > BIG / x) return 0;
    *r = a * b;
    return 1;
}

static int rset(term_t *t, int32_t n, int32_t d)
{
    int32_t g = gcd(n, d);
    n = dv(n, g); d = dv(d, g);
    if (n > LIM || n < -LIM || d > LIM) return 0;
    t->num = n; t->den = d;
    return 1;
}

static int radd(term_t *t, int32_t n, int32_t d)
{
    int32_t g = gcd(t->den, d), a, b, dd;
    if (!mulc(dv(t->den, g), d, &dd) || !mulc(t->num, dv(d, g), &a) ||
        !mulc(n, dv(t->den, g), &b)) return 0;
    return rset(t, a + b, dd);
}

/* t = x*y. The caller sets t's monomial to x's plus y's first; a variable
   under both roots comes out, √X√X = X, and adds its power here. √2√6 = 2√3.
   Two different variables under one root are refused: √X√Y is √(XY) only
   while both are positive, and emit() could not say which it meant. */
static int rmul(term_t *t, const term_t *x, const term_t *y)
{
    int32_t g1 = gcd(x->num, y->den), g2 = gcd(y->num, x->den), n, d, a, b, g, r;
    uint32_t v, both;
    uint8_t k;
    if (x->rad == 1 && y->rad == 1) {           /* Speed: no root, as most: what the rest comes to then */
        if (!mulc(dv(x->num, g1), dv(y->num, g2), &n) || !mulc(dv(x->den, g2), dv(y->den, g1), &d)) return 0;
        t->rad = 1;
        return rset(t, n, d);
    }
    a = x->rad & RNUM; b = y->rad & RNUM; g = gcd(a, b);
    v = (x->rad ^ y->rad) & ~RNUM; both = x->rad & y->rad & ~RNUM;
    if (!mulc(dv(x->num, g1), dv(y->num, g2), &n) || !mulc(n, g, &n) ||
        !mulc(dv(x->den, g2), dv(y->den, g1), &d) || !mulc(dv(a, g), dv(b, g), &r) || r > LIM ||
        (v & (v - 1))) return 0;
    if (both & IMAG) n = -n;
    if (both & ~IMAG)
        for (k = 0; k < NV; k++) if (both >> (RVAR + k) & 1) t->mono += 1u << 4 * k;
    t->rad = v | (uint32_t)r;
    return rset(t, n, d);
}

/* ---- polynomials ---- */

static uint8_t degree(unsigned m)
{
    uint8_t d = 0;
    for (; m; m >>= 4) d += m & 15;
    return d;
}

/* Total degree, doubled: a variable under a root counts half; i none. */
static uint8_t degree2(const term_t *t)
{
    return 2 * degree(t->mono) + !!(t->rad & ~RNUM & ~IMAG);
}

/* Term order, and the order emit() writes them in: degree, then monomial,
   then no root before a root, smaller radicands first -- 1+√(2), X²+X√(X)+X.
   Multiplying both sides by the same term without a root keeps it, which is
   all divinto needs: it never divides by a root. */
static int before(const term_t *a, const term_t *b)
{
    uint8_t da = degree2(a), db = degree2(b);
    if (da != db) return da > db;
    return a->mono != b->mono ? a->mono > b->mono : a->rad < b->rad;
}

static int addterm(poly_t *p, const term_t *x)
{
    term_t *t = p->t, *end = t + p->n;
    for (; t < end; t++)
        if (t->mono == x->mono && t->rad == x->rad) {
            if (!radd(t, x->num, x->den)) return 0;
            if (!t->num) { *t = *--end; p->n--; }
            return 1;
        }
    if (!x->num) return 1;
    if (p->n == MAXT) return 0;
    *t = *x;
    p->n++;
    return 1;
}

static void pconst(poly_t *p, int32_t v)
{
    p->n = v != 0;
    p->t[0].num = v; p->t[0].den = 1; p->t[0].mono = 0; p->t[0].rad = 1;
}

/* some term of p has a root */
static int hasrad(const poly_t *p)
{
    const term_t *x;
    for (x = p->t; x < p->t + p->n; x++) if (x->rad != 1) return 1;
    return 0;
}

static void pneg(poly_t *p)
{
    uint8_t k;
    for (k = 0; k < p->n; k++) p->t[k].num = -p->t[k].num;
}

/* p is a constant; for a denominator, that means 1 */
static int isconst(const poly_t *p)
{
    return p->n == 1 && !p->t[0].mono;
}

/* p's first term in canonical order; p is not zero */
static const term_t *lead(const poly_t *p)
{
    const term_t *l = p->t, *y;
    for (y = p->t + 1; y < p->t + p->n; y++) if (before(y, l)) l = y;
    return l;
}

/* 1/x as a coefficient; x is not zero, and has no root */
static void rinv(term_t *r, const term_t *x)
{
    r->num = x->num < 0 ? -x->den : x->den;
    r->den = iabs(x->num);
    r->rad = 1;
}

/* every coefficient of p times c */
static int scale(poly_t *p, const term_t *c)
{
    term_t *t;
    for (t = p->t; t < p->t + p->n; t++) if (!rmul(t, t, c)) return 0;
    return 1;
}

/* p and q over p's first coefficient, making p monic; p is not zero */
static int monic(poly_t *p, poly_t *q)
{
    term_t inv;
    rinv(&inv, lead(p));
    return scale(p, &inv) && (!q || scale(q, &inv));
}

#define MULTI (~0u)           /* univ(): more than one variable */

/* The one variable in p, as its nibble mask in a monomial; 0 for a constant. */
static unsigned univ(const poly_t *p)
{
    unsigned v = 0, m;
    const term_t *x;
    uint8_t k;
    for (x = p->t; x < p->t + p->n; x++)
        for (k = 0; k < 4 * NV; k += 4)
            if (x->mono >> k & 15) {
                m = 15u << k;
                if (v && v != m) return MULTI;
                v = m;
            }
    return v;
}

/* the nibble-wise smaller of two monomials: their gcd */
static unsigned mgcd(unsigned a, unsigned b)
{
    unsigned r = 0, m;
    if (a == b) return a;
    for (m = 15; m && m <= 15u << 4 * (NV - 1); m <<= 4)   /* masks, not shifts: the eZ80 shifts a bit at a time */
        r |= (a & m) < (b & m) ? a & m : b & m;
    return r;
}

/* the monomial dividing every term of p, and lo */
static unsigned content(const poly_t *p, unsigned lo)
{
    const term_t *x;
    for (x = p->t; x < p->t + p->n; x++) lo = mgcd(lo, x->mono);
    return lo;
}

/* p /= the monomial c, which divides every term: no nibble borrows */
static void divmono(poly_t *p, unsigned c)
{
    term_t *x;
    for (x = p->t; x < p->t + p->n; x++) x->mono -= c;
}

/* p, with no monomial factor and more than one term, cannot be factored if
   some variable V is in it only to the first power, with one term holding
   all of V or one all of the rest: p = aV + b, and a factor without V
   divides a and b, so it divides a monomial and is one -- which p has not.
   X+Y, XY+1, 2X-3Y+1. */
static int prime(const poly_t *p)
{
    const term_t *x;
    uint8_t k, a, b, e;
    for (k = 0; k < 4 * NV; k += 4) {
        a = b = 0;
        for (x = p->t; x < p->t + p->n; x++) {
            e = x->mono >> k & 15;
            if (e > 1) break;
            if (e) a++; else b++;
        }
        if (x == p->t + p->n && a && (a == 1 || b == 1)) return 1;
    }
    return 0;
}

static rat_t *get(ps_t *s)
{
    return s->top < s->lim ? &s->pool[s->top++] : 0;
}

/* scratch for one polynomial: half a pool slot */
static poly_t *tmp(ps_t *s)
{
    rat_t *r = get(s);
    return r ? &r->n : 0;
}

static int unroot(ps_t *s, poly_t *p);

/* acc *= f */
static int mulinto(ps_t *s, poly_t *acc, const poly_t *f)
{
    poly_t *r = tmp(s);
    const term_t *x, *y;
    term_t t;
    if (!r) return 0;
    r->n = 0;
    for (x = acc->t; x < acc->t + acc->n; x++)
        for (y = f->t; y < f->t + f->n; y++) {
            t.mono = x->mono + y->mono;
            if (!rmul(&t, x, y) || t.mono & OVF || !addterm(r, &t)) return 0;
        }
    *acc = *r;
    s->top--;
    return !s->nk || unroot(s, acc);
}

/* acc -= c*f, c a term, for as long as f's first term divides acc's; the c's
   add up in q when it is not 0. Each step takes away acc's first term and
   adds only later ones, so it ends: q gains a term per step and MAXT bounds
   it, or, with no q, acc and f are in one variable and its degree does. 0 on
   overflow, or when f has a root: acc may have them, f may not. A term of acc
   under a root is then that root times a polynomial without, which f divides
   or does not on its own, and acc's first term is some such first term. */
static int rem(poly_t *acc, const poly_t *f, poly_t *q)
{
    const term_t *d = lead(f), *l, *y;
    term_t inv, c, t;
    uint8_t k;
    if (hasrad(f)) return 0;
    rinv(&inv, d);
    while (acc->n) {
        l = lead(acc);
        for (k = 0; k < 4 * NV; k += 4)
            if ((l->mono >> k & 15) < (d->mono >> k & 15)) return 1;
        c.mono = l->mono - d->mono;
        if (!rmul(&c, l, &inv) || (q && !addterm(q, &c))) return 0;
        for (y = f->t; y < f->t + f->n; y++) {
            t.mono = c.mono + y->mono;
            if (!rmul(&t, &c, y) || t.mono & OVF) return 0;
            t.num = -t.num;
            if (!addterm(acc, &t)) return 0;
        }
    }
    return 1;
}

/* acc /= f and 1 when f divides it exactly: (X²-1)/(X-1) is X+1. Else 2 when
   it does not, 0 on overflow -- which proves nothing -- and acc is spoilt.
   Long division by f's first term: when f divides acc, the first term of
   every remainder is a multiple of it, so one that is not proves f does not
   divide acc. */
static int divinto(ps_t *s, poly_t *acc, const poly_t *f)
{
    poly_t *q = tmp(s);
    int ok = 0;
    if (!q) return 0;
    q->n = 0;
    if (f->n && rem(acc, f, q)) ok = acc->n ? 2 : 1;
    if (ok == 1) *acc = *q;
    s->top--;
    return ok;
}

/* g = gcd(g, f), both in the same one variable, g monic: Euclid over the
   rationals, each remainder made monic to keep its coefficients small. */
static int gcdinto(ps_t *s, poly_t *g, const poly_t *f)
{
    poly_t *a = tmp(s), *b = tmp(s), *t;
    if (!b || hasrad(g) || hasrad(f)) return 0;
    *a = *g; *b = *f;
    while (b->n) {
        if (!monic(b, 0) || !rem(a, b, 0)) return 0;
        t = a; a = b; b = t;
    }
    *g = *a;
    s->top -= 2;
    return 1;
}

/* The total degree every term of p has, or -1 when they differ. */
static int homog(const poly_t *p)
{
    const term_t *x;
    int d = -1, e;
    uint8_t k;
    for (x = p->t; x < p->t + p->n; x++) {
        for (e = 0, k = 0; k < 4 * NV; k += 4) e += x->mono >> k & 15;
        if (d >= 0 && e != d) return -1;
        d = e;
    }
    return d;
}

/* g = gcd(p, m) when p is homogeneous in two variables U and V, m anything.
   Every factor of p is homogeneous in U, V, so the gcd is too, and it divides
   each part of m of one degree in U, V and one monomial in the rest: the
   parts are homogeneous, and the gcd of p and all of them (a part of m under
   a root is taken without it: p has none). U = 1 makes each a
   polynomial in V alone, merging no terms (one degree: a term's V fixes its
   U), and takes their gcd to the gcd of those; giving each of its terms back
   its U, up to the full degree, undoes it. No power of U is lost: p comes in
   with its own monomial taken out, so U divides neither p nor the gcd.
   (X²-Y²+X-Y)/(X²-Y²): parts X²-Y² and X-Y, through 1-Y² and 1-Y: X-Y.
   1 done, 2 when p is not like that, 0 on overflow. */
static int homgcd(ps_t *s, poly_t *g, poly_t *h, const poly_t *p, const poly_t *m)
{
    unsigned w = 0, v, uv, key;
    const term_t *x, *y;
    term_t t, *z;
    uint8_t k, uk = 0, vk = 0, nv = 0, e = 0, dx;
    for (x = p->t; x < p->t + p->n; x++) w |= x->mono;
    for (k = 0; k < 4 * NV; k += 4)
        if (w >> k & 15) { if (nv++) uk = k; else vk = k; }
    if (nv != 2 || homog(p) < 0 || hasrad(p)) return 2;
    v = 15u << vk;
    uv = v | 15u << uk;
    *g = *p;
    divmono(g, content(g, g->t[0].mono));
    for (z = g->t; z < g->t + g->n; z++) z->mono &= v;
    if (!monic(g, 0)) return 0;
    for (x = m->t; x < m->t + m->n && !isconst(g); x++) {
        key = x->mono & ~uv;
        dx = (x->mono >> uk & 15) + (x->mono >> vk & 15);
        for (y = m->t; y < x; y++)
            if ((y->mono & ~uv) == key && y->rad == x->rad &&
                (y->mono >> uk & 15) + (y->mono >> vk & 15) == dx) break;
        if (y < x) continue;                    /* that part is done */
        h->n = 0;
        for (y = x; y < m->t + m->n; y++)
            if ((y->mono & ~uv) == key && y->rad == x->rad &&
                (y->mono >> uk & 15) + (y->mono >> vk & 15) == dx) {
                t = *y;
                t.mono &= v;
                t.rad = 1;
                addterm(h, &t);                 /* distinct: V fixes U */
            }
        if (!gcdinto(s, g, h)) return 0;
    }
    for (z = g->t; z < g->t + g->n; z++) if ((z->mono >> vk & 15) > e) e = z->mono >> vk & 15;
    for (z = g->t; z < g->t + g->n; z++) z->mono |= (e - (z->mono >> vk & 15)) << uk;
    return 1;
}

/* r in lowest terms, d then 1 or monic. Lowest terms is proved, never
   assumed: the common monomial comes out, after which a side with one term
   shares no factor with the other, and no variable dividing one side divides
   the other. So a side's own monomial factor can be set aside too: when the
   rest of it, g, is in a single variable V, any common factor is in V alone,
   so it divides every V-part of the other side (its terms grouped by their
   other variables) and Euclid finds it -- B(X+4) against 24BX-X²+108B-4X.
   When g is prime(), it all divides the other side or none of it does:
   (X²-Y²)/(X+Y) is X-Y, (X+Y)/(X-Y) stays. Else one side dividing the other
   exactly settles it, or homgcd() does. Returns 1 when proved, 2 when not --
   (X²+X-Y²+Y)/(X²+2XY+Y²+X+Y) -- and 0 on overflow or a zero denominator.
   With roots, d has none (rationalize() sees to that) and n is a sum of
   different roots times polynomials without: a factor of d divides n when
   it divides each of those, which is what the groups above take apart, so
   the proof never divides by n then. (X-√(2))/(X²-2) is in lowest terms. */
static int reduce(ps_t *s, rat_t *r)
{
    poly_t *n = &r->n, *d = &r->d, *m = n, *g, *h;
    term_t t;
    unsigned lo, v, w;
    uint8_t top = s->top, k, j, ok = 1;

    if (!d->n) return 0;
    if (!n->n) { pconst(d, 1); return 1; }
    lo = content(d, content(n, d->t[0].mono));
    divmono(n, lo);
    divmono(d, lo);
    if (n->n > 1 && d->n > 1) {
        if (!(g = tmp(s)) || !(h = tmp(s))) goto fail;
        *g = *d;
        divmono(g, content(g, g->t[0].mono));
        if ((v = univ(g)) == MULTI && !prime(g) && !hasrad(n)) {
            *g = *n;
            divmono(g, content(g, g->t[0].mono));
            v = univ(g);
            m = d;
        }
        if (v != MULTI) {
            if (!monic(g, 0)) goto fail;
            for (k = 0; k < m->n && !isconst(g); k++) {
                w = m->t[k].mono & ~v;
                for (j = 0; j < k && ((m->t[j].mono & ~v) != w || m->t[j].rad != m->t[k].rad); j++) ;
                if (j < k) continue;            /* that group is done */
                h->n = 0;
                for (j = k; j < m->n; j++)
                    if ((m->t[j].mono & ~v) == w && m->t[j].rad == m->t[k].rad) {
                        t = m->t[j];
                        t.mono &= v;
                        t.rad = 1;
                        addterm(h, &t);         /* distinct, fewer than MAXT */
                    }
                if (!gcdinto(s, g, h)) goto fail;
            }
        } else if (prime(g)) {
            *h = *m;
            if (!(k = divinto(s, h, g))) goto fail;
            if (k == 2) pconst(g, 1);
        } else {
            pconst(g, 1);
            *h = *n;
            if (divinto(s, h, d) == 1) { *n = *h; pconst(d, 1); }
            else {
                *h = *d;
                if (!hasrad(n) && divinto(s, h, n) == 1) { *d = *h; pconst(n, 1); }
                else if (!(k = homgcd(s, g, h, d, n)) || (k == 2 && !(k = homgcd(s, g, h, n, d))))
                    goto fail;
                else if (k == 2) ok = 2;
            }
        }
        if (!isconst(g) && (divinto(s, n, g) != 1 || divinto(s, d, g) != 1)) goto fail;
    }
    s->top = top;
    return monic(d, n) ? ok : 0;
fail:
    s->top = top;
    return 0;
}

/* No root below the bar: while d has one, n and d times d with the terms
   under some root √P negated. d = A + B√P, times A - B√P, is A² - P·B²,
   with no √P and no root that d did not have. 1/√(2) is √(2)/2,
   1/(1+√(2)) is √(2)-1 once reduced. 0 on overflow. */
static int rationalize(ps_t *s, rat_t *r)
{
    poly_t *c = tmp(s);
    term_t *x;
    uint32_t p, q;
    if (!c) return 0;
    while (hasrad(&r->d)) {
        for (x = r->d.t; x->rad == 1; x++) ;
        p = x->rad & ~RNUM;                     /* a variable, */
        if (!p) {                               /* or the least prime under a root */
            q = x->rad;
            for (p = 2; q % p; p++) ;
        }
        *c = r->d;
        for (x = c->t; x < c->t + c->n; x++)
            if (p > RNUM ? x->rad & p : (x->rad & RNUM) % p == 0) x->num = -x->num;
        if (!mulinto(s, &r->n, c) || !mulinto(s, &r->d, c)) return 0;
    }
    s->top--;
    return 1;
}

/* r is a plain number: no variable, no root, over 1 */
static int plain(const rat_t *r)
{
    const term_t *d = r->d.t, *x = r->n.t;
    return r->d.n == 1 && d->num == 1 && d->den == 1 && !d->mono && d->rad == 1 &&
           (!r->n.n || (r->n.n == 1 && !x->mono && x->rad == 1));
}

/* a *= f, or a /= f when inv; 0 when that divides by zero. Speed: two plain
   numbers are one coefficient times another, what the whole way below comes
   to (its reduce() then scales by 1); it still goes when that overflows, to
   fail as it did. */
static int rmulinto(ps_t *s, rat_t *a, const rat_t *f, uint8_t inv)
{
    term_t t, c;
    if (inv && !f->n.n) return 0;
    if (s->top < s->lim && plain(a) && plain(f)) {
        if (!f->n.n) a->n.n = 0;
        if (!a->n.n) return 1;
        if (inv) rinv(&c, f->n.t); else c = f->n.t[0];
        t.mono = 0;
        if (rmul(&t, a->n.t, &c)) { a->n.t[0] = t; return 1; }
    }
    return mulinto(s, &a->n, inv ? &f->d : &f->n) &&
           mulinto(s, &a->d, inv ? &f->n : &f->d) && rationalize(s, a) && reduce(s, a);
}

/* a == b, their terms in any order */
static int same(const poly_t *a, const poly_t *b)
{
    const term_t *x, *y;
    if (a->n != b->n) return 0;
    for (x = a->t; x < a->t + a->n; x++) {
        for (y = b->t; y < b->t + b->n && (y->mono != x->mono || y->rad != x->rad); y++) ;
        if (y == b->t + b->n || y->num != x->num || y->den != x->den) return 0;
    }
    return 1;
}

/* a += f, as a/b + c/d = (ad + cb)/bd, or (a + c)/b when d is b, which keeps
   1/X^4+1/X^4 clear of X^8; f is used up */
static int raddinto(ps_t *s, rat_t *a, rat_t *f)
{
    uint8_t k;
    if (plain(a) && plain(f)) return !f->n.n || addterm(&a->n, f->n.t);  /* reduce() would scale by 1 */
    if (!same(&a->d, &f->d) &&
        (!mulinto(s, &f->n, &a->d) || !mulinto(s, &a->n, &f->d) ||
         !mulinto(s, &a->d, &f->d))) return 0;
    for (k = 0; k < f->n.n; k++)
        if (!addterm(&a->n, &f->n.t[k])) return 0;
    return reduce(s, a);
}

/* acc = acc^e; a negative e is a power of 1/acc. n^e and d^e share no factor
   when n and d share none, and d^e stays monic, so no reduce: one could only
   re-prove what the base had, and symce_engine's last reduce proves the
   answer either way. 1/acc can put a root below the bar, so that one is
   rationalized and reduced first. */
static int powinto(ps_t *s, rat_t *acc, int32_t e)
{
    rat_t *b = get(s);
    if (!b || (!acc->n.n && e <= 0)) return 0;  /* 0^0, and 0^-1 divides by zero */
    *b = *acc;
    if (e < 0) {
        acc->n = b->d; acc->d = b->n; e = -e;
        if (!rationalize(s, acc) || !reduce(s, acc)) return 0;
        *b = *acc;
    }
    pconst(&acc->n, 1); pconst(&acc->d, 1);
    while (e--)
        if (!mulinto(s, &acc->n, &b->n) || !mulinto(s, &acc->d, &b->d)) return 0;
    s->top--;
    return 1;
}

static void rconst(rat_t *r, int32_t v)
{
    pconst(&r->n, v);
    pconst(&r->d, 1);
}

/* Kernels (ps_t.kf), and the heads of functions as typed: sin( .. ln( are
   kernels of their own; e is K_E, log( and logBASE( are K_LOG with a base.
   K_LABS is ln|u|, ln(abs(u)), the Calculus menu's: INTEGRAL(1/X,X) is
   ln(abs(X)). K_ROOT is rootk()'s U = P, which a term only has under its
   root: √(X+1), abs(X) (kb S when P is S²). */
enum { K_SIN = 1, K_COS, K_TAN, K_EXP, K_LOG, K_E, H_LOG, H_LOGB, K_LABS, K_ROOT };
#define KN(s, k) (NV - 1 - (s)->nv - (k))       /* kernel k's nibble, after the variables' */

/* Each head: its code, its length, its tokens, '(' included. putk() writes
   a kernel back as the first head with its code: cos( in letters, since the
   OS's result routine turns the cos( token 0xC4 into the pi glyph. */
static const uint8_t heads[] = {
    K_SIN, 1, T_SIN,
    K_COS, 7, T_BB, 0xB2, T_BB, 0xBF, T_BB, 0xC3, T_LPAR,   /* c o s ( */
    K_COS, 1, 0xC4,
    K_TAN, 1, T_TAN,
    K_EXP, 1, T_EXP,
    K_LABS, 2, T_LN, T_ABS,                             /* before ln( */
    K_LOG, 1, T_LN,
    K_E, 2, T_BB, 0x31,
    H_LOG, 1, T_LOG,
    H_LOGB, 2, T_EXT, 0x34,
    0
};

/* The function head at s->i: its code, s->i past it; 0 when none */
static uint8_t head(ps_t *s)
{
    const uint8_t *h;
    uint8_t k;
    for (h = heads; *h; h += 2 + h[1]) {
        for (k = 0; k < h[1] && s->i + k < s->len && s->in[s->i + k] == h[2 + k]; k++) ;
        if (k == h[1]) { s->i += k; return *h; }
    }
    return 0;
}

/* a function head starts a factor, in TOLN..TCOLLECT only */
static uint8_t ishead(ps_t *s)
{
    unsigned i = s->i;
    uint8_t h = s->fn ? head(s) : 0;
    s->i = i;
    return h;
}

static int fncall(ps_t *s, rat_t *out, uint8_t h);

/* ---- parser: each level returns 0 on anything out of scope ---- */

/* The OS writes an n/d box out as (numerator) EF2E denominator. Measured on
   OS 5.8.4, that bar binds exactly like / -- one level with * and implied
   multiplication, left to right, under ^: 3n/d2^2 is 3/4, 6n/d2n/d3 is 1 -- so
   it reads as T_DIV. term() is the only place that consumes one. */
static int peek(const ps_t *s)
{
    if (s->i >= s->len) return -1;
    if (s->in[s->i] == T_EXT && s->i + 1 < s->len && s->in[s->i + 1] == T_FRAC)
        return T_DIV;
    return s->in[s->i];
}

static int expr(ps_t *s, rat_t *out);

/* A TI real, as the OS keeps Ans: sign (bit 7) and type, exponent 0x80 + e,
   then 14 BCD digits d.ddd... times 10^e. Exact, so only when the digits end
   within six of each other: N * 10^k with N up to 999999. */
static int preal(poly_t *out, const uint8_t *r)
{
    int32_t n = 0, d = 1;
    int e = r[1] - 0x80, j = 13, k;

    if (r[0] & 0x1F) return 0;                  /* a list, a complex, ... */
    while (j >= 0 && !(r[2 + j / 2] >> (j & 1 ? 0 : 4) & 15)) j--;
    if (j > 5) return 0;
    for (k = 0; k <= j; k++) n = n * 10 + (r[2 + k / 2] >> (k & 1 ? 0 : 4) & 15);
    for (k = j; k < e; k++) if ((n *= 10) > LIM) return 0;
    for (k = e; k < j; k++) if ((d *= 10) > LIM) return 0;
    pconst(out, 1);
    if (!rset(&out->t[0], r[0] & 0x80 ? -n : n, d)) return 0;
    out->n = n != 0;
    return 1;
}

/* v = s²·r with r squarefree: r, and s at *sq. v <= LIM: primes to 1000. */
static int32_t sqfree(int32_t v, int32_t *sq)
{
    int32_t p, q = 1;
    for (p = 2; p * p <= v; p++)
        while (v % (p * p) == 0) { v /= p * p; q *= p; }
    *sq = q;
    return v;
}

/* r = √r, for r = c·M/K: a positive number, monomials M over K, no root in
   either. That is √(c·M·K)/|K|. A variable's even power comes out whole; an
   odd one leaves it under the root, which says it is not negative. So √(X²),
   which is |X|, is refused, as is a second variable left under the root:
   √(XY) is no √X√Y when both are negative. With one at most, K's only odd
   power is that variable's, so |K| is K. √(8) is 2√(2), √(X³) is X√(X),
   √(1/2) is √(2)/2, √(1/X) is √(X)/X. The rest, √(X+1), √(-X), √(X²),
   is rootk()'s. */
static int rootk(ps_t *s, rat_t *r);

static int sqrtinto(ps_t *s, rat_t *r)
{
    term_t *t = r->n.t, *d = r->d.t;
    unsigned m, out = 0;
    uint32_t v = 0;
    int32_t a, b, sa, sb;
    uint8_t k, e;
    if (!r->n.n) return 1;                      /* √(0) */
    if (r->n.n != 1 || r->d.n != 1 || t->rad != 1 || d->rad != 1 || t->num < 0 || d->num != 1 ||
        (s->nk && (t->mono | d->mono) & 0xFFFFFFu >> 4 * s->nv))  /* no kernel under a root */
        return rootk(s, r);
    m = mgcd(t->mono, d->mono);                 /* M and K share nothing */
    t->mono -= m; d->mono -= m;
    m = t->mono + d->mono;
    for (k = 0; k < NV; k++) {
        e = m >> 4 * k & 15;
        if (e & 1) {
            if (v) return rootk(s, r);
            v = (uint32_t)1 << (RVAR + k);
        } else if (e & 2) return rootk(s, r);
        out |= (unsigned)(e >> 1) << 4 * k;
    }
    a = sqfree(t->num, &sa);
    b = sqfree(t->den, &sb);
    if (!mulc(sb, b, &sb) || !mulc(a, b, &a) || a > LIM || !rset(t, sa, sb)) return 0;
    t->mono = out;
    t->rad = v | (uint32_t)a;
    return reduce(s, r);
}

static int primary(ps_t *s, rat_t *out)
{
    int c = peek(s);
    if (is_digit(c) || c == T_DOT) {            /* 12, 2.5, .5, 5. -- exact */
        int32_t v = 0, d = 1;
        uint8_t dot = 0, digits = 0;
        for (;; s->i++) {
            c = peek(s);
            if (c == T_DOT && !dot) { dot = 1; continue; }
            if (!is_digit(c)) break;
            digits = 1;
            if ((v = v * 10 + (c - 0x30)) > LIM || (dot && (d *= 10) > LIM)) return 0;
        }
        if (!digits || c == T_DOT) return 0;    /* a lone point, 1.2.3 */
        s->dec |= dot;
        rconst(out, 1);
        out->n.n = v != 0;
        return rset(&out->n.t[0], v, d);
    }
    if (is_var(c)) {
        s->i++;
        rconst(out, 1);
        out->n.t[0].mono = 1u << 4 * (NV - 1 - s->rank[c - T_VAR0]);
        return 1;
    }
    if (c == T_ANS && s->ans && s->ans[0] == ANS_REAL) {
        s->i++;
        pconst(&out->d, 1);
        return preal(&out->n, s->ans + 1);
    }
    if (c == T_ANS && s->ans && s->depth < MAXDEPTH) {
        /* The last answer, parsed in place like a parenthesised entry. It is
           engine output, so it holds no Ans itself: one level at most. */
        const uint8_t *in = s->in;
        unsigned len = s->len, i = s->i;
        int ok;
        s->in = s->ans + 1; s->len = s->ans[0]; s->i = 0; s->ans = 0;
        s->depth++;
        s->inans = 1;
        ok = expr(s, out) && s->i == s->len;
        s->inans = 0;
        s->depth--;
        s->ans = s->in - 1; s->in = in; s->len = len; s->i = i + 1;
        return ok;
    }
    if ((c == T_LPAR || c == T_SQRT || c == T_ABS) && s->depth < MAXDEPTH) {
        int root = c;
        s->i++;
        s->depth++;
        if (!expr(s, out)) return 0;
        s->depth--;
        if ((c = peek(s)) == T_RPAR) s->i++;
        else if (c >= 0) return 0;
        if (root == T_ABS && !powinto(s, out, 2)) return 0;    /* abs(u) is √(u²) */
        return root == T_LPAR || sqrtinto(s, out);
    }
    if (s->fn && (c = head(s))) return fncall(s, out, (uint8_t)c);
    return 0;
}

static int postfix(ps_t *s, rat_t *out)
{
    int c;
    if (!primary(s, out)) return 0;
    while ((c = peek(s)) == T_SQR || c == T_CUBE || c == T_INV) {
        s->i++;
        if (!powinto(s, out, c == T_SQR ? 2 : c == T_CUBE ? 3 : -1)) return 0;
    }
    return 1;
}

static int power(ps_t *s, rat_t *out)
{
    if (!postfix(s, out)) return 0;
    while (peek(s) == T_POW) {
        rat_t *e = get(s);
        const poly_t *p;
        uint8_t neg = 0;
        int32_t v;
        for (s->i++; peek(s) == T_NEG; s->i++) neg ^= 1;
        if (!e || !postfix(s, e)) return 0;
        /* a whole number -MAXPOW..MAXPOW, and nothing else */
        p = &e->n;
        if (!isconst(&e->d) || p->n > 1 || (p->n && (p->t[0].mono || p->t[0].den != 1 || p->t[0].rad != 1)))
            return 0;
        v = p->n ? p->t[0].num : 0;
        if (neg) v = -v;
        if (v < -MAXPOW || v > MAXPOW) return 0;
        s->top--;
        if (!powinto(s, out, v)) return 0;
    }
    return 1;
}

static int unary(ps_t *s, rat_t *out)
{
    uint8_t neg = 0;
    for (; peek(s) == T_NEG; s->i++) neg ^= 1;
    if (!power(s, out)) return 0;
    if (neg) pneg(&out->n);
    return 1;
}

static int term(ps_t *s, rat_t *out)
{
    rat_t *f = get(s);
    if (!f || !unary(s, out)) return 0;
    for (;;) {
        int c = peek(s);
        if (c == T_MUL || c == T_DIV) s->i += s->in[s->i] == T_EXT ? 2 : 1;
        else if (!is_digit(c) && c != T_DOT && !is_var(c) && c != T_LPAR && c != T_ANS && c != T_SQRT &&
                 c != T_ABS && !ishead(s)) break;
        if (!unary(s, f) || !rmulinto(s, out, f, c == T_DIV)) return 0;
    }
    s->top--;
    return 1;
}

static int expr(ps_t *s, rat_t *out)
{
    rat_t *f = get(s);
    if (!f || !term(s, out)) return 0;
    for (;;) {
        int c = peek(s);
        if (c != T_ADD && c != T_SUB) break;
        s->i++;
        if (!term(s, f)) return 0;
        if (c == T_SUB) pneg(&f->n);
        if (!raddinto(s, out, f)) return 0;
    }
    s->top--;
    return 1;
}

/* ---- output ---- */

#define PUT(b) do { if (n >= MAXOUT) return 0; out[n++] = (uint8_t)(b); } while (0)

static uint8_t number(uint8_t *out, uint8_t n, int32_t v)
{
    uint8_t d[7], k = 0;                        /* v <= LIM: six digits */
    do d[k++] = (uint8_t)(0x30 + v % 10); while (v /= 10);
    while (k) PUT(d[--k]);
    return n;
}

/* a/den as a decimal, den dividing 10^6 and a/den in lowest terms: 0.25 */
static uint8_t decimal(uint8_t *out, uint8_t n, int32_t a, int32_t den)
{
    int32_t f = a % den * (1000000L / den), p = 100000L;   /* six places */
    if (!(n = number(out, n, a / den))) return 0;
    PUT(T_DOT);
    for (; f; f %= p, p /= 10) PUT(0x30 + f / p);
    return n;
}

/* p in canonical order (insertion sort); 1 when it is written last term
   first, a negative first term behind a positive one: 1-X. -1+i stays: a
   real part leads. */
static uint8_t order(poly_t *p)
{
    uint8_t k, j, rev = 0;
    for (k = 1; k < p->n; k++)
        for (j = k; j && before(&p->t[j], &p->t[j - 1]); j--) {
            term_t t = p->t[j]; p->t[j] = p->t[j - 1]; p->t[j - 1] = t;
        }
    if (p->n && p->t[0].num < 0)
        for (k = 1; k < p->n; k++) if (p->t[k].num > 0 && !(p->t[k].rad & IMAG)) rev = 1;
    return rev;
}

static uint8_t putk(const ps_t *s, uint8_t k, uint8_t *out, uint8_t n);
static uint8_t answer(const ps_t *s, rat_t *r, uint8_t *out, uint8_t n);

/* p, appended at out + n; returns the new length, 0 when it does not fit.
   After a decimal entry a coefficient that ends shows as a decimal: 0.3X. */
static uint8_t emit(const ps_t *s, poly_t *p, uint8_t *out, uint8_t n)
{
    uint8_t k, j, kr, rev = order(p);
    uint32_t r;

    if (!p->n) { PUT(0x30); return n; }
    for (k = 0; k < p->n; k++) {
        const term_t *t = &p->t[rev ? p->n - 1 - k : k];
        int32_t a = t->num;
        uint8_t dot = s->dec && t->den != 1 && 1000000L % t->den == 0;
        if (a < 0) { PUT(k ? T_SUB : T_NEG); a = -a; }
        else if (k) PUT(T_ADD);
        if (dot) { if (!(n = decimal(out, n, a, t->den))) return 0; }
        else if ((a != 1 || (!t->mono && t->rad == 1)) && !(n = number(out, n, a))) return 0;
        for (j = 0; j < s->nv + s->nk; j++) {   /* the variables, then kernels: 2Xsin(X) */
            uint8_t e = t->mono >> 4 * (NV - 1 - j) & 15;
            if (!e) continue;
            if (j < s->nv) PUT(s->var[j]);
            else if (!(n = putk(s, j - s->nv, out, n))) return 0;
            if (e == 2) PUT(T_SQR);
            else if (e == 3) PUT(T_CUBE);
            else if (e > 3) { PUT(T_POW); PUT(0x30 + e); }
        }
        r = t->rad & ~IMAG;
        for (kr = j = 0; j < s->nk; j++)        /* a kernel under the root: rootk()'s */
            if (r >> (RVAR + KN(s, j)) & 1) { r ^= (uint32_t)1 << (RVAR + KN(s, j)); kr = j + 1; }
        if (r != 1) {                           /* 3X√(2), √(2X) */
            PUT(T_SQRT);
            if ((r & RNUM) != 1 && !(n = number(out, n, r & RNUM))) return 0;
            for (j = 0; j < s->nv; j++) if (r >> (RVAR + NV - 1 - j) & 1) PUT(s->var[j]);
            PUT(T_RPAR);
        }
        if (kr--) {                             /* √(X+1), abs(X) */
            PUT(s->kb[kr] ? T_ABS : T_SQRT);
            if (!(n = answer(s, s->kb[kr] ? s->kb[kr] : s->ka[kr], out, n))) return 0;
            PUT(T_RPAR);
        }
        if (t->rad & IMAG) PUT(T_I);            /* √(3)i */
        if (t->den != 1 && !dot) { PUT(T_DIV); if (!(n = number(out, n, t->den))) return 0; }
    }
    return n;
}

/* n and d times the one factor that leaves whole numbers with no common
   factor: the lcm of the denominators over the gcd of the numerators */
static int lg(const poly_t *p, int32_t *l, int32_t *g)
{
    const term_t *x;
    for (x = p->t; x < p->t + p->n; x++) {
        *g = gcd(x->num, *g);
        if (!mulc(*l, dv(x->den, gcd(x->den, *l)), l)) return 0;
    }
    return 1;
}

static int whole(rat_t *r)
{
    int32_t l = 1, g = 0;
    term_t f;
    f.rad = 1;
    return lg(&r->n, &l, &g) && lg(&r->d, &l, &g) && rset(&f, l, g) &&
           scale(&r->n, &f) && scale(&r->d, &f);
}

/* A polynomial as emit() writes it. A fraction as N/D, D's first term
   positive; each side in parentheses unless it is one term, and for D a
   single variable at that: 1/2X would be X/2. */
static uint8_t answer(const ps_t *s, rat_t *r, uint8_t *out, uint8_t n)
{
    uint8_t pn = r->n.n > 1, pd;
    if (isconst(&r->d)) return emit(s, &r->n, out, n);
    if (!whole(r)) return 0;
    pd = r->d.n > 1 || r->d.t[0].num != 1 || univ(&r->d) == MULTI;
    if (pn) PUT(T_LPAR);
    if (!(n = emit(s, &r->n, out, n))) return 0;
    if (pn) PUT(T_RPAR);
    PUT(T_DIV);
    if (pd) PUT(T_LPAR);
    if (!(n = emit(s, &r->d, out, n))) return 0;
    if (pd) PUT(T_RPAR);
    return n;
}

/* Columns the answer takes on the home screen. Classic draws each token in
   one but "√(" 2, " or " sin( tan( log( 4, ln( e^( 3, logBASE( 8; a small
   letter or e (BB xx) is one. MathPrint (mp) draws each A/B stacked, without
   its parentheses (docs §7, measured), so a fraction is as wide as its wider
   side, plus one column kept for the ends of the bar. A power is raised, so
   '^' takes none. A sign between terms, or in front, takes one of its own.
   With a function in the answer, MathPrint still stacks each A/B (measured:
   log₅(X) over log₅(e)), but every name counts its Classic columns and every
   parenthesis one, although e^('s and a bar's are not drawn and a fraction's
   sides and an exponent are small type: never fewer than drawn. abs( is 4
   in Classic; MathPrint draws it and its ')' as a bar each (measured). */
static unsigned width(const uint8_t *t, uint8_t n, uint8_t mp)
{
    unsigned w = 0;
    uint8_t side = 0, top = 0, over = 0, depth = 0, fn = 0, k, c;
    for (k = 0; k < n; k++) fn |= t[k] == T_BB || t[k] == T_EXT || (t[k] >= T_LN && t[k] <= T_TAN);
    if (!mp) {
        for (k = 0; k < n; k++) {
            c = t[k];
            w += c == T_SQRT ? 2 : c == T_OR || c == T_SIN || c == T_TAN || c == T_LOG || c == T_ABS ? 4 :
                 c == T_LN || c == T_EXP ? 3 : c == T_EXT ? 8 : 1;
            k += c == T_BB || c == T_EXT;
        }
        return w;
    }
    for (k = 0; k <= n; k++) {
        c = k < n ? t[k] : T_ADD;               /* a last '+' ends the last term */
        if (c == T_BB || c == T_EXT || (c >= T_LN && c <= T_TAN)) {
            /* a letter, or a function's name and '(': its Classic columns */
            side += c == T_BB ? 1 : c == T_EXT ? 8 : c == T_LN || c == T_EXP ? 3 : 4;
            depth += c != T_BB;
            k += c == T_BB || c == T_EXT;
        }
        else if (c == T_LPAR || c == T_SQRT || c == T_ABS) { depth++; side += c != T_LPAR || fn; }
        else if (c == T_RPAR) { depth--; side += fn; }
        else if (c == T_DIV) { top = side; side = 0; over = 1; }
        else if (depth || (c != T_ADD && c != T_SUB && c != T_NEG && c != T_EQ && c != T_OR &&
                           c != T_LCOMMA && c != T_LBRACE && c != T_RBRACE))
            side += c != T_POW;
        else {
            w += (over && top > side ? top : side) + over + (k < n ? c == T_OR ? 4 : 1 : 0);
            top = side = over = 0;
        }
    }
    return w;
}

/* ---- commands: EXPAND( FACTOR( SOLVE( DERIV( ----
   Typed as letters (the SymCE menu, ALPHA+DOWN, types them), so the OS
   would read FACTOR(X) as F*A*C*T*O*R*X: a command is always answered here,
   with an answer or an error screen, never left to the OS. Over the
   rationals, with the ideas (not the code) of KhiCAS/Giac: */

#define SYMCE_ERR 0xFF        /* symce_engine: out holds "MESSAGE",0,"LINE",0,0 */
#define MAXF      8           /* factors of one answer */
/* In ranges: A=B LEFT..CSOLVE, and a variable too SOLVE..CSOLVE; exact roots
   SOLVE..POLYROOTS, as a list from CZEROS; a polynomial in the variable
   CPOLYROOTS..POLYQUOTIENT; two of them POLYREMAINDER..POLYGCD. From TOLN on,
   functions are read (fncmd()); past TEXPAND, tan(u) is sin(u)/cos(u). */
enum { C_LEFT = 1, C_RIGHT, C_SOLVE, C_CSOLVE, C_CZEROS, C_CPOLYROOTS, C_POLYROOTS,
       C_NROOTS, C_POLYCOEFFS, C_POLYDEGREE, C_POLYREMAINDER, C_POLYQUOTIENT, C_POLYGCD,
       C_EXPAND, C_COMDENOM, C_DERIV, C_FACTOR, C_CFACTOR,
       C_TOLN, C_TOLOGBASE, C_TEXPAND, C_TOEXP, C_TOSIN, C_TOCOS, C_TCOLLECT,
       /* the Calculus menu's: calc(), and from INTEGRAL on calc2() */
       C_DERIVAT, C_TANGENTLINE, C_NORMALLINE, C_CENTRALDIFF, C_TAYLOR, C_IMPDIF,
       C_INTEGRAL, C_LIMIT, C_SUM, C_PRODUCT, C_FMIN, C_FMAX, C_ARCLEN, C_SERIES, C_DOMINANTTERM,
       /* the Geometry menu's: geo(); up to REFLECT a line may be f alone */
       C_DISTANCE, C_SLOPE, C_PARALLEL, C_PERPENDICULAR, C_INTERSECT, C_REFLECT, C_MIDPOINT, C_LINE,
       C_PERPBISECTOR, C_PARTITION, C_AREA, C_PERIMETER, C_CENTROID, C_CIRCUMCENTER, C_ORTHOCENTER,
       C_INCENTER, C_HYPOT, C_LEG, C_CIRCLE, C_ROTATE, C_DILATE,
       NCMD = C_DILATE };
static const char cmds[] = "LEFT\0RIGHT\0SOLVE\0CSOLVE\0CZEROS\0CPOLYROOTS\0POLYROOTS\0"
    "NROOTS\0POLYCOEFFS\0POLYDEGREE\0POLYREMAINDER\0POLYQUOTIENT\0POLYGCD\0"
    "EXPAND\0COMDENOM\0DERIV\0FACTOR\0CFACTOR\0"
    "TOLN\0TOLOGBASE\0TEXPAND\0TOEXP\0TOSIN\0TOCOS\0TCOLLECT\0"
    "DERIVAT\0TANGENTLINE\0NORMALLINE\0CENTRALDIFF\0TAYLOR\0IMPDIF\0"
    "INTEGRAL\0LIMIT\0SUM\0PRODUCT\0FMIN\0FMAX\0ARCLEN\0SERIES\0DOMINANTTERM\0"
    "DISTANCE\0SLOPE\0PARALLEL\0PERPENDICULAR\0INTERSECT\0REFLECT\0MIDPOINT\0LINE\0"
    "PERPBISECTOR\0PARTITION\0AREA\0PERIMETER\0CENTROID\0CIRCUMCENTER\0ORTHOCENTER\0"
    "INCENTER\0HYPOT\0LEG\0CIRCLE\0ROTATE\0DILATE";

typedef struct {
    int8_t  d;                /* degree */
    int32_t a[8];             /* whole coefficients, a[0] first */
} ip_t;

typedef struct {
    poly_t  *f[MAXF];         /* each primitive, first term positive */
    uint8_t  e[MAXF];         /* its power; bit 7: below the bar */
    uint8_t  n;
    uint8_t  split;           /* 1: X²-2 is (X-√(2))(X+√(2)); 2: that, or fail (SOLVE); +4: over C */
    term_t   c;               /* times this number */
} fl_t;

/* "MSG",0,LINE then TAIL,0,0: 26 bytes at most (hook.c) */
static uint8_t error(uint8_t *out, const char *msg, const char *line, const char *tail)
{
    while ((*out++ = (uint8_t)*msg++)) ;
    while ((*out = (uint8_t)*line++)) out++;
    while ((*out++ = (uint8_t)*tail++)) ;
    *out = 0;
    return SYMCE_ERR;
}

/* c *= n/d, d > 0 */
static int cmul(term_t *c, int32_t n, int32_t d)
{
    term_t x;
    x.mono = 0; x.rad = 1;
    return rset(&x, n, d) && rmul(c, c, &x);
}

/* The divisors of n > 0, ascending, one per call: *i starts at 0; 0 after
   the last. Those up to √n by trial, then n over them, going back down. */
static int32_t nextdiv(int32_t n, int32_t *i)
{
    int32_t j = *i;
    if (j >= 0) {
        for (j++; j * j <= n; j++) if (n % j == 0) return *i = j;
    } else j = -j;
    while (--j > 0) if (n % j == 0 && j * j != n) { *i = -j; return n / j; }
    return 0;
}

/* q = a/b when that leaves whole coefficients: 1; 0 when not, -1 past LIM */
static int ipdiv(ip_t *q, const ip_t *a, const ip_t *b)
{
    int64_t r[8], c;
    int8_t i, j;
    for (i = 0; i <= a->d; i++) r[i] = a->a[i];
    q->d = a->d - b->d;
    for (i = q->d; i >= 0; i--) {
        c = r[i + b->d];
        if (c % b->a[b->d]) return 0;
        c /= b->a[b->d];
        if (c > LIM || c < -LIM) return -1;
        q->a[i] = (int32_t)c;
        for (j = 0; j <= b->d; j++) r[i + j] -= c * b->a[j];
    }
    for (i = 0; i < b->d; i++) if (r[i]) return 0;
    return 1;
}

/* p over the number that leaves whole coefficients with no common factor,
   first term positive; that number goes into the constant (or below it) */
static int prim(fl_t *fl, poly_t *p, uint8_t den)
{
    int32_t l = 1, g = 0, sg = lead(p)->num < 0 ? -1 : 1;
    term_t f;
    f.mono = 0; f.rad = 1;
    return lg(p, &l, &g) && rset(&f, sg * l, g) && scale(p, &f) &&
           (den ? cmul(&fl->c, sg * l, g) : cmul(&fl->c, sg * g, l));
}

static poly_t *slot(fl_t *fl)
{
    return fl->n < MAXF ? fl->f[fl->n] : 0;
}

/* The factor just written to slot(), to the power e: merged into an equal one */
static int commit(fl_t *fl, uint8_t e)
{
    poly_t *p = fl->f[fl->n];
    uint8_t k;
    for (k = 0; k < fl->n; k++)
        if (!((fl->e[k] ^ e) & 0x80) && same(fl->f[k], p)) { fl->e[k] += e & 0x7F; return 1; }
    fl->e[fl->n++] = e;
    return 1;
}

static int push(fl_t *fl, const poly_t *q, uint8_t den)
{
    poly_t *p = slot(fl);
    if (!p) return 0;
    *p = *q;
    return prim(fl, p, den) && commit(fl, den << 7 | 1);
}

/* a as a polynomial in the variable at nibble shift k; with a second one at
   shift u (0xFF: none), each term filled up to a's degree with it */
static int puship(fl_t *fl, const ip_t *a, uint8_t k, uint8_t u, uint8_t den)
{
    poly_t *p = slot(fl);
    term_t *t;
    int8_t j;
    if (!p) return 0;
    p->n = 0;
    for (j = 0; j <= a->d; j++) {
        if (!a->a[j]) continue;
        if (a->a[j] > LIM || a->a[j] < -LIM) return 0;
        t = &p->t[p->n++];
        t->num = a->a[j]; t->den = 1; t->rad = 1;
        t->mono = (unsigned)j << k | (u == 0xFF ? 0 : (unsigned)(a->d - j) << u);
    }
    return prim(fl, p, den) && commit(fl, den << 7 | 1);
}

/* αX²+βX+γ, irreducible. With a positive discriminant D = s²r, as KhiCAS
   does by default: (2αX+β-s√r)(2αX+β+s√r)/(4α), each over their gcd g.
   Over C (split & 4) a negative one too, -D = s²r: (2αX+β-s√(r)i)(...). */
static int pushquad(fl_t *fl, const ip_t *b, uint8_t k, uint8_t u, uint8_t den)
{
    int64_t D = (int64_t)b->a[1] * b->a[1] - 4 * (int64_t)b->a[2] * b->a[0];
    int32_t sq, r, g, al2 = 2 * b->a[2];
    uint32_t ci = 0;
    uint8_t h, sp = fl->split & 3;
    poly_t *p;
    term_t t;
    if (fl->split & 4 && D < 0) { D = -D; ci = IMAG; }
    if (!sp || D <= 0 || D > LIM)
        return sp == 2 && D > LIM ? 0 : puship(fl, b, k, u, den);
    r = sqfree((int32_t)D, &sq);
    g = gcd(sq, gcd(b->a[1], al2));
    for (h = 0; h < 2; h++) {
        if (!(p = slot(fl))) return 0;
        p->n = 0;
        t.den = 1; t.rad = 1;
        t.mono = 1u << k; t.num = al2 / g; addterm(p, &t);
        t.mono = u == 0xFF ? 0 : 1u << u;
        t.num = b->a[1] / g; addterm(p, &t);
        t.num = h ? sq / g : -sq / g; t.rad = (uint32_t)r | ci; addterm(p, &t);
        if (!prim(fl, p, den) || !commit(fl, den << 7 | 1)) return 0;
    }
    return den ? cmul(&fl->c, al2, g) && cmul(&fl->c, 2, g) : cmul(&fl->c, g, al2) && cmul(&fl->c, g, 2);
}

/* a: whole coefficients, no common factor, a[0] != 0, a[d] > 0. Rational
   roots p/q first, p | a[0], q | a[d], each tried only when q-p divides a(1)
   and q+p divides a(-1); then quadratic factors, Kronecker's way: g(0),
   g(1), g(-1) divide a(0), a(1), a(-1) and fix g. What is left has no factor
   of degree 1 or 2, so it is irreducible below degree 6. */
NOINLINE static int ipfactor(fl_t *fl, ip_t *a, uint8_t k, uint8_t u, uint8_t den)
{
    ip_t q, b;
    int32_t p, c, f1, fm, i0, i1, im, a0, v1, vm, al;
    int64_t z;
    int8_t j, sg;
    int r;
again:
    if (a->d < 2) return !a->d || puship(fl, a, k, u, den);
    for (f1 = fm = 0, j = 0; j <= a->d; j++) { f1 += a->a[j]; fm += j & 1 ? -a->a[j] : a->a[j]; }
    a0 = a->a[0] < 0 ? -a->a[0] : a->a[0];
    b.d = 1;
    for (i0 = 0; (p = nextdiv(a0, &i0)); )
        for (i1 = 0; (c = nextdiv(a->a[a->d], &i1)); )
            for (sg = -1; sg < 2; sg += 2) {
                if (gcd(p, c) != 1 || (c - sg * p ? f1 % (c - sg * p) : f1) ||
                    (c + sg * p ? fm % (c + sg * p) : fm)) continue;
                b.a[0] = -sg * p; b.a[1] = c;
                if ((r = ipdiv(&q, a, &b)) < 0) return 0;
                if (r) { if (!puship(fl, &b, k, u, den)) return 0; *a = q; goto again; }
            }
    if (a->d == 2) return pushquad(fl, a, k, u, den);
    if (a->d == 3) return puship(fl, a, k, u, den);
    /* ponytail: the search is about |a(0)a(1)a(-1)|^(1/2) steps; past this, SYMCE LIMIT */
    if (f1 < 0) f1 = -f1;
    if (fm < 0) fm = -fm;
    if ((z = (int64_t)a0 * f1) > 10000000000LL || z * fm > 10000000000LL) return 0;
    b.d = 2;
    for (i0 = 0; (p = nextdiv(a0, &i0)); )
        for (i1 = 0; (v1 = nextdiv(f1, &i1)); )
            for (im = 0; (vm = nextdiv(fm, &im)); )
                for (sg = 0; sg < 4; sg++) {            /* g(1) = ±v1, g(-1) = ±vm */
                    int32_t g1 = sg & 1 ? -v1 : v1, gm = sg & 2 ? -vm : vm;
                    if ((g1 - gm) & 1) continue;
                    al = (g1 + gm) / 2 - p;
                    if (!al || a->a[a->d] % al) continue;
                    c = al < 0 ? -1 : 1;                /* first term positive */
                    b.a[0] = c * p; b.a[1] = c * (g1 - gm) / 2; b.a[2] = c * al;
                    if ((r = ipdiv(&q, a, &b)) < 0) return 0;
                    if (r) { if (!pushquad(fl, &b, k, u, den)) return 0; *a = q; goto again; }
                }
    /* ponytail: two cubics (or a cubic and a quartic) would pass unseen here */
    return a->d < 6 && puship(fl, a, k, u, den);
}

/* p's coefficients as those of its variable at nibble shift k */
static void toip(ip_t *a, const poly_t *p, uint8_t k)
{
    const term_t *x;
    int8_t j;
    for (j = 0; j < 8; j++) a->a[j] = 0;
    a->d = 0;
    for (x = p->t; x < p->t + p->n; x++) {
        j = x->mono >> k & 15;
        a->a[j] = x->num;
        if (j > a->d) a->d = j;
    }
}

/* the terms of p with the variable at shift k to the power e, without it */
static void slice(poly_t *out, const poly_t *p, uint8_t k, uint8_t e)
{
    const term_t *x;
    out->n = 0;
    for (x = p->t; x < p->t + p->n; x++)
        if ((x->mono >> k & 15) == e) { out->t[out->n] = *x; out->t[out->n++].mono &= ~(15u << k); }
}

/* Grouping: p as a polynomial in the variable at shift k, the coefficient
   with the fewest terms, its monomial taken out, is g; when g divides every
   other coefficient it divides p: XY+X+Y+1 is (Y+1)(X+1). Then p /= g and 1;
   2 when not so, 0 on overflow. */
static int group(ps_t *s, poly_t *p, poly_t *g, uint8_t k)
{
    poly_t *h = tmp(s);
    const term_t *x;
    uint8_t e, best = 0, bn = MAXT + 1, cnt[8];
    int ok = 2, r;
    if (!h) return 0;
    for (e = 0; e < 8; e++) cnt[e] = 0;
    for (x = p->t; x < p->t + p->n; x++) cnt[x->mono >> k & 15]++;
    for (e = 0; e < 8; e++) if (cnt[e] && cnt[e] < bn) { bn = cnt[e]; best = e; }
    if (bn < 2 || bn == p->n) goto done;        /* a monomial coefficient; or no such variable */
    slice(g, p, k, best);
    divmono(g, content(g, g->t[0].mono));
    for (e = 0; e < 8; e++)
        if (cnt[e] && e != best) {
            slice(h, p, k, e);
            if ((r = divinto(s, h, g)) != 1) { ok = r ? 2 : 0; goto done; }
        }
    *h = *p;
    ok = divinto(s, h, g) == 1;
    *p = *h;
done:
    s->top--;
    return ok;
}

/* p, primitive, no monomial factor, two terms or more: its irreducible
   factors into fl. One variable: ipfactor. Else prime() settles it, or p is
   homogeneous in two variables -- Y = 1 leaves one, whose factors each get
   their Y back, (X²-Y²) through X²-1 -- or grouping splits it. Else 0. */
static int irred(ps_t *s, fl_t *fl, poly_t *p, uint8_t den)
{
    unsigned v = 0, w = 0;
    const term_t *x;
    poly_t *g;
    ip_t a;
    uint8_t k, u = 0xFF, nv = 0, top = s->top;
    int r = 0;
    if (hasrad(p)) return 0;
    for (x = p->t; x < p->t + p->n; x++) w |= x->mono;
    for (k = 4 * NV; k; ) if (w >> (k -= 4) & 15) { if (nv++) u = k; else v = k; }
    if (nv == 1 || (nv == 2 && homog(p) >= 0)) {
        toip(&a, p, (uint8_t)v);
        return ipfactor(fl, &a, (uint8_t)v, nv == 1 ? 0xFF : u, den);
    }
    if (prime(p)) return push(fl, p, den);
    if (!(g = tmp(s))) return 0;
    for (k = 0; k < 4 * NV; k += 4) {
        if (!(w >> k & 15)) continue;
        if (!(r = group(s, p, g, k))) break;
        if (r == 1) {
            r = prim(fl, g, den) && irred(s, fl, g, den) && prim(fl, p, den) && irred(s, fl, p, den);
            break;
        }
        r = 0;
    }
    s->top = top;
    return r;
}

/* p, not zero, into fl: its number, a factor per variable of its monomial,
   then the rest */
static int fac(ps_t *s, fl_t *fl, poly_t *p, uint8_t den)
{
    unsigned m;
    uint8_t k;
    poly_t *f;
    if (!prim(fl, p, den)) return 0;
    m = content(p, p->t[0].mono);
    divmono(p, m);
    for (k = 0; k < 4 * NV; k += 4)
        if (m >> k & 15) {
            if (!(f = slot(fl))) return 0;
            pconst(f, 1);
            f->t[0].mono = 1u << k;
            commit(fl, (uint8_t)(den << 7 | (m >> k & 15)));
        }
    return isconst(p) || irred(s, fl, p, den);
}

static int64_t isqrt(int64_t y)
{
    int64_t x = 0, b = (int64_t)1 << 30;
    for (; b; b >>= 1) if ((x + b) * (x + b) <= y) x += b;
    return x;
}

/* a term's coefficient times 2^20, its root included, near enough to sort by */
static int64_t tapprox(const term_t *x)
{
    return (x->rad == 1 ? (int64_t)1 << 20 : isqrt((int64_t)(x->rad & RNUM) << 40)) * x->num / x->den;
}

/* a number: p's terms have no variable. Its real part, or with im = IMAG
   its imaginary part. */
static int64_t approx(const poly_t *p, uint32_t im)
{
    const term_t *x;
    int64_t v = 0;
    for (x = p->t; x < p->t + p->n; x++) if ((x->rad & IMAG) == im) v += tapprox(x);
    return v;
}

/* p's second term in canonical order; p has two or more */
static const term_t *second(const poly_t *p)
{
    const term_t *l = lead(p), *x, *b = 0;
    for (x = p->t; x < p->t + p->n; x++) if (x != l && (!b || before(x, b))) b = x;
    return b;
}

/* Factor a goes after factor b: single variables first, then by degree,
   first variable, then second term: (X-√(3))(X-1)(X+1)(X+√(3)), (X-Y)(X+Y). */
static int after(const fl_t *fl, uint8_t a, uint8_t b)
{
    const poly_t *x = fl->f[a], *y = fl->f[b];
    const term_t *sx, *sy;
    uint8_t da = degree2(lead(x)), db = degree2(lead(y));
    if ((x->n > 1) != (y->n > 1)) return x->n > 1;
    if (da != db) return da > db;
    if (lead(x)->mono != lead(y)->mono) return lead(x)->mono < lead(y)->mono;
    if (x->n == 1) return 0;
    sx = second(x); sy = second(y);
    if (sx->mono != sy->mono) return sx->mono > sy->mono;
    return tapprox(sx) > tapprox(sy);
}

static uint8_t putpow(uint8_t *out, uint8_t n, int32_t e)
{
    if (e == 2) PUT(T_SQR);
    else if (e == 3) PUT(T_CUBE);
    else if (e > 3) { PUT(T_POW); n = number(out, n, e); }
    return n;
}

/* v > 0 as primes: 2²*3 */
static uint8_t primes(uint8_t *out, uint8_t n, int32_t v)
{
    int32_t p;
    uint8_t e, first = 1;
    if (v == 1) return number(out, n, 1);
    for (p = 2; v > 1; p++) {
        if (p * p > v) p = v;
        for (e = 0; v % p == 0; e++) v /= p;
        if (!e) continue;
        if (!first) PUT(T_MUL);
        first = 0;
        if (!(n = number(out, n, p)) || !(n = putpow(out, n, e))) return 0;
    }
    return n;
}

/* v is a prime power, or 1 */
static int ppow(int32_t v)
{
    int32_t p;
    for (p = 2; p * p <= v && v % p; p++) ;
    if (p * p > v) return 1;
    while (v % p == 0) v /= p;
    return v == 1;
}

/* c·F1^e1·F2^e2.../(D1^e...), each factor in parentheses unless it is one
   variable; the part below the bar in parentheses when it has two items */
NOINLINE static uint8_t putfl(const ps_t *s, fl_t *fl, uint8_t *out)
{
    uint8_t n = 0, k, j, t, o[MAXF], nf = 0, dn = fl->c.den != 1, bare;
    int32_t a = fl->c.num;
    for (k = 0; k < fl->n; k++) {
        o[k] = k;
        for (j = k; j && after(fl, o[j - 1], o[j]); j--) { t = o[j]; o[j] = o[j - 1]; o[j - 1] = t; }
        nf += !(fl->e[k] & 0x80);
        dn += fl->e[k] >> 7;
    }
    bare = a == 1 && nf == 1 && !dn;            /* X²+1, not (X²+1) */
    if (a < 0) { PUT(T_NEG); a = -a; }
    if ((a != 1 || !nf) && !(n = number(out, n, a))) return 0;
    for (j = 0; j < 2; j++) {
        for (k = 0; k < fl->n; k++) {
            poly_t *f = fl->f[o[k]];
            uint8_t e = fl->e[o[k]];
            if ((e >> 7) != j) continue;
            if (j && !n) return 0;
            bare &= e == 1;
            if (f->n > 1 && !bare) PUT(T_LPAR);
            if (!(n = emit(s, f, out, n))) return 0;
            if (f->n > 1 && !bare) PUT(T_RPAR);
            if (!(n = putpow(out, n, e & 0x7F))) return 0;
        }
        if (j || !dn) break;
        PUT(T_DIV);
        if (dn > 1) PUT(T_LPAR);
        if (fl->c.den != 1 && !(n = number(out, n, fl->c.den))) return 0;
    }
    if (dn > 1) PUT(T_RPAR);
    return n;
}

/* the power of the variable at nibble j in p; 0xFF when it is under a root */
static uint8_t vdeg(const poly_t *p, uint8_t j)
{
    const term_t *x;
    uint8_t d = 0;
    for (x = p->t; x < p->t + p->n; x++) {
        if (x->rad >> (RVAR + j) & 1) return 0xFF;
        if ((x->mono >> 4 * j & 15) > d) d = x->mono >> 4 * j & 15;
    }
    return d;
}

/* r = the root of f = AV + B, of degree 1 in V (nibble j): -B/A */
static int root(ps_t *s, rat_t *r, const poly_t *f, uint8_t j)
{
    const term_t *x;
    term_t t;
    r->n.n = r->d.n = 0;
    for (x = f->t; x < f->t + f->n; x++) {
        t = *x;
        if (x->mono >> 4 * j & 15) { t.mono -= 1u << 4 * j; if (!addterm(&r->d, &t)) return 0; }
        else { t.num = -t.num; if (!addterm(&r->n, &t)) return 0; }
    }
    return rationalize(s, r) && reduce(s, r) == 1;
}

/* f in one variable has no real root: every power even and every
   coefficient positive, or a quadratic with D < 0 */
static int noroot(const poly_t *f, uint8_t j)
{
    const term_t *x;
    int32_t c[3] = {0, 0, 0};
    uint8_t e, pos = 1;
    for (x = f->t; x < f->t + f->n; x++) {
        e = x->mono >> 4 * j & 15;
        if (e & 1 || x->num < 0) pos = 0;
        if (e < 3) c[e] = x->num;
    }
    return pos || (vdeg(f, j) == 2 && (int64_t)c[1] * c[1] < 4 * (int64_t)c[2] * c[0]);
}


/* A root as answer() writes it, but a sum of fractions over one bar:
   (√(5)-1)/2, (1-Y)/2 */
static uint8_t putroot(const ps_t *s, rat_t *r, uint8_t *out, uint8_t n)
{
    int32_t l = 1, g = 0;
    term_t f;
    if (s->dec || !isconst(&r->d) || r->n.n < 2) return answer(s, r, out, n);
    if (!lg(&r->n, &l, &g)) return 0;
    if (l == 1) return answer(s, r, out, n);
    f.mono = 0; f.rad = 1; f.num = l; f.den = 1;
    if (!scale(&r->n, &f)) return 0;
    PUT(T_LPAR);
    if (!(n = emit(s, &r->n, out, n))) return 0;
    PUT(T_RPAR);
    PUT(T_DIV);
    return number(out, n, l);
}

/* V·dP/dV, V at nibble j: each term times its power of V, a V under the root
   counting half. No negative powers, unlike dP/dV itself. */
static int xd(poly_t *out, const poly_t *p, uint8_t j)
{
    const term_t *x;
    int32_t e;
    out->n = 0;
    for (x = p->t; x < p->t + p->n; x++) {
        e = 2 * (x->mono >> 4 * j & 15) + (x->rad >> (RVAR + j) & 1);
        if (!e) continue;
        out->t[out->n] = *x;
        if (!cmul(&out->t[out->n++], e, 2)) return 0;
    }
    return 1;
}

/* acc = acc mod f, both polynomials in the variable at nibble j with the
   others as coefficients; the quotient adds into q, if any. f's first coefficient in
   V must be a number, so nothing is ever divided by a polynomial: 0 when it
   is not, or on overflow. Each step takes away a term of acc of degree m or
   more in V and adds only terms of lower degree, so it ends. */
NOINLINE static int vrem(poly_t *acc, const poly_t *f, poly_t *q, uint8_t j)
{
    const term_t *d = 0, *x, *y;
    term_t inv, c, t;
    uint8_t m = vdeg(f, j);
    for (x = f->t; x < f->t + f->n; x++)
        if ((x->mono >> 4 * j & 15) == m) { if (d) return 0; d = x; }
    if (d->mono != (unsigned)m << 4 * j || d->rad != 1) return 0;
    rinv(&inv, d);
    for (;;) {
        for (x = acc->t; x < acc->t + acc->n && (x->mono >> 4 * j & 15) < m; x++) ;
        if (x == acc->t + acc->n) return 1;
        c.mono = x->mono - d->mono;
        if (!rmul(&c, x, &inv) || (q && !addterm(q, &c))) return 0;
        for (y = f->t; y < f->t + f->n; y++) {
            t.mono = c.mono + y->mono;
            if (!rmul(&t, &c, y) || t.mono & OVF) return 0;
            t.num = -t.num;
            if (!addterm(acc, &t)) return 0;
        }
    }
}

/* ---- NROOTS: a float of its own, m·2^e with 2^61 <= |m| < 2^62, or m = 0.
   Integer steps only, the same on the host and on the calculator, so both
   print the same digits; CEdev's double is 32 bits, its long double 6 KB. */
typedef struct { int64_t m; int e; } fp_t;
typedef struct { fp_t r, i; } cx_t;
#define FTOP ((int64_t)1 << 62)

/* Speed: CEdev's int64 helpers go a bit at a time -- a 32x32 multiply ~7900
   cycles, a shift by 31 ~4000 -- so on the calculator the hot steps are
   assembly below, byte at a time, multiplying with mlt; the host runs the
   plain C after it. The same integers either way (w[0] is the low half: both
   are little-endian). */
typedef union { int64_t v; uint64_t u; uint32_t w[2]; } w64_t;

/* x's top bit; a branch, as clang's x >> 31 is 31 one-bit shifts here */
static uint32_t top(uint32_t x)
{
    if ((int32_t)x < 0) { __asm__ volatile (""); return 1; }
    return 0;
}

/* r = x*y, all 64 bits */
#ifdef __ez80__
void symce_umul(w64_t *r, uint32_t x, uint32_t y);
/* Byte by byte, column by column: HL sums a column's mlt products and the
   carry in, its low byte goes out and HL >>= 8 (through the stack, over a
   zero byte pushed first). DEU is 0 throughout: only d, e and mlt touch it. */
#define UM(i, j) "\tld\td, (iy + " #i ")\n\tld\te, (iy + " #j ")\n\tmlt\tde\n\tadd\thl, de\n"
#define UMOUT "\tld\ta, l\n\tld\t(bc), a\n\tinc\tbc\n\tpush\thl\n\tinc\tsp\n\tpop\thl\n\tdec\tsp\n"
__asm__("\t.section\t.text._symce_umul,\"ax\",@progbits\n"
        "\t.globl\t_symce_umul\n"
        "_symce_umul:\n"
        "\tld\tiy, 0\n\tadd\tiy, sp\n"         /* r at iy+3, x at iy+6..9, y at iy+12..15 */
        "\tld\tbc, (iy + 3)\n"
        "\tld\thl, 0\n\tpush\thl\n\tld\tde, 0\n"
        UM(6, 12) UMOUT
        UM(6, 13) UM(7, 12) UMOUT
        UM(6, 14) UM(7, 13) UM(8, 12) UMOUT
        UM(6, 15) UM(7, 14) UM(8, 13) UM(9, 12) UMOUT
        UM(7, 15) UM(8, 14) UM(9, 13) UMOUT
        UM(8, 15) UM(9, 14) UMOUT
        UM(9, 15) UMOUT
        "\tld\ta, l\n\tld\t(bc), a\n"
        "\tpop\thl\n\tret\n");

/* p = the top of a·b as fmul takes it (the C below says what), over a
   32-byte frame at ix: ah 0, al 4, bh 8, bl 12, s 16, t 24; saved ix 32,
   p at ix+38, a at ix+41, b at ix+44 */
void symce_mul62(w64_t *p, const int64_t *a, const int64_t *b);
#define M62SPLIT(src, h, l) "\tld\tiy, (ix + " #src ")\n\tld\thl, (iy)\n\tld\t(ix + " #l "), hl\n" \
    "\tld\ta, (iy + 3)\n\tld\tc, a\n\tand\ta, 127\n\tld\t(ix + " #l " + 3), a\n\tld\ta, c\n\trla\n" \
    "\tld\ta, (iy + 4)\n\trla\n\tld\t(ix + " #h "), a\n\tld\ta, (iy + 5)\n\trla\n\tld\t(ix + " #h " + 1), a\n" \
    "\tld\ta, (iy + 6)\n\trla\n\tld\t(ix + " #h " + 2), a\n\tld\ta, (iy + 7)\n\trla\n\tld\t(ix + " #h " + 3), a\n"
/* (ix+d) = x·y unsigned, x and y the 4 bytes at ix+x and ix+y */
#define M62UM(d, x, y) "\tld\thl, (ix + " #y " + 3)\n\tpush\thl\n\tld\thl, (ix + " #y ")\n\tpush\thl\n" \
    "\tld\thl, (ix + " #x " + 3)\n\tpush\thl\n\tld\thl, (ix + " #x ")\n\tpush\thl\n" \
    "\tlea\thl, ix + " #d "\n\tpush\thl\n\tcall\t_symce_umul\n\tld\thl, 15\n\tadd\thl, sp\n\tld\tsp, hl\n"
/* high half of (ix+d) -= the 4 bytes at ix+x, when the top bit at ix+s is set */
#define M62FIX(d, x, s) "\tbit\t7, (ix + " #s " + 3)\n\tjr\tz, 1f\n" \
    "\tld\ta, (ix + " #d " + 4)\n\tsub\ta, (ix + " #x ")\n\tld\t(ix + " #d " + 4), a\n" \
    "\tld\ta, (ix + " #d " + 5)\n\tsbc\ta, (ix + " #x " + 1)\n\tld\t(ix + " #d " + 5), a\n" \
    "\tld\ta, (ix + " #d " + 6)\n\tsbc\ta, (ix + " #x " + 2)\n\tld\t(ix + " #d " + 6), a\n" \
    "\tld\ta, (ix + " #d " + 7)\n\tsbc\ta, (ix + " #x " + 3)\n\tld\t(ix + " #d " + 7), a\n1:\n"
#define M62ADD(k) "\tld\ta, (ix + 16 + " #k ")\n\tadc\ta, (ix + 24 + " #k ")\n\tld\t(iy + " #k "), a\n"
__asm__("\t.section\t.text._symce_mul62,\"ax\",@progbits\n"
        "\t.globl\t_symce_mul62\n"
        "_symce_mul62:\n"
        "\tpush\tix\n\tld\tix, -32\n\tadd\tix, sp\n\tld\tsp, ix\n"
        M62SPLIT(41, 0, 4) M62SPLIT(44, 8, 12)
        M62UM(16, 0, 12) M62FIX(16, 12, 0)     /* s = ah·bl */
        M62UM(24, 4, 8) M62FIX(24, 4, 8)       /* t = al·bh */
        "\tlea\tiy, ix + 16\n\tor\ta, a\n"      /* s += t */
        M62ADD(0) M62ADD(1) M62ADD(2) M62ADD(3) M62ADD(4) M62ADD(5) M62ADD(6) M62ADD(7)
        "\tld\ta, (ix + 19)\n\trla\n"           /* t = s >> 31 */
        "\tld\ta, (ix + 20)\n\trla\n\tld\t(ix + 24), a\n\tld\ta, (ix + 21)\n\trla\n\tld\t(ix + 25), a\n"
        "\tld\ta, (ix + 22)\n\trla\n\tld\t(ix + 26), a\n\tld\ta, (ix + 23)\n\trla\n\tld\t(ix + 27), a\n"
        "\tsbc\ta, a\n\tld\t(ix + 28), a\n\tld\t(ix + 29), a\n\tld\t(ix + 30), a\n\tld\t(ix + 31), a\n"
        M62UM(16, 0, 8) M62FIX(16, 8, 0) M62FIX(16, 0, 8)     /* s = ah·bh */
        "\tld\tiy, (ix + 38)\n\tor\ta, a\n"     /* p = s + t */
        M62ADD(0) M62ADD(1) M62ADD(2) M62ADD(3) M62ADD(4) M62ADD(5) M62ADD(6) M62ADD(7)
        "\tld\thl, 32\n\tadd\thl, sp\n\tld\tsp, hl\n\tpop\tix\n\tret\n");

/* v[0] = x, v[1] = y, v[2] = q (0 in): 62 times q <<= 1, then x -= y and
   q |= 1 if x >= y, then x <<= 1 */
void symce_div62(w64_t *v);
#define D62SH(o) "\tsla\t(iy + " #o ")\n\trl\t(iy + " #o " + 1)\n\trl\t(iy + " #o " + 2)\n\trl\t(iy + " #o " + 3)\n" \
    "\trl\t(iy + " #o " + 4)\n\trl\t(iy + " #o " + 5)\n\trl\t(iy + " #o " + 6)\n\trl\t(iy + " #o " + 7)\n"
#define D62CP(k) "\tld\ta, (iy + " #k ")\n\tsbc\ta, (iy + 8 + " #k ")\n"
#define D62SB(k) "\tld\ta, (iy + " #k ")\n\tsbc\ta, (iy + 8 + " #k ")\n\tld\t(iy + " #k "), a\n"
__asm__("\t.section\t.text._symce_div62,\"ax\",@progbits\n"
        "\t.globl\t_symce_div62\n"
        "_symce_div62:\n"
        "\tld\tiy, 0\n\tadd\tiy, sp\n\tld\tiy, (iy + 3)\n\tld\tb, 62\n"
        "1:\n" D62SH(16)
        "\tor\ta, a\n" D62CP(0) D62CP(1) D62CP(2) D62CP(3) D62CP(4) D62CP(5) D62CP(6) D62CP(7)
        "\tjr\tc, 2f\n"
        "\tor\ta, a\n" D62SB(0) D62SB(1) D62SB(2) D62SB(3) D62SB(4) D62SB(5) D62SB(6) D62SB(7)
        "\tset\t0, (iy + 16)\n"
        "2:\n" D62SH(0)
        "\tdec\tb\n\tjp\tnz, 1b\n\tret\n");

/* m (not 0) shifted to 2^61 <= |m| < 2^62, its magnitude truncated going
   down; returns the shift, its bit length less 62 */
int symce_norm(int64_t *m);
#define NRNEG "\tld\thl, (ix + 3)\n\tld\tb, 8\n\tor\ta, a\n1:\n\tld\ta, 0\n\tsbc\ta, (hl)\n\tld\t(hl), a\n\tinc\thl\n\tdjnz\t1b\n"
__asm__("\t.section\t.text._symce_norm,\"ax\",@progbits\n"
        "\t.globl\t_symce_norm\n"
        "_symce_norm:\n"
        "\tpush\tix\n\tld\tix, 3\n\tadd\tix, sp\n"   /* m at ix+3 */
        "\tld\tiy, (ix + 3)\n\tld\ta, (iy + 7)\n\tld\tc, a\n\trla\n\tjr\tnc, 3f\n"
        NRNEG
        "3:\n\tld\thl, (ix + 3)\n\tld\tde, 7\n\tadd\thl, de\n"   /* hl at the top byte */
        "\tld\td, 64\n"                           /* d = bit length */
        "4:\n\tld\ta, (hl)\n\tor\ta, a\n\tjr\tnz, 5f\n\tdec\thl\n\tld\ta, d\n\tsub\ta, 8\n\tld\td, a\n\tjr\t4b\n"
        "5:\n\trla\n\tjr\tc, 6f\n\tdec\td\n\tjr\t5b\n"
        "6:\n\tld\ta, d\n\tsub\ta, 62\n\tld\te, a\n\tjp\tz, 9f\n\tjp\tc, 7f\n"
        /* down 1 or 2 bits */
        "\tld\tb, a\n"
        "8:\n\tsrl\t(iy + 7)\n\trr\t(iy + 6)\n\trr\t(iy + 5)\n\trr\t(iy + 4)\n\trr\t(iy + 3)\n\trr\t(iy + 2)\n\trr\t(iy + 1)\n\trr\t(iy)\n\tdjnz\t8b\n\tjp\t9f\n"
        /* up 62 - d bits: whole bytes, then bits */
        "7:\n\tneg\n\tld\tb, a\n"
        "10:\n\tld\ta, b\n\tcp\ta, 8\n\tjr\tc, 11f\n"
        "\tld\ta, (iy + 6)\n\tld\t(iy + 7), a\n\tld\ta, (iy + 5)\n\tld\t(iy + 6), a\n\tld\ta, (iy + 4)\n\tld\t(iy + 5), a\n"
        "\tld\ta, (iy + 3)\n\tld\t(iy + 4), a\n\tld\ta, (iy + 2)\n\tld\t(iy + 3), a\n\tld\ta, (iy + 1)\n\tld\t(iy + 2), a\n"
        "\tld\ta, (iy)\n\tld\t(iy + 1), a\n\tld\t(iy), 0\n\tld\ta, b\n\tsub\ta, 8\n\tld\tb, a\n\tjr\t10b\n"
        "11:\n\tinc\tb\n\tjr\t13f\n"
        "12:\n\tsla\t(iy)\n\trl\t(iy + 1)\n\trl\t(iy + 2)\n\trl\t(iy + 3)\n\trl\t(iy + 4)\n\trl\t(iy + 5)\n\trl\t(iy + 6)\n\trl\t(iy + 7)\n"
        "13:\n\tdjnz\t12b\n"
        "9:\n\tbit\t7, c\n\tjr\tz, 14f\n"
        NRNEG
        "14:\n\tld\ta, e\n\trla\n\tsbc\thl, hl\n\tld\tl, e\n\tpop\tix\n\tret\n");
/* *m >>= d, arithmetic, d < 63: whole bytes, then bits */
void symce_sar(w64_t *m, uint8_t d);
__asm__("\t.section\t.text._symce_sar,\"ax\",@progbits\n"
        "\t.globl\t_symce_sar\n"
        "_symce_sar:\n"
        "\tld\tiy, 0\n\tadd\tiy, sp\n\tld\tc, (iy + 6)\n\tld\tiy, (iy + 3)\n"
        "\tld\ta, (iy + 7)\n\trla\n\tsbc\ta, a\n\tld\te, a\n"       /* the sign's byte */
        "\tld\ta, c\n\tsrl\ta\n\tsrl\ta\n\tsrl\ta\n\tjr\tz, 2f\n\tld\tb, a\n"
        "1:\n\tld\ta, (iy + 1)\n\tld\t(iy), a\n\tld\ta, (iy + 2)\n\tld\t(iy + 1), a\n\tld\ta, (iy + 3)\n\tld\t(iy + 2), a\n"
        "\tld\ta, (iy + 4)\n\tld\t(iy + 3), a\n\tld\ta, (iy + 5)\n\tld\t(iy + 4), a\n\tld\ta, (iy + 6)\n\tld\t(iy + 5), a\n"
        "\tld\ta, (iy + 7)\n\tld\t(iy + 6), a\n\tld\t(iy + 7), e\n\tdjnz\t1b\n"
        "2:\n\tld\ta, c\n\tand\ta, 7\n\tret\tz\n\tld\tb, a\n"
        "3:\n\tsra\t(iy + 7)\n\trr\t(iy + 6)\n\trr\t(iy + 5)\n\trr\t(iy + 4)\n\trr\t(iy + 3)\n\trr\t(iy + 2)\n\trr\t(iy + 1)\n\trr\t(iy)\n\tdjnz\t3b\n"
        "\tret\n");
#else
/* the host: the same integers, the plain way */
static void symce_mul62(w64_t *p, const int64_t *a, const int64_t *b)
{
    int64_t ah = *a >> 31, bh = *b >> 31, al = *a & 0x7FFFFFFF, bl = *b & 0x7FFFFFFF;
    p->v = ah * bh + ((ah * bl + al * bh) >> 31);
}
static void symce_div62(w64_t *v)
{
    uint8_t k;
    for (k = 0; k < 62; k++) {
        v[2].u <<= 1;
        if (v[0].u >= v[1].u) { v[0].u -= v[1].u; v[2].u |= 1; }
        v[0].u <<= 1;
    }
}
static void symce_sar(w64_t *m, uint8_t d) { m->v >>= d; }
static int symce_norm(int64_t *m)
{
    uint64_t u = *m < 0 ? 0 - (uint64_t)*m : (uint64_t)*m;
    int n = 0;
    while (n < 64 && u >> n) n++;
    u = n > 62 ? u >> (n - 62) : u << (62 - n);
    *m = *m < 0 ? (int64_t)(0 - u) : (int64_t)u;
    return n - 62;
}
#endif

static void wneg(w64_t *x) { x->w[0] = 0 - x->w[0]; x->w[1] = ~x->w[1] + !x->w[0]; }

/* a = m·2^e with 2^61 <= |m| < 2^62: |m| shifted by its bit length less 62,
   down (truncating, as m /= 2 did) or up */
static void fset(fp_t *a, int64_t m, int e)
{
    a->m = m; a->e = e;
    if (m) a->e += symce_norm(&a->m);
}

static void fadd(fp_t *r, const fp_t *a, const fp_t *b)
{
    const fp_t *t;
    w64_t u;
    int d;
    if (!a->m || (b->m && b->e > a->e)) { t = a; a = b; b = t; }
    d = a->e - b->e;
    u.v = 0;
    if (b->m && d < 63) { u.v = b->m; symce_sar(&u, d); }
    fset(r, a->m + u.v, a->e);
}

/* the top of the product, from 31-bit halves: within 2^-59 */
static void fmul(fp_t *r, const fp_t *a, const fp_t *b)
{
    w64_t s;
    symce_mul62(&s, &a->m, &b->m);
    fset(r, s.v, a->e + b->e + 62);
}

static void fdiv(fp_t *r, const fp_t *a, const fp_t *b)
{
    /* |a|/|b| a bit at a time, 62 of them: x < 2y throughout */
    w64_t v[3];
    uint8_t neg = 0;
    v[0].v = a->m; v[1].v = b->m; v[2].v = 0;
    if (top(v[0].w[1])) { wneg(&v[0]); neg = 1; }
    if (top(v[1].w[1])) { wneg(&v[1]); neg ^= 1; }
    symce_div62(v);
    if (neg) wneg(&v[2]);
    fset(r, v[2].v, a->e - b->e - 61);
}

static void cxmul(cx_t *z, const cx_t *a, const cx_t *b)
{
    cx_t t;
    fp_t u;
    fmul(&t.r, &a->r, &b->r); fmul(&u, &a->i, &b->i); u.m = -u.m; fadd(&t.r, &t.r, &u);
    fmul(&t.i, &a->r, &b->i); fmul(&u, &a->i, &b->r); fadd(&t.i, &t.i, &u);
    *z = t;
}

static void cxsub(cx_t *z, const cx_t *a, const cx_t *b)
{
    fp_t u;
    u = b->r; u.m = -u.m; fadd(&z->r, &a->r, &u);
    u = b->i; u.m = -u.m; fadd(&z->i, &a->i, &u);
}

/* the size of z, as the exponent of its larger part */
static int fe(const cx_t *z)
{
    int a = z->r.m ? z->r.e : -30000, b = z->i.m ? z->i.e : -30000;
    return a > b ? a : b;
}

/* The m roots of z^m + c[m-1]z^(m-1) + ... + c[0], c[0] not 0, by
   Durand-Kerner, each new guess used at once. 0 when they do not settle. */
NOINLINE static int dk(cx_t *z, const fp_t *c, uint8_t m)
{
    cx_t v, d, t;
    fp_t u;
    uint16_t it;
    uint8_t k, e, done;
    fset(&t.r, 6, -4); fset(&t.i, 15, -4);      /* (6+15i)/16, no root of unity */
    fset(&z[0].r, 1, (c[0].e + 62) / m);        /* |c0|^(1/m), their mean size */
    z[0].i.m = 0;
    for (k = 1; k < m; k++) cxmul(&z[k], &z[k - 1], &t);
    for (it = 0; it < 500; it++) {
        done = 1;
        for (k = 0; k < m; k++) {
            fset(&v.r, 1, 0); v.i.m = 0;
            for (e = m; e--; ) { cxmul(&v, &v, &z[k]); fadd(&v.r, &v.r, &c[e]); }
            fset(&d.r, 1, 0); d.i.m = 0;
            for (e = 0; e < m; e++) if (e != k) { cxsub(&t, &z[k], &z[e]); cxmul(&d, &d, &t); }
            if (!d.r.m && !d.i.m) return 0;
            fmul(&u, &d.r, &d.r); fmul(&t.r, &d.i, &d.i); fadd(&u, &u, &t.r);
            d.i.m = -d.i.m;                     /* v/d = v·conj(d)/|d|² */
            cxmul(&v, &v, &d);
            fdiv(&v.r, &v.r, &u); fdiv(&v.i, &v.i, &u);
            cxsub(&z[k], &z[k], &v);
            if (fe(&z[k]) > 40) return 0;       /* past 2^100: diverging */
            if (fe(&v) > fe(&z[k]) - 48) done = 0;
        }
        if (done) return 1;
    }
    return 0;
}

static int64_t toint(const fp_t *v, uint8_t half)   /* v >= 0 rounded down, or with half 1 to nearest */
{
    int s = -v->e;
    w64_t t;
    if (!v->m || s > 62) return 0;
    if (s <= 0) return FTOP;
    t.v = v->m;                                 /* (m + half·2^(s-1)) >> s, the same floor */
    symce_sar(&t, s - 1);
    return (t.v + half) >> 1;
}

static const int64_t p10[11] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000,
                                1000000000, 10000000000LL};

/* v >= 0 to dg (4..10) significant digits, TI style: .5, 1.414213562, 20 */
static uint8_t putfp(uint8_t *out, uint8_t n, fp_t v, uint8_t dg)
{
    int64_t q;
    int e = 0;
    uint8_t d[10], k, len = dg;
    fp_t ten;
    if (!v.m) { PUT(0x30); return n; }
    fset(&ten, 10, 0);
    while (toint(&v, 0) >= p10[dg]) { fdiv(&v, &v, &ten); e++; }
    while (toint(&v, 0) < p10[dg - 1]) { fmul(&v, &v, &ten); e--; }
    if ((q = toint(&v, 1)) == p10[dg]) { q = p10[dg - 1]; e++; }
    for (k = 0; k < dg; k++)                    /* by subtraction: the eZ80 divides a bit at a time */
        for (d[k] = 0; q >= p10[dg - 1 - k]; d[k]++) q -= p10[dg - 1 - k];
    while (len > 1 && !d[len - 1]) { len--; e++; }
    e += len;                                   /* digits before the point */
    if (e <= 0) PUT(T_DOT);
    for (; e < 0; e++) PUT(0x30);
    for (k = 0; k < len || k < e; k++) {
        if (k == e && k) PUT(T_DOT);
        PUT(0x30 + (k < len ? d[k] : 0));
    }
    return n;
}

/* {z0,z1,...}: a+bi, a, bi, i */
NOINLINE static uint8_t putlist(uint8_t *out, const cx_t *z, uint8_t m, uint8_t dg)
{
    uint8_t n = 0, k, h;
    fp_t v;
    PUT(T_LBRACE);
    for (k = 0; k < m; k++) {
        if (k) PUT(T_LCOMMA);
        v = z[k].r;
        if (v.m || !z[k].i.m) {
            if (v.m < 0) { PUT(T_NEG); v.m = -v.m; }
            if (!(n = putfp(out, n, v, dg))) return 0;
        }
        if (!(v = z[k].i).m) continue;
        if (v.m < 0) { PUT(z[k].r.m ? T_SUB : T_NEG); v.m = -v.m; }
        else if (z[k].r.m) PUT(T_ADD);
        h = n;
        if (!(n = putfp(out, n, v, dg))) return 0;
        if (n == h + 1 && out[h] == 0x31) n = h;    /* i, not 1i */
        PUT(T_I);
    }
    PUT(T_RBRACE);
    return n;
}

/* z goes after y: real roots first, then by real part, then imaginary; parts
   equal to 2^-40 of the root are equal, as a conjugate pair's real parts */
NOINLINE static int later(const cx_t *z, const cx_t *y)
{
    fp_t u, d;
    if (!z->i.m != !y->i.m) return !y->i.m;
    u = y->r; u.m = -u.m; fadd(&d, &z->r, &u);
    if (d.m && d.e > fe(z) - 40) return d.m > 0;
    u = y->i; u.m = -u.m; fadd(&d, &z->i, &u);
    return d.m > 0;
}

/* NROOTS: every root of p, a polynomial in the variable at nibble j alone and
   not 0, as decimals. A zero root comes out first, then the squarefree part,
   p/gcd(p, p'), goes to dk(), or p itself when that gcd overflows. Two roots
   within 2^-20 of each other are refused: a repeated root dk() found twice,
   or a cluster too tight to tell apart. So a part under 2^-32 of its root is
   0 (real, or imaginary): a true one would put its conjugate that close. As
   many digits as fit, 10 down to 4, else TOO WIDE. */
NOINLINE static uint8_t nroots(ps_t *s, poly_t *p, uint8_t j, uint8_t *out)
{
    poly_t *g = tmp(s), *d = tmp(s);
    rat_t *w = get(s);
    cx_t *z = (cx_t *)w, t;
    fp_t *c = (fp_t *)(z + 8);
    const term_t *x;
    unsigned m0;
    uint8_t m, k, e, n;
    int ez;
    if (!w || univ(p) & ~(15u << 4 * j) || hasrad(p)) return 0;
    m0 = content(p, p->t[0].mono);
    divmono(p, m0);
    *g = *p;
    if (xd(d, p, j) && (divmono(d, 1u << 4 * j), monic(g, 0)) && gcdinto(s, g, d) && divinto(s, p, g) != 1)
        return 0;
    m = vdeg(p, j);
    for (k = 0; k <= m; k++) c[k].m = 0;
    for (x = p->t; x < p->t + p->n; x++) {
        e = x->mono >> 4 * j & 15;
        fset(&c[e], x->num, 0); fset(&t.r, x->den, 0); fdiv(&c[e], &c[e], &t.r);
    }
    for (k = 0; k < m; k++) fdiv(&c[k], &c[k], &c[m]);   /* monic */
    if (m && !dk(z, c, m)) return 0;
    for (k = 0; k < m; k++)
        for (e = 0; e < k; e++) { cxsub(&t, &z[k], &z[e]); if (fe(&t) < fe(&z[k]) - 20) return 0; }
    if (m0) { z[m].r.m = z[m].i.m = 0; m++; }
    for (k = 0; k < m; k++) {
        ez = fe(&z[k]) - 32;
        if (z[k].i.e < ez) z[k].i.m = 0;
        if (z[k].r.e < ez) z[k].r.m = 0;
        for (e = k; e && later(&z[e - 1], &z[e]); e--) { t = z[e]; z[e] = z[e - 1]; z[e - 1] = t; }
    }
    for (e = 10; e > 3; e--)
        if ((n = putlist(out, z, m, e)) && width(out, n, s->mp) <= SCR_WIDTH) return n;
    return error(out, "TOO WIDE", "", "");
}

/* ---- Convert Expression and Trigonometry: TOLN .. TCOLLECT ----
   Each value of a function is a kernel, a variable of its own to the
   polynomial code, its argument kept at the top of the pool (keep()) for
   good: sin(X)² is the kernel sin(X) squared. The ideas are the usual ones
   (KhiCAS/Giac's texpand, tcollect, lin; the Nspire's own examples), none
   of the code. */

static int same2(const rat_t *a, const rat_t *b)
{
    return same(&a->n, &b->n) && same(&a->d, &b->d);
}

/* r, kept for good at the top of the pool, where get() does not reach; an
   equal one already kept is returned instead, so equal arguments are the
   same pointer */
static rat_t *keep(ps_t *s, const rat_t *r)
{
    rat_t *k;
    for (k = s->pool + s->lim; k < s->pool + NPOOL; k++) if (same2(k, r)) return k;
    if (s->lim <= s->top) return 0;
    k = s->pool + --s->lim;
    *k = *r;
    return k;
}

/* The kernel f of a (kept here) and b (kept already): its nibble, found or
   added after the others; 0xFF when there is no nibble left */
static uint8_t kern(ps_t *s, uint8_t f, rat_t *a, rat_t *b)
{
    uint8_t k;
    if (a && !(a = keep(s, a))) return 0xFF;
    for (k = 0; k < s->nk && (s->kf[k] != f || s->ka[k] != a || s->kb[k] != b); k++) ;
    if (k == s->nk) {
        if (s->nv + k >= NV) return 0xFF;
        s->kf[k] = f; s->ka[k] = a; s->kb[k] = b;
        s->nk++;
    }
    return KN(s, k);
}

/* out = that kernel */
static int kvar(ps_t *s, rat_t *out, uint8_t f, rat_t *a, rat_t *b)
{
    uint8_t j = kern(s, f, a, b);
    if (j == 0xFF) return 0;
    rconst(out, 1);
    out->n.t[0].mono = 1u << 4 * j;
    return 1;
}

/* some kernel is in r, under a root too */
static int haskern(const ps_t *s, const rat_t *r)
{
    const term_t *x;
    unsigned m = 0;
    uint32_t v = 0;
    if (!s->nk) return 0;
    for (x = r->n.t; x < r->n.t + r->n.n; x++) { m |= x->mono; v |= x->rad; }
    for (x = r->d.t; x < r->d.t + r->d.n; x++) { m |= x->mono; v |= x->rad; }
    return (m & 0xFFFFFFu >> 4 * s->nv) || (v >> RVAR & ((1u << (NV - s->nv)) - 1));
}

/* r is the whole number v, not 0 */
static int isnum(const rat_t *r, int32_t v)
{
    const term_t *t = r->n.t;
    return r->n.n == 1 && isconst(&r->d) && !t->mono && t->num == v && t->den == 1 && t->rad == 1;
}

/* r is e */
static int is_e(const ps_t *s, const rat_t *r)
{
    const term_t *t = r->n.t;
    uint8_t k;
    if (r->n.n != 1 || !isconst(&r->d) || t->num != 1 || t->den != 1 || t->rad != 1) return 0;
    for (k = 0; k < s->nk; k++) if (s->kf[k] == K_E && t->mono == 1u << 4 * KN(s, k)) return 1;
    return 0;
}

/* r is not a number <= 0: with a variable in it, it is taken to be positive */
static int pos(const rat_t *r)
{
    const term_t *x;
    if (!isconst(&r->d)) return 1;
    for (x = r->n.t; x < r->n.t + r->n.n; x++) if (x->mono) return 1;
    return approx(&r->n, 0) > 0;
}

/* The sign of r, proved: 1, -1, 0; 2 when approx() is too close to 0 to
   tell, 3 when r is no real number (a variable or kernel in it). approx()
   is off by less than a unit per term and |c| more (isqrt, the division),
   in units of 2^-20; distinct square roots are independent over the
   rationals, so r with terms is not 0. */
static int nsign(const rat_t *r)
{
    const term_t *x;
    int64_t v, err = 2;
    if (!isconst(&r->d) || r->d.t[0].rad != 1) return 3;
    for (x = r->n.t; x < r->n.t + r->n.n; x++) {
        if (x->mono || x->rad & ~RNUM) return 3;
        err += (x->num < 0 ? -x->num : x->num) / x->den + 2;
    }
    if (!r->n.n) return 0;
    v = approx(&r->n, 0);
    if (r->d.t[0].num < 0) v = -v;
    return v > err ? 1 : v < -err ? -1 : 2;
}

/* out = the logarithm of u to base t (0: e): 1 when u is t, 0 when u is 1 */
static int mklog(ps_t *s, rat_t *out, rat_t *u, rat_t *t)
{
    if (t ? same2(u, t) : is_e(s, u)) { rconst(out, 1); return 1; }
    if (isnum(u, 1)) { rconst(out, 0); return 1; }
    if ((haskern(s, u) && !is_e(s, u)) || !pos(u)) return 0;
    return kvar(s, out, K_LOG, u, t);
}

/* to += p times the monomial m, negated when neg */
static int addmono(poly_t *to, const poly_t *p, unsigned m, uint8_t neg)
{
    const term_t *x;
    term_t t;
    for (x = p->t; x < p->t + p->n; x++) {
        t = *x;
        if ((t.mono += m) & OVF) return 0;
        if (neg) t.num = -t.num;
        if (!addterm(to, &t)) return 0;
    }
    return 1;
}

/* p has a variable or kernel in it, under a root too */
static int sym(const poly_t *p)
{
    const term_t *x;
    for (x = p->t; x < p->t + p->n; x++) if (x->mono || x->rad & ~RNUM & ~IMAG) return 1;
    return 0;
}

static int psqrt(ps_t *s, poly_t *q, const poly_t *p, uint8_t j);

/* r = √r when sqrtinto() has no monomial for it and it has a variable:
   √(X+1), √(XY), √(-X), √(X²). N/D is √(N)/√(D), N, D < 0 too: that is
   i√(-N)/(i√(-D)). N is c·P, c > 0 and P's numbers whole and coprime, and
   √(N) is √(c)√U, U the kernel P under the root: rmul() makes √U√U U, and
   unroot() P again. P = S² in one variable shows as abs(S). A number is the
   OS's: 0. */
static int rootk(ps_t *s, rat_t *r)
{
    uint8_t top = s->top, j;
    rat_t *p = get(s), *b = get(s);
    const term_t *d = r->d.t;
    term_t f;
    int32_t l = 1, g = 0, q;
    unsigned v;
    if (!b) return 0;
    if (r->d.n != 1 || d->mono || d->num != 1 || d->den != 1 || d->rad != 1) {
        b->n = r->d;
        pconst(&b->d, 1);
        pconst(&r->d, 1);
        if (!sqrtinto(s, r) || !sqrtinto(s, b) || !rmulinto(s, r, b, 1)) return 0;
    } else {
        if (hasrad(&r->n) || !sym(&r->n) || !lg(&r->n, &l, &g) || !mulc(g, l, &q) || q > LIM) return 0;
        *p = *r;
        f.mono = 0; f.rad = 1;
        if (!rset(&f, l, g) || !scale(&p->n, &f)) return 0;
        v = univ(&p->n);
        for (j = 0; j < NV && v != 15u << 4 * j; j++) ;
        pconst(&b->d, 1);
        if (j == NV || !psqrt(s, &b->n, &p->n, j)) b = 0;
        else if (!(b = keep(s, b))) return 0;
        if ((j = kern(s, K_ROOT, p, b)) == 0xFF) return 0;
        q = sqfree(q, &g);
        rconst(r, 1);
        if (!rset(r->n.t, g, l)) return 0;
        r->n.t[0].rad = (uint32_t)q | (uint32_t)1 << (RVAR + j);
    }
    s->top = top;
    return 1;
}

/* p with U^e, where rmul() made √U√U U, U^(e-1)·P: mulinto() does the rest */
static int unroot(ps_t *s, poly_t *p)
{
    poly_t *f;
    uint8_t k, g, i;
    for (k = 0; k < s->nk; k++) {
        if (s->kf[k] != K_ROOT) continue;
        g = 4 * KN(s, k);
        for (i = 0; i < p->n; )
            if (!(p->t[i].mono >> g & 15)) i++;
            else {
                if (!(f = tmp(s))) return 0;
                f->n = 1;
                f->t[0] = p->t[i];
                f->t[0].mono -= 1u << g;
                p->t[i] = p->t[--p->n];
                if (!mulinto(s, f, &s->ka[k]->n) || !addmono(p, f, 0, 0)) return 0;
                s->top--;
                i = 0;
            }
    }
    return 1;
}

/* t times i^e */
static void muli(term_t *t, uint8_t e)
{
    for (; e; e--) {
        if (t->rad & IMAG) t->num = -t->num;
        t->rad ^= IMAG;
    }
}

static uint8_t imag(const poly_t *p)
{
    uint8_t k, c = 0;
    for (k = 0; k < p->n; k++) c += (p->t[k].rad & IMAG) != 0;
    return c;
}

/* p mod sin(u)² + cos(u)² - 1, in the nibble of kernel f (K_SIN or K_COS)
   of each u that has both: f's powers go down to 0 or 1. w is scratch. */
static int pyth(ps_t *s, poly_t *p, uint8_t f, poly_t *w)
{
    uint8_t k, m;
    for (k = 0; k < s->nk; k++)
        for (m = 0; m < s->nk; m++)
            if (s->kf[k] == f && s->kf[m] == (K_SIN ^ K_COS ^ f) && s->ka[k] == s->ka[m]) {
                pconst(w, -1);
                w->t[1] = w->t[2] = w->t[0];
                w->t[1].num = w->t[2].num = 1;
                w->t[1].mono = 2u << 4 * KN(s, k);
                w->t[2].mono = 2u << 4 * KN(s, m);
                w->n = 3;
                if (!vrem(p, w, 0, KN(s, k))) return 0;
            }
    return 1;
}

/* TEXPAND: out = sin, cos or tan (f) of a, which has no kernel and a first
   term that is positive. Each term of a is n times an angle A, n its number
   (7 at most; a number c is 1 times c), and sin(nA), cos(nA) are the addition
   formulas turned n times over sin(A), cos(A) kernels, then sin² = 1-cos²:
   sin(3X) is 4sin(X)cos(X)²-sin(X). A lone angle stays: sin(X), tan(X/2). */
static int texpand(ps_t *s, rat_t *out, uint8_t f, rat_t *a)
{
    rat_t *w = get(s), *v = get(s), *u = get(s);
    poly_t *S = &w->n, *C = &w->d;
    const term_t *x;
    term_t t;
    int32_t n;
    uint8_t sj, cj, e;
    if (!u) return 0;
    if (!isconst(&a->d) || (a->n.n == 1 && (a->n.t[0].num == 1 || !a->n.t[0].mono)))
        return kvar(s, out, f, a, 0);
    pconst(S, 0);
    pconst(C, 1);
    for (x = a->n.t; x < a->n.t + a->n.n; x++) {
        t = *x;
        n = !t.mono ? (t.num < 0 ? -1 : 1) : t.num;
        if (n > 7 || n < -7) return 0;
        t.num = !t.mono ? t.num * n : 1;
        pconst(&u->n, 1);
        u->n.t[0] = t;
        pconst(&u->d, 1);
        if ((sj = kern(s, K_SIN, u, 0)) == 0xFF || (cj = kern(s, K_COS, u, 0)) == 0xFF) return 0;
        for (e = (uint8_t)(n < 0 ? -n : n); e; e--) {
            /* sin(B±A) = sin B cos A ± cos B sin A, cos(B±A) = cos B cos A ∓ sin B sin A */
            v->n.n = v->d.n = 0;
            if (!addmono(&v->n, S, 1u << 4 * cj, 0) || !addmono(&v->n, C, 1u << 4 * sj, n < 0) ||
                !addmono(&v->d, C, 1u << 4 * cj, 0) || !addmono(&v->d, S, 1u << 4 * sj, n > 0))
                return 0;
            *w = *v;
        }
    }
    if (!pyth(s, S, K_SIN, &u->n) || !pyth(s, C, K_SIN, &u->n)) return 0;
    out->n = f == K_COS ? *C : *S;
    pconst(&out->d, 1);
    if (f != K_TAN) return 1;
    out->d = *C;
    return reduce(s, out);
}

/* After a function head h: its argument (and a logBASE('s base), then ')'
   (which may be left off at the end), and out = the function of it. sin(-u)
   is -sin(u); an angle with a kernel in it is refused, and so is a
   logarithm of a number <= 0 or to base 1. In TOLN and TOLOGBASE every
   logarithm goes to the one base: log_c(u) = log_t(u)/log_t(c).
   ponytail: NOINLINE keeps its locals out of postfix's frame, which every
   nesting level pays for. */
NOINLINE static int apply(ps_t *s, rat_t *out, uint8_t h, rat_t *a, rat_t *b, rat_t *c);

NOINLINE static int fncall(ps_t *s, rat_t *out, uint8_t h)
{
    rat_t *a, *b, *c = 0;
    uint8_t top = s->top, k;
    int ok;
    if (h == K_E) return kvar(s, out, K_E, 0, 0);
    if (s->depth >= MAXDEPTH || !(a = get(s)) || !(b = get(s))) return 0;
    s->depth++;
    if (!expr(s, a) || reduce(s, a) != 1) return 0;
    if (h == H_LOGB) {                          /* logBASE(u,c); in Ans the comma is its glyph */
        if (peek(s) != (s->inans ? T_LCOMMA : T_COMMA)) return 0;
        s->i++;
        if (!expr(s, b) || reduce(s, b) != 1) return 0;
        c = b;
    }
    s->depth--;
    for (k = h == K_LABS ? 2 : 1; k--; )        /* ln(abs(u)) closes twice */
        if ((ok = peek(s)) == T_RPAR) s->i++;
        else if (ok >= 0) return 0;
    ok = apply(s, out, h, a, b, c);
    s->top = top;
    return ok;
}

/* out = the function h of a (which it may negate), c logBASE('s base or 0,
   b scratch: fncall()'s, and calc()'s for a kernel at a point */
NOINLINE static int apply(ps_t *s, rat_t *out, uint8_t h, rat_t *a, rat_t *b, rat_t *c)
{
    rat_t *t;
    uint8_t neg = 0;
    int ok = 1;
    if (h <= K_EXP && haskern(s, a)) return 0;
    if (h <= K_TAN) {
        if (a->n.n && lead(&a->n)->num < 0) { pneg(&a->n); neg = h != K_COS; }
        if (!a->n.n) rconst(out, h == K_COS);
        else if (s->fn == C_TEXPAND) ok = texpand(s, out, h, a);
        else if (h == K_TAN && s->fn > C_TEXPAND) {         /* sin(u)/cos(u) */
            if (!kvar(s, out, K_SIN, a, 0) || !kvar(s, b, K_COS, a, 0)) return 0;
            out->d = b->n;
        } else ok = kvar(s, out, h, a, 0);
    } else if (h == K_EXP) {
        if (!a->n.n) rconst(out, 1);
        else ok = isnum(a, 1) ? kvar(s, out, K_E, 0, 0) : kvar(s, out, K_EXP, a, 0);
    } else if (h == K_LABS) {                   /* ln|u|: calc() and TOLN only */
        if (s->fn != C_TOLN || (ok = nsign(a)) == 0 || ok == 2) return 0;
        if (ok == 3 && haskern(s, a)) return 0;
        if (ok < 0 || (ok == 3 && lead(&a->n)->num < 0)) pneg(&a->n);     /* |-u| = |u| */
        return ok == 3 ? kvar(s, out, K_LABS, a, 0) : mklog(s, out, a, 0);
    } else if (h == K_ROOT) *out = *a;          /* U at a point: evalat()'s */
    else {
        if (h == H_LOG) rconst(c = b, 10);
        if (c && is_e(s, c)) c = 0;
        if (c && (haskern(s, c) || !pos(c) || isnum(c, 1) || !(c = keep(s, c)))) return 0;
        t = s->fn <= C_TOLOGBASE ? s->tb : c;
        if (!mklog(s, out, a, t)) return 0;
        if (c != t) {
            if (!c && !kvar(s, c = a, K_E, 0, 0)) return 0;
            ok = mklog(s, b, c, t) && rmulinto(s, out, b, 1);
        }
    }
    if (neg) pneg(&out->n);
    return ok;
}

/* kernel k as it is typed: sin(u), cos(u) in letters, tan(u), e^(u), e,
   ln(u), log(u) for base 10, else logBASE(u,b) */
static uint8_t putk(const ps_t *s, uint8_t k, uint8_t *out, uint8_t n)
{
    const uint8_t *h = heads;
    rat_t *b = s->kb[k];
    uint8_t f = b ? (isnum(b, 10) ? H_LOG : H_LOGB) : s->kf[k], j;
    if (s->kf[k] == K_ROOT) {                   /* U = P; unroot() leaves none */
        PUT(T_LPAR);
        if (!(n = answer(s, s->ka[k], out, n))) return 0;
        PUT(T_RPAR);
        return n;
    }
    while (*h != f) h += 2 + h[1];
    for (j = 0; j < h[1]; j++) PUT(h[2 + j]);
    if (f == K_E) return n;
    if (!(n = answer(s, s->ka[k], out, n))) return 0;
    if (f == H_LOGB) {
        PUT(T_LCOMMA);
        if (!(n = answer(s, b, out, n))) return 0;
    }
    if (f == K_LABS) PUT(T_RPAR);
    PUT(T_RPAR);
    return n;
}

#define MAXTH 6                 /* angles in one TCOLLECT or TOEXP */
#define KSIN  0x20000000UL      /* linear(): an angle's sin, not its cos, */
#define TAGS  (7UL << 26 | KSIN)        /* and the angle, its index + 1 at bit 26 */

/* TCOLLECT, TOEXP (exp): one side of the answer. Each term of p, a
   polynomial in sin and cos kernels, becomes terms of out tagged with an
   angle θ of th[] standing for e^(θi) (TOEXP), or for cos θ and, with KSIN,
   sin θ (TCOLLECT, θ's first term made positive). With E = e^(ui) and
   F = E², sin(u)^a cos(u)^b is ((E-1/E)/(2i))^a ((E+1/E)/2)^b, which is
   (F-1)^a (F+1)^b / (E^(a+b) 2^(a+b) i^a): expanded in F, F^r is
   e^((2r-a-b)ui), one angle per term. F sits in the nibble of u's first
   kernel. Every angle has a whole-number denominator. */
static int linear(ps_t *s, poly_t *out, const poly_t *p, poly_t *th, uint8_t *nth, uint8_t exp)
{
    rat_t *w = get(s), *v = get(s);
    poly_t *P = &w->n, *f = &w->d, *a = &v->n;
    const term_t *x, *y, *z;
    term_t t;
    uint8_t da[NV], k, m, e, sn, dn, neg, id;
    if (!v) return 0;
    out->n = 0;
    for (x = p->t; x < p->t + p->n; x++) {
        pconst(P, 1);
        sn = dn = 0;
        for (k = 0; k < NV; k++) da[k] = 0;
        for (k = 0; k < s->nk; k++) {
            if (!(e = x->mono >> 4 * KN(s, k) & 15)) continue;
            if (s->kf[k] > K_COS || !isconst(&s->ka[k]->d)) return 0;
            for (m = 0; s->ka[m] != s->ka[k]; m++) ;
            da[m] += e;
            dn += e;
            if (s->kf[k] == K_SIN) sn += e;
            pconst(f, s->kf[k] == K_SIN ? -1 : 1);  /* F-1 or F+1 */
            f->t[1] = f->t[0];
            f->t[1].num = 1;
            f->t[1].mono = 1u << 4 * KN(s, m);
            f->n = 2;
            while (e--) if (!mulinto(s, P, f)) return 0;
        }
        if (dn > 16) return 0;
        for (y = P->t; y < P->t + P->n; y++) {
            a->n = 0;
            for (k = 0; k < s->nk; k++)
                if (da[k])
                    for (z = s->ka[k]->n.t; z < s->ka[k]->n.t + s->ka[k]->n.n; z++) {
                        t = *z;
                        if (!cmul(&t, 2 * (int8_t)(y->mono >> 4 * KN(s, k) & 15) - da[k], 1) ||
                            !addterm(a, &t)) return 0;
                    }
            t = *x;
            t.mono &= ~(s->nk ? 0xFFFFFFu >> 4 * s->nv : 0);
            if (!cmul(&t, y->num, (int32_t)1 << dn)) return 0;
            muli(&t, -sn & 3);
            neg = !exp && a->n && lead(a)->num < 0;
            if (neg) pneg(a);
            if (a->n) {
                for (id = 0; id < *nth && !same(&th[id], a); id++) ;
                if (id == *nth) {
                    if (id == MAXTH) return 0;
                    th[(*nth)++] = *a;
                }
                t.rad |= (uint32_t)(id + 1) << 26;
            }
            if (!addterm(out, &t)) return 0;
            if (!exp && a->n) {                 /* e^(θi) = cos θ + i sin θ */
                muli(&t, neg ? 3 : 1);
                t.rad |= KSIN;
                if (!addterm(out, &t)) return 0;
            }
        }
    }
    s->top -= 2;
    return 1;
}

/* the first term of p with that tag */
static term_t *tagged(poly_t *p, uint32_t tag)
{
    term_t *x;
    for (x = p->t; x < p->t + p->n; x++) if ((x->rad & TAGS) == tag) return x;
    return 0;
}

/* TCOLLECT, TOEXP (exp): p through linear(), both sides, then its kernels
   replaced by one per angle used, in order -- A-B before A+B (TCOLLECT), X
   before -X (TOEXP) -- sin before cos. TCOLLECT's imaginary parts all
   cancel; TOEXP's below the bar are all of it or none. Then reduced. */
static int lin(ps_t *s, rat_t *p, uint8_t exp)
{
    rat_t *r = get(s), *u = get(s);
    poly_t *th = (poly_t *)get(s), *q = &u->n;
    uint8_t nth = 0, o[MAXTH], k, j, h, g;
    uint32_t tag;
    term_t *x;
    get(s);
    if (!get(s)) return 0;                      /* th[]: three slots in a row, six polynomials */
    r->d = p->d;
    if (!linear(s, &r->n, &p->n, th, &nth, exp) ||
        (!isconst(&p->d) && !linear(s, &r->d, &p->d, th, &nth, exp))) return 0;
    for (k = 0; k < nth; k++)
        for (o[j = k] = k; j; j--) {
            *q = th[o[j - 1]];
            if (!addmono(q, &th[o[j]], 0, 1)) return 0;
            if ((lead(q)->num > 0) == exp) break;
            g = o[j]; o[j] = o[j - 1]; o[j - 1] = g;
        }
    s->nk = 0;
    for (k = 0; k < nth; k++)
        for (h = 0; h < 2; h++) {
            tag = (uint32_t)(o[k] + 1) << 26 | (h ? 0 : KSIN);
            if (!tagged(&r->n, tag) && !tagged(&r->d, tag)) continue;
            u->n = th[o[k]];
            pconst(&u->d, 1);
            if (exp) for (j = 0; j < u->n.n; j++) muli(&u->n.t[j], 1);
            if ((g = kern(s, exp ? K_EXP : h ? K_COS : K_SIN, u, 0)) == 0xFF) return 0;
            while ((x = tagged(&r->n, tag)) || (x = tagged(&r->d, tag))) {
                x->rad &= ~TAGS;
                x->mono |= 1u << 4 * g;
            }
        }
    if (exp && imag(&r->d)) {
        if (imag(&r->d) != r->d.n) return 0;
        for (k = 0; k < r->n.n; k++) muli(&r->n.t[k], 1);
        for (k = 0; k < r->d.n; k++) muli(&r->d.t[k], 1);
    }
    if (!exp && (imag(&r->n) || imag(&r->d))) return 0;
    *p = *r;
    return reduce(s, p);
}

/* TOLOGBASE: the last comma outside parentheses in the entry, or len */
static unsigned comma(const uint8_t *in, unsigned len)
{
    unsigned k, at = len;
    int d = 0;
    uint8_t c;
    for (k = 0; k < len; k++) {
        c = in[k];
        if (c == T_RPAR) d--;
        else if (c == T_COMMA && !d) at = k;
        else if (c == T_LPAR || c == T_SQRT || c == T_ABS || (c >= T_LN && c <= T_TAN) ||
                 (c == T_EXT && k + 1 < len && in[k + 1] == 0x34)) d++;
        k += c == T_EXT || c == T_BB;           /* two bytes */
    }
    return at;
}

/* N/D, D one term: each term of N over D on its own, as the Nspire writes
   it: ln(X)/ln(10)+1. r is scratch. */
static uint8_t split(const ps_t *s, rat_t *p, uint8_t *out, rat_t *r)
{
    uint8_t k, n = 0, rev = order(&p->n);
    term_t *x = r->n.t;
    unsigned m;
    for (k = 0; k < p->n.n; k++) {
        *x = p->n.t[rev ? p->n.n - 1 - k : k];
        r->d.t[0] = p->d.t[0];
        r->n.n = r->d.n = 1;
        m = mgcd(x->mono, r->d.t[0].mono);
        x->mono -= m;
        r->d.t[0].mono -= m;
        if (k) {
            if (x->num < 0) { PUT(T_SUB); x->num = -x->num; }
            else PUT(T_ADD);
        }
        if (!(n = answer(s, r, out, n))) return 0;
    }
    return n;
}

/* TOLN .. TCOLLECT: the entry is expr [, base] [)]. ponytail: NOINLINE,
   3.3 KB smaller than inlined into engine(). */
NOINLINE static uint8_t fncmd(ps_t *s, uint8_t cmd, const char *name, uint8_t *out)
{
    rat_t *p, *r;
    term_t *x;
    unsigned k;
    int32_t l = 1, g = 0;
    uint8_t n = 0, f;

    s->fn = cmd;
    if (cmd == C_TOEXP && s->deg) return error(out, "MODE", "USE RADIAN", "");
    if (cmd == C_TOLOGBASE) {                   /* the base first: what every log goes to */
        if ((k = comma(s->in, s->len)) == s->len) return error(out, "ARGUMENT", name, "");
        s->i = k + 1;
        if (!(p = get(s)) || !expr(s, p)) return 0;
        if (peek(s) == T_RPAR) s->i++;
        if (s->i != s->len || reduce(s, p) != 1) return 0;
        if (!is_e(s, p)) {
            if (haskern(s, p)) return 0;
            if (!pos(p) || isnum(p, 1)) return error(out, "DOMAIN", name, "");
            if (!(s->tb = keep(s, p))) return 0;
        }
        s->nk = 0; s->top = 0; s->i = 0; s->len = k;
    }
    if (!(p = get(s)) || !(r = get(s)) || !expr(s, p)) return 0;
    if (peek(s) == T_RPAR) s->i++;
    if (s->i != s->len || !reduce(s, p)) return 0;
    if (cmd == C_TOSIN || cmd == C_TOCOS) {     /* cos(u)² is 1-sin(u)², sin(u)² 1-cos(u)² */
        f = cmd == C_TOSIN ? K_COS : K_SIN;
        for (k = 0; k < s->nk; k++)
            if (s->kf[k] == f && (vdeg(&p->n, KN(s, k)) > 1 || vdeg(&p->d, KN(s, k)) > 1) &&
                kern(s, K_SIN ^ K_COS ^ f, s->ka[k], 0) == 0xFF) return 0;
        if (!pyth(s, &p->n, f, &r->n) || !pyth(s, &p->d, f, &r->n) || !reduce(s, p)) return 0;
    }
    if ((cmd == C_TOEXP || cmd == C_TCOLLECT) && !lin(s, p, cmd == C_TOEXP)) return 0;
    if (cmd == C_TOEXP && isconst(&p->d) && p->n.n && imag(&p->n) == p->n.n) {
        /* all of it times i: (e^(Xi)-e^(-Xi))/(2i) */
        if (!lg(&p->n, &l, &g)) return 0;
        for (x = p->n.t; x < p->n.t + p->n.n; x++) {
            if (!cmul(x, l, 1)) return 0;
            muli(x, 1);
        }
        if (p->n.n > 1) PUT(T_LPAR);
        if (!(n = emit(s, &p->n, out, n))) return 0;
        if (p->n.n > 1) PUT(T_RPAR);
        PUT(T_DIV);
        if (l > 1) { PUT(T_LPAR); if (!(n = number(out, n, l))) return 0; }
        PUT(T_I);
        if (l > 1) PUT(T_RPAR);
        return n;
    }
    if (isconst(&p->d)) return putroot(s, p, out, 0);
    return p->d.n > 1 ? answer(s, p, out, 0) : split(s, p, out, r);
}

/* ---- the Calculus menu: DERIV( with functions, DERIVAT( TANGENTLINE(
   NORMALLINE( CENTRALDIFF( TAYLOR( IMPDIF(. Exact and symbolic, with the
   ideas (not the code) of KhiCAS/Giac and the Nspire's own examples: a
   kernel's derivative is another kernel -- sin' cos, cos' -sin, e^(u)' e^(u),
   ln(u)' 1/u, tan' 1+tan², which stays a polynomial for TAYLOR -- times u',
   and dP/dV is P's own V-part plus dP/dK·dK/dV for each kernel K of P.
   Logarithms are all ln (TOLN): log(u) is ln(u)/ln(10), a constant below. */

static int drat(ps_t *s, rat_t *out, const rat_t *r, uint8_t j);

/* out = d/dV of kernel k, V at nibble j */
static int dkern(ps_t *s, rat_t *out, uint8_t k, uint8_t j)
{
    rat_t *a = s->ka[k];
    poly_t *w;
    uint8_t f = s->kf[k], g;
    int ok;
    if (f == K_E) { rconst(out, 0); return 1; }
    if (!drat(s, out, a, j)) return 0;          /* u' */
    if (!out->n.n || f == K_ROOT) return 1;     /* U = u: √U' is dpoly()'s */
    if (f == K_LOG || f == K_LABS) return rmulinto(s, out, a, 1);
    if (!(w = tmp(s))) return 0;
    pconst(w, 1);
    if (f == K_TAN) {                           /* 1 + tan(u)² */
        w->t[1] = w->t[0];
        w->t[1].mono = 2u << 4 * KN(s, k);
        w->n = 2;
    } else {                                    /* e^(u), cos(u), -sin(u) */
        g = f == K_EXP ? KN(s, k) : kern(s, K_SIN ^ K_COS ^ f, a, 0);
        if (g == 0xFF) return 0;
        w->t[0].mono = 1u << 4 * g;
        if (f == K_COS) w->t[0].num = -1;
    }
    ok = mulinto(s, &out->n, w);                /* u' has no kernel: nothing to reduce */
    s->top--;
    return ok;
}

/* out = dP/dV for a polynomial P in variables and kernels */
static int dpoly(ps_t *s, rat_t *out, const poly_t *p, uint8_t j)
{
    rat_t *t = get(s), *d = get(s);
    uint8_t k, g, nk = s->nk;
    if (!d || !xd(&out->n, p, j)) return 0;
    pconst(&out->d, 1);
    out->d.t[0].mono = 1u << 4 * j;
    if (!reduce(s, out)) return 0;
    for (k = 0; k < nk; k++) {
        g = KN(s, k);
        if (!vdeg(p, g)) continue;
        if (!dkern(s, d, k, j)) return 0;
        if (!d->n.n) continue;
        if (!xd(&t->n, p, g)) return 0;         /* K·dP/dK, over K */
        pconst(&t->d, 1);
        t->d.t[0].mono = 1u << 4 * g;
        if (!rmulinto(s, t, d, 0) || !raddinto(s, out, t)) return 0;
    }
    s->top -= 2;
    return 1;
}

/* out = dR/dV, out not r: (N/D)' = (N' - (N/D)·D')/D */
static int drat(ps_t *s, rat_t *out, const rat_t *r, uint8_t j)
{
    rat_t *b = get(s), *q = get(s);
    if (!q || !dpoly(s, out, &r->n, j)) return 0;
    if (!isconst(&r->d)) {
        if (!dpoly(s, b, &r->d, j)) return 0;
        *q = *r;
        if (!rmulinto(s, b, q, 0)) return 0;
        pneg(&b->n);
        if (!raddinto(s, out, b)) return 0;
    }
    q->n = r->d;
    pconst(&q->d, 1);
    if (!rmulinto(s, out, q, 1)) return 0;
    s->top -= 2;
    return 1;
}

/* out = P with V (nibble j) := v, and √V := w: Horner, down V's powers */
static int hpoly(ps_t *s, rat_t *out, const poly_t *p, uint8_t j, const rat_t *v, const rat_t *w)
{
    rat_t *c = get(s);
    const term_t *x;
    term_t t;
    uint32_t rb = (uint32_t)1 << (RVAR + j);
    uint8_t m = 0, e, h;
    if (!c) return 0;
    for (x = p->t; x < p->t + p->n; x++) if ((x->mono >> 4 * j & 15) > m) m = x->mono >> 4 * j & 15;
    rconst(out, 0);
    for (e = m + 1; e--; ) {
        if (e < m && !rmulinto(s, out, v, 0)) return 0;
        for (h = 0; h < 2; h++) {               /* the part without √V, then with */
            rconst(c, 0);
            for (x = p->t; x < p->t + p->n; x++)
                if ((x->mono >> 4 * j & 15) == e && !(x->rad & rb) == !h) {
                    t = *x;
                    t.mono &= ~(15u << 4 * j);
                    t.rad &= ~rb;
                    if (!addterm(&c->n, &t)) return 0;
                }
            if (!c->n.n) continue;
            if (h && (!w || !rmulinto(s, c, w, 0))) return 0;
            if (!raddinto(s, out, c)) return 0;
        }
    }
    s->top--;
    return 1;
}

/* r with V (nibble j) := v: 1, 2 where that is undefined (a zero below the
   bar, √ of a negative number), 0 past SymCE */
static int subst(ps_t *s, rat_t *r, uint8_t j, const rat_t *v)
{
    rat_t *n = get(s), *d = get(s), *w = get(s);
    const term_t *x;
    if (!w) return 0;
    for (x = r->n.t; x < r->n.t + r->n.n && !(x->rad >> (RVAR + j) & 1); x++) ;
    if (x == r->n.t + r->n.n) w = 0;            /* reduce() keeps roots off the bar */
    else {
        if (v->n.n && !pos(v)) return 2;
        *w = *v;
        if (!sqrtinto(s, w)) return 0;
    }
    if (!hpoly(s, n, &r->n, j, v, w) || !hpoly(s, d, &r->d, j, v, w)) return 0;
    if (!d->n.n) return 2;
    if (!rmulinto(s, n, d, 1)) return 0;
    *r = *n;
    s->top -= 3;
    return 1;
}

/* r depends on V (nibble j), through a kernel's argument too */
static int dep(const ps_t *s, const rat_t *r, uint8_t j)
{
    uint8_t k;
    if (vdeg(&r->n, j) || vdeg(&r->d, j)) return 1;
    for (k = 0; k < s->nk; k++)
        if ((vdeg(&r->n, KN(s, k)) || vdeg(&r->d, KN(s, k))) && s->ka[k] && dep(s, s->ka[k], j)) return 1;
    return 0;
}

/* r at V = v, V's kernels too: sin(X) at 0 is 0, e^(X) at 1 is e. subst()'s
   1, 2, 0; ln of a number <= 0 is 2. */
static int evalat(ps_t *s, rat_t *r, uint8_t j, const rat_t *v)
{
    rat_t *a = get(s), *b = get(s), *f = get(s);
    uint8_t k, g, nk = s->nk, m = 0;
    int ok;
    if (!f) return 0;
    /* D at v on its own first: X/tan(X) at 0 is 0/0, not 0/tan(0) */
    if (!isconst(&r->d)) {
        a->n = r->d;
        pconst(&a->d, 1);
        if ((ok = evalat(s, a, j, v)) != 1) return ok;
        if (!a->n.n) return 2;
    }
    /* the kernels in r before V goes: X²ln(X) at 0 is 0ln(0), not 0 */
    for (k = 0; k < nk; k++)
        if (vdeg(&r->n, KN(s, k)) || vdeg(&r->d, KN(s, k))) m |= 1 << k;
    if ((ok = subst(s, r, j, v)) != 1) return ok;
    for (k = 0; k < nk; k++) {
        g = KN(s, k);
        if (!(m >> k & 1) || !s->ka[k] || !dep(s, s->ka[k], j)) continue;
        *a = *s->ka[k];
        if ((ok = subst(s, a, j, v)) != 1) return ok;
        if ((s->kf[k] == K_LOG && !pos(a)) || (s->kf[k] == K_LABS && !a->n.n)) return 2;
        if (!apply(s, f, s->kf[k], a, b, 0)) return 0;
        if ((ok = subst(s, r, g, f)) != 1) return ok;
    }
    s->top -= 3;
    return 1;
}

/* r a whole number 0..MAXPOW: it; else -1 */
static int ord(const rat_t *r)
{
    const term_t *t = r->n.t;
    if (!r->n.n) return 0;
    return r->n.n == 1 && isconst(&r->d) && !t->mono && t->den == 1 && t->rad == 1 &&
           t->num > 0 && t->num <= MAXPOW ? (int)t->num : -1;
}

/* ", V" next, V a variable alone: its nibble, s->i past it; else 0xFF */
static uint8_t comvar(ps_t *s)
{
    unsigned i = s->i;
    uint8_t c;
    if (peek(s) != T_COMMA || i + 1 >= s->len || !is_var(c = s->in[i + 1]) ||
        (i + 2 < s->len && s->in[i + 2] != T_COMMA && s->in[i + 2] != T_RPAR)) return 0xFF;
    s->i += 2;
    return (uint8_t)(NV - 1 - s->rank[c - T_VAR0]);
}

/* Infinity, as the TI-84 has none: 1E99 (E99, -1E99) alone for an argument,
   s->i past it; 1, -1 its sign, 0 when not so */
static int8_t infty(ps_t *s)
{
    const uint8_t *t = s->in + s->i;
    unsigned n = s->len - s->i, k = 0;
    int8_t sg = 1;
    if (k < n && t[k] == T_NEG) { sg = -1; k++; }
    if (k < n && t[k] == 0x31) k++;
    if (k + 3 > n || t[k] != T_EE || t[k + 1] != 0x39 || t[k + 2] != 0x39) return 0;
    k += 3;
    if (k < n && t[k] != T_COMMA && t[k] != T_RPAR) return 0;
    s->i += k;
    return sg;
}

/* w = V - a, or V + a when add */
static int vplus(rat_t *w, const rat_t *a, uint8_t j, uint8_t add)
{
    *w = *a;
    if (!add) pneg(&w->n);
    return addmono(&w->n, &a->d, 1u << 4 * j, 0);
}

/* The answer: N/D with D one term as each term of N over D (split()) when
   kernels are in it, or always (sp: SERIES' 1/X+X/6) */
static uint8_t result(const ps_t *s, rat_t *p, uint8_t *out, rat_t *q, uint8_t sp)
{
    if (isconst(&p->d) || p->d.n > 1 || (!sp && !haskern(s, p))) return answer(s, p, out, 0);
    return split(s, p, out, q);
}

/* ---- INTEGRAL .. DOMINANTTERM (calc2()): exact, as the Nspire's, with the
   ideas (not the code) of KhiCAS/Giac and sympy; what they cannot prove is
   refused. */

/* the nibbles of m that p's terms use */
static unsigned used(const poly_t *p, unsigned m)
{
    const term_t *x;
    unsigned u = 0;
    for (x = p->t; x < p->t + p->n; x++) u |= x->mono;
    return u & m;
}

/* the nibbles of the kernels with V (nibble j) in their argument */
static unsigned vkern(const ps_t *s, uint8_t j)
{
    unsigned m = 0;
    uint8_t k;
    for (k = 0; k < s->nk; k++) if (s->ka[k] && dep(s, s->ka[k], j)) m |= 15u << 4 * KN(s, k);
    return m;
}

/* V is under a root in r, or in a kernel's argument there */
static int rooted(const ps_t *s, const rat_t *r, uint8_t j)
{
    uint8_t k;
    if (vdeg(&r->n, j) == 0xFF || vdeg(&r->d, j) == 0xFF) return 1;
    for (k = 0; k < s->nk; k++)
        if ((vdeg(&r->n, KN(s, k)) || vdeg(&r->d, KN(s, k))) && s->ka[k] &&
            (s->kf[k] == K_ROOT ? dep(s, s->ka[k], j) : rooted(s, s->ka[k], j))) return 1;
    return 0;
}

/* r, with terms, is surely not 0: one term (no kernel's value at a number
   is 0 once apply() has folded ln(1), sin(0), ...), or a polynomial in e
   alone, e being transcendental. e^(2)-e² and sin(1)²+cos(1)²-1 are 0. */
static int canon(const ps_t *s, const rat_t *r)
{
    unsigned m;
    uint8_t k;
    if (r->n.n < 2) return 1;
    m = used(&r->n, 0xFFFFFFu >> 4 * s->nv);
    for (k = 0; k < s->nk; k++) if (s->kf[k] == K_E) m &= ~(15u << 4 * KN(s, k));
    return !m;
}

/* nsign(a - b) */
static int cmpr(ps_t *s, const rat_t *a, const rat_t *b)
{
    rat_t *d = get(s), *t = get(s);
    int v = 3;
    if (!t) return 3;
    *d = *a; *t = *b;
    pneg(&t->n);
    if (raddinto(s, d, t)) v = nsign(d);
    s->top -= 2;
    return v;
}

/* r in [a, b] (strict: in (a, b)), a or b 0 for that side open: 1 or 0;
   2 when that cannot be told */
static int inside(ps_t *s, const rat_t *r, const rat_t *a, const rat_t *b, uint8_t strict)
{
    int u = a ? cmpr(s, r, a) : 1, v = b ? cmpr(s, b, r) : 1;
    if (u > 1 || v > 1) return 2;
    return strict ? u > 0 && v > 0 : u >= 0 && v >= 0;
}

/* r is a whole number, *v */
static int intval(const rat_t *r, int32_t *v)
{
    const term_t *t = r->n.t;
    *v = 0;
    if (!r->n.n) return 1;
    if (r->n.n != 1 || !isconst(&r->d) || t->mono || t->den != 1 || t->rad != 1) return 0;
    *v = t->num;
    return 1;
}

/* out += ∫p/Vᵐ dV, times Vᵐ: c·Vᵉ·√Vʳ goes to c·Vᵉ⁺¹√Vʳ/(e-m+1+r/2);
   0 at 1/V, a logarithm */
static int ipoly(poly_t *out, const poly_t *p, uint8_t j, uint8_t m)
{
    const term_t *x;
    term_t t;
    int32_t k;
    for (x = p->t; x < p->t + p->n; x++) {
        t = *x;
        k = 2 * ((int32_t)(x->mono >> 4 * j & 15) - m) + 2 + (x->rad >> (RVAR + j) & 1);
        if (!k || (t.mono += 1u << 4 * j) & OVF || !cmul(&t, 2, k) || !addterm(out, &t))
            return 0;
    }
    return 1;
}

/* u = aV + b, a a number: a; 0 when u is not so */
static int rate(const rat_t *u, uint8_t j, term_t *a)
{
    const term_t *x;
    uint8_t n = 0;
    if (!isconst(&u->d) || vdeg(&u->n, j) != 1) return 0;     /* d monic: 1 */
    for (x = u->n.t; x < u->n.t + u->n.n; x++)
        if (x->mono >> 4 * j & 15) {
            if (n++ || x->mono != 1u << 4 * j || x->rad != 1) return 0;
            *a = *x;
        }
    a->mono = 0;
    return 1;
}

/* fl empty, splitting over √, its factors in the three slots at f: six */
static void flinit(fl_t *fl, rat_t *f)
{
    uint8_t k;
    fl->n = 0;
    fl->split = 1;
    fl->c.num = fl->c.den = 1; fl->c.mono = 0; fl->c.rad = 1;
    for (k = 0; k < MAXF; k++) fl->f[k] = k > 5 ? 0 : k & 1 ? &f[k / 2].d : &f[k / 2].n;
}

/* r *= Vᵉ, r free of V: on the side the power belongs */
static int vpow(rat_t *r, uint8_t j, int8_t e)
{
    poly_t *p = e < 0 ? &r->d : &r->n;
    term_t *x;
    if (e < 0) e = -e;
    if (e > 7) return 0;
    for (x = p->t; x < p->t + p->n; x++) if ((x->mono += (unsigned)e << 4 * j) & OVF) return 0;
    return 1;
}

/* a term of p has a product or power with sin or cos of V in it */
static int trigprod(const ps_t *s, const poly_t *p, unsigned vk)
{
    const term_t *x;
    uint8_t k;
    for (x = p->t; x < p->t + p->n; x++)
        if (degree(x->mono & vk) > 1)
            for (k = 0; k < s->nk; k++) if (s->kf[k] <= K_COS && x->mono >> 4 * KN(s, k) & 15) return 1;
    return 0;
}

/* out += ∫Q·K dV by parts, over and over, Q a polynomial in V used up, K
   e^(u)'s (the monomial km, f 0) or f = sin(u) or cos(u) (kn, cn: their
   nibbles), u linear at rate a: Σ (-1)ⁱQ⁽ⁱ⁾Iᵢ₊₁, Iₙ K's n-th
   antiderivative: e^'s over aⁿ, sin(u + (φ-n)π/2)/aⁿ, φ 1 for cos. */
static int tabular(ps_t *s, poly_t *out, poly_t *q, unsigned km, uint8_t f, uint8_t kn, uint8_t cn,
                   const term_t *a, uint8_t j)
{
    poly_t *d = tmp(s);
    const term_t *x;
    term_t ia, c, t;
    uint8_t i, m;
    if (!d) return 0;
    rinv(&ia, a);
    ia.mono = 0;
    c = ia;                                     /* (-1)ⁱ/aⁱ⁺¹ */
    for (i = 0; q->n; i++) {
        m = (uint8_t)((f == K_COS) - i - 1) & 3;        /* sin(u + mπ/2): ±sin, ±cos */
        for (x = q->t; x < q->t + q->n; x++) {
            t.mono = x->mono + (f ? 1u << 4 * (m & 1 ? cn : kn) : km);
            if (!rmul(&t, x, &c) || t.mono & OVF) return 0;
            if (f && m & 2) t.num = -t.num;
            if (!addterm(out, &t)) return 0;
        }
        if (!xd(d, q, j)) return 0;
        divmono(d, 1u << 4 * j);
        *q = *d;
        t = c;
        t.num = -t.num;
        if (!rmul(&c, &t, &ia)) return 0;
    }
    s->top--;
    return 1;
}

/* out = Σ r⁽ᵏ⁾(a)/k!·Vᵏ for k to n, V standing for V-a: r's Taylor
   coefficients at a, r used up. With first, up to the first that is not 0,
   its index in *at (n+1: none); one that may be 0 unseen (canon()) is past
   SymCE. Else the first *at of them are 0, and the sum is over V^*at:
   Vᵏ⁻*ᵃᵗ. evalat()'s 1, 2, 0. */
NOINLINE static int tpoly(ps_t *s, rat_t *out, rat_t *r, uint8_t j, const rat_t *a, uint8_t n,
                          uint8_t first, uint8_t *at)
{
    rat_t *x = get(s), *q = get(s);
    term_t f;
    uint8_t k, i, sh = first ? 0 : *at;
    int v;
    if (!q) return 0;
    rconst(out, 0);
    f.mono = 0; f.rad = 1; f.num = 1; f.den = 1;
    for (k = 0; ; k++) {
        *x = *r;
        if ((v = evalat(s, x, j, a)) != 1) return v;
        if (x->n.n) {
            if ((first && !canon(s, x)) || k < sh || k - sh > 7) return 0;
            for (i = 0; i < x->n.n; i++) x->n.t[i].mono += (unsigned)(k - sh) << 4 * j;
            if (!scale(&x->n, &f) || !raddinto(s, out, x)) return 0;
            if (first) break;
        }
        if (k == n) { k++; break; }
        if (!drat(s, q, r, j)) return 0;
        *r = *q;
        f.den *= k + 1;
    }
    *at = k;
    s->top -= 2;
    return 1;
}

/* c = the first of P's Taylor coefficients at a not 0, to the n-th; its
   index in *at, n+1 when none. tpoly()'s 1, 2, 0. */
static int tfirst(ps_t *s, rat_t *c, const poly_t *P, uint8_t j, const rat_t *a, uint8_t n, uint8_t *at)
{
    rat_t *x = get(s);
    int v;
    if (!x) return 0;
    x->n = *P;
    pconst(&x->d, 1);
    if ((v = tpoly(s, c, x, j, a, n, 1, at)) != 1) return v;
    if (*at <= n) divmono(&c->n, (unsigned)*at << 4 * j);
    s->top--;
    return 1;
}

/* F = F(b) - F(a): evalat()'s 1, 2, 0 */
static int defint(ps_t *s, rat_t *F, uint8_t j, const rat_t *a, const rat_t *b)
{
    rat_t *x = get(s);
    int v;
    if (!x) return 0;
    *x = *F;
    if ((v = evalat(s, x, j, a)) != 1 || (v = evalat(s, F, j, b)) != 1) return v;
    pneg(&x->n);
    if (!raddinto(s, F, x)) return 0;
    s->top--;
    return 1;
}

/* p at ±∞, a rational function of V: c = N's first coefficient in V over
   D's, *e N's degree less D's; 0 when p is not so */
static int atinf(ps_t *s, rat_t *c, const rat_t *p, uint8_t j, int8_t *e)
{
    rat_t *x = get(s);
    unsigned vk = vkern(s, j);
    uint8_t dn = vdeg(&p->n, j), dd = vdeg(&p->d, j);
    if (!x || dn == 0xFF || dd == 0xFF || used(&p->n, vk) || used(&p->d, vk)) return 0;
    slice(&c->n, &p->n, 4 * j, dn);
    slice(&x->n, &p->d, 4 * j, dd);
    pconst(&c->d, 1);
    pconst(&x->d, 1);
    *e = (int8_t)(dn - dd);
    if (!rmulinto(s, c, x, 1)) return 0;
    s->top--;
    return 1;
}

/* q with q² = p, p in V alone with no root: the long square root, from the
   top power down, its first coefficient a square; 0 when there is none */
static int psqrt(ps_t *s, poly_t *q, const poly_t *p, uint8_t j)
{
    poly_t *r = tmp(s);
    const term_t *x;
    term_t t;
    int32_t a, b;
    uint8_t m = vdeg(p, j), e = m / 2 + 1;
    if (!r || m & 1 || !p->n || hasrad(p) || univ(p) & ~(15u << 4 * j)) return 0;
    t = *lead(p);
    a = (int32_t)isqrt(t.num);
    b = (int32_t)isqrt(t.den);
    if (t.num < 0 || a * a != t.num || b * b != t.den) return 0;
    q->n = 1;
    q->t[0] = t;
    q->t[0].num = a; q->t[0].den = b;
    q->t[0].mono = (unsigned)(m / 2) << 4 * j;
    for (;;) {
        *r = *q;                                /* r = p - q² */
        if (!mulinto(s, r, q)) return 0;
        pneg(r);
        for (x = p->t; x < p->t + p->n; x++) if (!addterm(r, x)) return 0;
        if (!--e) break;
        /* the next term: r's at V^(m/2+e-1), over twice q's first */
        for (x = r->t; x < r->t + r->n && x->mono != (unsigned)(m / 2 + e - 1) << 4 * j; x++) ;
        if (x == r->t + r->n) continue;
        t = *x;
        t.mono = (unsigned)(e - 1) << 4 * j;
        if (!cmul(&t, q->t[0].den, 2 * q->t[0].num) || !addterm(q, &t)) return 0;
    }
    s->top--;
    return !r->n;
}

/* ∫N/D dV, D in V alone: N = Q·D + R, ∫Q, then R/D in partial fractions
   over D's factors (fac(), split over √). At the root r of a factor
   f = αV+β to the power m, R/D is Σ Aₜ(V-r)^(t-m) plus a part regular there,
   Aₜ = h⁽ᵗ⁾(r)/t!, h = R/(αᵐ·D/fᵐ); (V-r)⁻¹ integrates to ln|f|, (V-r)⁻ᵏ to
   -αᵏ⁻¹/((k-1)fᵏ⁻¹), added to rp (0 for out) to be written after the ln's,
   as sympy's ln(X)-ln(X+1)+1/(X+1). An irreducible quadratic's arctan is
   refused. With ends lo, hi: 2 when a root is between them, 0 when that
   cannot be told. */
NOINLINE static int pfrac(ps_t *s, rat_t *out, rat_t *p, uint8_t j, const rat_t *lo, const rat_t *hi,
                          rat_t *rp)
{
    rat_t *h = get(s), *a = get(s), *f = get(s), *r = get(s);
    fl_t fl;
    term_t c, al;
    uint8_t i, m, t, k, top = s->top;
    int32_t fact;
    int v;
    if (!r) return 0;
    h->n.n = 0;
    if (!vrem(&p->n, &p->d, &h->n, j)) return 0;        /* R is left in p->n */
    rconst(out, 0);
    if (!ipoly(&out->n, &h->n, j, 0)) return 0;
    for (i = 0; ; i++) {
        get(s); get(s);
        if (!get(s)) return 0;
        flinit(&fl, s->pool + top);
        f->n = p->d;
        if (!fac(s, &fl, &f->n, 0)) return 0;
        s->top = top;
        if (i == fl.n) break;
        f->n = *fl.f[i];
        m = fl.e[i];
        if (vdeg(&f->n, j) != 1 || !root(s, r, &f->n, j)) return 0;
        if (lo && ((v = inside(s, r, lo, hi, 0)) || (v = inside(s, r, hi, lo, 0)))) return v == 1 ? 2 : 0;
        if (m == 1) {                           /* h = R/D': a √ in D/f stays off the bar */
            a->n = p->d;
            pconst(&a->d, 1);
            if (!drat(s, h, a, j)) return 0;
            *a = *h;
        } else {
            f->d = f->n;                        /* fᵐ, then D/fᵐ times αᵐ */
            for (k = 1; k < m; k++) if (!mulinto(s, &f->d, &f->n)) return 0;
            a->n = p->d;
            a->d.n = 0;
            if (!vrem(&a->n, &f->d, &a->d, j) || a->n.n) return 0;
            c = *lead(&f->d);
            c.mono = 0;
            a->n = a->d;
            pconst(&a->d, 1);
            if (!scale(&a->n, &c)) return 0;
        }
        h->n = p->n;
        pconst(&h->d, 1);
        if (!rmulinto(s, h, a, 1)) return 0;
        al = *lead(&f->n);
        for (t = 0, fact = 1; t < m; t++) {
            k = m - t;
            *a = *h;
            if (subst(s, a, j, r) != 1) return 0;
            c.num = 1; c.den = fact; c.rad = 1; c.mono = 0;
            for (v = 1; v < k; v++) if (!cmul(&c, al.num, al.den)) return 0;
            if ((k > 1 && !cmul(&c, -1, k - 1)) || !scale(&a->n, &c)) return 0;
            if (k == 1) {                       /* the last: ln|f| */
                pconst(&f->d, 1);
                if (!kvar(s, r, K_LABS, f, 0) || !rmulinto(s, a, r, 0)) return 0;
            } else {
                for (v = 1; v < k; v++) if (!mulinto(s, &a->d, &f->n)) return 0;
                if (!rationalize(s, a) || !reduce(s, a)) return 0;
            }
            if (!raddinto(s, k > 1 && rp ? rp : out, a)) return 0;
            if (k > 1) {
                if (!drat(s, a, h, j)) return 0;
                *h = *a;
                fact *= t + 1;
            }
        }
    }
    s->top -= 4;
    return 1;
}

/* out = ∫p dV, no constant: pfrac() when V is below the bar. Else term by
   term over D, the terms grouped by their kernels of V: a polynomial; by
   parts, a polynomial times sin, cos, or e^'s of linear u (tabular()); or
   times ln(u), ln|u|, u = α(V-r): ∫G·ln(u) = G̃·ln(u) - ∫G̃/(V-r) with
   G̃ = ∫G - (∫G)(r). Products of sin and cos go linear first (TCOLLECT),
   when lk. 1; 2 a pole between lo and hi; 0 when not so. p is used up;
   pfrac()'s rp. */
NOINLINE static int integ(ps_t *s, rat_t *out, rat_t *p, uint8_t j, const rat_t *lo, const rat_t *hi, uint8_t lk,
                          rat_t *rp)
{
    rat_t *g, *u, *t;
    unsigned vk = vkern(s, j), km;
    term_t a, ro;
    uint8_t i, k, kk = 0, f, ex, e, sn, cn;
    int v;
    if (vdeg(&p->d, j) || used(&p->d, vk)) {
        if (p->d.n == 1 && !(p->d.t[0].mono & ~(15u << 4 * j)) && vdeg(&p->n, j) == 0xFF && !used(&p->n, vk)) {
            rconst(out, 0);                     /* √V over Vᵐ: powers, 1/√(X) 2√(X) */
            if (!ipoly(&out->n, &p->n, j, p->d.t[0].mono >> 4 * j & 15)) return 0;
            out->d = p->d;
            return reduce(s, out) != 0;
        }
        if (used(&p->d, vk) || vdeg(&p->n, j) == 0xFF || univ(&p->d) != 15u << 4 * j) return 0;
        if (!used(&p->n, vk)) return pfrac(s, out, p, j, lo, hi, rp);
        /* e^(X)+1/X: N's terms with kernels of V over D, a polynomial, apart */
        if (!(g = get(s)) || !(u = get(s))) return 0;
        g->n.n = u->n.n = 0;
        for (i = 0; i < p->n.n; )
            if (p->n.t[i].mono & vk) { g->n.t[g->n.n++] = p->n.t[i]; p->n.t[i] = p->n.t[--p->n.n]; }
            else i++;
        if (!vrem(&g->n, &p->d, &u->n, j) || g->n.n) return 0;
        pconst(&u->d, 1);
        if (p->n.n) { if ((v = pfrac(s, out, p, j, lo, hi, rp)) != 1) return v; }
        else rconst(out, 0);
        if ((v = integ(s, g, u, j, lo, hi, lk, 0)) != 1) return v;
        v = raddinto(s, out, g) != 0;
        s->top -= 2;
        return v;
    }
    if (lk && trigprod(s, &p->n, vk)) {         /* sin(X)cos(X) is sin(2X)/2 */
        for (k = 0; k < s->nk; k++) if (s->kf[k] > K_COS) return 0;
        if (!lin(s, p, 0)) return 0;
        vk = vkern(s, j);
        if (vdeg(&p->d, j) || used(&p->d, vk)) return 0;
    }
    if (!(g = get(s)) || !(u = get(s)) || !(t = get(s))) return 0;
    rconst(out, 0);
    while (p->n.n) {
        km = p->n.t[0].mono & vk;
        g->n.n = 0;
        for (i = 0; i < p->n.n; )
            if ((p->n.t[i].mono & vk) == km) {
                g->n.t[g->n.n] = p->n.t[i];
                g->n.t[g->n.n++].mono -= km;
                p->n.t[i] = p->n.t[--p->n.n];
            } else i++;
        if (!km) {
            if (!ipoly(&out->n, &g->n, j, 0)) return 0;
            continue;
        }
        if (vdeg(&g->n, j) == 0xFF) return 0;
        f = ex = 0;
        ro.num = 0; ro.den = 1; ro.mono = 0; ro.rad = 1;
        for (k = 0; k < s->nk; k++) {
            if (!(e = km >> 4 * KN(s, k) & 15)) continue;
            if (!rate(s->ka[k], j, &a)) return 0;
            if (s->kf[k] == K_EXP) {            /* e^'s: at the rate Σ eₖaₖ */
                ex = 1;
                if (!radd(&ro, e * a.num, a.den)) return 0;
            } else if (f || km != 1u << 4 * KN(s, k)) return 0;
            else { f = s->kf[k]; kk = k; }
        }
        if (f ? ex : !ro.num) return 0;
        if (!f) {
            if (!tabular(s, &out->n, &g->n, km, 0, 0, 0, &ro, j)) return 0;
        } else if (f <= K_COS) {
            sn = kern(s, K_SIN, s->ka[kk], 0);
            cn = kern(s, K_COS, s->ka[kk], 0);
            if (sn == 0xFF || cn == 0xFF || !tabular(s, &out->n, &g->n, 0, f, sn, cn, &a, j)) return 0;
        } else if (f == K_LOG || f == K_LABS) {
            t->n.n = 0;
            if (!ipoly(&t->n, &g->n, j, 0) || !root(s, u, &s->ka[kk]->n, j)) return 0;
            pconst(&t->d, 1);
            *g = *t;                            /* ∫G, and at r */
            if (subst(s, t, j, u) != 1 || !isconst(&t->d) || !addmono(&g->n, &t->n, 0, 1) ||
                !vplus(t, u, j, 0) || !isconst(&t->d)) return 0;
            u->n = g->n;                        /* G̃/(V-r) */
            u->d.n = 0;
            if (!vrem(&u->n, &t->n, &u->d, j) || u->n.n) return 0;
            pneg(&u->d);
            if (!addmono(&out->n, &g->n, km, 0) || !ipoly(&out->n, &u->d, j, 0)) return 0;
        } else return 0;                        /* tan */
    }
    out->d = p->d;
    s->top -= 3;
    return reduce(s, out) != 0;
}

/* LIMIT, DOMINANTTERM. At ±∞ (inf): N/D's first terms in V, rational f
   only. At a: f(a) when it is defined, else the first Taylor terms of N and D
   there, (c/d)(V-a)^(vn-vd); a pole's sign from c/d and the side (dir) --
   "+INFINITY", "-INFINITY", "UNDEFINED" when the sides differ. √V only at
   a > 0, or a = 0 from the right when f(0) is defined. */
NOINLINE static uint8_t clim(ps_t *s, uint8_t cmd, const char *name, uint8_t *out, rat_t *p, uint8_t j,
                             rat_t *a, rat_t *b, int8_t inf)
{
    rat_t *c = p + 1, *d = p + 2;
    uint8_t vn, vd, rt, lim = cmd == C_LIMIT;
    int8_t e;
    int v, sg = 1, dir = 0;
    if (b && (dir = nsign(b)) > 1) return error(out, "ARGUMENT", name, "");
    if (inf) {
        if (!atinf(s, c, p, j, &e)) return 0;
        if (!lim) { if (!vpow(c, j, e)) return 0; }
        else if (e < 0) rconst(c, 0);
        else if (e > 0) {
            if ((sg = nsign(c)) > 1) return 0;
            if (inf < 0 && e & 1) sg = -sg;
            goto big;
        }
        return result(s, c, out, d, 1);
    }
    if (!a) { a = p + 3; rconst(a, 0); }        /* DOMINANTTERM(f,V): at 0 */
    if (!p->n.n) return result(s, p, out, c, 0);
    if ((rt = rooted(s, p, j)) && (sg = nsign(a)) != 1 && (sg || dir != 1 || !lim)) return 0;
    if (lim) {
        *c = *p;
        if ((v = evalat(s, c, j, a)) == 1) return result(s, c, out, d, 0);
        if (!v || (rt && sg != 1)) return 0;
    }
    if (tfirst(s, d, &p->d, j, a, 7, &vd) != 1 || vd > 7 ||
        tfirst(s, c, &p->n, j, a, lim ? vd : 7, &vn) != 1 || vn > 7) return 0;
    if (!lim) {                                 /* (c/d)(V-a)^(vn-vd) */
        if (!rmulinto(s, c, d, 1) || !vplus(d, a, j, 0) || !powinto(s, d, (int32_t)vn - vd) ||
            !rmulinto(s, c, d, 0)) return 0;
        return result(s, c, out, d, 1);
    }
    if (vn > vd) rconst(c, 0);                  /* N vanishes faster */
    else if (vn == vd) { if (!rmulinto(s, c, d, 1)) return 0; }
    else {
        if ((v = nsign(c)) > 1 || (sg = nsign(d)) > 1) return 0;
        sg *= v;
        if ((vd - vn) & 1) {
            if (!dir) return error(out, "UNDEFINED", name, "");
            sg *= dir;
        }
        goto big;
    }
    return result(s, c, out, d, 0);
big:
    return error(out, sg > 0 ? "+INFINITY" : "-INFINITY", name, "");
}

/* SERIES(f,V,order[,a]): with W = V-a, f = N/D = W^-vd·N/D̃, D̃ = D/W^vd;
   N and D̃ as Taylor polynomials to W^(order+vd), then N/D̃ by ascending
   division: 1/sin(X) is 1/X+X/6+7X³/360 to order 3. Powers stay below 8. */
NOINLINE static uint8_t cser(ps_t *s, uint8_t *out, rat_t *p, uint8_t j, rat_t *a, uint8_t o)
{
    rat_t *A = p + 1, *B = p + 2, *c = p + 3, *q = get(s), *t = get(s), *u;
    term_t *x;
    uint8_t vd, k, m;
    if (!t || rooted(s, p, j) || tfirst(s, c, &p->d, j, a, 7, &vd) != 1 || (m = o + vd) > 7) return 0;
    t->n = p->n; pconst(&t->d, 1);
    k = 0;
    if (tpoly(s, A, t, j, a, m, 0, &k) != 1) return 0;
    t->n = p->d; pconst(&t->d, 1);
    k = vd;                                     /* D̃ = D/W^vd */
    if (tpoly(s, B, t, j, a, m + vd, 0, &k) != 1) return 0;
    if (!(u = get(s)) || !mulinto(s, &A->n, &B->d) || !mulinto(s, &B->n, &A->d)) return 0;
    pconst(&A->d, 1);
    slice(&c->n, &B->n, 4 * j, 0);              /* D̃(a), not 0 */
    pconst(&c->d, 1);
    rconst(q, 0);
    for (k = 0; k <= m; k++) {                  /* qₖ = [Wᵏ](A - B·q)/D̃(a) */
        slice(&t->n, &A->n, 4 * j, k);
        t->d = A->d;
        if (!t->n.n) continue;
        if (!rmulinto(s, t, c, 1)) return 0;
        u->n.n = 0;
        for (x = B->n.t; x < B->n.t + B->n.n; x++)
            if ((x->mono >> 4 * j & 15) <= m - k && !addterm(&u->n, x)) return 0;
        pconst(&u->d, 1);
        if (!rmulinto(s, u, t, 0) || !vpow(u, j, (int8_t)k) || !vpow(t, j, (int8_t)k)) return 0;
        pneg(&u->n);
        if (!raddinto(s, q, t) || !raddinto(s, A, u)) return 0;
    }
    if (!a->n.n) { if (!vpow(q, j, -(int8_t)vd)) return 0; }
    else if (!vplus(c, a, j, 0) || subst(s, q, j, c) != 1 || !powinto(s, c, -(int32_t)vd) ||
             !rmulinto(s, q, c, 0)) return 0;
    return result(s, q, out, t, 1);
}

/* SUM, PRODUCT. Whole ends 100 terms apart at most, f not a polynomial in V
   (on the calculator a term costs some 25 ms): term by term, and past the
   end Karr's convention -- an empty range is 0 or 1, one backwards the
   negative or reciprocal of the range between. Else SUM of a polynomial in V
   (Newton's forward differences, Σ Δⁱf(a)·C(n,i+1), n = b-a+1 terms, whole
   when it is a number) plus c·e^'s of linear u, E(V): geometric,
   c(E(b+1) - E(a))/(e^ρ - 1). */
NOINLINE static uint8_t csum(ps_t *s, uint8_t cmd, const char *name, uint8_t *out, rat_t *p, uint8_t j,
                             rat_t *a, rat_t *b)
{
    rat_t *acc = p + 1, *t = p + 2, *w = p + 3, *x = get(s), *g = get(s);
    unsigned vk = vkern(s, j), km = 0;
    term_t ro, a1;
    int32_t l, h, k;
    uint8_t i, top = s->top, nk = s->nk, lim = s->lim;
    int v = 0;
    if (!g) return 0;
    if ((cmd == C_PRODUCT || vdeg(&p->d, j) || used(&p->d, vk) || used(&p->n, vk)) &&
        intval(a, &l) && intval(b, &h) && h - l < 100 && l - h <= 101) {
        if ((i = h < l - 1)) { k = l; l = h + 1; h = k - 1; }
        rconst(acc, cmd == C_SUM ? 0 : 1);
        for (k = l; k <= h; k++) {
            *t = *p;
            rconst(w, k);
            if ((v = evalat(s, t, j, w)) == 2) goto at;
            if (!v || (cmd == C_SUM ? !raddinto(s, acc, t) : !rmulinto(s, acc, t, 0))) break;
        }
        if (k > h) {
            if (i) {
                if (cmd == C_SUM) pneg(&acc->n);
                else if (!acc->n.n) return error(out, "DIVIDE BY 0", "", "");
                else if (!powinto(s, acc, -1)) return 0;
            }
            return result(s, acc, out, t, 0);
        }
        /* e^(2X) from 1 to 9: nine kernels, too many; its closed form, then,
           the kernels made on the way let go */
        s->top = top; s->nk = nk; s->lim = lim;
    }
    if (cmd == C_PRODUCT || vdeg(&p->d, j) || used(&p->d, vk) || vdeg(&p->n, j) == 0xFF) return 0;
    *x = *a;
    pneg(&x->n);
    *t = *b;
    if (!raddinto(s, x, t) || !addmono(&x->n, &x->d, 0, 0) || !reduce(s, x)) return 0;
    if (nsign(x) < 3 && !intval(x, &k)) return error(out, "ARGUMENT", name, "");
    rconst(acc, 0);
    g->n.n = 0;
    g->d = p->d;
    for (i = 0; i < p->n.n; )                   /* the e^ terms into g */
        if (p->n.t[i].mono & vk) {
            if ((g->n.n && (p->n.t[i].mono & vk) != km) || p->n.t[i].mono >> 4 * j & 15) return 0;
            km = p->n.t[i].mono & vk;
            g->n.t[g->n.n++] = p->n.t[i];
            p->n.t[i] = p->n.t[--p->n.n];
        } else i++;
    if (g->n.n) {
        ro.num = 0; ro.den = 1; ro.mono = 0; ro.rad = 1;
        for (i = 0; i < s->nk; i++)
            if ((k = km >> 4 * KN(s, i) & 15) &&
                (s->kf[i] != K_EXP || !rate(s->ka[i], j, &a1) || !radd(&ro, k * a1.num, a1.den))) return 0;
        if (!ro.num) return 0;
        *acc = *g;
        *w = *b;
        if (!addmono(&w->n, &w->d, 0, 0) || (v = evalat(s, acc, j, w)) != 1 || (v = evalat(s, g, j, a)) != 1)
            goto at;
        pneg(&g->n);
        rconst(w, 1);
        w->n.t[0].num = ro.num; w->n.t[0].den = ro.den;
        if (!raddinto(s, acc, g) || !apply(s, g, K_EXP, w, t, 0)) return 0;
        rconst(w, -1);
        if (!raddinto(s, g, w) || !rmulinto(s, acc, g, 1)) return 0;
    }
    *g = *x;                                    /* C(n, 1) */
    for (i = 0; p->n.n; i++) {
        *t = *p;
        if ((v = subst(s, t, j, a)) != 1) goto at;
        if (!rmulinto(s, t, g, 0) || !raddinto(s, acc, t)) return 0;
        *t = *p;                                /* f := f(V+1) - f */
        rconst(w, 1);
        if (!addmono(&w->n, &w->d, 1u << 4 * j, 0) || subst(s, t, j, w) != 1) return 0;
        pneg(&p->n);
        if (!raddinto(s, t, p)) return 0;
        if (!(*p = *t).n.n) break;
        *t = *x;                                /* C(n, i+2) = C(n, i+1)(n-i-1)/(i+2) */
        rconst(w, -(int32_t)i - 1);
        ro.num = 1; ro.den = i + 2; ro.mono = 0; ro.rad = 1;
        if (!raddinto(s, t, w) || !rmulinto(s, g, t, 0) || !scale(&g->n, &ro)) return 0;
    }
    return result(s, acc, out, t, 0);
at:
    return v == 2 ? error(out, "DOMAIN", name, "") : 0;
}

/* FMIN, FMAX's candidate i: a root of fl's factor i inside (a, b), then the
   ends a and b; 1 with it in r, 0 when there is none, -1 past SymCE */
static int cand(ps_t *s, const fl_t *fl, uint8_t i, rat_t *r, const rat_t *a, const rat_t *b, uint8_t j)
{
    const rat_t *e = i == fl->n ? a : b;
    int v;
    if (i >= fl->n) {
        if (!e) return 0;
        *r = *e;
        return 1;
    }
    if (vdeg(fl->f[i], j) != 1) return noroot(fl->f[i], j) ? 0 : -1;
    if (!root(s, r, fl->f[i], j) || (v = inside(s, r, a, b, 1)) == 2) return -1;
    return v;
}

/* FMIN, FMAX (f negated): f rational in V alone, no pole in [a, b]. The
   candidates are the real roots of f′'s numerator inside (a, b) -- exact,
   fac() splitting over √; one it cannot find is refused -- and the ends;
   the least value wins, ties joined by " or ". An open end where f falls
   without bound, or below every candidate, is "AT -INFINITY" (or +, or
   both: "AT INFINITY"). */
NOINLINE static uint8_t cfmin(ps_t *s, uint8_t cmd, const char *name, uint8_t *out, rat_t *p, uint8_t j,
                              rat_t *a, rat_t *b)
{
    rat_t *q = p + 1, *best = p + 2, *x = p + 3, *r = get(s), *fs = get(s);
    fl_t fl;
    int64_t key[MAXF + 2], kt;
    unsigned vm = 15u << 4 * j, win = 0;
    uint8_t i, k, n = 0, nw = 0, at = 0, tt = 0, o[MAXF + 2];
    int8_t e;
    int v;
    get(s);
    if (!get(s) || haskern(s, p) || hasrad(&p->n) || (univ(&p->n) | univ(&p->d)) & ~vm ||
        (a && nsign(a) > 1) || (b && nsign(b) > 1)) return 0;
    if (a && b && cmpr(s, a, b) == 1) return error(out, "ARGUMENT", name, "");
    if (cmd == C_FMAX) pneg(&p->n);
    if (!drat(s, q, p, j)) return 0;
    if (!q->n.n) return error(out, "ALWAYS TRUE", name, "");
    if (!isconst(&p->d)) {                      /* no pole in [a, b] */
        flinit(&fl, fs);
        x->n = p->d;
        if (!fac(s, &fl, &x->n, 0)) return 0;
        for (i = 0; i < fl.n; i++)
            if (vdeg(fl.f[i], j) == 1 ? !root(s, r, fl.f[i], j) || inside(s, r, a, b, 0) : !noroot(fl.f[i], j))
                return 0;
    }
    flinit(&fl, fs);
    x->n = q->n;
    if (!fac(s, &fl, &x->n, 0)) return 0;
    for (i = 0; i < fl.n + 2; i++) {
        if ((v = cand(s, &fl, i, r, a, b, j)) < 0) return 0;
        if (!v) continue;
        *x = *p;
        if (subst(s, x, j, r) != 1) return 0;
        if ((v = win ? cmpr(s, x, best) : -1) > 1) return 0;
        if (v < 0) { *best = *x; win = 0; }
        if (v <= 0) win |= 1u << i;
    }
    for (k = 0; k < 2; k++) {                   /* the open ends */
        if (k ? b : a) continue;
        if (!atinf(s, x, p, j, &e)) return 0;
        if (e > 0) {
            if ((v = nsign(x)) > 1) return 0;
            if (!k && e & 1) v = -v;
            if (v < 0) at |= 1 << k;
        } else {
            if (e < 0) rconst(x, 0);
            if ((v = win ? cmpr(s, x, best) : -1) > 1) return 0;
            if (v < 0) tt |= 1 << k;
        }
    }
    if (!at) at = tt;
    if (at) return error(out, at == 3 ? "AT INFINITY" : at == 1 ? "AT -INFINITY" : "AT +INFINITY", name, "");
    for (i = 0; i < fl.n + 2; i++)
        if (win >> i & 1) {
            cand(s, &fl, i, r, a, b, j);
            key[nw] = approx(&r->n, 0);
            o[nw] = i;
            for (k = nw++; k && key[k - 1] > key[k]; k--) {
                kt = key[k]; key[k] = key[k - 1]; key[k - 1] = kt;
                e = (int8_t)o[k]; o[k] = o[k - 1]; o[k - 1] = (uint8_t)e;
            }
        }
    for (k = 0; k < nw; k++) {
        cand(s, &fl, o[k], r, a, b, j);
        if (k) PUT(T_OR);
        PUT(s->var[NV - 1 - j]);
        PUT(T_EQ);
        if (!(n = putroot(s, r, out, n))) return 0;
    }
    return n;
}

/* t = t√(1+t²) + ln(t+√(1+t²)), t a number: twice ∫√(1+t²)dt */
static int asinht(ps_t *s, rat_t *t)
{
    rat_t *u = get(s), *w = get(s);
    if (!w) return 0;
    *u = *t;
    rconst(w, 1);
    if (!rmulinto(s, u, t, 0) || !raddinto(s, u, w) || !sqrtinto(s, u)) return 0;
    *w = *u;
    if (!rmulinto(s, u, t, 0) || !raddinto(s, w, t) || !mklog(s, t, w, 0) || !raddinto(s, t, u)) return 0;
    s->top -= 2;
    return 1;
}

/* ARCLEN: ∫ₐᵇ √(1+f′²) for f′ rational in V, √V allowed; f defined at the
   ends (else DOMAIN), f′ too (else improper: refused). g = 1+f′² constant:
   √g(b-a); g = h², h rational (psqrt()): ±∫h, h has one sign on [a, b]
   (h² >= 1, no pole: integ()); g = βV+γ: (2/(3β))·g√g; f′ = mV+c:
   (T(f′(b)) - T(f′(a)))/(2m), T asinht(), at number ends. Else refused:
   no digits that are not proved. */
NOINLINE static uint8_t carc(ps_t *s, const char *name, uint8_t *out, rat_t *p, uint8_t j, rat_t *a, rat_t *b)
{
    rat_t *g = p + 1, *h = p + 2, *x = p + 3, *y = get(s), *z = get(s);
    term_t m;
    uint8_t k, num = nsign(a) < 3 && nsign(b) < 3;
    int v = 0, sg;
    if (!z || !drat(s, g, p, j) || haskern(s, g)) return 0;
    for (k = 0; k < 2; k++) {
        *x = *p;
        if ((v = evalat(s, x, j, k ? b : a)) != 1) goto at;
        *x = *g;
        if (evalat(s, x, j, k ? b : a) != 1) return 0;
    }
    *h = *g;
    rconst(x, 1);
    if (!rmulinto(s, h, g, 0) || !raddinto(s, h, x)) return 0;
    if (!vdeg(&h->n, j) && !vdeg(&h->d, j)) {  /* f linear */
        *x = *a;
        pneg(&x->n);
        *z = *b;
        if (!sqrtinto(s, h) || !raddinto(s, x, z) || !rmulinto(s, h, x, 0)) return 0;
        return result(s, h, out, x, 0);
    }
    if (psqrt(s, &y->n, &h->n, j) && psqrt(s, &y->d, &h->d, j)) {
        *x = *y;
        if ((v = evalat(s, x, j, a)) != 1 || (sg = nsign(x)) > 1 || (v = integ(s, z, y, j, a, b, 0, 0)) != 1 ||
            (v = defint(s, z, j, a, b)) != 1) goto at;
        if (sg < 0) pneg(&z->n);
        return result(s, z, out, x, 0);
    }
    if (num && rate(h, j, &m)) {                /* (2/(3β))·(G(b) - G(a)), G = g√g */
        for (k = 0; k < 2; k++) {
            *y = *h;
            if (subst(s, y, j, k ? b : a) != 1) return 0;
            *z = *y;
            if (!sqrtinto(s, z) || !rmulinto(s, y, z, 0)) return 0;
            if (!k) *x = *y;
            else { pneg(&y->n); if (!raddinto(s, x, y)) return 0; }
        }
        pneg(&x->n);
        m.mono = 0;
        if (!cmul(&m, 3, 2) || !scale(&x->d, &m) || !reduce(s, x)) return 0;
        return result(s, x, out, y, 0);
    }
    if (num && rate(g, j, &m)) {                /* f′ = mV+c */
        for (k = 0; k < 2; k++) {
            *y = *g;
            if (evalat(s, y, j, k ? b : a) != 1 || !asinht(s, y)) return 0;
            if (k) { pneg(&y->n); if (!raddinto(s, x, y)) return 0; }
            else *x = *y;
        }
        pneg(&x->n);
        m.mono = 0;
        if (!cmul(&m, 2, 1) || !scale(&x->d, &m) || !reduce(s, x)) return 0;
        return result(s, x, out, y, 0);
    }
    return 0;
at:
    return v == 2 ? error(out, "DOMAIN", name, "") : 0;
}

/* INTEGRAL(f,V[,a,b]) here; the rest of the Calculus menu's second half
   each in its own function */
/* INTEGRAL(f,V) with √U, U = aV+b the kernel k: f at V := (V-b)/a, over a,
   √V for √U -- ∫f du/a, u = U, in V. Returns U, for V := U after; 0 when
   f is not so. */
static void rswap(poly_t *p, uint32_t a, uint32_t b)
{
    uint8_t i;
    for (i = 0; i < p->n; i++)
        if (p->t[i].rad & a) p->t[i].rad ^= a | b;
}
static rat_t *lsub(ps_t *s, rat_t *p, uint8_t j, uint8_t k)
{
    rat_t *u = s->ka[k], *w = get(s);
    unsigned vk = vkern(s, j);
    term_t a, c;
    uint8_t i;
    if (!w || s->kb[k] || !rate(u, j, &a) || vdeg(&p->n, j) == 0xFF || used(&p->n, vk) || used(&p->d, vk))
        return 0;
    rinv(&c, &a);
    *w = *u;
    for (i = 0; i < w->n.n; i++)                /* (V - b)/a */
        if (w->n.t[i].mono == 1u << 4 * j) w->n.t[i].num = w->n.t[i].den = 1;
        else w->n.t[i].num = -w->n.t[i].num;
    if (!scale(&w->n, &c) || subst(s, p, j, w) != 1) return 0;
    rswap(&p->n, (uint32_t)1 << (RVAR + KN(s, k)), (uint32_t)1 << (RVAR + j));   /* D has no root */
    if (!scale(&p->n, &c) || !reduce(s, p)) return 0;
    s->top--;
    return u;
}

NOINLINE static uint8_t calc2(ps_t *s, uint8_t cmd, const char *name, uint8_t *out, rat_t *p, uint8_t j,
                              rat_t *a, rat_t *b, uint8_t o, int8_t inf)
{
    rat_t *r = p + 2, *u = 0;
    int v;
    uint8_t n = 0, k = 0, nk = s->nk, ku = 0;
    for (; k < nk; k++)                         /* √(P(V)): LIMIT at a point, INTEGRAL at P = aV+b */
        if (s->kf[k] == K_ROOT && (cmd != C_LIMIT || inf) && dep(s, s->ka[k], j) &&
            (cmd != C_INTEGRAL || u || !(u = lsub(s, p, j, ku = k)))) return 0;
    k = 0;
    if (cmd == C_LIMIT || cmd == C_DOMINANTTERM) return clim(s, cmd, name, out, p, j, a, b, inf);
    if (cmd == C_SUM || cmd == C_PRODUCT) return csum(s, cmd, name, out, p, j, a, b);
    if (cmd == C_FMIN || cmd == C_FMAX) return cfmin(s, cmd, name, out, p, j, a, b);
    if (cmd == C_ARCLEN) return carc(s, name, out, p, j, a, b);
    if (cmd == C_SERIES) return cser(s, out, p, j, a, o);
    rconst(r, 0);
    v = integ(s, p + 1, p, j, u ? 0 : a, u ? 0 : b, !(a && (haskern(s, a) || haskern(s, b))), a && !u ? 0 : r);
    if (u && v == 1) {                          /* V := u, √V its kernel again, less the constant: ∫1/√(X+1)+X, X²/2+2√(X+1) */
        if (r->n.n || s->nk != nk) return 0;
        rswap(&p[1].n, (uint32_t)1 << (RVAR + j), (uint32_t)1 << (RVAR + KN(s, ku)));
        if (subst(s, p + 1, j, u) != 1) return 0;
        if (isconst(&p[1].d)) {
            for (k = 0; k < p[1].n.n; k++) {
                r->n.n = 1; r->n.t[0] = p[1].n.t[k];
                if (dep(s, r, j)) p[1].n.t[n++] = p[1].n.t[k];
            }
            p[1].n.n = n;
            n = k = 0; rconst(r, 0);
        }
    }
    if (v == 1 && a && defint(s, p + 1, j, a, b) != 1) return 0;  /* ln(X) from 0: improper, refused */
    if (v != 1) return v == 2 ? error(out, "DOMAIN", name, "") : 0;
    if (!r->n.n) return result(s, p + 1, out, r, 0);
    /* the ln's, then the fraction pfrac() left in r: +B, or -B for -B's sign */
    if (p[1].n.n) {
        if (!(k = n = result(s, p + 1, out, p + 3, 0))) return 0;
        PUT(T_ADD);
    }
    if ((n = answer(s, r, out, n)) && k && (out[k + 1] == T_NEG || (out[k + 1] == T_LPAR && out[k + 2] == T_NEG))) {
        pneg(&r->n);
        out[k] = T_SUB;
        n = answer(s, r, out, k + 1);
    }
    return n;
}

/* The entry is f [= g], V [, Y] [, a [, b]] [)]: DERIV(f,V[,order])
   DERIVAT(f,V,a[,order]) TANGENTLINE(f,V,a) NORMALLINE(f,V,a)
   CENTRALDIFF(f,V,h) TAYLOR(f,V,order[,a]) IMPDIF(f=g,V,Y[,order]), and
   calc2()'s INTEGRAL(f,V[,a,b]) LIMIT(f,V,a[,dir]) SUM(f,V,a,b)
   PRODUCT(f,V,a,b) FMIN(f,V[,a[,b]]) FMAX(..) ARCLEN(f,V,a,b)
   SERIES(f,V,order[,a]) DOMINANTTERM(f,V[,a]). DERIV comes here when
   command() could not answer: its old refusals stay. */
NOINLINE static uint8_t calc(ps_t *s, uint8_t cmd, const char *name, uint8_t *out)
{
    /* INTEGRAL..DOMINANTTERM: bit m set when m arguments may follow V */
    static const uint8_t nargs[] = { 5, 6, 4, 4, 7, 7, 4, 6, 3 };
    rat_t *p, *q, *r, *w, *z, *c, *e[2] = {0, 0};
    term_t f;
    uint8_t j, y = 0xFF, m, k, o = 1, lo, taylor = cmd == C_TAYLOR || cmd == C_SERIES;
    int8_t inf = 0;
    int v;

    s->fn = C_TOLN;
    if (!(p = get(s)) || !(q = get(s)) || !(r = get(s)) || !(w = get(s)) || !expr(s, p)) return 0;
    if (cmd == C_IMPDIF && peek(s) == T_EQ) {   /* f = g is f - g = 0 */
        s->i++;
        if (!expr(s, q)) return 0;
        pneg(&q->n);
        if (!raddinto(s, p, q)) return 0;
    }
    if ((j = comvar(s)) == 0xFF && (cmd != C_DERIV || peek(s) == T_COMMA)) goto bad;
    if (cmd == C_IMPDIF && ((y = comvar(s)) == 0xFF || y == j)) goto bad;
    for (m = 0; m < 2 && peek(s) == T_COMMA; m++) {
        s->i++;
        if (!(e[m] = get(s))) return 0;
        if (!m && (cmd == C_LIMIT || cmd == C_DOMINANTTERM) && (inf = infty(s))) rconst(e[m], 0);
        else if (!expr(s, e[m]) || !reduce(s, e[m])) return 0;
    }
    if (peek(s) == T_RPAR) s->i++;
    if (peek(s) == T_COMMA) goto bad;
    if (s->i != s->len || !reduce(s, p)) return 0;
    if (j == 0xFF) {                            /* DERIV(f): X, else the first variable */
        if (!s->nv) { out[0] = 0x30; return 1; }    /* DERIV(sin(1)) */
        for (k = 0; k < s->nv && s->var[k] != 0x58; k++) ;
        j = NV - 1 - (k < s->nv ? k : 0);
    }
    /* a, b: DERIV IMPDIF 0..1 (order), DERIVAT 1..2 (point, order), TAYLOR
       1..2 (order, point), the rest 1 (point, CENTRALDIFF's step) */
    lo = cmd != C_DERIV && cmd != C_IMPDIF;
    if (cmd >= C_INTEGRAL ? !(nargs[cmd - C_INTEGRAL] >> m & 1) : m < lo || m > 1 + (cmd == C_DERIVAT || taylor))
        goto bad;
    if ((z = cmd == C_DERIVAT ? e[1] : taylor || !lo ? e[0] : 0)) {
        if ((v = ord(z)) < 0 || (!v && cmd == C_IMPDIF)) goto bad;
        o = (uint8_t)v;
    }
    c = taylor ? e[1] : lo ? e[0] : 0;
    if (taylor && !c) { c = e[0]; rconst(c, 0); }   /* about 0; the order is read */
    if ((c && dep(s, c, j)) || (e[1] && dep(s, e[1], j))) goto bad;
    /* degrees: sin(V)' is cos(V)·π/180 there, no answer SymCE gives */
    if (s->deg && cmd != C_CENTRALDIFF)
        for (k = 0; k < s->nk; k++)
            if (s->kf[k] <= K_TAN && (dep(s, s->ka[k], j) || (y != 0xFF && dep(s, s->ka[k], y))))
                return error(out, "MODE", "USE RADIAN", "");
    if (cmd >= C_INTEGRAL)
        return calc2(s, cmd, name, out, p, j, taylor ? c : e[0], taylor ? 0 : e[1], o, inf);
    v = 0;
    f.mono = 0; f.rad = 1; f.num = 1;

    if (cmd == C_CENTRALDIFF) {                 /* (f(V+h) - f(V-h))/(2h) */
        if (!c->n.n) return error(out, "DIVIDE BY 0", "", "");
        *q = *p;
        if (!vplus(w, c, j, 1) || (v = evalat(s, q, j, w)) != 1 ||
            !vplus(w, c, j, 0) || (v = evalat(s, p, j, w)) != 1) goto at;
        pneg(&p->n);
        f.den = 2;
        if (!raddinto(s, p, q) || !rmulinto(s, p, c, 1) || !scale(&p->n, &f)) return 0;
    } else if (cmd == C_IMPDIF) {               /* y' = -F_V/F_Y; y'' = dy'/dV + dy'/dY·y' */
        if (!drat(s, q, p, j) || !drat(s, r, p, y)) return 0;
        if (!r->n.n) return error(out, "DIVIDE BY 0", "", "");
        if (!rmulinto(s, q, r, 1)) return 0;
        pneg(&q->n);
        *p = *q;
        while (--o) {
            if (!drat(s, r, p, j) || !drat(s, w, p, y) || !rmulinto(s, w, q, 0) || !raddinto(s, r, w))
                return 0;
            *p = *r;
        }
    } else if (taylor) {                        /* the sum of f⁽ᵏ⁾(a)·(V-a)ᵏ/k! */
        rat_t *d = get(s), *x = get(s);
        if (!x || !vplus(w, c, j, 0)) return 0;
        rconst(r, 0);
        rconst(d, 1);
        for (k = 0; ; k++) {
            *x = *p;
            if ((v = evalat(s, x, j, c)) != 1) goto at;
            if (!rmulinto(s, x, d, 0) || !raddinto(s, r, x)) return 0;
            if (k == o) break;
            f.den = k + 1;
            if (!drat(s, q, p, j) || !rmulinto(s, d, w, 0) || !scale(&d->n, &f)) return 0;
            *p = *q;
        }
        *p = *r;
    } else if (cmd == C_TANGENTLINE || cmd == C_NORMALLINE) {  /* f(a) + f'(a)(V-a), f(a) - (V-a)/f'(a) */
        *q = *p;
        if (!drat(s, r, p, j) || (v = evalat(s, q, j, c)) != 1 || (v = evalat(s, r, j, c)) != 1) goto at;
        if (!vplus(w, c, j, 0)) return 0;
        if (cmd == C_NORMALLINE) {
            if (!r->n.n) return error(out, "DIVIDE BY 0", "", "");
            if (!rmulinto(s, w, r, 1)) return 0;
            pneg(&w->n);
        } else if (!rmulinto(s, w, r, 0)) return 0;
        if (!raddinto(s, q, w)) return 0;
        *p = *q;
    } else {                                    /* DERIV, DERIVAT */
        for (k = 0; k < o; k++) {
            if (!drat(s, q, p, j)) return 0;
            *p = *q;
        }
        if (cmd == C_DERIVAT && (v = evalat(s, p, j, c)) != 1) goto at;
    }
    return result(s, p, out, q, 0);
at:
    return v == 2 ? error(out, "DOMAIN", name, "") : 0;
bad:
    return cmd == C_DERIV ? 0 : error(out, "ARGUMENT", name, "");
}

/* ---- DISTANCE .. DILATE (geo()): points (x,y), numbers, and lines or a
   circle as equations in X and Y, all exact. From DISTANCE to REFLECT an
   argument f alone is the line Y=f. ---- */

#define MAXG 6                /* arguments */
#define JX(s) (NV - 1 - (s)->rank[0x58 - T_VAR0])   /* X's nibble: engine() ranks X and Y */
#define JY(s) (NV - 1 - (s)->rank[0x59 - T_VAR0])
enum { G_NUM = 1, G_PT, G_EQ };                 /* an argument's kind: 2 bits each, the first lowest */
#define G2(a, b)    ((a) | (b) << 2)
#define G3(a, b, c) (G2(a, b) | (c) << 4)

/* d = a + b, or a - b when neg; any of them may be the same */
static int radd3(ps_t *s, rat_t *d, const rat_t *a, const rat_t *b, uint8_t neg)
{
    rat_t *t = get(s);
    int ok;
    if (!t) return 0;
    *t = *b;
    if (neg) pneg(&t->n);
    *d = *a;
    ok = raddinto(s, d, t);
    s->top--;
    return ok;
}

/* d = p·q - r·t; d may be p or r */
static int cross(ps_t *s, rat_t *d, const rat_t *p, const rat_t *q, const rat_t *r, const rat_t *t)
{
    rat_t *w = get(s);
    int ok;
    if (!w) return 0;
    *w = *r;
    *d = *p;
    ok = rmulinto(s, w, t, 0) && rmulinto(s, d, q, 0) && radd3(s, d, d, w, 1);
    s->top--;
    return ok;
}

/* d = x² + y²; d may be either */
static int sumsq(ps_t *s, rat_t *d, const rat_t *x, const rat_t *y)
{
    rat_t *w = get(s);
    int ok;
    if (!w) return 0;
    *w = *y;
    *d = *x;
    ok = rmulinto(s, d, d, 0) && rmulinto(s, w, w, 0) && raddinto(s, d, w);
    s->top--;
    return ok;
}

/* d = |P_j - P_i|², P_k = (x[k], y[k]) */
static int dist2(ps_t *s, rat_t *d, rat_t **x, rat_t **y, uint8_t i, uint8_t j)
{
    rat_t *u = get(s);
    int ok;
    if (!u) return 0;
    ok = radd3(s, u, x[j], x[i], 1) && radd3(s, d, y[j], y[i], 1) && sumsq(s, d, u, d);
    s->top--;
    return ok;
}

/* v = c0 + c1·x + c2·y: line c's left side at (x, y) */
static int lval(ps_t *s, rat_t *v, rat_t **c, const rat_t *x, const rat_t *y)
{
    rat_t *w = get(s);
    int ok;
    if (!w) return 0;
    *v = *x;
    *w = *y;
    ok = rmulinto(s, v, c[1], 0) && rmulinto(s, w, c[2], 0) && raddinto(s, v, w) && radd3(s, v, v, c[0], 0);
    s->top--;
    return ok;
}

/* P_j = P_i + t(P_j - P_i), P_k = (a[k], b[k]) */
static int lerp(ps_t *s, rat_t **a, rat_t **b, uint8_t i, uint8_t j, const rat_t *t)
{
    uint8_t k;
    for (k = 0; k < 2; k++, a = b)
        if (!radd3(s, a[j], a[j], a[i], 1) || !rmulinto(s, a[j], t, 0) || !radd3(s, a[j], a[j], a[i], 0))
            return 0;
    return 1;
}

/* e's numerator as c0 + c1X + c2Y (nc 3: a line) or + c3X² + c4Y² with
   c3 = c4 (nc 5: a circle); 0 when it is neither, or on a limit. A
   denominator free of X and Y is dropped: the same curve.
   ponytail: a limit here says ARGUMENT, not SYMCE LIMIT; 10 slots at most. */
static int gcoef(ps_t *s, const rat_t *e, rat_t **c, uint8_t nc)
{
    uint8_t jx = JX(s), jy = JY(s), k, ex, ey;
    const term_t *x;
    term_t t;
    for (k = 0; k < nc; k++) {
        if (!(c[k] = get(s))) return 0;
        rconst(c[k], 0);
    }
    if (vdeg(&e->d, jx) || vdeg(&e->d, jy)) return 0;
    for (x = e->n.t; x < e->n.t + e->n.n; x++) {
        t = *x;
        ex = t.mono >> 4 * jx & 15;
        ey = t.mono >> 4 * jy & 15;
        k = ex ? 2 * ex - 1 : 2 * ey;          /* 1 X Y X² Y²: 0 1 2 3 4 */
        if ((ex && ey) || k >= nc || (t.rad >> (RVAR + jx) & 1) || (t.rad >> (RVAR + jy) & 1)) return 0;
        t.mono &= ~(15u << 4 * jx | 15u << 4 * jy);
        if (!addterm(&c[k]->n, &t)) return 0;
    }
    return nc == 3 ? c[1]->n.n || c[2]->n.n : c[3]->n.n && same(&c[3]->n, &c[4]->n);
}

/* (x, y) where lines c and d meet: 1; 2 when parallel, 3 when the same line;
   0 on a limit. x and y are neither c nor d. */
static int gsolve(ps_t *s, rat_t *x, rat_t *y, rat_t **c, rat_t **d)
{
    rat_t *t = get(s);
    if (!t || !cross(s, t, c[1], d[2], d[1], c[2]) || !cross(s, x, c[2], d[0], d[2], c[0]) ||
        !cross(s, y, d[1], c[0], c[1], d[0])) return 0;
    if (!t->n.n) return x->n.n || y->n.n ? 2 : 3;
    return rmulinto(s, x, t, 1) && rmulinto(s, y, t, 1);
}

/* (x, y), the circumcenter less P0, with P1 and P2 moved by -P0: it is
   where B·U = |B|²/2 for B each of them. gsolve()'s answer. */
static int circum(ps_t *s, rat_t *x, rat_t *y, rat_t **a, rat_t **b)
{
    static const term_t mhalf = { -1, 2, 0, 1 };
    rat_t *c[3], *d[3], **e = c;
    uint8_t k;
    for (k = 1; k < 3; k++, e = d) {
        if (!radd3(s, a[k], a[k], a[0], 1) || !radd3(s, b[k], b[k], b[0], 1) || !(e[0] = get(s)) ||
            !sumsq(s, e[0], a[k], b[k]) || !scale(&e[0]->n, &mhalf)) return 0;
        e[1] = a[k];
        e[2] = b[k];
    }
    return gsolve(s, x, y, c, d);
}

/* cos d°, d one of 0 30 45 60 90 */
static void cosd(rat_t *r, uint8_t d)
{
    rconst(r, d != 90);
    if (d % 90) { r->n.t[0].den = 2; r->n.t[0].rad = d == 30 ? 3 : d == 45 ? 2 : 1; }
}

/* √r at out + n: exact, or √(r); DOMAIN when r < 0 */
static uint8_t groot(ps_t *s, const char *name, uint8_t *out, uint8_t n, rat_t *r)
{
    rat_t *t = get(s);
    int k = nsign(r);
    if (k < 0) return error(out, "DOMAIN", name, "");
    if (!t) return 0;
    *t = *r;
    if (sqrtinto(s, t)) return answer(s, t, out, n);
    if (k == 2) return 0;
    PUT(T_SQRT);
    if (!(n = answer(s, r, out, n))) return 0;
    PUT(T_RPAR);
    return n;
}

/* (x,y) at out + n */
static uint8_t putpt(const ps_t *s, uint8_t *out, uint8_t n, rat_t *x, rat_t *y)
{
    PUT(T_LPAR);
    if (!(n = answer(s, x, out, n))) return 0;
    PUT(T_LCOMMA);
    if (!(n = answer(s, y, out, n))) return 0;
    PUT(T_RPAR);
    return n;
}

/* the line through (x, y) with normal (p, q), not both 0: p(X-x) + q(Y-y)
   = 0, as Y=y-p(X-x)/q, or X=x when q is 0 */
static uint8_t putline(ps_t *s, uint8_t *out, rat_t *p, rat_t *q, rat_t *x, const rat_t *y)
{
    rat_t *w = get(s);
    uint8_t n = 0;
    if (!w) return 0;
    if (!q->n.n) {
        PUT(0x58); PUT(T_EQ);
        return answer(s, x, out, n);
    }
    if (!vplus(w, x, JX(s), 0) || !rmulinto(s, w, p, 0) || !rmulinto(s, w, q, 1)) return 0;
    pneg(&w->n);
    if (!radd3(s, w, w, y, 0)) return 0;
    PUT(0x59); PUT(T_EQ);
    return answer(s, w, out, n);
}

/* (X-h)²+(Y-k)²=r2; X² when h is 0 */
static uint8_t putcircle(ps_t *s, uint8_t *out, rat_t *h, rat_t *k, rat_t *r2)
{
    rat_t *w = get(s), *c = h;
    uint8_t n = 0, i;
    if (!w) return 0;
    for (i = 0; i < 2; i++, c = k) {
        if (i) PUT(T_ADD);
        if (c->n.n) {
            PUT(T_LPAR);
            if (!vplus(w, c, i ? JY(s) : JX(s), 0) || !(n = answer(s, w, out, n))) return 0;
            PUT(T_RPAR);
        } else PUT(0x58 + i);
        PUT(T_SQR);
    }
    PUT(T_EQ);
    return answer(s, r2, out, n);
}

NOINLINE static uint8_t geo(ps_t *s, uint8_t cmd, const char *name, uint8_t *out)
{
    rat_t *a[MAXG], *b[MAXG], *c[5], *d[3], *p, *q, *r;
    unsigned sg = 0, at;
    uint8_t m, k, i, top, n;
    int32_t v;

    /* (x, y), expr, or expr = expr, each */
    for (m = 0; ; ) {
        if (m == MAXG || !(a[m] = get(s))) return 0;
        at = s->i; top = s->top; k = G_NUM;
        if (peek(s) == T_LPAR) {                /* a point, or a number in parentheses */
            s->i++;
            if ((b[m] = get(s)) && expr(s, a[m]) && peek(s) == T_COMMA) {
                s->i++;
                if (expr(s, b[m]) && ((v = peek(s)) == T_RPAR || v < 0)) { s->i += v >= 0; k = G_PT; }
            }
            if (k != G_PT) { s->i = at; s->top = top; s->depth = 0; }
        }
        if (k != G_PT) {
            if (!expr(s, a[m])) return 0;
            if (peek(s) == T_EQ) {
                s->i++;
                if (!(p = get(s)) || !expr(s, p)) return 0;
                pneg(&p->n);
                if (!raddinto(s, a[m], p)) return 0;
                s->top--;
                k = G_EQ;
            }
        }
        if (!reduce(s, a[m]) || (k == G_PT && !reduce(s, b[m]))) return 0;
        sg |= (unsigned)k << 2 * m++;
        if (peek(s) != T_COMMA) break;
        s->i++;
    }
    if (peek(s) == T_RPAR) s->i++;
    if (s->i != s->len || !(p = get(s)) || !(q = get(s)) || !(r = get(s))) return 0;
    if (cmd <= C_REFLECT) {                     /* f: Y=f */
        for (k = 0; k < m; k++)
            if ((sg >> 2 * k & 3) == G_NUM) {
                if (!addmono(&a[k]->n, &a[k]->d, 1u << 4 * JY(s), 1)) return 0;
                sg += 2u << 2 * k;
            }
        if (sg == G2(G_EQ, G_PT)) {             /* the point first */
            c[0] = a[0]; a[0] = a[1]; a[1] = c[0]; b[0] = b[1]; sg = G2(G_PT, G_EQ);
        }
    }

    switch (cmd) {
    case C_DISTANCE:                            /* |P0P1|; |c0 + c1x + c2y|/√(c1² + c2²) */
        if (sg == G2(G_PT, G_PT)) return dist2(s, p, a, b, 0, 1) ? groot(s, name, out, 0, p) : 0;
        if (sg != G2(G_PT, G_EQ) || !gcoef(s, a[1], c, 3)) goto bad;
        if (!lval(s, p, c, a[0], b[0]) || !sumsq(s, q, c[1], c[2]) || !sqrtinto(s, q) || (v = nsign(p)) > 1)
            return 0;
        if (v < 0) pneg(&p->n);
        if (!rmulinto(s, p, q, 1)) return 0;
        break;
    case C_SLOPE:
        if (sg == G2(G_PT, G_PT)) {
            if (!radd3(s, p, b[1], b[0], 1) || !radd3(s, q, a[1], a[0], 1)) return 0;
        } else if (sg == G_EQ && gcoef(s, a[0], c, 3)) {
            *p = *c[1]; pneg(&p->n); *q = *c[2];
        } else goto bad;
        if (!q->n.n) return error(out, "DIVIDE BY 0", "", "");
        if (!rmulinto(s, p, q, 1)) return 0;
        break;
    case C_PARALLEL: case C_PERPENDICULAR:      /* normal (c1, c2); (c2, -c1) */
        if (sg != G2(G_PT, G_EQ) || !gcoef(s, a[1], c, 3)) goto bad;
        k = cmd == C_PERPENDICULAR;
        *p = *c[1 + k]; *q = *c[2 - k];
        if (k) pneg(&q->n);
        return putline(s, out, p, q, a[0], b[0]);
    case C_INTERSECT:
        if (sg != G2(G_EQ, G_EQ) || !gcoef(s, a[0], c, 3) || !gcoef(s, a[1], d, 3)) goto bad;
        if ((k = gsolve(s, p, q, c, d)) == 2) return error(out, "NO SOLUTION", "PARALLEL", "");
        if (k == 3) return error(out, "ALWAYS TRUE", "SAME LINE", "");
        if (!k) return 0;
        return putpt(s, out, 0, p, q);
    case C_REFLECT:                             /* P - 2(c0 + c1x + c2y)/(c1² + c2²)·(c1, c2) */
        if (sg != G2(G_PT, G_EQ) || !gcoef(s, a[1], c, 3)) goto bad;
        rconst(r, 2);
        if (!lval(s, p, c, a[0], b[0]) || !sumsq(s, q, c[1], c[2]) || !rmulinto(s, p, q, 1) ||
            !rmulinto(s, p, r, 0)) return 0;
        for (k = 0; k < 2; k++)
            if (!rmulinto(s, c[1 + k], p, 0) || !radd3(s, k ? b[0] : a[0], k ? b[0] : a[0], c[1 + k], 1))
                return 0;
        return putpt(s, out, 0, a[0], b[0]);
    case C_LINE: case C_PERPBISECTOR:           /* normal (dy, -dx), (m, -1); (dx, dy) at the midpoint */
        if (sg == G2(G_PT, G_NUM) && cmd == C_LINE) { *p = *a[1]; rconst(q, -1); }
        else if (sg == G2(G_PT, G_PT)) {
            k = cmd == C_LINE;
            if (!radd3(s, k ? q : p, a[1], a[0], 1) || !radd3(s, k ? p : q, b[1], b[0], 1)) return 0;
            if (k) pneg(&q->n);
            else {
                rconst(r, 1); r->n.t[0].den = 2;
                if (!lerp(s, a, b, 1, 0, r)) return 0;
            }
        } else goto bad;
        if (!p->n.n && !q->n.n) goto bad;       /* one point twice */
        return putline(s, out, p, q, a[0], b[0]);
    case C_MIDPOINT: case C_PARTITION:          /* P0 + t(P1 - P0): t 1/2, m/(m + n) */
        if (cmd == C_MIDPOINT && sg == G2(G_PT, G_PT)) { rconst(r, 1); r->n.t[0].den = 2; }
        else if (cmd == C_PARTITION && sg == (G2(G_PT, G_PT) | G2(G_NUM, G_NUM) << 4)) {
            if (!radd3(s, r, a[2], a[3], 0)) return 0;
            if (!r->n.n) return error(out, "DIVIDE BY 0", "", "");
            if (!rmulinto(s, a[2], r, 1)) return 0;
            r = a[2];
        } else goto bad;
        return lerp(s, a, b, 0, 1, r) ? putpt(s, out, 0, a[1], b[1]) : 0;
    case C_AREA: case C_PERIMETER: case C_CENTROID:
        if (m < 3 || sg != (0xAAAu & ((1u << 2 * m) - 1))) goto bad;   /* points only */
        rconst(p, 0); rconst(q, 0);
        for (k = 0; k < m; k++) {
            i = k + 1 < m ? k + 1 : 0;
            if (cmd == C_AREA ? !cross(s, r, a[k], b[i], a[i], b[k]) || !raddinto(s, p, r) :
                cmd == C_PERIMETER ? !dist2(s, r, a, b, k, i) || !sqrtinto(s, r) || !raddinto(s, p, r) :
                !radd3(s, p, p, a[k], 0) || !radd3(s, q, q, b[k], 0)) return 0;
        }
        if (cmd == C_PERIMETER) break;
        rconst(r, cmd == C_AREA ? 2 : m);
        if (!rmulinto(s, p, r, 1)) return 0;
        if (cmd == C_AREA) {                    /* the shoelace, made positive */
            if ((v = nsign(p)) > 1) return 0;
            if (v < 0) pneg(&p->n);
            break;
        }
        return rmulinto(s, q, r, 1) ? putpt(s, out, 0, p, q) : 0;
    case C_INCENTER:                            /* (|P1P2|P0 + |P2P0|P1 + |P0P1|P2)/perimeter */
        if (sg != G3(G_PT, G_PT, G_PT)) goto bad;
        rconst(p, 0); rconst(q, 0); rconst(r, 0);
        for (k = 0; k < 3; k++)                 /* the sides first: the points move below */
            if (!(c[k] = get(s)) || !dist2(s, c[k], a, b, (k + 1) % 3, (k + 2) % 3) || !sqrtinto(s, c[k]) ||
                !radd3(s, r, r, c[k], 0)) return 0;
        for (k = 0; k < 3; k++)
            if (!rmulinto(s, a[k], c[k], 0) || !rmulinto(s, b[k], c[k], 0) ||
                !radd3(s, p, p, a[k], 0) || !radd3(s, q, q, b[k], 0)) return 0;
        return rmulinto(s, p, r, 1) && rmulinto(s, q, r, 1) ? putpt(s, out, 0, p, q) : 0;
    case C_CIRCUMCENTER: case C_ORTHOCENTER:    /* U; A + B + C - 2U */
        if (sg != G3(G_PT, G_PT, G_PT)) goto bad;
        if (!(k = circum(s, p, q, a, b))) return 0;
        if (k > 1) return error(out, "NO SOLUTION", "COLLINEAR", "");
        for (k = 0; k < 2; k++) {
            rat_t *u = k ? q : p, **e = k ? b : a;
            if (cmd == C_ORTHOCENTER) {
                static const term_t m2 = { -2, 1, 0, 1 };
                if (!scale(&u->n, &m2) || !radd3(s, u, u, e[1], 0) || !radd3(s, u, u, e[2], 0)) return 0;
            }
            if (!radd3(s, u, u, e[0], 0)) return 0;
        }
        return putpt(s, out, 0, p, q);
    case C_HYPOT: case C_LEG:                   /* √(a² + b²), √(c² - a²) */
        if (sg != G2(G_NUM, G_NUM)) goto bad;
        if (!rmulinto(s, a[0], a[0], 0) || !rmulinto(s, a[1], a[1], 0) || !radd3(s, p, a[0], a[1], cmd == C_LEG))
            return 0;
        return groot(s, name, out, 0, p);
    case C_CIRCLE:
        if (sg == G2(G_PT, G_NUM)) {            /* center, radius */
            if (!rmulinto(s, a[1], a[1], 0)) return 0;
            return putcircle(s, out, a[0], b[0], a[1]);
        }
        if (sg == G2(G_PT, G_PT))               /* center, a point on it */
            return dist2(s, p, a, b, 0, 1) ? putcircle(s, out, a[0], b[0], p) : 0;
        if (sg == G3(G_PT, G_PT, G_PT)) {       /* through three points */
            if (!(k = circum(s, p, q, a, b))) return 0;
            if (k > 1) return error(out, "NO SOLUTION", "COLLINEAR", "");
            if (!sumsq(s, r, p, q) || !radd3(s, p, p, a[0], 0) || !radd3(s, q, q, b[0], 0)) return 0;
            return putcircle(s, out, p, q, r);
        }
        /* its equation: center (-c1, -c2)/(2c3), radius² that squared less c0/c3 */
        if ((sg != G_EQ && sg != G_NUM) || !gcoef(s, a[0], c, 5)) goto bad;
        rconst(r, -2);
        *p = *c[1]; *q = *c[2];
        if (!rmulinto(s, r, c[3], 0) || !rmulinto(s, p, r, 1) || !rmulinto(s, q, r, 1) ||
            !sumsq(s, r, p, q) || !rmulinto(s, c[0], c[3], 1) || !radd3(s, r, r, c[0], 1)) return 0;
        n = 0;
        PUT(0x43); PUT(T_EQ);
        if (!(n = putpt(s, out, n, p, q))) return 0;
        PUT(T_LCOMMA); PUT(0x52); PUT(T_EQ);
        return groot(s, name, out, n, r);
    case C_ROTATE: case C_DILATE:               /* C + R(P - C), C + k(P - C); C the origin if not given */
        if (sg == G2(G_PT, G_NUM)) {
            if (!(a[2] = get(s)) || !(b[2] = get(s))) return 0;
            rconst(a[2], 0); rconst(b[2], 0);
        } else if (sg != G3(G_PT, G_NUM, G_PT)) goto bad;
        if (cmd == C_DILATE) return lerp(s, a, b, 2, 0, a[1]) ? putpt(s, out, 0, a[0], b[0]) : 0;
        /* degrees, a multiple of 30 or 45: cos and sin exact */
        if (!intval(a[1], &v)) return 0;
        v %= 360;
        if (v < 0) v += 360;
        if ((i = v % 90) % 30 && i != 45) return 0;
        cosd(p, i); cosd(q, 90 - i);
        if (!radd3(s, a[0], a[0], a[2], 1) || !radd3(s, b[0], b[0], b[2], 1) ||
            !cross(s, r, a[0], p, b[0], q)) return 0;
        pneg(&q->n);
        if (!cross(s, b[0], b[0], p, a[0], q)) return 0;
        a[0] = r;
        for (i = v / 90; i; i--) {              /* a quarter turn: (x, y) to (-y, x) */
            p = a[0]; a[0] = b[0]; b[0] = p;
            pneg(&a[0]->n);
        }
        if (!radd3(s, a[0], a[0], a[2], 0) || !radd3(s, b[0], b[0], b[2], 0)) return 0;
        return putpt(s, out, 0, a[0], b[0]);
    }
    return answer(s, p, out, 0);
bad:
    return error(out, "ARGUMENT", name, "");
}

/* SOLVE(N=0,V) with N = A + B√P, √P its one root in V (√V, or the kernel
   √U, abs(S) with U = S²): N := A²-B²P, t = A, B, P, D for rok(). 1, t 0
   when N has no root in V; 0 for two roots, or A²-B²P 0: √(X²)=X. */
NOINLINE static int rsplit(ps_t *s, rat_t *p, uint8_t j, rat_t **t)
{
    rat_t *a;
    poly_t *b;
    uint32_t rb = 0, m;
    uint8_t i, k, kr = 0;
    *t = 0;
    for (i = 0; i < p->n.n; i++) {
        if (!(m = p->n.t[i].rad & ~(IMAG | RNUM)) || m == rb) continue;
        for (k = 0; k < s->nk && m != (uint32_t)1 << (RVAR + KN(s, k)); k++) ;
        if (m != (uint32_t)1 << (RVAR + j) && (k == s->nk || !dep(s, s->ka[k], j))) continue;
        if (rb || (k < s->nk && s->kf[k] != K_ROOT)) return 0;
        rb = m; kr = k;
    }
    if (!rb) return 1;
    if (!(a = get(s)) || !get(s) || !get(s) || !get(s) || !(b = tmp(s))) return 0;
    rconst(a, 0); rconst(a + 1, 0);
    for (i = 0; i < p->n.n; i++) {
        m = p->n.t[i].rad & rb;
        p->n.t[i].rad &= ~rb;
        if (!addterm(&a[m != 0].n, &p->n.t[i])) return 0;
    }
    if (kr < s->nk) a[2] = *s->ka[kr];          /* U's bar is 1: rootk() */
    else { rconst(a + 2, 1); a[2].n.t[0].mono = 1u << 4 * j; }
    a[3].n = p->d; pconst(&a[3].d, 1);
    p->n = a[0].n; *b = a[1].n;
    if (!mulinto(s, &p->n, &a[0].n) || !mulinto(s, b, &a[1].n) || !mulinto(s, b, &a[2].n)) return 0;
    pneg(b);
    for (i = 0; i < b->n; i++) if (!addterm(&p->n, &b->t[i])) return 0;
    *t = a;                                     /* D's roots out in rok(): reduce() may drop them */
    return p->n.n && reduce(s, p) == 1;
}

/* v a root of rsplit()'s A²-B²P: 1 when A+B√P is 0 there too, A and B of
   opposite signs (or 0) with P >= 0, and D is not 0; 0 when not; -1 when
   that is not proved */
NOINLINE static int rok(ps_t *s, const rat_t *t, uint8_t j, const rat_t *v)
{
    rat_t *w = get(s);
    int g[4];
    uint8_t i;
    for (i = 0; i < 4; i++) {
        if (!w) return -1;
        *w = t[i];
        if (evalat(s, w, j, v) != 1 || (g[i] = nsign(w)) > 1) return -1;
    }
    s->top--;
    return g[3] && g[2] >= 0 && g[0] * g[1] <= 0;
}

/* The command's arguments are the entry now: expr [= expr | , expr]
   [, variable] [)]. The answer's length, SYMCE_ERR with the message written,
   or 0: past what SymCE can do. */
NOINLINE static uint8_t command(ps_t *s, uint8_t cmd, const char *name, uint8_t *out, uint8_t split)
{
    rat_t *p = get(s), *q = 0, *r, *w = 0;
    poly_t *a, *b;
    fl_t fl;
    struct { int64_t re, im; uint8_t k; } key[MAXF], kt;
    int32_t l = 1, g = 0;
    term_t f;
    uint8_t n = 0, j = 0, k, m, cnt = 0;
    int c;

    if (!p || !expr(s, p)) return 0;
    if (cmd <= C_CSOLVE) {                      /* A=B, and SOLVE(A=B,V) all of it */
        if (peek(s) != T_EQ) goto syntax;
        s->i++;
        if (!(q = get(s)) || !expr(s, q)) return 0;
        if (cmd == C_RIGHT) p = q;
        else if (cmd >= C_SOLVE) {
            pneg(&q->n);
            if (!raddinto(s, p, q)) return 0;
            if (peek(s) != T_COMMA || s->i + 1 >= s->len || !is_var(s->in[s->i + 1])) goto syntax;
        }
        q = 0;
    } else if (cmd >= C_POLYREMAINDER && cmd <= C_POLYGCD) {
        if (peek(s) != T_COMMA) return error(out, "ARGUMENT", name, "");
        s->i++;
        if (!(q = get(s)) || !expr(s, q)) return 0;
    }
    if (peek(s) == T_COMMA) {
        s->i++;
        if (!is_var(c = peek(s))) return 0;
        j = NV - 1 - s->rank[c - T_VAR0];
        s->i++;
    } else if (s->nv) {                         /* X, else the first variable */
        for (k = 0; k < s->nv && s->var[k] != 0x58; k++) ;
        j = NV - 1 - (k < s->nv ? k : 0);
    }                                           /* none: j = 0, and every power is 0 */
    if (peek(s) == T_RPAR) s->i++;
    if (s->i != s->len || reduce(s, p) != 1 || (q && reduce(s, q) != 1)) return 0;

    if (cmd <= C_RIGHT || cmd == C_EXPAND || cmd == C_COMDENOM) return answer(s, p, out, 0);
    if (cmd == C_SOLVE) { if (!rsplit(s, p, j, &w)) return 0; }
    else for (k = 0; k < s->nk; k++)            /* √(P(V)) is no polynomial in V: DERIV is calc()'s */
        if (s->kf[k] == K_ROOT && dep(s, s->ka[k], j)) return 0;
    if (cmd == C_DERIV) {
        r = get(s); a = tmp(s); b = tmp(s);
        if (!b || !xd(a, &p->n, j) || !mulinto(s, a, &p->d) || !xd(b, &p->d, j) || !mulinto(s, b, &p->n))
            return 0;
        pneg(b);
        r->n = *a;
        for (k = 0; k < b->n; k++) if (!addterm(&r->n, &b->t[k])) return 0;
        r->d = p->d;
        if (!mulinto(s, &r->d, &p->d)) return 0;
        for (k = 0; k < r->d.n; k++)
            if ((r->d.t[k].mono += 1u << 4 * j) & OVF) return 0;
        return reduce(s, r) == 1 ? answer(s, r, out, 0) : 0;
    }
    /* a polynomial in V: V neither below the bar nor under a root */
    if (cmd >= C_CPOLYROOTS && cmd <= C_POLYQUOTIENT &&
        (vdeg(&p->d, j) || vdeg(&p->n, j) == 0xFF || (q && (vdeg(&q->d, j) || vdeg(&q->n, j) == 0xFF))))
        return error(out, "DATA TYPE", name, "");
    if (cmd == C_POLYDEGREE) return number(out, 0, vdeg(&p->n, j));
    if (cmd == C_POLYCOEFFS) {                  /* {a_m,...,a_0}, each over p's denominator */
        r = get(s);
        PUT(T_LBRACE);
        for (m = vdeg(&p->n, j) + 1; m--; ) {
            if (!r) return 0;
            slice(&r->n, &p->n, 4 * j, m);
            r->d = p->d;
            if (reduce(s, r) != 1 || !(n = answer(s, r, out, n))) return 0;
            PUT(m ? T_LCOMMA : T_RBRACE);
        }
        return n;
    }
    if (cmd <= C_POLYQUOTIENT && cmd >= C_POLYREMAINDER) {
        /* pn = A·qn + R, so P = (A·qd/pd)·Q + R/pd */
        if (!q->n.n) return error(out, "DIVIDE BY 0", "", "");
        if (!(a = tmp(s))) return 0;
        a->n = 0;
        if (!vrem(&p->n, &q->n, a, j)) return 0;
        if (cmd == C_POLYQUOTIENT) { p->n = *a; if (!mulinto(s, &p->n, &q->d)) return 0; }
        return reduce(s, p) == 1 ? answer(s, p, out, 0) : 0;
    }
    fl.n = 0;
    fl.c.num = fl.c.den = 1; fl.c.mono = 0; fl.c.rad = 1;
    if (cmd == C_POLYGCD) {
        /* P over what reduce() leaves of P/Q, whole coefficients with no
           common factor and the first positive, times the gcd of the two
           contents: gcd(a/b, c/d) = gcd(a, c)/lcm(b, d). gcd(4X+4, 6X+6) is
           2X+2, gcd(12, 18) 6, gcd(X/2+1/2, X+1) X/2+1/2. */
        if (!isconst(&p->d) || !isconst(&q->d)) return error(out, "DATA TYPE", name, "");
        a = &p->n; b = &q->n;
        if (hasrad(a) || hasrad(b) || !lg(a, &l, &g) || !lg(b, &l, &g)) return 0;
        if (!a->n) *a = *b;
        else if (b->n) {
            if (!(r = get(s))) return 0;
            r->n = *a; r->d = *b;
            if (reduce(s, r) != 1 || divinto(s, a, &r->n) != 1) return 0;
        }
        f.mono = 0; f.rad = 1;
        if (a->n && (!prim(&fl, a, 0) || !rset(&f, g, l) || !scale(a, &f))) return 0;
        return emit(s, a, out, 0);
    }
    fl.split = cmd >= C_FACTOR ? split : 2;
    if (cmd == C_CFACTOR || (cmd >= C_CSOLVE && cmd <= C_CPOLYROOTS)) fl.split |= 4;
    for (k = 0; k < MAXF; k += 2) {
        if (!(r = get(s))) return 0;
        fl.f[k] = &r->n; fl.f[k + 1] = &r->d;
    }
    if (cmd >= C_FACTOR) {
        if (!p->n.n) { PUT(0x30); return n; }
        if (hasrad(&p->n)) return 0;
        if (!s->nv) {                           /* a number: its primes */
            a = &p->n;
            if (a->t[0].num < 0) PUT(T_NEG);
            if (!(n = primes(out, n, a->t[0].num < 0 ? -a->t[0].num : a->t[0].num))) return 0;
            if (a->t[0].den == 1) return n;
            PUT(T_DIV);
            if (!ppow(a->t[0].den)) PUT(T_LPAR);
            if (!(n = primes(out, n, a->t[0].den))) return 0;
            if (!ppow(a->t[0].den)) PUT(T_RPAR);
            return n;
        }
        if (!fac(s, &fl, &p->n, 0) || (!isconst(&p->d) && !fac(s, &fl, &p->d, 1))) return 0;
        return putfl(s, &fl, out);
    }

    /* The zeros of the numerator; reduce() left none in common with the
       denominator. Degree 1 in V: V = -B/A, whatever else is in it. Over C
       (split & 4) pushquad() split the quadratics with no real root too. */
    a = &p->n;
    if (!a->n) return error(out, "ALWAYS TRUE", "", "");
    if (cmd == C_NROOTS) return nroots(s, a, j, out);
    if (!(m = vdeg(a, j)) && !isconst(a)) return 0;
    if (m == 1) { *fl.f[0] = *a; fl.e[0] = 1; fl.n = 1; }
    else if (m && (m == 0xFF || !fac(s, &fl, a, 0))) return 0;
    r = get(s);
    for (k = 0; k < fl.n; k++) {
        if (!(m = vdeg(fl.f[k], j))) continue;
        if (m == 1) {                           /* in order: real, then by real part */
            if (!r || !root(s, r, fl.f[k], j)) return 0;
            if (w && (c = rok(s, w, j, r)) != 1) {  /* A+B√P squared: its extra roots out */
                if (c) return 0;
                continue;
            }
            kt.k = k;
            kt.im = univ(a) == MULTI ? 0 : approx(&r->n, IMAG);
            kt.re = univ(a) == MULTI ? k : approx(&r->n, 0) + (kt.im ? (int64_t)1 << 58 : 0);
            for (m = cnt++; m && (key[m - 1].re > kt.re || (key[m - 1].re == kt.re && key[m - 1].im > kt.im)); m--)
                key[m] = key[m - 1];
            key[m] = kt;
        } else if (fl.split & 4 || univ(fl.f[k]) == MULTI || !noroot(fl.f[k], j)) return 0;
    }
    if (!cnt && cmd < C_CZEROS) return error(out, "NO SOLUTION", "", "");
    if (cmd >= C_CZEROS) PUT(T_LBRACE);
    for (k = 0; k < cnt; k++) {
        if (!root(s, r, fl.f[key[k].k], j)) return 0;
        if (cmd >= C_CZEROS) { if (k) PUT(T_LCOMMA); }
        else {
            if (k) PUT(T_OR);
            PUT(s->var[NV - 1 - j]);
            PUT(T_EQ);
        }
        if (!(n = putroot(s, r, out, n))) return 0;
    }
    if (cmd >= C_CZEROS) PUT(T_RBRACE);
    return n;
syntax:
    return error(out, "SYNTAX", name, cmd < C_SOLVE ? "(A=B)" : "(A=B,X)");
}

/* The entry is `len` tokens at `in`. Writes the answer, 1..MAXOUT display
   tokens, to `out` and returns its length; 0 leaves the entry to the OS.
   `mode` is MODE_MP in MathPrint, where the screen fits more (width()), and
   MODE_DEC when the answer is to be in decimals wherever they end.
   `work` is SYMCE_WORK bytes of scratch. `ans` is the last answer as a length
   byte then its tokens, or 0; it must not overlap `out`. MODE_DEG: degrees. */
static uint8_t engine(const uint8_t *in, unsigned len, uint8_t *out, void *work,
                      const uint8_t *ans, uint8_t mode)
{
    uint8_t n, cmd, split, own;
    const char *name = cmds;
    ps_t s;
    rat_t *p;
    unsigned k = 0;

    if (!len) return 0;
    /* A command: its name in letters, then '(', and the rest its arguments.
       From here on it is never left to the OS, which would read it as a
       product of variables. */
    for (cmd = 1; cmd <= NCMD; cmd++) {
        for (k = 0; name[k] && k < len && in[k] == (uint8_t)name[k]; k++) ;
        if (!name[k] && k < len && in[k] == T_LPAR) break;
        while (*name++) ;
    }
    if (cmd > NCMD) cmd = 0;
    else { in += k + 1; len -= k + 1; }
    /* Ans counts only if the entry uses it: 2+2 stays the OS's even right
       after 4X. When Ans is SymCE's own answer (own), the OS's Ans was never
       set (hook.c), so the entry is answered here or is an error screen, never
       left to the OS: POLYDEGREE(X²), then +1, is 3, not the OS's stale Ans
       plus 1 (measured: 1; and 2√(2), then *3, was 3). */
    for (k = 0; k < len && in[k] != T_ANS; k++) ;
    s.ans = k < len && ans && ans[0] && (ans[0] <= MAXOUT || ans[0] == ANS_REAL) ? ans : 0;
    own = s.ans && s.ans[0] != ANS_REAL;
    n = 0;
    if (len > MAXLEN) goto limit;
    s.in = in; s.len = len; s.i = 0;
    s.pool = (rat_t *)work; s.top = 0; s.depth = 0; s.nv = 0;
    s.dec = mode & MODE_DEC;
    s.mp = mode & MODE_MP;
    s.deg = mode & MODE_DEG;
    s.lim = NPOOL; s.fn = 0; s.nk = 0; s.inans = 0; s.tb = 0;

    /* Ranks go out in alphabetical order, so comparing two packed monomials
       is already the tie-break the output wants. A byte in the variable range
       can also be the second half of a two-byte token; that over-counts, but
       the parse refuses the entry at the first byte of such a token anyway. */
    for (k = 0; k < NVTOK; k++) s.rank[k] = 0;
    for (k = 0; k < len; k++) if (is_var(in[k])) s.rank[in[k] - T_VAR0] = 1;
    if (cmd >= C_DISTANCE) s.rank[0x58 - T_VAR0] = s.rank[0x59 - T_VAR0] = 1;   /* lines, circles */
    if (own)
        for (k = 1; k <= s.ans[0]; k++)
            if (is_var(s.ans[k])) s.rank[s.ans[k] - T_VAR0] = 1;
    for (k = 0; k < NVTOK; k++)
        if (s.rank[k]) {
            if (s.nv == NV) goto limit;
            s.var[s.nv] = (uint8_t)(T_VAR0 + k);
            s.rank[k] = s.nv++;
        }
    if (cmd >= C_TOLN) {
        n = cmd >= C_DISTANCE ? geo(&s, cmd, name, out) :
            cmd > C_TCOLLECT ? calc(&s, cmd, name, out) : fncmd(&s, cmd, name, out);
        /* a point's (3/2,1/2) as Classic counts it: width() stacks fractions
           only where parentheses are not a point's */
        if (n == SYMCE_ERR || (n && width(out, n, s.mp && cmd < C_DISTANCE) <= SCR_WIDTH)) return n;
        goto wide;
    }
    /* A factored answer too wide for the screen is tried again without
       splitting quadratics over their roots. */
    if (cmd)
        for (split = 1; ; split = 0) {
            s.i = 0; s.top = 0; s.depth = 0;
            n = command(&s, cmd, name, out, split);
            if (!n && cmd == C_DERIV) {         /* with functions, or an order */
                s.i = 0; s.top = 0; s.depth = 0;
                n = calc(&s, cmd, name, out);
            }
            if (n == SYMCE_ERR || (n && width(out, n, s.mp) <= SCR_WIDTH)) return n;
            if (cmd < C_FACTOR || !split) goto wide;
        }
    /* Plain arithmetic is the OS's job, unless a root in it stays one: √(8)
       is 2√(2), but √(4) is the OS's 2, and so is anything in DEC. */
    for (k = 0; k < len && in[k] != T_SQRT; k++) ;
    if (!own && !s.nv && (k == len || mode & MODE_DEC)) return 0;

    p = get(&s);
    if (expr(&s, p) && s.i == len && reduce(&s, p) == 1) {
        if (!own && !s.nv && !hasrad(&p->n)) return 0;
        n = answer(&s, p, out, 0);
        if (n && width(out, n, s.mp) <= SCR_WIDTH) return n;
    }
    /* ponytail: with own, a list or an equation in Ans, or sin(Ans), is a
       SYMCE LIMIT; the OS could only get it wrong. */
    if (!own) return 0;
wide:
    if (n) return error(out, "TOO WIDE", s.mp ? "" : "TRY MATHPRINT", "");
limit:
    return cmd || own ? error(out, "SYMCE LIMIT", cmd ? name : "ANS", "") : 0;
}

/* engine(), and after an answer, at out[MAXOUT], the columns Classic draws
   it in: hook.c right-aligns it by them.
   The engine runs from flash, and a CE before rev M fetches each flash byte
   in 6 + (flash wait states) cycles. The OS leaves 4; for the run, 3, what
   every CEdev program has run at since 2017: FACTOR(X^4-5X^2+6) 729 -> 661 ms
   in CEmu, the same answer. Lower is faster and misreads flash now and then,
   so never. Rev M+ (serial flash, cached) ignores the port and reads it as 0;
   0, or a value some other program already lowered, is left alone. */
uint8_t symce_engine(const uint8_t *in, unsigned len, uint8_t *out, void *work,
                     const uint8_t *ans, uint8_t mode)
{
#ifdef __ez80__
    volatile uint8_t *const ws = (uint8_t *)0xE00005;
    uint8_t old = *ws;
    if (old > 3)
        *ws = 3;
#endif
    uint8_t n = engine(in, len, out, work, ans, mode);
#ifdef __ez80__
    if (old > 3)
        *ws = old;
#endif
    unsigned w;
    if (n && n != SYMCE_ERR) {
        w = width(out, n, 0);
        out[MAXOUT] = w > 255 ? 255 : (uint8_t)w;
    }
    return n;
}
