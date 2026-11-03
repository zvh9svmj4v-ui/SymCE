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
 * Commands, typed from the SymCE menu (menu.c) as lowercase tokens, one
 * expression then an optional variable, X by default, else the first one.
 * SOLVE takes an equation and the variable to solve for, always:
 * SOLVE(A=B,V); anything short of that is ERROR: SYNTAX.
 *
 *     EXPAND((X+1)²) -> X²+2X+1        DERIV(X³,X) -> 3X²
 *     FACTOR(X²-4) -> (X-2)(X+2)       FACTOR(X²+X-1) -> (2X+1-√(5))(2X+1+√(5))/4
 *     FACTOR(12) -> 2²*3               SOLVE(X²=2,X) -> X=-√(2) or X=√(2)
 *     SOLVE(X+2=4,X) -> X=2            SOLVE(X+2Y=3,Y) -> Y=(3-X)/2
 *
 * In SOLVE and CSOLVE every letter but the one solved for stands for the
 * value stored in it, as in the OS's own solve(): 5→X, then SOLVE(X+2Y=3,Y),
 * is Y=-1. A letter with no value stored stays a letter, and the stored value
 * of the solved-for letter is ignored. A value is exact when it is a decimal
 * (preal(): 5, -3, 2.5); a stored 1/3 is ERROR: SYMCE LIMIT, and so is a
 * complex value. Nothing stored is changed, and no other command looks.
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
 * a zero divisor, fractional powers, a root of more than one term, a power over 15 anywhere on the way, a
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
#include <stdarg.h>

#define NV        6           /* distinct variables, 4 bits of power each */
#define MAXT      24          /* terms in one polynomial */
#define NPOOL     24          /* values in `work`; a scratch polynomial takes one */
#define MAXDEPTH  6           /* nested parentheses; each level holds 2 of NPOOL */
#define MAXPOW    31          /* largest exponent operand; X^16 fails in madd() */
#define LIM       999999L     /* any stored numerator or denominator */
#define BIG       1000000000L /* intermediates: two still add inside int32 */
#define MAXLEN    64          /* longer entries are left to the OS */
#define SCR_WIDTH 26          /* home screen columns; the answer must fit */
#define MAXOUT    64          /* answer tokens; MathPrint fits more than 26 */
#define MODE_DEC  0x01        /* symce_engine's mode: MODE ANSWERS: DEC */
#define MODE_MP   0x20        /* and MathPrint, bit 5 as in the OS's flags */
#define MODE_DEG  0x04        /* and degrees, bit 2 as in trigFlags */
#define MODE_INNER 0x08       /* nested(): a command inside another one; its answer may be any width */
#define MODE_NEST 0x10        /* nested(): the entry had commands inside, now answered: never left to the OS */
#define MODE_EVAL 0x40        /* DESOLVE's own calls: an expression with functions, any size, any width, numbers answered too */
#define MODE_STEP 0x02        /* steps( : record the steps of the outermost command in the log (below ps_t) */
#define MODE_PRGM 0xC0        /* symce_engine only (both high bits, which no other caller sets): expr("cmd(...)") inside a TI-BASIC program,
                                 the parser hook's A=1 (app/main.asm phook). No Ans, any width, no steps( , no Ans sync; the answer
                                 is tokens ready for the OS to parse again: a number as it is, anything symbolic as a string (2A first) */
#define MODE_WRAP 0x80        /* symce_engine only (bit 7 set, bit 6 clear): wrap bare commands in TI-BASIC into expr("...") */
/* The step log, STOFF bytes into `work` (0xD13800 on the calculator: past the pool and nested()'s
   buffers, in the grapher's undo-log area (dead whenever the engine runs: the pool already covers
   its start; the text park (graph.c symce_text) shares 0xD13800 but is used only inside a grapher
   text call, never between the engine run and the end of steps_show), below colTab 0xD13D60):
     0 magic 0xA5 once the run is finished, 1 step count, 2 flags (1: full, 2: steps unreliable),
     3 entry length, 4..67 the entry, 68..69 bytes used, 70.. the steps, each
     [rule][plen|rule bit 8 << 7][param tokens][elen][expression tokens], then [alen][answer tokens]. */
#define STOFF (SYMCE_WORK + 0x550)   /* 0x4900 on the calculator; the host's rat_t is bigger */
#define STCAP 0x3B0
#define STHDR 70
#define STMAGIC 0xA5
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
#define T_AND  0x40           /* " and " */
#define T_PRIME 0xAE          /* ' (2nd ANGLE 2): Y', Y'' */
#define T_STO  0x04           /* the store arrow */
/* Lists and i go out as the GLYPHS the OS result routine maps to their tokens
   (table 0xB1AB2, docs §7): the comma token 0x2B would be read as glyph '+'
   and filed as +, and the i token 0x2C as a comma. */
#define T_LBRK 0x06           /* [ and ], a matrix's brackets: the OS's tokens, and as drawn */
#define T_RBRK 0x07
#define T_LBRACE 0x08
#define T_RBRACE 0x09
#define T_LCOMMA 0x2C         /* glyph ',': filed and drawn as token 0x2B */
#define T_I      0xD7         /* glyph i: filed and drawn as token 0x2C */
/* Functions, in and out; none of these is in that table. Two-byte: logBASE(
   is EF 34, e BB 31, small letters BB B0.. */
#define T_SIN  0xC2
#define T_TAN  0xC6
#define T_ASIN 0xC3           /* sin⁻¹( cos⁻¹( tan⁻¹( */
#define T_ACOS 0xC5
#define T_ATAN 0xC7
#define T_PI   0xAC
#define T_LN   0xBE
#define T_EXP  0xBF           /* e^( */
#define T_LOG  0xC0
#define T_BB   0xBB
/* 1 when t starts a 2-byte token, whose second byte is no token of its own: Y1 is 5E 10, theta-max 63 11 */
static uint8_t two(uint8_t t) { return (t >= 0x5C && t <= 0x63) || t == 0x7E || t == 0xAA || t == T_EXT || t == T_BB; }
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
#ifdef __ez80__
typedef char stoff_fits[STOFF == 0x4900 ? 1 : -1];
#endif
typedef char work_fits[SYMCE_WORK <= 0x4F00 ? 1 : -1];   /* 0xD0EF00..0xD13E00 */
typedef char mono_fits[4 * NV <= 24 ? 1 : -1];   /* unsigned is 24 bits on the eZ80 */

typedef struct {
    const uint8_t *in;
    unsigned len, i, i0;      /* i0: where calc() read f from, to read it again */
    const uint8_t *ans;       /* the last answer, length or ANS_REAL first; 0 when none */
    rat_t   *pool;
    uint8_t  top, depth, nv;
    uint8_t  dec;             /* a decimal number was read: the answer shows decimals */
    uint8_t  mp;              /* MathPrint: width() */
    uint8_t  rank[NVTOK];     /* variable token - T_VAR0 -> alphabetical rank */
    uint8_t  var[NV];         /* rank -> token */
    uint8_t  lim;             /* get() takes slots below it; kernels' arguments sit above */
    uint8_t  fn;              /* TOLN..TCOLLECT: functions are read */
    uint8_t  home;            /* the plain entry: X^X is a K_POW kernel */
    uint8_t  kn;              /* calc() for DERIV: a kernel may stand in another's argument, sin(cos(X)) */
    uint8_t  q;               /* calc2(): the second try, where pfrac() may use qpart() */
    uint8_t  lt;              /* calc(): the pool top before LIMIT's steps took theirs */
    uint8_t  pl;              /* usub(): the new variable is positive, so pfrac() writes ln(u), not ln|u| */
    uint8_t  deg;             /* MODE_DEG */
    uint8_t  noexp;           /* SOLVE: sin(2X) stays a kernel, TEXPAND's double angle is not wanted */
    uint8_t  wide;            /* the answer may be wider than the screen when steps( shows it (trig SOLVE: n families) */
    uint8_t  inans;           /* reading SymCE's own Ans: a comma is glyph 0x2C */
    uint8_t  nk;              /* kernels, at ranks nv.. after the variables: */
    uint8_t  kf[NV];          /* K_SIN.., */
    rat_t   *ka[NV], *kb[NV]; /* argument, and a logarithm's base (0: e) */
    rat_t   *la, *lb;         /* calc2()'s limits, whose π and e lin() keeps in step with the kernels it renumbers */
    rat_t   *tb;              /* TOLN's and TOLOGBASE's base (0: e) */
    const uint8_t *(*look)(uint8_t);   /* letter token -> its stored TI real, or 0 */
    uint32_t sm;              /* SOLVE's unknowns, a bit per letter: the other letters are looked up */
    uint8_t *st;              /* the step log (MODE_STEP), or 0 */
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

/* *m += a, two monomials; 0 when a power passes 15, a carry out of its
   nibble. The top one leaves the 24 bits: past them on the host, gone on
   the eZ80, where it shows as a sum below a.
   ponytail: 15 is 4 bits a power; 5-bit fields in a uint32 when more matters */
static int madd(unsigned *m, unsigned a)
{
    unsigned r = *m + a;
    if ((*m ^ a ^ r) & 0x111110u || (r & 0xFFFFFFu) < a) return 0;
    *m = r;
    return 1;
}

/* t = x*y. The caller sets t's monomial to x's plus y's first (madd()); a variable
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
        for (k = 0; k < NV; k++) if (both >> (RVAR + k) & 1 && !madd(&t->mono, 1u << 4 * k)) return 0;
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
            t.mono = x->mono;
            if (!madd(&t.mono, y->mono) || !rmul(&t, x, y) || !addterm(r, &t)) return 0;
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
            t.mono = c.mono;
            if (!madd(&t.mono, y->mono) || !rmul(&t, &c, y)) return 0;
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
   root: √(X+1), abs(X) (kb S when P is S²). K_POW is a power with a
   variable in its exponent, ka^kb: X^X, 2^X, on the home screen only
   (ps_t.home), where nothing but the polynomial code and putk() sees it. */
enum { K_SIN = 1, K_COS, K_TAN, K_EXP, K_LOG, K_E, H_LOG, H_LOGB, K_LABS, K_ROOT, K_POW,
       K_ASIN, K_ACOS, K_ATAN, K_PI, K_FACT };      /* sin⁻¹( cos⁻¹( tan⁻¹( π: ps_t.kn's KX_INV, DERIV's */
#define KX_NEST 1       /* ps_t.kn: a kernel may stand in another's argument */
#define KX_INV  2       /* sin⁻¹( cos⁻¹( tan⁻¹( and π */
#define KX_POW  4       /* a power with a variable or fractional exponent, away from the home screen */
#define KX_ALT  8       /* SUM: n! (K_FACT, evaluated at a point only), and a negative number to a variable power */
#define T_FACT  0x2D    /* the ! of MATH PRB */
#define KN(s, k) (NV - 1 - (s)->nv - (k))       /* kernel k's nibble, after the variables' */

/* Each head: its code, its length, its tokens, '(' included. putk() writes
   a kernel back as the first head with its code: cos( in letters, since the
   OS's result routine turns the cos( token 0xC4 into the pi glyph. */
static const uint8_t heads[] = {
    K_SIN, 1, T_SIN,
    K_COS, 7, T_BB, 0xB2, T_BB, 0xBF, T_BB, 0xC3, T_LPAR,   /* c o s ( */
    K_COS, 1, 0xC4,
    K_TAN, 1, T_TAN,
    K_ASIN, 1, T_ASIN,
    K_ACOS, 1, T_ACOS,
    K_ATAN, 1, T_ATAN,
    K_PI, 1, T_PI,
    K_EXP, 1, T_EXP,
    K_LABS, 2, T_LN, T_ABS,                             /* before ln( */
    K_LOG, 1, T_LN,
    K_E, 2, T_BB, 0x31,
    K_PI, 1, 0xAC,
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

/* a function head starts a factor, whatever the command: where functions are not read
   (SOLVE's polynomial parse) primary() then refuses it, so 2sin(X) is SYMCE LIMIT, not a
   term that ends early and a SYNTAX error */
static uint8_t ishead(ps_t *s)
{
    unsigned i = s->i;
    uint8_t h = head(s);
    s->i = i;
    return h;
}

static int fncall(ps_t *s, rat_t *out, uint8_t h);
static rat_t *keep(ps_t *s, const rat_t *r);
static int kvar(ps_t *s, rat_t *out, uint8_t f, rat_t *a, rat_t *b);
static int haskern(const ps_t *s, const rat_t *r, uint8_t pi);
static int isnum(const rat_t *r, int32_t v);
static int sym(const poly_t *p);
static int pos(const rat_t *r);
static int dep(const ps_t *s, const rat_t *r, uint8_t j);

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
        const uint8_t *v;
        s->i++;
        if (s->sm && !(s->sm >> (c - T_VAR0) & 1) && s->look && (v = s->look(c))) {   /* its stored value */
            pconst(&out->d, 1);
            return preal(&out->n, v);
        }
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

static int intval(const rat_t *r, int32_t *v);

NOINLINE static int32_t fact(int32_t v)
{
    int32_t f = 1;
    while (v > 1) f *= v--;
    return f;
}

static int postfix(ps_t *s, rat_t *out)
{
    int c;
    if (!primary(s, out)) return 0;
    int32_t v;
    while ((c = peek(s)) == T_SQR || c == T_CUBE || c == T_INV || (c == T_FACT && s->kn & KX_ALT)) {
        s->i++;
        if (c != T_FACT) { if (!powinto(s, out, c == T_SQR ? 2 : c == T_CUBE ? 3 : -1)) return 0; }
        else if (intval(out, &v)) {             /* 5! at once; K! stays a kernel until K is put in */
            if (v < 0 || v > 9) return 0;
            rconst(out, fact(v));
        } else if (!kvar(s, out, K_FACT, out, 0)) return 0;
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
        if (neg) pneg(&e->n);
        p = &e->n;
        /* on the home screen, an exponent with a variable in it: the kernel
           base^e, a variable to the rest. 1^X is 1; 0^X, and a kernel in
           either (X^X^X, X^√(X+1)), are left to the OS. */
        if (s->kn & KX_POW && isconst(&e->d) && p->n == 1 && !p->t[0].mono && p->t[0].rad == 1 && p->t[0].den == 2 &&
            p->t[0].num >= -MAXPOW && p->t[0].num <= MAXPOW) {      /* DERIV: X^(3/2) is X√(X) */
            v = p->t[0].num;
            s->top--;
            if (!sqrtinto(s, out) || !powinto(s, out, v)) return 0;
            continue;
        }
        if (s->home ? (sym(p) || sym(&e->d)) :
            s->kn & KX_POW && (sym(p) || sym(&e->d) || (isconst(&e->d) && p->n == 1 && !p->t[0].mono && p->t[0].rad == 1 &&
                                                          p->t[0].den != 1 && sym(&out->n)))) {
            rat_t *b;
            if (!out->n.n || (s->home ? haskern(s, out, 0) || haskern(s, e, 0) : !pos(out) && !(s->kn & KX_ALT && out->n.n == 1 && !out->n.t[0].mono && isconst(&out->d))) || !(b = keep(s, e))) return 0;
            s->top--;
            if (!isnum(out, 1) && !kvar(s, out, K_POW, out, b)) return 0;
            continue;
        }
        /* else a whole number -MAXPOW..MAXPOW, and nothing else */
        if (!isconst(&e->d) || p->n > 1 || (p->n && (p->t[0].mono || p->t[0].den != 1 || p->t[0].rad != 1)))
            return 0;
        v = p->n ? p->t[0].num : 0;
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

static uint8_t putpow(uint8_t *out, uint8_t n, int32_t e)
{
    if (e == 2) PUT(T_SQR);
    else if (e == 3) PUT(T_CUBE);
    else if (e > 3) { PUT(T_POW); n = number(out, n, e); }
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
            else if (e > 1 && s->kf[j - s->nv] == K_POW) {     /* (X^X)², not X^X² */
                PUT(T_LPAR);
                if (!(n = putk(s, j - s->nv, out, n))) return 0;
                PUT(T_RPAR);
            }
            else if (!(n = putk(s, j - s->nv, out, n))) return 0;
            if (!(n = putpow(out, n, e))) return 0;
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
   in Classic; MathPrint draws it and its ')' as a bar each (measured).
   A '/' inside parentheses (sin(X/2), e^(X/2)) is a stacked fraction too (measured: 2sin(X/2)
   83 px, sin(X/2)cos(X/3) 126 px, the parentheses grown): from the '(' or the last sign
   or comma it is as wide as its wider side, with no bar column. It used to take the
   term's one top/side, so a second one (cos after sin) overwrote the first's columns.
   Measured (build 3, 15 answers): e^( is an 'e' and a small raised exponent, 1 column
   (TAYLOR answer 28 columns drew in 249 px). When the whole answer is one stacked fraction
   with functions and a parenthesized numerator, both sides are small type, 7.6 to 8.3 px a
   column against 11.2: 3/4 of its columns (36 drew in 275 px, 35 in 289, 41 ran off). A
   fraction among other terms stays whole (TANGENTLINE's, 29, ran off), and so does one whose
   numerator is a bare product: the OS stacks only the last factor (8sin(B)³cos(B)³/(..),
   49 columns, ran off). */
static unsigned width(const uint8_t *t, uint8_t n, uint8_t mp)
{
    unsigned w = 0, u;
    uint8_t side = 0, top = 0, over = 0, depth = 0, fn = 0, fr = 0, cs = 0, pn = 0, base[8], tp[8], k, c, q, m, z;
    /* ponytail: depth is taken mod 8; an answer nested deeper is far wider than the screen anyway */
#define FOLD() do { m = depth & 7; if (depth && (fr >> m & 1)) { fr &= ~(1 << m); z = side - base[m]; side = base[m] + (tp[m] > z ? tp[m] : z); } } while (0)
    for (k = 0; k < n; k++) fn |= t[k] == T_BB || t[k] == T_EXT || (t[k] >= T_LN && t[k] <= T_ATAN);
    if (!mp) {
        for (k = 0; k < n; k++) {
            c = t[k];
            w += c == T_SQRT ? 2 : c == T_AND ? 5 : c == T_OR || c == T_SIN || c == T_TAN || c == T_LOG || c == T_ABS ? 4 :
                 c == T_LN || c == T_EXP ? 3 : c == T_EXT ? 8 : c == T_ASIN || c == T_ACOS || c == T_ATAN ? 6 : 1;
            k += c == T_BB || c == T_EXT;
        }
        return w;
    }
    for (k = 0; k <= n; k++) {
        c = k < n ? t[k] : T_ADD;               /* a last '+' ends the last term */
        if (c == T_BB || c == T_EXT || (c >= T_LN && c <= T_ATAN)) {
            /* a letter, or a function's name and '(': its Classic columns */
            side += c == T_BB || c == T_EXP ? 1 : c == T_EXT ? 8 : c == T_LN ? 3 : c == T_ASIN || c == T_ACOS || c == T_ATAN ? 6 : 4;
            if (c != T_BB) base[++depth & 7] = side;
            k += c == T_BB || c == T_EXT;
        }
        else if (c == T_LPAR || c == T_SQRT || c == T_ABS) { side += c != T_LPAR || fn; base[++depth & 7] = side; }
        else if (c == T_RPAR) { FOLD(); depth--; side += fn; }
        else if (c == T_DIV && depth) { FOLD(); m = depth & 7; tp[m] = side - base[m]; side = base[m]; fr |= 1 << m; }
        else if (c == T_DIV) { top = side; side = 0; over = 1; pn = k && t[cs] == T_LPAR && t[k - 1] == T_RPAR; }
        else if (depth || (c != T_ADD && c != T_SUB && c != T_NEG && (c < T_EQ || c > T_EQ + 5) && c != T_OR && c != T_AND &&
                           c != T_LCOMMA && c != T_LBRACE && c != T_RBRACE && c != T_LBRK && c != T_RBRK))
        {
            q = depth && (c == T_ADD || c == T_SUB || c == T_NEG || c == T_COMMA);
            if (q) FOLD();
            side += c != T_POW;
            if (q) base[depth & 7] = side;
        }
        else {
            u = (over && top > side ? top : side) + over;
            w += (over && pn && fn && !w && k >= n ? u * 3 / 4 : u) + (k < n ? c == T_OR ? 4 : c == T_AND ? 5 : 1 : 0);
            top = side = over = 0;
            cs = k + 1;
        }
    }
#undef FOLD
    return w;
}

/* Columns a MathPrint answer takes with a matrix [[..][..]] in it: the OS draws
   it as a grid (measured: 3x3 of fractions 100 px wide), each column its
   widest cell plus one, the two brackets 2; what comes before it is width().
   Anything else is width() as is. */
NOINLINE static unsigned mpwidth(const uint8_t *t, uint8_t n)
{
    uint8_t cw[20], k = 0, c = 0, s, e;
    unsigned w;
    while (k + 1 < n && !(t[k] == T_LBRK && t[k + 1] == T_LBRK)) k++;
    if (k + 1 >= n) return width(t, n, 1);
    w = k ? width(t, k, 1) : 0;
    for (e = 0; e < 20; e++) cw[e] = 0;
    for (k += 2, s = k; k < n; k++) {
        if (t[k] != T_LCOMMA && t[k] != T_RBRK) continue;
        e = width(t + s, k - s, 1);
        if (c < 20 && e > cw[c]) cw[c] = e;
        c++;
        s = k + 1;
        if (t[k] == T_RBRK) {
            c = 0;
            if (k + 1 < n && t[k + 1] == T_LBRK) s = ++k + 1;
            else break;
        }
    }
    for (e = 0; e < 20; e++) w += cw[e] ? cw[e] + 1 : 0;
    return w + 2;
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
       C_EXPAND, C_COMDENOM, C_PARTFRAC, C_DERIV, C_FACTOR, C_CFACTOR,
       C_TOLN, C_TOLOGBASE, C_TEXPAND, C_TOEXP, C_TOSIN, C_TOCOS, C_TCOLLECT,
       /* the Calculus menu's: calc(), and from INTEGRAL on calc2() */
       C_DERIVAT, C_TANGENTLINE, C_NORMALLINE, C_CENTRALDIFF, C_TAYLOR, C_IMPDIF,
       C_INTEGRAL, C_LIMIT, C_SUM, C_PRODUCT, C_FMIN, C_FMAX, C_ARCLEN, C_SERIES, C_DOMINANTTERM, C_DESOLVE,
       /* the Geometry menu's: geo(); up to REFLECT a line may be f alone */
       C_DISTANCE, C_SLOPE, C_PARALLEL, C_PERPENDICULAR, C_INTERSECT, C_REFLECT, C_MIDPOINT, C_LINE,
       C_PERPBISECTOR, C_PARTITION, C_AREA, C_PERIMETER, C_CENTROID, C_CIRCUMCENTER, C_ORTHOCENTER,
       C_INCENTER, C_HYPOT, C_LEG, C_CIRCLE, C_ROTATE, C_DILATE,
       /* the Matrix menu's: matrix() */
       C_MDET, C_MINV, C_MRREF, C_MTRANS, C_DOTP, C_CROSSP, C_NORM,
       /* steps( wraps a whole entry (symce_engine); anywhere else it is an error */
       C_STEPS, NCMD = C_STEPS };
static const char cmds[] = "LEFT\0RIGHT\0SOLVE\0CSOLVE\0CZEROS\0CPOLYROOTS\0POLYROOTS\0"
    "NROOTS\0POLYCOEFFS\0POLYDEGREE\0POLYREMAINDER\0POLYQUOTIENT\0POLYGCD\0"
    "EXPAND\0COMDENOM\0PARTFRAC\0DERIV\0FACTOR\0CFACTOR\0"
    "TOLN\0TOLOGBASE\0TEXPAND\0TOEXP\0TOSIN\0TOCOS\0TCOLLECT\0"
    "DERIVAT\0TANGENTLINE\0NORMALLINE\0CENTRALDIFF\0TAYLOR\0IMPDIF\0"
    "INTEGRAL\0LIMIT\0SUM\0PRODUCT\0FMIN\0FMAX\0ARCLEN\0SERIES\0DOMINANTTERM\0DESOLVE\0"
    "DISTANCE\0SLOPE\0PARALLEL\0PERPENDICULAR\0INTERSECT\0REFLECT\0MIDPOINT\0LINE\0"
    "PERPBISECTOR\0PARTITION\0AREA\0PERIMETER\0CENTROID\0CIRCUMCENTER\0ORTHOCENTER\0"
    "INCENTER\0HYPOT\0LEG\0CIRCLE\0ROTATE\0DILATE\0"
    "MDET\0MINV\0MRREF\0MTRANS\0DOTP\0CROSSP\0NORM\0STEPS";

typedef struct {
    int8_t  d;                /* degree */
    int32_t a[16];            /* whole coefficients, a[0] first */
} ip_t;
typedef char ip_fits[3 * sizeof(ip_t) <= sizeof(poly_t) ? 1 : -1];   /* irred()'s tmp() */

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

/* q = a/b when that leaves whole coefficients: 1; 0 when not, -1 past LIM.
   b of degree 1 or 2, so only r[i..i+2] of the remainder is ever live: w. */
static int ipdiv(ip_t *q, const ip_t *a, const ip_t *b)
{
    int64_t w[3], c;
    int8_t i, j;
    q->d = a->d - b->d;
    for (j = 0; j <= b->d; j++) w[j] = a->a[q->d + j];
    for (i = q->d; ; i--) {
        c = w[b->d];
        if (c % b->a[b->d]) return 0;
        c /= b->a[b->d];
        if (c > LIM || c < -LIM) return -1;
        q->a[i] = (int32_t)c;
        for (j = b->d; j--; ) w[j] -= c * b->a[j];
        if (!i) break;
        for (j = b->d; j; j--) w[j] = w[j - 1];
        w[0] = a->a[i - 1];
    }
    for (j = 0; j < b->d; j++) if (w[j]) return 0;
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
   of degree 1 or 2, so it is irreducible below degree 6. a[1] and a[2] are
   scratch: the quotient and the divisor tried. */
NOINLINE static int ipfactor(fl_t *fl, ip_t *a, uint8_t k, uint8_t u, uint8_t den)
{
    ip_t *q = a + 1, *b = a + 2;
    int32_t p, c, f1, fm, i0, i1, im, a0, v1, vm, al;
    int64_t z;
    int8_t j, sg;
    int r;
again:
    if (a->d < 2) return !a->d || puship(fl, a, k, u, den);
    for (f1 = fm = 0, j = 0; j <= a->d; j++) { f1 += a->a[j]; fm += j & 1 ? -a->a[j] : a->a[j]; }
    a0 = a->a[0] < 0 ? -a->a[0] : a->a[0];
    b->d = 1;
    for (i0 = 0; (p = nextdiv(a0, &i0)); )
        for (i1 = 0; (c = nextdiv(a->a[a->d], &i1)); )
            for (sg = -1; sg < 2; sg += 2) {
                if (gcd(p, c) != 1 || (c - sg * p ? f1 % (c - sg * p) : f1) ||
                    (c + sg * p ? fm % (c + sg * p) : fm)) continue;
                b->a[0] = -sg * p; b->a[1] = c;
                if ((r = ipdiv(q, a, b)) < 0) return 0;
                if (r) { if (!puship(fl, b, k, u, den)) return 0; *a = *q; goto again; }
            }
    if (a->d == 2) return pushquad(fl, a, k, u, den);
    if (a->d == 3) return puship(fl, a, k, u, den);
    /* ponytail: the search is about |a(0)a(1)a(-1)|^(1/2) steps; past this, SYMCE LIMIT */
    if (f1 < 0) f1 = -f1;
    if (fm < 0) fm = -fm;
    if ((z = (int64_t)a0 * f1) > 10000000000LL || z * fm > 10000000000LL) return 0;
    b->d = 2;
    for (i0 = 0; (p = nextdiv(a0, &i0)); )
        for (i1 = 0; (v1 = nextdiv(f1, &i1)); )
            for (im = 0; (vm = nextdiv(fm, &im)); )
                for (sg = 0; sg < 4; sg++) {            /* g(1) = ±v1, g(-1) = ±vm */
                    int32_t g1 = sg & 1 ? -v1 : v1, gm = sg & 2 ? -vm : vm;
                    if ((g1 - gm) & 1) continue;
                    al = (g1 + gm) / 2 - p;
                    if (!al || a->a[a->d] % al) continue;
                    c = al < 0 ? -1 : 1;                /* first term positive */
                    b->a[0] = c * p; b->a[1] = c * (g1 - gm) / 2; b->a[2] = c * al;
                    if ((r = ipdiv(q, a, b)) < 0) return 0;
                    if (r) { if (!pushquad(fl, b, k, u, den)) return 0; *a = *q; goto again; }
                }
    /* ponytail: two cubics (or a cubic and a quartic) would pass unseen here */
    return a->d < 6 && puship(fl, a, k, u, den);
}

/* p's coefficients as those of its variable at nibble shift k */
static void toip(ip_t *a, const poly_t *p, uint8_t k)
{
    const term_t *x;
    int8_t j;
    for (j = 0; j < 16; j++) a->a[j] = 0;
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
    uint8_t e, best = 0, bn = MAXT + 1, cnt[16];
    int ok = 2, r;
    if (!h) return 0;
    for (e = 0; e < 16; e++) cnt[e] = 0;
    for (x = p->t; x < p->t + p->n; x++) cnt[x->mono >> k & 15]++;
    for (e = 0; e < 16; e++) if (cnt[e] && cnt[e] < bn) { bn = cnt[e]; best = e; }
    if (bn < 2 || bn == p->n) goto done;        /* a monomial coefficient; or no such variable */
    slice(g, p, k, best);
    divmono(g, content(g, g->t[0].mono));
    for (e = 0; e < 16; e++)
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
    ip_t *a;
    uint8_t k, u = 0xFF, nv = 0, top = s->top;
    int r = 0;
    if (hasrad(p)) return 0;
    for (x = p->t; x < p->t + p->n; x++) w |= x->mono;
    for (k = 4 * NV; k; ) if (w >> (k -= 4) & 15) { if (nv++) u = k; else v = k; }
    if (nv == 1 || (nv == 2 && homog(p) >= 0)) {
        if (!(a = (ip_t *)tmp(s))) return 0;    /* and ipfactor's scratch: off the stack */
        toip(a, p, (uint8_t)v);
        r = ipfactor(fl, a, (uint8_t)v, nv == 1 ? 0xFF : u, den);
        s->top = top;
        return r;
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
NOINLINE static uint8_t putfl(const ps_t *s, fl_t *fl, uint8_t *out, uint8_t n)
{
    uint8_t k, j, t, o[MAXF], nf = 0, dn = fl->c.den != 1, bare;
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
    if (!d || d->mono != (unsigned)m << 4 * j || d->rad != 1) return 0;
    rinv(&inv, d);
    for (;;) {
        for (x = acc->t; x < acc->t + acc->n && (x->mono >> 4 * j & 15) < m; x++) ;
        if (x == acc->t + acc->n) return 1;
        c.mono = x->mono - d->mono;
        if (!rmul(&c, x, &inv) || (q && !addterm(q, &c))) return 0;
        for (y = f->t; y < f->t + f->n; y++) {
            t.mono = c.mono;
            if (!madd(&t.mono, y->mono) || !rmul(&t, &c, y)) return 0;
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
typedef char nroots_fits[16 * sizeof(cx_t) + 16 * sizeof(fp_t) <= sizeof(rat_t) ? 1 : -1];
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
   carry in, its low byte goes out and HL >>= 8: stored at iy-6, read back
   from iy-5, the byte at iy-3 a zero pushed first. That scratch stays above
   SP: an interrupt pushes its return address below SP, so a shift through
   the stack (push hl; inc sp; pop hl; dec sp) lost the zero byte to an
   interrupt arriving after the pop and put its PC's top byte in HL's.
   DEU is 0 throughout: only d, e and mlt touch it. */
#define UM(i, j) "\tld\td, (iy + " #i ")\n\tld\te, (iy + " #j ")\n\tmlt\tde\n\tadd\thl, de\n"
#define UMOUT "\tld\ta, l\n\tld\t(bc), a\n\tinc\tbc\n\tld\t(iy - 6), hl\n\tld\thl, (iy - 5)\n"
__asm__("\t.section\t.text._symce_umul,\"ax\",@progbits\n"
        "\t.globl\t_symce_umul\n"
        "_symce_umul:\n"
        "\tld\tiy, 0\n\tadd\tiy, sp\n"         /* r at iy+3, x at iy+6..9, y at iy+12..15 */
        "\tld\tbc, (iy + 3)\n"
        "\tld\thl, 0\n\tpush\thl\n\tpush\thl\n\tld\tde, 0\n"
        UM(6, 12) UMOUT
        UM(6, 13) UM(7, 12) UMOUT
        UM(6, 14) UM(7, 13) UM(8, 12) UMOUT
        UM(6, 15) UM(7, 14) UM(8, 13) UM(9, 12) UMOUT
        UM(7, 15) UM(8, 14) UM(9, 13) UMOUT
        UM(8, 15) UM(9, 14) UMOUT
        UM(9, 15) UMOUT
        "\tld\ta, l\n\tld\t(bc), a\n"
        "\tpop\thl\n\tpop\thl\n\tret\n");

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
    fp_t *c = (fp_t *)(z + 16);
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
static int haskern(const ps_t *s, const rat_t *r, uint8_t pi)
{
    const term_t *x;
    unsigned m = 0;
    uint8_t k;
    uint32_t v = 0;
    if (!s->nk) return 0;
    for (x = r->n.t; x < r->n.t + r->n.n; x++) { m |= x->mono; v |= x->rad; }
    for (x = r->d.t; x < r->d.t + r->d.n; x++) { m |= x->mono; v |= x->rad; }
    if (pi) for (k = 0; k < s->nk; k++) if (s->kf[k] == K_PI) m &= ~(15u << 4 * KN(s, k));   /* sin(X+pi): pi is a number */
    return (m & 0xFFFFFFu >> 4 * s->nv) || (v >> RVAR & ((1u << (NV - s->nv)) - 1));
}

/* r is the whole number v, not 0 */
static int isnum(const rat_t *r, int32_t v)
{
    const term_t *t = r->n.t;
    return r->n.n == 1 && isconst(&r->d) && !t->mono && t->num == v && t->den == 1 && t->rad == 1;
}

/* a kernel besides π and e is in r */
static int kdepx(const ps_t *s, const rat_t *r)
{
    uint8_t k;
    for (k = 0; k < s->nk; k++)
        if (s->kf[k] != K_PI && s->kf[k] != K_E && (vdeg(&r->n, KN(s, k)) || vdeg(&r->d, KN(s, k)))) return 1;
    return 0;
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
NOINLINE static int exact(ps_t *s, rat_t *out, uint8_t h, const rat_t *a);
static int mklog(ps_t *s, rat_t *out, rat_t *u, rat_t *t)
{
    if (t ? same2(u, t) : is_e(s, u)) { rconst(out, 1); return 1; }
    if (isnum(u, 1)) { rconst(out, 0); return 1; }
    if (!t && (s->kn & KX_INV) && exact(s, out, K_LOG, u)) return 1;
    if (!t && u->n.n == 1 && u->d.n == 1 && !u->n.t->mono && u->n.t->rad == 1 && u->n.t->num == 1 && u->n.t->den > 1 &&
        !u->d.t->mono && u->d.t->rad == 1 && u->d.t->num == 1 && u->d.t->den == 1) {        /* ln(1/3) is -ln(3) */
        uint8_t top = s->top;
        rat_t *w = get(s);
        int ok = w != 0;
        if (ok) { rconst(w, u->n.t->den); ok = mklog(s, out, w, 0) && out->n.n == 1; }
        if (ok) out->n.t[0].num = -out->n.t[0].num;
        s->top = top;
        return ok;
    }
    if ((haskern(s, u, 0) && !is_e(s, u) && !(s->kn & KX_NEST)) || !pos(u)) return 0;
    return kvar(s, out, K_LOG, u, t);
}

/* to += p times the monomial m, negated when neg */
static int addmono(poly_t *to, const poly_t *p, unsigned m, uint8_t neg)
{
    const term_t *x;
    term_t t;
    for (x = p->t; x < p->t + p->n; x++) {
        t = *x;
        if (!madd(&t.mono, m)) return 0;
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

/* sin(n·π/12) for n 0..12 as num/den·√rad; den 0 where it is not a plain value */
static const int8_t sn_[13] = { 0, 0, 1, 1, 1, 0, 1, 0, 1, 1, 1, 0, 0 };
static const int8_t sd_[13] = { 1, 0, 2, 2, 2, 0, 1, 0, 2, 2, 2, 0, 1 };
static const int8_t sr_[13] = { 1, 1, 1, 2, 3, 1, 1, 1, 3, 2, 1, 1, 1 };

/* out = h(a) when a is an exact value: sin cos tan of a rational times π
   in sixths and quarters, sin⁻¹ and tan⁻¹ of the matching 1/2 √2/2 √3/2 1
   √3/3 √3 (a > 0, apply() takes the sign), ln of e^k. 1 when done, 0 when
   not: it stays a kernel. */
NOINLINE static int exact(ps_t *s, rat_t *out, uint8_t h, const rat_t *a)
{
    const term_t *x = a->n.t;
    int32_t num, den, u;
    uint8_t k, q = 0xFF, e = 0xFF, n, m, r1, r2;
    int8_t n1, d1, n2, d2;
    if (a->n.n != 1 || a->d.n != 1 || a->d.t[0].num != 1 || a->d.t[0].den != 1 || a->d.t[0].rad != 1 || a->d.t[0].mono) return 0;
    for (k = 0; k < s->nk; k++)
        if (s->kf[k] == K_PI) q = KN(s, k); else if (s->kf[k] == K_E) e = KN(s, k);
    if (h == K_LOG) {                           /* e^k: the e's power, whole */
        if (e == 0xFF || x->num != 1 || x->den != 1 || x->rad != 1 || !x->mono || x->mono >> 4 * e << 4 * e != x->mono) return 0;
        u = (int32_t)(x->mono >> 4 * e);
        if (u > 15) return 0;
        rconst(out, u);
        return 1;
    }
    if (h <= K_TAN) {
        if (q == 0xFF || x->mono != 1u << 4 * q || x->rad != 1) return 0;
        u = x->num * 12;
        if (u % x->den) return 0;
        n = (uint8_t)(((u / x->den) % 24 + 24) % 24);
        m = (uint8_t)((n + 6) % 24);            /* cos(n) = sin(n + 6) */
        r1 = n > 12; r2 = m > 12;               /* signs */
        n = r1 ? 24 - n : n; m = r2 ? 24 - m : m;
        n1 = sn_[n]; d1 = sd_[n]; n2 = sn_[m]; d2 = sd_[m];
        if (!d1 || !d2) return 0;
        if (h == K_SIN) { num = n1; den = d1; k = sr_[n]; u = r1; }
        else if (h == K_COS) { num = n2; den = d2; k = sr_[m]; u = r2; }
        else {
            if (!n2) return 0;                  /* tan(π/2) */
            u = r1 ^ r2;
            num = n1 * d2; den = n2 * d1; k = 1;
            if (n1 && sr_[n] != sr_[m]) {       /* only √3 against 1 */
                k = 3;
                if (sr_[m] == 3) den *= 3;
            }
        }
        if (!num) { rconst(out, 0); return 1; }
        for (n = (uint8_t)num, m = (uint8_t)den; m; ) { r1 = n % m; n = m; m = r1; }
        rconst(out, u ? -num / n : num / n);
        out->n.t[0].den = den / n;
        out->n.t[0].rad = k;
        return 1;
    }
    if ((h != K_ASIN && h != K_ATAN) || x->mono || x->num != 1) return 0;
    num = x->den; k = (uint8_t)x->rad;          /* 1/num·√k, as a multiple of π */
    if (h == K_ATAN) u = num == 1 ? (k == 1 ? 4 : k == 3 ? 3 : 0) : num == 3 && k == 3 ? 6 : 0;
    else u = num == 1 && k == 1 ? 2 : num == 2 ? (k == 1 ? 6 : k == 2 ? 4 : k == 3 ? 3 : 0) : 0;
    if (!u || !kvar(s, out, K_PI, 0, 0)) return 0;
    out->n.t[0].den = u;
    return 1;
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
    if (h == K_PI) return kvar(s, out, K_PI, 0, 0);
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
    if (h <= K_EXP && haskern(s, a, h <= K_TAN) && !(s->kn & KX_NEST)) return 0;
    if (h >= K_ASIN) {                          /* DERIV: sin⁻¹(u) cos⁻¹(u) tan⁻¹(u), left as they are */
        if (!(s->kn & KX_INV)) return 0;
        if (a->n.n && h != K_ACOS && lead(&a->n)->num < 0) { pneg(&a->n); neg = 1; }
        if (!a->n.n && h != K_ACOS) rconst(out, 0);
        else if (!exact(s, out, h, a)) ok = kvar(s, out, h, a, 0);
    } else if (h <= K_TAN) {
        if (a->n.n && lead(&a->n)->num < 0) { pneg(&a->n); neg = h != K_COS; }
        if (!a->n.n) rconst(out, h == K_COS);
        else if (exact(s, out, h, a)) ;
        else if (s->fn == C_TEXPAND && !s->noexp) ok = texpand(s, out, h, a);
        else if (h == K_TAN && s->fn > C_TEXPAND) {         /* sin(u)/cos(u) */
            if (!kvar(s, out, K_SIN, a, 0) || !kvar(s, b, K_COS, a, 0)) return 0;
            out->d = b->n;
        } else ok = kvar(s, out, h, a, 0);
    } else if (h == K_EXP) {
        if (!a->n.n) rconst(out, 1);
        else ok = isnum(a, 1) ? kvar(s, out, K_E, 0, 0) : kvar(s, out, K_EXP, a, 0);
    } else if (h == K_LABS) {                   /* ln|u|: calc() and TOLN only */
        if (s->fn != C_TOLN || (ok = nsign(a)) == 0 || ok == 2) return 0;
        if (ok == 3 && a->n.n == 1 && a->n.t[0].num > 0 && a->d.n == 1 && a->d.t[0].num > 0 && !kdepx(s, a)) ok = 1;      /* π, e², 2π: positive */
        if (ok == 3 && haskern(s, a, 0)) return 0;
        if (ok < 0 || (ok == 3 && lead(&a->n)->num < 0)) pneg(&a->n);     /* |-u| = |u| */
        return ok == 3 ? kvar(s, out, K_LABS, a, 0) : mklog(s, out, a, 0);
    } else if (h == K_ROOT) *out = *a;          /* U at a point: evalat()'s */
    else {
        if (h == H_LOG) rconst(c = b, 10);
        if (c && is_e(s, c)) c = 0;
        if (c && (haskern(s, c, 0) || !pos(c) || isnum(c, 1) || !(c = keep(s, c)))) return 0;
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

/* r as answer() writes it, in parentheses unless it is one variable or a
   number with no sign: K_POW's base and exponent, X^X, 2^X, (X+1)^(2X).
   A number right after an operand gets a * in its '(''s place instead, as
   3·2^X would otherwise read 32^X. */
static uint8_t putpar(const ps_t *s, rat_t *r, uint8_t *out, uint8_t n)
{
    uint8_t m = n, k, c = m ? out[m - 1] : T_ADD;
    PUT(T_LPAR);
    if (!(n = answer(s, r, out, n))) return 0;
    for (k = m + 1; k < n && (is_digit(out[k]) || out[k] == T_DOT); k++) ;
    if (k < n && (n != m + 2 || !is_var(out[m + 1]))) PUT(T_RPAR);
    else if (k == n && (is_digit(c) || c == T_DOT || is_var(c) || c == T_RPAR || c == T_SQR || c == T_CUBE))
        out[m] = T_MUL;
    else for (n--; m < n; m++) out[m] = out[m + 1];
    return n;
}

/* kernel k as it is typed: sin(u), cos(u) in letters, tan(u), e^(u), e,
   ln(u), log(u) for base 10, else logBASE(u,b); K_POW's putpar() twice */
static uint8_t putk(const ps_t *s, uint8_t k, uint8_t *out, uint8_t n)
{
    const uint8_t *h = heads;
    rat_t *b = s->kb[k];
    uint8_t f = b ? (isnum(b, 10) ? H_LOG : H_LOGB) : s->kf[k], j;
    if (s->kf[k] == K_POW) {
        if (!(n = putpar(s, s->ka[k], out, n))) return 0;
        PUT(T_POW);
        return putpar(s, b, out, n);
    }
    if (s->kf[k] == K_ROOT) {                   /* U = P; unroot() leaves none */
        PUT(T_LPAR);
        if (!(n = answer(s, s->ka[k], out, n))) return 0;
        PUT(T_RPAR);
        return n;
    }
    while (*h != f) h += 2 + h[1];
    for (j = 0; j < h[1]; j++) PUT(h[2 + j]);
    if (f == K_E || f == K_PI) return n;
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

/* lin() has renumbered the kernels: the limits' π and e (kinds sf[], nibbles so[]) get new ones */
NOINLINE static int nlim(ps_t *s, const uint8_t *sf, const uint8_t *so, uint8_t nq)
{
    rat_t *r;
    poly_t *pp;
    term_t *x;
    uint8_t i, k, g[NV], v;
    unsigned m, w;
    for (i = 0; i < nq; i++) if ((g[i] = kern(s, sf[i], 0, 0)) == 0xFF) return 0;
    for (k = 0; k < 2; k++)
        if ((r = k ? s->lb : s->la))
            for (v = 0; v < 2; v++)
                for (x = (pp = v ? &r->d : &r->n)->t; x < pp->t + pp->n; x++) {
                    for (m = x->mono, w = 0, i = 0; i < nq; i++) {
                        m &= ~(15u << 4 * so[i]);
                        w |= (x->mono >> 4 * so[i] & 15) << 4 * g[i];
                    }
                    x->mono = m | w;
                }
    return 1;
}

/* TCOLLECT, TOEXP (exp): p through linear(), both sides, then its kernels
   replaced by one per angle used, in order -- A-B before A+B (TCOLLECT), X
   before -X (TOEXP) -- sin before cos. TCOLLECT's imaginary parts all
   cancel; TOEXP's below the bar are all of it or none. Then reduced. */
static int lin(ps_t *s, rat_t *p, uint8_t exp)
{
    rat_t *r = get(s), *u = get(s);
    poly_t *th = (poly_t *)get(s), *q = &u->n;
    uint8_t nth = 0, o[MAXTH], k, j, h, g, nq, sf[NV], so[NV];
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
    for (k = nq = 0; k < s->nk; k++)
        if (s->kf[k] == K_PI || s->kf[k] == K_E) { sf[nq] = s->kf[k]; so[nq++] = KN(s, k); }
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
    if (nq && !nlim(s, sf, so, nq)) return 0;
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
        else if (c == T_LPAR || c == T_SQRT || c == T_ABS || (c >= T_LN && c <= T_ATAN) ||
                 (c == T_EXT && k + 1 < len && in[k + 1] == 0x34)) d++;
        k += two(c);                            /* two bytes */
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
            if (haskern(s, p, 0)) return 0;
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

/* a rat that is V itself */
static int isvar(const rat_t *r, uint8_t j)
{
    const term_t *t = r->n.t;
    return r->n.n == 1 && isconst(&r->d) && t->num == 1 && t->den == 1 && t->mono == 1u << 4 * j && t->rad == 1;
}

/* out = d/dV of r; V itself is 1, with no pool slot for it */
static int dvar(ps_t *s, rat_t *out, const rat_t *r, uint8_t j)
{
    if (isvar(r, j)) { rconst(out, 1); return 1; }
    return drat(s, out, r, j);
}

/* the derivative of kernel k by its own argument, sin⁻¹(u): 1/√(1-u²), cos⁻¹(u): -1/√(1-u²), tan⁻¹(u): 1/(1+u²) */
NOINLINE static int oinv(ps_t *s, rat_t *o, uint8_t k)
{
    rat_t *t = get(s);
    uint8_t f = s->kf[k];
    if (!t) return 0;
    *t = *s->ka[k];
    if (!rmulinto(s, t, s->ka[k], 0)) return 0;             /* u² */
    rconst(o, 1);
    if (f != K_ATAN) pneg(&t->n);
    if (!raddinto(s, o, t) || (f != K_ATAN && !sqrtinto(s, o))) return 0;
    *t = *o;
    rconst(o, f == K_ACOS ? -1 : 1);
    f = rmulinto(s, o, t, 1);
    s->top--;
    return f;
}

/* out = d/dV of the power kernel k, a^b: b a number, b·a^(b-1)·a'; else a^b·(b'·ln(a) + b·a'/a) */
NOINLINE static int dpow(ps_t *s, rat_t *out, uint8_t k, uint8_t j)
{
    rat_t *a = s->ka[k], *b = s->kb[k], *t, *u, *w;
    uint8_t neg = 0, top = s->top;
    if (!dep(s, a, j) && !dep(s, b, j)) rconst(out, 0);
    else if (!dep(s, b, j)) {
        if (!dvar(s, out, a, j)) return 0;        /* a' */
        if (out->n.n) {
            if (!(t = get(s)) || !(u = get(s))) return 0;
            *t = *b;
            rconst(u, -1);
            if (!raddinto(s, t, u)) return 0;   /* b-1 */
            neg = t->n.n == 1 && !t->n.t[0].mono && t->n.t[0].num < 0;
            if (neg) pneg(&t->n);
            if (!(b = keep(s, t)) || !kvar(s, u, K_POW, a, b) || !rmulinto(s, out, s->kb[k], 0) || !rmulinto(s, out, u, neg)) return 0;
        }
    } else {
        if (!(t = get(s)) || !(u = get(s)) || !(w = get(s))) return 0;
        if (!mklog(s, t, a, 0) || !dvar(s, u, b, j) || !rmulinto(s, t, u, 0)) return 0;     /* b'·ln(a) */
        if (!dvar(s, u, a, j)) return 0;
        if (u->n.n && (!rmulinto(s, u, b, 0) || !rmulinto(s, u, a, 1) || !raddinto(s, t, u))) return 0;
        if (!kvar(s, u, K_POW, a, b) || !rmulinto(s, t, u, 0)) return 0;
        *out = *t;
    }
    s->top = top;
    return 1;
}

/* out = d/dV of kernel k, V at nibble j */
static int dkern(ps_t *s, rat_t *out, uint8_t k, uint8_t j)
{
    rat_t *a = s->ka[k];
    poly_t *w;
    uint8_t f = s->kf[k], g;
    int ok;
    if (f == K_E || f == K_PI) { rconst(out, 0); return 1; }
    if (f == K_POW) return dpow(s, out, k, j);
    if (!dvar(s, out, a, j)) return 0;            /* u' */
    if (!out->n.n || f == K_ROOT) return 1;     /* U = u: √U' is dpoly()'s */
    if (f == K_LOG || f == K_LABS) return rmulinto(s, out, a, 1);
    if (f >= K_ASIN) {
        rat_t *o = get(s);
        ok = o && oinv(s, o, k) && rmulinto(s, out, o, 0);
        s->top--;
        return ok;
    }
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
/* kernel k holds V (nibble j): in its argument, or a power's exponent too */
static int kdep(const ps_t *s, uint8_t k, uint8_t j)
{
    return s->ka[k] && (dep(s, s->ka[k], j) || (s->kf[k] == K_POW && dep(s, s->kb[k], j)));
}

static int dep(const ps_t *s, const rat_t *r, uint8_t j)
{
    uint8_t k;
    if (vdeg(&r->n, j) || vdeg(&r->d, j)) return 1;
    for (k = 0; k < s->nk; k++)
        if ((vdeg(&r->n, KN(s, k)) || vdeg(&r->d, KN(s, k))) && kdep(s, k, j)) return 1;
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
        if (!(m >> k & 1) || !kdep(s, k, j)) continue;
        *a = *s->ka[k];
        if ((ok = subst(s, a, j, v)) != 1) return ok;
        if (s->kf[k] == K_POW || s->kf[k] == K_FACT) {      /* 2^X at a whole number, X! at 0..9; else left */
            int32_t e = 0;
            if (s->kf[k] == K_POW) {
                *b = *s->kb[k];
                if ((ok = subst(s, b, j, v)) != 1) return ok;
            } else { *b = *a; rconst(a, 1); }
            if (!intval(b, &e) || e < -MAXPOW || e > MAXPOW) return 0;
            if (s->kf[k] == K_FACT) {
                if (e < 0 || e > 9) return 0;
                rconst(f, fact(e));
            } else {
                if (!a->n.n && e < 0) return 2;
                *f = *a;
                if (!powinto(s, f, e)) return 0;
            }
            if ((ok = subst(s, r, g, f)) != 1) return ok;
            continue;
        }
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

/* a kernel of the calculus extensions is in play: sin⁻¹( cos⁻¹( tan⁻¹( π, a power with a variable or fraction */
static int gnew(const ps_t *s)
{
    uint8_t k;
    for (k = 0; k < s->nk; k++) if (s->kf[k] >= K_POW) return 1;
    return 0;
}

/* N/D with D several terms: the terms of N with the same kernels, each group over D and cancelled, added.
   X·tan⁻¹(X) is X/(X²+1)+tan⁻¹(X), not (X²tan⁻¹(X)+X+tan⁻¹(X))/(X²+1). The groups with no kernel come first. */
NOINLINE static uint8_t gsplit(ps_t *s, rat_t *p, uint8_t *out, rat_t *q)
{
    unsigned km = 0xFFFFFFu >> 4 * s->nv;
    uint32_t rk = (uint32_t)((1u << (NV - s->nv)) - 1) << RVAR;
    uint8_t i, j, k, n = 0, pass;
    const term_t *x, *y;
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < p->n.n; i++) {
            x = &p->n.t[i];
            if ((pass == 0) != !((x->mono & km) || (x->rad & rk))) continue;
            for (j = 0; j < i; j++) {
                y = &p->n.t[j];
                if ((y->mono & km) == (x->mono & km) && (y->rad & rk) == (x->rad & rk)) break;
            }
            if (j < i) continue;
            q->n.n = 0;
            for (j = i; j < p->n.n; j++) {
                y = &p->n.t[j];
                if ((y->mono & km) == (x->mono & km) && (y->rad & rk) == (x->rad & rk) && !addterm(&q->n, y)) return 0;
            }
            q->d = p->d;
            if (reduce(s, q) != 1) return 0;
            k = n;
            if (k) PUT(T_ADD);
            if (!(n = answer(s, q, out, n))) return 0;
            if (k && (out[k + 1] == T_NEG || (out[k + 1] == T_LPAR && out[k + 2] == T_NEG))) {
                pneg(&q->n);
                out[k] = T_SUB;
                if (!(n = answer(s, q, out, k + 1))) return 0;
            }
        }
    return n;
}

/* The answer: N/D with D one term as each term of N over D (split()) when
   kernels are in it, or always (sp: SERIES' 1/X+X/6) */
static uint8_t result(const ps_t *s, rat_t *p, uint8_t *out, rat_t *q, uint8_t sp)
{
    if (p->d.n > 1 && !isconst(&p->d) && gnew(s)) return gsplit((ps_t *)s, p, out, q);
    if (isconst(&p->d) || p->d.n > 1 || (!sp && !haskern(s, p, 0))) return answer(s, p, out, 0);
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
    for (k = 0; k < s->nk; k++) if (kdep(s, k, j)) m |= 15u << 4 * KN(s, k);
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
    for (k = 0; k < s->nk; k++) if (s->kf[k] == K_E || s->kf[k] == K_PI) m &= ~(15u << 4 * KN(s, k));
    return !m;
}

/* nsign() for a number with π and e in it: those two at 2^-20, else 3 */
static int nsignk(const ps_t *s, const rat_t *r)
{
    const term_t *x;
    int64_t v = 0, w, err = 100;
    uint8_t k, m;
    if (!isconst(&r->d) || r->d.t[0].rad != 1 || kdepx(s, r)) return 3;
    for (x = r->n.t; x < r->n.t + r->n.n; x++) {
        if ((x->rad & ~RNUM) || x->num > 30000 || x->num < -30000) return 3;
        w = tapprox(x);
        for (k = 0; k < s->nk; k++)
            for (m = (uint8_t)(x->mono >> 4 * KN(s, k) & 15); m--; ) w = w * (s->kf[k] == K_PI ? 3294199 : 2850325) >> 20;
        v += w;
        err += 50;
    }
    if (r->d.t[0].num < 0) v = -v;
    return v > err ? 1 : v < -err ? -1 : 2;
}

/* nsign(a - b) */
static int cmpr(ps_t *s, const rat_t *a, const rat_t *b)
{
    rat_t *d = get(s), *t = get(s);
    int v = 3;
    if (!t) return 3;
    *d = *a; *t = *b;
    pneg(&t->n);
    if (raddinto(s, d, t) && (v = nsign(d)) == 3) v = nsignk(s, d);
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
        if (!k || !madd(&t.mono, 1u << 4 * j) || !cmul(&t, 2, k) || !addterm(out, &t))
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
    if (e > 15) return 0;
    for (x = p->t; x < p->t + p->n; x++) if (!madd(&x->mono, (unsigned)e << 4 * j)) return 0;
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
            t.mono = x->mono;
            if (!madd(&t.mono, f ? 1u << 4 * (m & 1 ? cn : kn) : km) || !rmul(&t, x, &c)) return 0;
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
            if ((first && !canon(s, x)) || k < sh || k - sh > 15) return 0;
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

/* The numbers of qpart() and iasin() are term_t fractions with no root, kept in lowest terms under LIM by radd() and rmul() */
static term_t fri(int32_t v)
{
    term_t t;
    t.num = v; t.den = 1; t.rad = 1; t.mono = 0;
    return t;
}
NOINLINE static int fra(term_t *r, const term_t *a, const term_t *b) { *r = *a; return radd(r, b->num, b->den); }
NOINLINE static int frm(term_t *r, const term_t *a, const term_t *b) { return rmul(r, a, b); }
NOINLINE static int frd(term_t *r, const term_t *a, const term_t *b)
{
    term_t i;
    if (!b->num) return 0;
    rinv(&i, b);
    return rmul(r, a, &i);
}

/* p's coefficients of V⁰..Vᵐ; 0 when it has a root, another variable, or a higher power */
NOINLINE static int fcoef(const poly_t *p, uint8_t j, term_t *c, uint8_t m)
{
    const term_t *x;
    uint8_t i, e;
    for (i = 0; i <= m; i++) c[i] = fri(0);
    for (x = p->t; x < p->t + p->n; x++) {
        e = x->mono >> 4 * j & 15;
        if (x->rad != 1 || e > m || x->mono != (unsigned)e << 4 * j) return 0;
        c[e].num = x->num; c[e].den = x->den;
    }
    return 1;
}

/* v > 0 = (√(n·d)/d)², n·d = s²·r with r squarefree: 1/√v = k·√r, k = d/(s·r), and r */
NOINLINE static int fsq(const term_t *v, term_t *k, uint32_t *r)
{
    int32_t sq, w, q;
    if (v->num <= 0 || !mulc(v->num, v->den, &w)) return 0;
    q = sqfree(w, &sq);
    *r = (uint32_t)q;
    *k = fri(0);
    return mulc(sq, q, &w) && rset(k, v->den, w);
}

NOINLINE static int qpart(ps_t *s, rat_t *out, const rat_t *p, const poly_t *fq, uint8_t j);

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
        if (m == 1 && s->q && vdeg(&f->n, j) == 2) {      /* an irreducible quadratic: ln and tan⁻¹ */
            if (!qpart(s, out, p, &f->n, j)) return 0;
            continue;
        }
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
                if (!kvar(s, r, s->pl && al.num > 0 && nsign(r) <= 0 ? K_LOG : K_LABS, f, 0) || !rmulinto(s, a, r, 0)) return 0;
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

/* steps( : the rules, and the sinks (below) */
enum { SR_SUB, SR_ADD, SR_DIV, SR_MUL, SR_EACH, SR_POW, SR_CHAIN, SR_PROD, SR_SUM, SR_TOP, SR_BOT, SR_QUOT,
       SR_MOVE, SR_FACTOR, SR_ZERO, SR_DISC, SR_NOREAL, SR_COMMON, SR_RR, SR_PDIV,
       SR_FIRST, SR_INT, SR_FB, SR_FA, SR_FD, SR_LDOM, SR_LSUB, SR_LLEAD,
       SR_PF, SR_PD, SR_PC, SR_QF, SR_CHIN, SR_EXP, SR_SWAP, SR_NTH, SR_RPIV, SR_RDIV, SR_RELIM, SR_RISO, SR_RSQ, SR_RBAD, SR_USUB, SR_UINT, SR_UBACK, SR_PARTU, SR_PARTV, SR_PFR, SR_PFI, SR_DFORM, SR_DMU, SR_DSEP, SR_DIC, SR_DCHAR, SR_DROOT, SR_DCPX, SR_DHOM, SR_DPART, SR_NSOL, SR_NSUB,
       /* stepsalg */ SR_PAIR, SR_SPLIT, SR_GROUP, SR_BRACKET, SR_PFAC, SR_QFORM, SR_DNUM, SR_PUTIN, SR_RADS, SR_QI, SR_QRES, SR_NOFAC, SR_SQRTS, SR_RRT, SR_RDIVF,
       /* stepsalg */ SR_XDIST, SR_XPOW, SR_XTERM, SR_COMB, SR_LCD, SR_CDRW, SR_CDADD, SR_LDIV, SR_LSUBT, SR_LQUO, SR_LREM, SR_EUC, SR_EUCZ, SR_PDEG, SR_PCO, SR_LEFT, SR_RIGHT, SR_NRQ, SR_NRA,
       SR_TSUM, SR_TDBL, SR_TTRI, SR_TPOW, SR_TPRD, SR_TLOG, SR_TLB, SR_TEUL, SR_TPYS, SR_TRES, SR_TNOP, SR_PFD, SR_PFSOL,
       SR_ABSC, SR_ABSS, SR_EXCL, SR_CLEARF, SR_IFLIP, SR_ICRIT, SR_ITEST,
       /* stepsgeo */ SR_GDIST, SR_GDSUB, SR_GSQ, SR_GSQM, SR_GSTD, SR_GPL, SR_GSLOPE, SR_GLSL, SR_GPTSP, SR_GPERP, SR_GPTS, SR_GVPAR, SR_GVPERP, SR_GHPERP, SR_GVERT, SR_GINT, SR_GELIM, SR_GSOLV, SR_GBACK, SR_GREFV, SR_GREFN, SR_GREFP, SR_GMID, SR_GBV, SR_GBH, SR_GSECT, SR_GSECP, SR_GSHOE, SR_GSHSUM, SR_GSHA, SR_GSIDE, SR_GPER, SR_GCEN, SR_GBIS, SR_GCROSS, SR_GALT, SR_GINSD, SR_GINSW, SR_GHYP, SR_GLEG, SR_GCIR, SR_GCIRR, SR_GCN1, SR_GCN2, SR_GCN3, SR_GCN4, SR_GROT0, SR_GROTCS, SR_GROTX, SR_GROTY, SR_GROTC, SR_GDIL, SR_GVAL, SR_GSIMP, SR_GCALC, SR_MDET2, SR_MDETE, SR_MDETM, SR_MDETV, SR_MDETS, SR_MISWAP, SR_MICOF, SR_MITR, SR_MIDIV, SR_MSWAP, SR_MSCALE, SR_MELIM, SR_MRDONE, SR_MTR, SR_MDOT, SR_MPROD, SR_MCRS, SR_MNRM,
       /* stepscalc */
       SR_KSPLIT, SR_KCONST, SR_KLIN, SR_KCLIN, SR_KPOW, SR_KSQRT, SR_KSIN, SR_KCOS, SR_KTAN, SR_KEXP, SR_KLN,
       SR_KCHAIN1, SR_KANY, SR_KCM, SR_KPROD, SR_KQUOT, SR_KPUT, SR_KQPUT, SR_KSIM, SR_KADD, SR_KORD, SR_KAT,
       SR_KCHAIN, SR_KOUT, SR_KIN, SR_KCMUL, SR_ISPLIT, SR_ICONST, SR_IPOW, SR_IROOT, SR_ISIN, SR_ICOS, SR_IEXP,
       SR_ILIN, SR_ICM, SR_IANY, SR_IPU, SR_IPV, SR_IPS, SR_IPF, SR_ISUM, SR_IC, SR_IFB, SR_IFA, SR_IFD, SR_IUSUB,
       SR_IUINT, SR_IUBACK, SR_IPFR, SR_IPFI, SR_LDIR, SR_LZ00, SR_LZN0, SR_LCANC, SR_LCANC2, SR_LSER, SR_LS0,
       SR_LSE, SR_LSI, SR_LDT, SR_LI1, SR_LI0, SR_LIE, SR_LIP, SR_TLD, SR_TLM, SR_TLY, SR_TLN, SR_TLL, SR_TLS,
       SR_CDP, SR_CDM, SR_CDF, SR_TYK, SR_TYS, SR_SE0, SR_SE1, SR_SE2, SR_SE3, SR_IMF, SR_IMX, SR_IMY, SR_IMD,
       SR_IMN, SR_SUL, SR_SUV, SR_SUS, SR_PRL, SR_PRV, SR_PRS, SR_SUF, SR_FMD, SR_FMR, SR_FME, SR_FMW, SR_FMX,
       SR_ARD, SR_ARI, SR_ARS, SR_ARF, SR_DYF, SR_DYI, SR_DYH, SR_DYP, SR_DYA, SR_DYV, SR_DYC, SR_DYS, SR_DYY,
       SR_DYX, SR_DYR, SR_DYD, SR_DYZ, SR_SQD, SR_SQP, SR_SQA, SR_IRP, SR_ISAME, SR_XBIN, SR_XBT,
       /* stepsfx */ SR_XLET, SR_XBACK, SR_XNEG, SR_XLN, SR_XLB, SR_XEXACT, SR_XCOMB, SR_XRAISE, SR_XSAME, SR_XDOM, SR_XLNB, SR_XROOT,
       SR_CYIS, SR_CYIC, SR_CYIT, SR_CYAS, SR_CYAC, SR_CYPER, SR_CYX, SR_CYRNG, SR_CYBND, SR_CYLET, SR_CYBACK, SR_CYSUM,
       /* calcgap */ SR_KASIN, SR_KACOS, SR_KATAN, SR_KPOWC, SR_KPOWB, SR_KPOWL, SR_IATAN, SR_IASIN,
       /* gaplim */ SR_LCJ, SR_LABS, SR_LSIN, SR_LGROW, SR_LGZ, SR_LGE, SR_LGI, SR_LSQ, SR_LOSC, SR_LPOW, SR_LP1, SR_LEXP, SR_LANS, SR_SGEO, SR_SGSUM, SR_SGINF, SR_SPS, SR_SPF, SR_STEL,
       /* gaps-de */ SR_DLP, SR_DLM, SR_DLX, SR_DLI, SR_DSX, SR_DSI
       /* end stepscalc */ };
static void stepa(ps_t *s, uint16_t rule, rat_t *par, rat_t *a, rat_t *b);
static void stept(ps_t *s, uint16_t rule, const uint8_t *pt, uint8_t pn, const uint8_t *et, uint8_t en);

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
        for (k = 0; k < s->nk; k++) if (s->kf[k] > K_COS && (!s->la || (s->kf[k] != K_PI && s->kf[k] != K_E))) return 0;
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
#ifdef NOSTEPSX
#define lpoint(...) ((void)0)
#define lser(...) ((void)0)
#define linf(...) ((void)0)
#else
NOINLINE static void lpoint(ps_t *s, const rat_t *raw, rat_t *p, uint8_t j, rat_t *a, uint8_t direct);
NOINLINE static void lser(ps_t *s, rat_t *p, rat_t *c, rat_t *d, uint8_t j, rat_t *a, uint8_t vn, uint8_t vd, uint8_t lim);
NOINLINE static void linf(ps_t *s, const rat_t *p, uint8_t j, int8_t e, rat_t *c, uint8_t lim);
#endif

/* The work of clim(): 1 the value in p[1], 5 the same to be answered term by term, 2 +INFINITY, 3 -INFINITY,
   4 UNDEFINED, 0 not so. dir is the side's sign (0 both). */
NOINLINE static uint8_t lcore(ps_t *s, uint8_t cmd, rat_t *p, uint8_t j, rat_t *a, int dir, int8_t inf, const rat_t *raw)
{
    rat_t *c = p + 1, *d = p + 2;
    uint8_t vn, vd, rt, lim = cmd == C_LIMIT;
    int8_t e;
    int v, sg = 1;
    if (inf) {
        if (rooted(s, p, j) || !atinf(s, c, p, j, &e)) return 0;
        if (s->st) linf(s, p, j, e, c, lim);
        if (!lim) { if (!vpow(c, j, e)) return 0; }
        else if (e < 0) rconst(c, 0);
        else if (e > 0) {
            if ((sg = nsign(c)) > 1) return 0;
            if (inf < 0 && e & 1) sg = -sg;
            goto big;
        }
        return 5;
    }
    if (!a) { a = p + 3; rconst(a, 0); }        /* DOMINANTTERM(f,V): at 0 */
    if (!p->n.n) { *c = *p; return 1; }
    if ((rt = rooted(s, p, j)) && (sg = nsign(a)) != 1 && (sg || dir != 1 || !lim)) return 0;
    if (lim) {
        uint8_t t0 = s->top;
        *c = *p;
        v = evalat(s, c, j, a);
        s->top = t0;                            /* a refused evalat leaves slots taken */
        if (v == 1) {
            if (s->st) lpoint(s, raw, p, j, a, 1);
            return 1;
        }
        if (!v || (rt && sg != 1)) return 0;
        if (s->st) lpoint(s, raw, p, j, a, 0);
    }
    if (tfirst(s, d, &p->d, j, a, 7, &vd) != 1 || vd > 7 ||
        tfirst(s, c, &p->n, j, a, lim ? vd : 7, &vn) != 1 || vn > 7) return 0;
    if (!lim) {                                 /* (c/d)(V-a)^(vn-vd) */
        if (s->st) lser(s, p, c, d, j, a, vn, vd, 0);
        if (!rmulinto(s, c, d, 1) || !vplus(d, a, j, 0) || !powinto(s, d, (int32_t)vn - vd) ||
            !rmulinto(s, c, d, 0)) return 0;
        return 5;
    }
    if (s->st) lser(s, p, c, d, j, a, vn, vd, 1);
    if (vn > vd) rconst(c, 0);                  /* N vanishes faster */
    else if (vn == vd) { if (!rmulinto(s, c, d, 1)) return 0; }
    else {
        if ((v = nsign(c)) > 1 || (sg = nsign(d)) > 1) return 0;
        sg *= v;
        if ((vd - vn) & 1) {
            if (!dir) return 4;
            sg *= dir;
        }
        goto big;
    }
    return 1;
big:
    return sg > 0 ? 2 : 3;
}

NOINLINE static uint8_t clim(ps_t *s, uint8_t cmd, const char *name, uint8_t *out, rat_t *p, uint8_t j,
                             rat_t *a, rat_t *b, int8_t inf, const rat_t *raw)
{
    int dir = 0;
    uint8_t v;
    if (b && (dir = nsign(b)) > 1) return error(out, "ARGUMENT", name, "");
    v = lcore(s, cmd, p, j, a, dir, inf, raw);
    if (v == 4) return error(out, "UNDEFINED", name, "");
    if (v > 1 && v < 4) return error(out, v == 2 ? "+INFINITY" : "-INFINITY", name, "");
    return v ? result(s, p + 1, out, p + 2, v == 5) : 0;
}

#ifdef NOSTEPSX
#define stc(...) ((void)0)
#define stf(...) ((void)0)
#define sesh(...) ((void)0)
#define sustep(...) 0
#define fmsteps(...) ((void)0)
#define ari(...) ((void)0)
#define arfin(s, r, out, q, a, b) result(s, r, out, q, 0)
#define tyk(...) ((void)0)
#define tlsteps(...) ((void)0)
#else
NOINLINE static void stc(ps_t *s, uint8_t v, uint16_t rule, const char *ef, const rat_t *r);
NOINLINE static void stfx(ps_t *s, uint8_t v, uint16_t rule, const char *pf, const char *ef, rat_t *const *a,
                          rat_t *fin, uint16_t rule2);
NOINLINE static void stf2(ps_t *s, uint8_t v, uint16_t rule, const char *pf, const char *ef, rat_t *const *a);
NOINLINE static void stn(ps_t *s, uint8_t v, uint16_t rule);
#define stf(s, v, rule, pf, ef, ...) stf2(s, v, rule, pf, ef, (rat_t *[]){ __VA_ARGS__ })
NOINLINE static void sesh(ps_t *s, uint8_t j, uint16_t rule, const rat_t *r, uint8_t vd);
NOINLINE static uint8_t sustep(ps_t *s, uint8_t cmd, rat_t *p, uint8_t j, int32_t l, int32_t h);
NOINLINE static void fmsteps(ps_t *s, uint8_t cmd, rat_t *p, rat_t *q, const fl_t *fl, rat_t *a, rat_t *b, uint8_t j);
NOINLINE static void ari(ps_t *s, uint8_t j, rat_t *g);
NOINLINE static void tyk(ps_t *s, uint8_t j, rat_t *x, rat_t *d, rat_t *c, uint8_t k);
NOINLINE static void tlsteps(ps_t *s, rat_t *p, uint8_t j, rat_t *c, uint8_t cmd);
/* ARCLEN's last step: the integral from a to b, its value */
NOINLINE static uint8_t arfin(ps_t *s, rat_t *r, uint8_t *out, rat_t *q, rat_t *a, rat_t *b)
{
    uint8_t n = result(s, r, out, q, 0), pb[MAXOUT], m;
    if (n && s->st && (m = answer(s, a, pb, 0)) && m + 2 < MAXOUT) {
        pb[m++] = T_LCOMMA;
        if ((m = answer(s, b, pb, m))) stept(s, SR_ARF, pb, m, out, n);
    }
    return n;
}
#endif

/* SERIES(f,V,order[,a]): with W = V-a, f = N/D = W^-vd·N/D̃, D̃ = D/W^vd;
   N and D̃ as Taylor polynomials to W^(order+vd), then N/D̃ by ascending
   division: 1/sin(X) is 1/X+X/6+7X³/360 to order 3. Powers stay below 16. */
NOINLINE static uint8_t cser(ps_t *s, uint8_t *out, rat_t *p, uint8_t j, rat_t *a, uint8_t o)
{
    rat_t *A = p + 1, *B = p + 2, *c = p + 3, *q = get(s), *t = get(s), *u;
    term_t *x;
    uint8_t vd, k, m;
    if (!t || rooted(s, p, j) || tfirst(s, c, &p->d, j, a, 7, &vd) != 1 || (m = o + vd) > 15) return 0;
    t->n = p->n; pconst(&t->d, 1);
    k = 0;
    if (tpoly(s, A, t, j, a, m, 0, &k) != 1) return 0;
    if (!a->n.n && !isconst(&p->d)) sesh(s, j, SR_SE1, A, 0);
    t->n = p->d; pconst(&t->d, 1);
    k = vd;                                     /* D̃ = D/W^vd */
    if (tpoly(s, B, t, j, a, m + vd, 0, &k) != 1) return 0;
    if (!a->n.n && !isconst(&p->d)) sesh(s, j, SR_SE2, B, vd);
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
    k = result(s, q, out, t, 1);
    if (k && s->st) stept(s, isconst(&p->d) ? SR_SE0 : SR_SE3, 0, 0, out, k);
    return k;
}

/* p = c·ρ^V (or c/ρ^V): one K_POW kernel with ±V for its exponent and a number for its base, to the first power
   in the top or the bottom, c free of V. c in c, ρ in ro; 0 when p is not so */
NOINLINE static uint8_t geof(ps_t *s, const rat_t *p, uint8_t j, rat_t *c, rat_t *ro)
{
    uint8_t k, g, i, sn, sd = 2, m, mm = 0, ng;
    const rat_t *b;
    const poly_t *q;
    for (k = 0; k < s->nk && !(s->kf[k] == K_POW && kdep(s, k, j)); k++) ;
    if (k == s->nk) return 0;
    b = s->ka[k];
    *ro = *s->kb[k];                            /* the exponent: V or -V */
    if ((ng = ro->n.n == 1 && ro->n.t[0].num < 0)) pneg(&ro->n);
    if (!isvar(ro, j) || b->n.n != 1 || b->n.t[0].mono || b->n.t[0].rad != 1 || !isconst(&b->d)) return 0;
    g = KN(s, k);
    *c = *p;
    for (sn = 0; sn < 2; sn++) {
        q = sn ? &c->d : &c->n;
        for (i = 0; i < q->n; i++) {
            m = q->t[i].mono >> 4 * g & 15;
            if (m > 1 || (i && m != mm)) return 0;
            mm = m;
        }
        if (q->n && mm) {
            if (sd != 2) return 0;
            sd = sn;
        }
    }
    if (sd == 2) return 0;
    divmono(sd ? &c->d : &c->n, 1u << 4 * g);
    *ro = *b;
    return !dep(s, c, j) && (!ng == !sd || powinto(s, ro, -1));
}

/* SUM(c·ρ^V, V, a, b), a whole and ρ a number: c(ρ^(b+1) - ρ^a)/(ρ - 1), b free of V, ρ > 0. To ∞: the first term
   over 1 - ρ when -1 < ρ < 1, else the terms grow (±INFINITY) or swing (UNDEFINED) */
NOINLINE static uint8_t sgeo(ps_t *s, uint8_t *out, const char *name, rat_t *p, uint8_t j, rat_t *a, rat_t *b, int8_t inf)
{
    uint8_t top = s->top, v = s->var[NV - 1 - j], n = 0;
    rat_t *t = p + 1, *w = p + 2, *u = p + 3, *c = get(s), *ro = get(s), *z = get(s), *y;
    int32_t l;
    int s1, s2;
    if (!z || !geof(s, p, j, c, ro) || !intval(a, &l) || l < -MAXPOW || l > MAXPOW) goto done;
    *t = *ro;
    if (!powinto(s, t, l) || !rmulinto(s, t, c, 0)) goto done;      /* the first term */
    *w = *ro; rconst(u, -1);
    if (!raddinto(s, w, u)) goto done;                              /* ρ - 1 */
    s1 = nsign(w);
    *z = *ro; rconst(u, 1);
    if (!raddinto(s, z, u)) goto done;                              /* ρ + 1 */
    s2 = nsign(z);
    if (s1 > 1 || s2 > 1) goto done;
    if (!inf && nsign(ro) != 1) goto done;
    stf(s, v, SR_SGEO, "$,$", 0, t, ro);
    if (inf) {
        if (s1 < 0 && s2 > 0) {
            *w = *ro; pneg(&w->n); rconst(u, 1);                    /* 1 - ρ */
            if (!raddinto(s, w, u) || !rmulinto(s, t, w, 1)) goto done;
            stf(s, v, SR_SGINF, 0, "$", t);
            n = result(s, t, out, w, 0);
        } else if (s1 > 0 && (s2 = nsign(c)) < 2 && s2) n = error(out, s2 > 0 ? "+INFINITY" : "-INFINITY", name, "");
        else if (s1 <= 0) n = error(out, "UNDEFINED", name, "");
        goto done;
    }
    *z = *b; rconst(u, 1);
    if (!raddinto(s, z, u)) goto done;                              /* b + 1 */
    if (intval(z, &l) && l >= -MAXPOW && l <= MAXPOW) {
        *u = *ro;
        if (!powinto(s, u, l)) goto done;
    } else if (!(y = keep(s, z)) || !kvar(s, u, K_POW, ro, y)) goto done;
    intval(a, &l);
    *z = *ro;
    if (!powinto(s, z, l)) goto done;
    pneg(&z->n);
    if (!raddinto(s, u, z) || !rmulinto(s, u, c, 0) || !rmulinto(s, u, w, 1)) goto done;
    stf(s, v, SR_SGSUM, 0, "$", u);
    n = result(s, u, out, w, 0);
done:
    s->top = top;
    return n;
}

/* SUM(N/D, V, a, ∞), N and D polynomials in V alone, a whole. Terms that fall no faster than 1/V grow (±INFINITY).
   N a number and D = d·Vᵐ, m even to 10: N/d(ζ(m) less the terms before a), ζ(m) = q·πᵐ. Else D with only whole
   simple roots r: N/D = Σ A/(V - r), A = N(r)/D′(r), and the sum telescopes to -Σ A·H(a-r-1), H the harmonic sums */
NOINLINE static uint8_t srat(ps_t *s, uint8_t *out, const char *name, rat_t *p, uint8_t j, rat_t *a)
{
    static const int32_t zq[] = { 6, 90, 945, 9450, 93555 };
    uint8_t top = s->top, nk = s->nk, lim = s->lim, v = s->var[NV - 1 - j], dn, dd, i, m = 0, n = 0;
    rat_t *t = p + 1, *w = p + 2, *acc = p + 3, *D = get(s), *Dp = get(s), *u = get(s), *x = get(s);
    const term_t *y, *ln = 0, *ld = 0;
    int32_t l, r, rt[8], e;
    if (!x || haskern(s, p, 0) || !intval(a, &l) || l < -MAXPOW || l > MAXPOW || !p->n.n || hasrad(&p->n) || hasrad(&p->d) ||
        ((univ(&p->n) | univ(&p->d)) & ~(15u << 4 * j)) || (dn = vdeg(&p->n, j)) == 0xFF || (dd = vdeg(&p->d, j)) == 0xFF) goto done;
    for (y = p->n.t; y < p->n.t + p->n.n; y++) if ((y->mono >> 4 * j & 15) == dn) ln = y;
    for (y = p->d.t; y < p->d.t + p->d.n; y++) if ((y->mono >> 4 * j & 15) == dd) ld = y;
    if (dd <= dn + 1) {                                             /* the terms stay or fall like 1/V */
        n = error(out, (ln->num < 0) != (ld->num < 0) ? "-INFINITY" : "+INFINITY", name, "");
        goto done;
    }
    if (p->n.n == 1 && p->d.n == 1 && !dn && !p->n.t[0].mono && p->d.t[0].mono == (unsigned)dd << 4 * j && dd < 11 && !(dd & 1)) {
        if (l < 1) { n = error(out, "DOMAIN", name, ""); goto done; }           /* the p-series */
        if (!kvar(s, u, K_PI, 0, 0) || !powinto(s, u, dd)) goto done;
        rconst(w, 1); w->n.t[0].den = zq[dd / 2 - 1];
        if (!rmulinto(s, u, w, 0)) goto done;
        for (r = 1; r < l; r++) {
            for (e = 1, i = 0; i < dd; i++) if ((e *= r) > LIM) goto done;
            rconst(w, -1); w->n.t[0].den = e;
            if (!raddinto(s, u, w)) goto done;
        }
        *t = *p; t->n.t[0].mono = t->d.t[0].mono = 0;
        if (!rmulinto(s, u, t, 0)) goto done;
        stf(s, v, SR_SPS, 0, "$", u);
        n = result(s, u, out, w, 0);
        goto done;
    }
    D->n = p->d; pconst(&D->d, 1);
    if (!drat(s, Dp, D, j)) goto done;
    for (r = -30; r <= 30; r++) {                                   /* the whole roots of D */
        *t = *D; rconst(w, r);
        if (evalat(s, t, j, w) != 1) goto done;
        if (!t->n.n) {
            if (r >= l) { n = error(out, "DOMAIN", name, ""); goto done; }
            if (m == 8) goto done;
            rt[m++] = r;
        }
    }
    if (m != dd) goto done;
    stf(s, v, SR_SPF, "$", 0, p);
    rconst(acc, 0);
    for (i = 0; i < m; i++) {
        x->n = p->n; pconst(&x->d, 1);
        rconst(w, rt[i]);
        *u = *Dp;
        if (evalat(s, x, j, w) != 1 || evalat(s, u, j, w) != 1 || !u->n.n || !rmulinto(s, x, u, 1)) goto done;    /* A */
        rconst(t, 0);
        for (e = l - rt[i] - 1; e > 0; e--) {                       /* H(a-r-1) */
            rconst(w, 1); w->n.t[0].den = e;
            if (!raddinto(s, t, w)) goto done;
        }
        if (!rmulinto(s, t, x, 0) || !raddinto(s, acc, t)) goto done;
    }
    pneg(&acc->n);
    stf(s, v, SR_STEL, 0, "$", acc);
    n = result(s, acc, out, w, 0);
done:
    s->top = top;
    if (!n) { s->nk = nk; s->lim = lim; }
    return n;
}

/* SUM, PRODUCT. Whole ends 100 terms apart at most, f not a polynomial in V
   (on the calculator a term costs some 25 ms): term by term, and past the
   end Karr's convention -- an empty range is 0 or 1, one backwards the
   negative or reciprocal of the range between. Else SUM of a polynomial in V
   (Newton's forward differences, Σ Δⁱf(a)·C(n,i+1), n = b-a+1 terms, whole
   when it is a number) plus c·e^'s of linear u, E(V): geometric,
   c(E(b+1) - E(a))/(e^ρ - 1). */
NOINLINE static uint8_t csum(ps_t *s, uint8_t cmd, const char *name, uint8_t *out, rat_t *p, uint8_t j,
                             rat_t *a, rat_t *b, int8_t inf)
{
    rat_t *acc = p + 1, *t = p + 2, *w = p + 3, *x = get(s), *g = get(s);
    unsigned vk = vkern(s, j), km = 0;
    term_t ro, a1;
    int32_t l, h, k;
    uint8_t i, top = s->top, nk = s->nk, lim = s->lim, lg = 0;
    int v = 0;
    if (!g) return 0;
    if (inf) return sgeo(s, out, name, p, j, a, b, 1) ?: srat(s, out, name, p, j, a);
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
            } else sustep(s, cmd, p, j, l, h);
            i = result(s, acc, out, t, 0);
            if (i && s->st && !(h < l - 1)) stept(s, cmd == C_SUM ? SR_SUS : SR_PRS, 0, 0, out, i);
            return i;
        }
        /* e^(2X) from 1 to 9: nine kernels, too many; its closed form, then,
           the kernels made on the way let go */
        s->top = top; s->nk = nk; s->lim = lim;
    }
    if (cmd == C_SUM && (i = sgeo(s, out, name, p, j, a, b, 0))) return i;
    if (cmd == C_PRODUCT || vdeg(&p->d, j) || used(&p->d, vk) || vdeg(&p->n, j) == 0xFF) return 0;
    if (s->st && intval(a, &l) && intval(b, &h) && h >= l && h - l < 100) lg = sustep(s, cmd, p, j, l, h);
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
    i = result(s, acc, out, t, 0);
    if (i && s->st) stept(s, lg ? SR_SUS : SR_SUF, 0, 0, out, i);
    return i;
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
    if (!get(s) || haskern(s, p, 0) || hasrad(&p->n) || (univ(&p->n) | univ(&p->d)) & ~vm ||
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
    fmsteps(s, cmd, p, q, &fl, a, b, j);
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
    if (s->st) stept(s, cmd == C_FMIN ? SR_FMW : SR_FMX, 0, 0, out, n);
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
    if (!z || !drat(s, g, p, j) || haskern(s, g, 0)) return 0;
    for (k = 0; k < 2; k++) {
        *x = *p;
        if ((v = evalat(s, x, j, k ? b : a)) != 1) goto at;
        *x = *g;
        if (evalat(s, x, j, k ? b : a) != 1) return 0;
    }
    *h = *g;
    rconst(x, 1);
    if (!rmulinto(s, h, g, 0) || !raddinto(s, h, x)) return 0;
    ari(s, j, g);
    if (!vdeg(&h->n, j) && !vdeg(&h->d, j)) {  /* f linear */
        *x = *a;
        pneg(&x->n);
        *z = *b;
        if (!sqrtinto(s, h) || !raddinto(s, x, z) || !rmulinto(s, h, x, 0)) return 0;
        return arfin(s, h, out, x, a, b);
    }
    if (psqrt(s, &y->n, &h->n, j) && psqrt(s, &y->d, &h->d, j)) {
        stc(s, 0, SR_ARS, "$", y);
        *x = *y;
        if ((v = evalat(s, x, j, a)) != 1 || (sg = nsign(x)) > 1 || (v = integ(s, z, y, j, a, b, 0, 0)) != 1 ||
            (v = defint(s, z, j, a, b)) != 1) goto at;
        if (sg < 0) pneg(&z->n);
        return arfin(s, z, out, x, a, b);
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
        return arfin(s, x, out, y, a, b);
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
        return arfin(s, x, out, y, a, b);
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

/* ---- steps( : the worked solution, recorded while the command runs ----
   There is no expression tree to trace, so each explainer below works out its steps from the
   data the command already holds (the two sides, the terms) and prints every expression with
   answer(): what the screen shows is what the engine would answer. A step that cannot be
   printed marks the log unreliable (flag 2) and symce_engine then reports no steps; the
   answer itself is never touched. The rule numbers are menu.c's strings, in this order. */

static uint8_t totok(const uint8_t *a, uint8_t n, uint8_t *w);

static void streset(uint8_t *st)
{
    if (st) { st[1] = st[64]; st[2] = 0; st[67] = 0; st[68] = st[65]; st[69] = st[66]; }   /* back to the steps before the command (nested()'s First: lines) */
}

/* One step: the rule, a parameter (or 0) and the expression a [= b] after it. */
static void stepa(ps_t *s, uint16_t rule, rat_t *par, rat_t *a, rat_t *b)
{
    uint8_t *w, n = 0, m = 0;
    unsigned u;
    if (!s->st || s->st[2]) return;
    u = s->st[68] | s->st[69] << 8;
    if (u + 2 * (MAXOUT + 3) + 4 > STCAP) { s->st[2] |= 1; return; }
    w = s->st + u;
    w[0] = (uint8_t)rule;
    if (par && !(n = answer(s, par, w + 2, 0))) goto bad;
    n = totok(w + 2, n, w + 2);
    if (n > 127) goto bad;          /* plen has 7 bits: bit 7 is the rule's bit 8 */
    w[1] = n | (rule >> 8) << 7;   /* rule bit 8 rides in the parameter length's top bit: more than 255 rules */
    if (a && !(m = answer(s, a, w + 3 + n, 0))) goto bad;
    if (b) {
        w[3 + n + m++] = T_EQ;
        if (!(m = answer(s, b, w + 3 + n, m))) goto bad;
    }
    m = totok(w + 3 + n, m, w + 3 + n);
    w[2 + n] = m;
    u += 3 + n + m;
    s->st[68] = (uint8_t)u; s->st[69] = (uint8_t)(u >> 8);
    s->st[1]++;
    return;
bad:
    s->st[2] |= 2;
}

/* r = the polynomial c1*V + c0 (coefficient terms; V at nibble j), over 1 */
static void lpoly(rat_t *r, const term_t *c1, const term_t *c0, uint8_t j)
{
    term_t t;
    r->n.n = 0;
    t = *c1; t.mono = 1u << 4 * j; t.rad = 1;
    addterm(&r->n, &t);
    t = *c0; t.mono = 0; t.rad = 1;
    addterm(&r->n, &t);
    pconst(&r->d, 1);
}

/* a coefficient c: V*c if v, else c, as a rat to print */
static void cpar(rat_t *r, const term_t *c, uint8_t v, uint8_t j, uint8_t neg)
{
    term_t t = *c;
    t.mono = v ? 1u << 4 * j : 0; t.rad = 1;
    if (neg) t.num = -t.num;
    r->n.n = 0;
    addterm(&r->n, &t);
    pconst(&r->d, 1);
}

/* SOLVE(L=R, V) with both sides of degree 1 in V and numbers: one move to a step. Each move is
   done to both sides: subtract the V term of the right, then the constant of the left, then divide
   by (or multiply by the inverse of) the coefficient left on V. */
NOINLINE static void lsteps(ps_t *s, const rat_t *L, const rat_t *R)
{
    uint8_t top = s->top, side, k, j, b;
    rat_t *l = get(s), *r = get(s), *t = get(s);
    term_t co[2][2], u;            /* co[side][0] constant, [1] V */
    const rat_t *side_[2];
    const term_t *x;
    if (!s->st || !t || !s->sm || (s->sm & (s->sm - 1))) goto done;
    for (b = 0; !(s->sm >> b & 1); b++) ;
    j = NV - 1 - s->rank[b];
    side_[0] = L; side_[1] = R;
    for (side = 0; side < 2; side++) {
        const rat_t *e = side_[side];
        if (e->d.n != 1 || e->d.t[0].mono || e->d.t[0].num != 1 || e->d.t[0].den != 1 || e->d.t[0].rad != 1) goto done;
        for (k = 0; k < 2; k++) { co[side][k].num = 0; co[side][k].den = 1; co[side][k].mono = 0; co[side][k].rad = 1; }
        for (x = e->n.t; x < e->n.t + e->n.n; x++) {
            if (x->rad != 1 || (x->mono && x->mono != 1u << 4 * j)) goto done;
            if (!radd(&co[side][!!x->mono], x->num, x->den)) goto done;
        }
    }
#ifndef NOSTEPSX
    for (k = 1, b = 0; k < s->st[3]; k++)      /* a bracket after an operator or number, not a command's */
        b += s->st[4 + k] == T_LPAR && (s->st[3 + k] == T_RPAR || s->st[3 + k] == T_ADD || s->st[3 + k] == T_SUB ||
             s->st[3 + k] == T_MUL || s->st[3 + k] == T_DIV || s->st[3 + k] == T_EQ || s->st[3 + k] == T_COMMA ||
             s->st[3 + k] == T_NEG || s->st[3 + k] == T_LPAR || (s->st[3 + k] >= 0x30 && s->st[3 + k] <= 0x39));
    if (b) {                                /* a bracket in the entry: both sides multiplied out first */
        lpoly(l, &co[0][1], &co[0][0], j); lpoly(r, &co[1][1], &co[1][0], j);
        stepa(s, SR_EXP, 0, l, r);
    }
    if (!co[0][1].num && co[1][1].num) {        /* V only on the right: swap the sides */
        for (k = 0; k < 2; k++) { u = co[0][k]; co[0][k] = co[1][k]; co[1][k] = u; }
        lpoly(l, &co[0][1], &co[0][0], j); lpoly(r, &co[1][1], &co[1][0], j);
        stepa(s, SR_SWAP, 0, l, r);
    }
#endif
    /* (A1 - A2)V + B1 = B2 + ... : refuse the ones with no single answer */
    u = co[0][1];
    if (!radd(&u, -co[1][1].num, co[1][1].den) || !u.num) goto done;
    /* move 1: the right side's V term, move 2: the left side's constant */
    for (k = 0; k < 2; k++) {
        term_t *m = &co[k ? 0 : 1][k ? 0 : 1];  /* k 0: right's V term, k 1: left's constant */
        term_t *to = k ? &co[1][0] : &co[0][1];
        uint8_t neg;
        if (!m->num) continue;
        u = *m;
        neg = u.num < 0;
        cpar(t, &u, !k, j, neg);
        if (!radd(to, -u.num, u.den)) goto bad;
        m->num = 0; m->den = 1;
        lpoly(l, &co[0][1], &co[0][0], j); lpoly(r, &co[1][1], &co[1][0], j);
        stepa(s, neg ? SR_ADD : SR_SUB, t, l, r);
    }
    /* move 3: the coefficient on V */
    u = co[0][1];
    if (u.num != 1 || u.den != 1) {
        term_t f;
        f.mono = 0; f.rad = 1;
        rinv(&f, &u);
        if (!rmul(&co[1][0], &co[1][0], &f)) goto bad;
        co[0][1].num = co[0][1].den = 1;
        lpoly(l, &co[0][1], &co[0][0], j); lpoly(r, &co[1][1], &co[1][0], j);
        if (u.den != 1 || u.num == -1) { cpar(t, &f, 0, j, 0); stepa(s, SR_MUL, t, l, r); }
        else { cpar(t, &u, 0, j, 0); stepa(s, SR_DIV, t, l, r); }
    }
    goto done;
bad:
    s->st[2] |= 2;
done:
    s->top = top;
}

/* A step into the log st: the rule, the parameter pt[pn] and the expression et[en], as tokens
   to keep, each made tokens first when cv (the glyphs the result routine maps). */
static uint8_t stlog(uint8_t *st, uint16_t rule, const uint8_t *pt, uint8_t pn, const uint8_t *et, uint8_t en, uint8_t cv)
{
    uint8_t *w;
    unsigned u;
    if (!st || st[2]) return 0;
    u = st[68] | st[69] << 8;
    if (u + pn + en + 4 > STCAP) { st[2] |= 1; return 0; }
    if (!cv && pn > 127) { st[2] |= 2; return 0; }
    w = st + u;
    w[0] = (uint8_t)rule;
    if (cv) pn = totok(pt, pn, w + 2);
    else for (u = 0; u < pn; u++) w[2 + u] = pt[u];
    if (pn > 127) { st[2] |= 2; return 0; }   /* plen has 7 bits: bit 7 is the rule's bit 8 */
    w[1] = pn | (rule >> 8) << 7;
    if (cv) en = totok(et, en, w + 3 + pn);
    else for (u = 0; u < en; u++) w[3 + pn + u] = et[u];
    w[2 + pn] = en;
    u = (st[68] | st[69] << 8) + 3 + pn + en;
    st[68] = (uint8_t)u; st[69] = (uint8_t)(u >> 8);
    st[1]++;
    return 1;
}

/* A step from engine tokens: pt, et are left as they were. */
static void stept(ps_t *s, uint16_t rule, const uint8_t *pt, uint8_t pn, const uint8_t *et, uint8_t en)
{
    stlog(s->st, rule, pt, pn, et, en, 1);
}

/* the discriminant of p, a quadratic in V (nibble j) with whole numbers: b^2 - 4ac */
static int pdisc(const poly_t *p, uint8_t j, int64_t *d)
{
    const term_t *x;
    int64_t c[3] = {0, 0, 0};
    uint8_t e;
    for (x = p->t; x < p->t + p->n; x++) {
        e = x->mono >> 4 * j & 15;
        if (x->rad != 1 || x->den != 1 || e > 2 || x->mono != (unsigned)e << 4 * j) return 0;
        c[e] = x->num;
    }
    *d = c[1] * c[1] - 4 * c[2] * c[0];
    return c[2] != 0;
}

/* v >= 0 is a perfect square */
static int sqok(int64_t v)
{
    int64_t r = 0;
    if (v < 0) return 0;
    while (r * r < v) r++;
    return r * r == v;
}

/* a step showing the number v after "Discriminant" */
static void dstep(ps_t *s, int64_t v)
{
    uint8_t o[20], n = 0, i;
    uint64_t u = v < 0 ? -(uint64_t)v : (uint64_t)v;
    char b[20];
    if (v < 0) o[n++] = T_NEG;
    i = 0;
    do { b[i++] = (char)(u % 10); u /= 10; } while (u);
    while (i) o[n++] = (uint8_t)(0x30 + b[--i]);
    stept(s, SR_DISC, 0, 0, o, n);
    if (v < 0) stept(s, SR_NOREAL, 0, 0, 0, 0);
}

#ifdef NOSTEPSX
#define qsteps(...) 0
#define rrsteps(...) 0
#define hsteps(...) ((void)0)
#define sqsteps(...) ((void)0)
#else
/* ---- stepsalg: how a quadratic factors, or is solved by the formula. Every token string is
   built here from whole numbers: what the screen shows is what the pupil would write. ---- */

/* the number v, any size and sign, at out + n */
static uint8_t inum(uint8_t *out, uint8_t n, int64_t v)
{
    uint8_t d[20], k = 0;
    uint64_t u = v < 0 ? -(uint64_t)v : (uint64_t)v;
    if (v < 0) PUT(T_NEG);
    do d[k++] = (uint8_t)(0x30 + u % 10); while (u /= 10);
    while (k) PUT(d[--k]);
    return n;
}

/* the term c V^e of a polynomial in the variable at nibble j: its sign, then |c| unless that is 1 */
static uint8_t ctok(const ps_t *s, uint8_t *out, uint8_t n, int64_t c, uint8_t e, uint8_t j, uint8_t first)
{
    if (!c) return n;
    if (c < 0) { PUT(first ? T_NEG : T_SUB); c = -c; }
    else if (!first) PUT(T_ADD);
    if ((c != 1 || !e) && !(n = inum(out, n, c))) return 0;
    if (e) { PUT(s->var[NV - 1 - j]); n = putpow(out, n, e); }
    return n;
}

/* k[2] V² + k[1] V + k[0] */
static uint8_t qtok(const ps_t *s, uint8_t *out, uint8_t n, const int64_t *k, uint8_t j)
{
    uint8_t e, f = 1;
    for (e = 3; e--; )
        if (k[e]) { if (!(n = ctok(s, out, n, k[e], e, j, f))) return 0; f = 0; }
    if (f) PUT(0x30);
    return n;
}

/* the whole coefficients of a quadratic in V (nibble j), k[0] first; 0 when p is not one */
static int qcoef(const poly_t *p, uint8_t j, int64_t *k)
{
    const term_t *x;
    uint8_t e;
    k[0] = k[1] = k[2] = 0;
    for (x = p->t; x < p->t + p->n; x++) {
        e = x->mono >> 4 * j & 15;
        if (x->rad != 1 || x->den != 1 || e > 2 || x->mono != (unsigned)e << 4 * j) return 0;
        k[e] = x->num;
    }
    return k[2] != 0;
}

/* "(" a V + b ")" at out + n; a is 1 for V alone */
static uint8_t btok(const ps_t *s, uint8_t *out, uint8_t n, int64_t a, int64_t b, uint8_t j)
{
    PUT(T_LPAR);
    if (!(n = ctok(s, out, n, a, 1, j, 1)) || !(n = ctok(s, out, n, b, 0, j, 0))) return 0;
    PUT(T_RPAR);
    return n;
}

/* a log step whose parameter is the numbers v[0..m-1] with commas between and whose expression is e[0..en-1] */
static void nstep(ps_t *s, uint16_t rule, const int64_t *v, uint8_t m, const uint8_t *e, uint8_t en)
{
    uint8_t pt[MAXOUT], pn = 0, i;
    for (i = 0; i < m; i++) {
        if (i) { if (pn >= MAXOUT) goto bad; pt[pn++] = T_COMMA; }
        if (!(pn = inum(pt, pn, v[i]))) goto bad;
    }
    stept(s, rule, pt, pn, e, en);
    return;
bad:
    s->st[2] |= 2;
}

/* the roots of the quadratic as "V=r1 or V=r2" in out, real or (split & 4) complex ones in fl's linear factors,
   the smaller real part first as the answer has them. 0 when fl has no two such factors. */
NOINLINE static uint8_t rtok(ps_t *s, const fl_t *fl, uint8_t j, uint8_t *out)
{
    uint8_t top = s->top, n = 0, k, c = 0;
    rat_t *r[2];
    if (!(r[0] = get(s)) || !(r[1] = get(s))) goto none;
    for (k = 0; k < fl->n && c < 2; k++)
        if (!(fl->e[k] & 0x80) && vdeg(fl->f[k], j) == 1 && root(s, r[c], fl->f[k], j)) c++;
    if (c < 2) goto none;
    k = approx(&r[1]->n, 0) < approx(&r[0]->n, 0) ||        /* a conjugate pair: the smaller imaginary part first, as the answer has it */
        (approx(&r[1]->n, 0) == approx(&r[0]->n, 0) && approx(&r[1]->n, IMAG) < approx(&r[0]->n, IMAG));
    if (k) { rat_t *t = r[0]; r[0] = r[1]; r[1] = t; }
    for (k = 0; k < 2; k++) {
        if (n + 3 > MAXOUT) goto none;
        if (k) out[n++] = T_OR;
        out[n++] = s->var[NV - 1 - j]; out[n++] = T_EQ;
        if (!(n = putroot(s, r[k], out, n))) goto none;
    }
    s->top = top;
    return n;
none:
    s->top = top;
    return 0;
}

/* v, in parentheses when it is negative */
static uint8_t ptok(uint8_t *out, uint8_t n, int64_t v)
{
    if (v < 0) PUT(T_LPAR);
    if (!(n = inum(out, n, v))) return 0;
    if (v < 0) PUT(T_RPAR);
    return n;
}

/* num/den in lowest terms, den > 0 */
static uint8_t ftok(uint8_t *out, uint8_t n, int64_t num, int64_t den)
{
    int64_t g = num < 0 ? -num : num, h = den;
    while (h) { int64_t t = g % h; g = h; h = t; }
    if (g > 1) { num /= g; den /= g; }
    if (!(n = inum(out, n, num))) return 0;
    if (den != 1) { PUT(T_DIV); n = inum(out, n, den); }
    return n;
}

/* √(v), v a whole number */
static uint8_t rtk(uint8_t *out, uint8_t n, int64_t v)
{
    PUT(T_SQRT);
    if (!(n = inum(out, n, v))) return 0;
    PUT(T_RPAR);
    return n;
}

/* The quadratic p in V (nibble j), whole numbers. bit 0 of mode: SOLVE's (else FACTOR's, p without a common
   factor), bit 1: only a part of a bigger equation. 0: it is not one, nothing logged; 1: it factors over the
   integers, shown; 2: everything explained; 3: its roots shown, the factors left to the caller. */
NOINLINE static uint8_t qsteps(ps_t *s, const poly_t *p, const fl_t *fl, uint8_t j, uint8_t mode, uint8_t *out)
{
    int64_t k[3], v[4], a, b, c, d, r, g, a1, p1, g1, g2;
    int32_t sq, m;
    uint8_t n, cx = fl->split & 4, solve = mode & 1, part = mode & 2;
    if (!qcoef(p, j, k)) return 0;
    if (solve && !part) {                       /* a common factor, and a first coefficient > 0: divide both sides */
        g = gcd(gcd(k[0] < 0 ? -k[0] : k[0], k[1] < 0 ? -k[1] : k[1]), k[2] < 0 ? -k[2] : k[2]);
        if (k[2] < 0) g = -g;
        if (g != 1) {
            for (n = 0; n < 3; n++) k[n] /= g;
            if (!(n = qtok(s, out, 0, k, j)) || n + 2 > MAXOUT) goto bad;
            out[n++] = T_EQ; out[n++] = 0x30;
            nstep(s, SR_DIV, &g, 1, out, n);
        }
    }
    a = k[2]; b = k[1]; c = k[0];
    if (!b && !part && solve && c) {            /* aV² = -c: the square root of both sides */
        if (!(n = ctok(s, out, 0, a, 2, j, 1)) || n + 2 > MAXOUT) goto bad;
        out[n++] = T_EQ;
        if (!(n = inum(out, n, -c))) goto bad;
        v[0] = c < 0 ? -c : c;
        nstep(s, c < 0 ? SR_ADD : SR_SUB, v, 1, out, n);
        if (a != 1) {
            if (!(n = ctok(s, out, 0, 1, 2, j, 1)) || n + 2 > MAXOUT) goto bad;
            out[n++] = T_EQ;
            if (!(n = ftok(out, n, -c, a))) goto bad;
            nstep(s, SR_DIV, &a, 1, out, n);
        }
        if ((c > 0) == (a > 0) && !cx) { stept(s, SR_NOREAL, 0, 0, 0, 0); return 2; }
        if (!(n = rtok(s, fl, j, out))) goto bad;
        stept(s, SR_SQRTS, 0, 0, out, n);
        return 2;
    }
    if (!c) return 1;                           /* V(aV+b): the factored line shows it */
    d = b * b - 4 * a * c;
    r = d >= 0 ? isqrt(d) : -1;
    if (r >= 0 && r * r == d) {                 /* two whole numbers that multiply to ac and add to b */
        v[0] = a * c; v[1] = b; v[2] = (b - r) / 2; v[3] = (b + r) / 2;
        nstep(s, SR_PAIR, v, 4, 0, 0);
        if (a == 1) {
            if (solve) return 1;
            if (!(n = btok(s, out, 0, 1, v[2], j)) || !(n = btok(s, out, n, 1, v[3], j))) goto bad;
            stept(s, SR_PFAC, 0, 0, out, n);
            return 1;
        }
        {   /* aV² + pV + qV + c, then a pair at a time: g1 V (a1 V + p1) + g2 (a1 V + p1) */
            uint8_t pt[MAXOUT], pn;
            if (!(pn = ctok(s, pt, 0, v[2], 1, j, 1)) || pn + 2 > MAXOUT) goto bad;
            pt[pn++] = T_COMMA;
            if (!(pn = ctok(s, pt, pn, v[3], 1, j, 1))) goto bad;
            if (!(n = ctok(s, out, 0, a, 2, j, 1)) || !(n = ctok(s, out, n, v[2], 1, j, 0)) ||
                !(n = ctok(s, out, n, v[3], 1, j, 0)) || !(n = ctok(s, out, n, c, 0, j, 0)) || n + 2 > MAXOUT) goto bad;
            if (solve) { out[n++] = T_EQ; out[n++] = 0x30; }
            stept(s, SR_SPLIT, pt, pn, out, n);
        }
        g = a < 0 ? -a : a;
        g1 = gcd((int32_t)g, (int32_t)(v[2] < 0 ? -v[2] : v[2]));
        a1 = a / g1; p1 = v[2] / g1;
        if (v[3] % a1) return 1;
        g2 = v[3] / a1;
        if (g1 != 1 && !(n = inum(out, 0, g1))) goto bad;
        if (g1 == 1) n = 0;
        if (n >= MAXOUT) goto bad;
        out[n++] = s->var[NV - 1 - j];
        if (!(n = btok(s, out, n, a1, p1, j))) goto bad;
        if (g2 < 0) { if (n >= MAXOUT) goto bad; out[n++] = T_SUB; } else { if (n >= MAXOUT) goto bad; out[n++] = T_ADD; }
        if ((g2 > 1 || g2 < -1) && !(n = inum(out, n, g2 < 0 ? -g2 : g2))) goto bad;
        if (!(n = btok(s, out, n, a1, p1, j)) || n + 2 > MAXOUT) goto bad;
        if (solve) { out[n++] = T_EQ; out[n++] = 0x30; }
        stept(s, SR_GROUP, 0, 0, out, n);
        if (!solve) {
            if (!(n = btok(s, out, 0, a1, p1, j)) || !(n = btok(s, out, n, g1, g2, j))) goto bad;
            stept(s, SR_BRACKET, 0, 0, out, n);
        }
        return 1;
    }
    /* no whole-number factors */
    if (d < 0 ? !cx || -d > LIM : d > LIM || !(fl->split & 3)) {
        if (!(n = ptok(out, 0, b))) goto bad;
        out[n++] = T_POW; out[n++] = 0x32; out[n++] = T_SUB; out[n++] = 0x34; out[n++] = T_MUL;
        if (!(n = ptok(out, n, a)) || n + 2 > MAXOUT) goto bad;
        out[n++] = T_MUL;
        if (!(n = ptok(out, n, c)) || n + 2 > MAXOUT) goto bad;
        out[n++] = T_EQ;
        if (!(n = inum(out, n, d))) goto bad;
        stept(s, SR_DNUM, 0, 0, out, n);
        stept(s, d < 0 ? SR_NOREAL : SR_NOFAC, 0, 0, 0, 0);
        return 2;
    }
    v[0] = a; v[1] = b; v[2] = c;
    nstep(s, SR_QFORM, v, 3, 0, 0);
    if (!(n = ptok(out, 0, b))) goto bad;
    out[n++] = T_POW; out[n++] = 0x32; out[n++] = T_SUB; out[n++] = 0x34; out[n++] = T_MUL;
    if (!(n = ptok(out, n, a)) || n + 2 > MAXOUT) goto bad;
    out[n++] = T_MUL;
    if (!(n = ptok(out, n, c)) || n + 2 > MAXOUT) goto bad;
    out[n++] = T_EQ;
    if (!(n = inum(out, n, d))) goto bad;
    stept(s, SR_DNUM, 0, 0, out, n);
    for (n = 0, g = 0; g < 2; g++) {            /* V=(-b±√d)/2a, both signs */
        if (n + 4 > MAXOUT) goto bad;
        if (g) out[n++] = T_OR;
        out[n++] = s->var[NV - 1 - j]; out[n++] = T_EQ; out[n++] = T_LPAR;
        if (!(n = inum(out, n, -b)) || n + 2 > MAXOUT) goto bad;
        out[n++] = g ^ (a > 0) ? T_SUB : T_ADD;      /* the smaller root first, as the answer has them */
        if (!(n = rtk(out, n, d)) || n + 2 > MAXOUT) goto bad;
        out[n++] = T_RPAR; out[n++] = T_DIV;
        if (!(n = inum(out, n, 2 * a))) goto bad;
    }
    stept(s, SR_PUTIN, 0, 0, out, n);
    if (d < 0 || (sqfree((int32_t)d, &sq), sq > 1)) {   /* the root of the discriminant, pulled apart */
        m = sqfree((int32_t)(d < 0 ? -d : d), &sq);
        if (!(n = rtk(out, 0, d)) || n + 2 > MAXOUT) goto bad;
        out[n++] = T_EQ;
        if (sq > 1 && !(n = inum(out, n, sq))) goto bad;
        if (m > 1 || d > 0) { if (!(n = rtk(out, n, m))) goto bad; }
        if (d < 0) { if (n >= MAXOUT) goto bad; out[n++] = T_I; }
        stept(s, d < 0 ? SR_QI : SR_RADS, 0, 0, out, n);
    }
    if (part) return 3;
    if (!solve) return 3;
    if (!(n = rtok(s, fl, j, out))) goto bad;
    stept(s, SR_QRES, 0, 0, out, n);
    return 2;
bad:
    s->st[2] |= 2;
    return 2;
}

/* A polynomial of degree 3 or more, cur (whole numbers, its common factors taken out), that fl factored: the
   rational-root test, then each root found and the factor divided out, until a quadratic is left (qsteps').
   mode as qsteps'. cur is spoilt. */
NOINLINE static uint8_t rrsteps(ps_t *s, fl_t *fl, rat_t *cur, uint8_t j, uint8_t mode, uint8_t *out)
{
    uint8_t top = s->top, k, n, e, m, t = 0, q, ret = 0;
    int64_t v[2];
    rat_t *r = get(s), *w = get(s);
    const term_t *x;
    if (!w) goto done;
    m = vdeg(&cur->n, j);
    for (k = 0; k < fl->n && !t; k++)
        t = !(fl->e[k] & 0x80) && fl->f[k]->n > 1 && vdeg(fl->f[k], j) == 1 && !hasrad(fl->f[k]);
    if (!t || m < 3 || m == 0xFF) goto quad;
    v[0] = v[1] = 0;
    for (x = cur->n.t; x < cur->n.t + cur->n.n; x++) {
        e = x->mono >> 4 * j & 15;
        if (!e) v[0] = x->num < 0 ? -x->num : x->num;
        if (e == m) v[1] = x->num < 0 ? -x->num : x->num;
    }
    if (v[0]) nstep(s, SR_RRT, v, 2, 0, 0);
    for (k = 0; k < fl->n; k++) {
        if (fl->e[k] & 0x80 || fl->f[k]->n < 2 || vdeg(fl->f[k], j) != 1 || hasrad(fl->f[k])) continue;
        for (e = fl->e[k] & 0x7F; e && m >= 3; e--) {
            if (!root(s, r, fl->f[k], j)) goto bad;
            out[0] = s->var[NV - 1 - j]; out[1] = T_EQ;
            if (!(n = answer(s, r, out, 2))) goto bad;
            w->n = *fl->f[k]; pconst(&w->d, 1);
            if (!(q = answer(s, w, out, n))) goto bad;
            stept(s, SR_RR, out, n, out + n, q - n);
            if (divinto(s, &cur->n, fl->f[k]) != 1) goto done;
            m--;
            if (!(n = answer(s, w, out, 0)) || !(q = answer(s, cur, out, n))) goto bad;
            stept(s, SR_RDIVF, out, n, out + n, q - n);
        }
    }
quad:
    if (m == 2) ret = qsteps(s, &cur->n, fl, j, mode | 2, out);
    goto done;
bad:
    s->st[2] |= 2;
done:
    s->top = top;
    return ret;
}

/* SOLVE of a polynomial of degree 3 or more (whole numbers, one variable): both sides divided by the number in
   front, the monomial factor taken out, the roots found one by one (rrsteps). */
NOINLINE static void hsteps(ps_t *s, const rat_t *p, fl_t *fl, uint8_t j, uint8_t *out)
{
    uint8_t top = s->top, n, e = vdeg(&p->n, j);
    int32_t l = 1, g = 0;
    int64_t v;
    unsigned mg = 0;
    const term_t *x;
    rat_t *u = get(s);
    term_t t;
    if (!u || e < 3 || e == 0xFF) goto done;
    u->n = p->n; pconst(&u->d, 1);
    for (x = u->n.t; x < u->n.t + u->n.n; x++) {
        if (x->den != 1 || x->rad != 1 || (x->mono & ~(15u << 4 * j))) goto done;
        mg = x > u->n.t ? mgcd(mg, x->mono) : x->mono;
    }
    if (!lg(&u->n, &l, &g)) goto done;
    if (lead(&u->n)->num < 0) g = -g;
    if (g != 1) {
        t.mono = 0; t.rad = 1;
        if (!rset(&t, 1, g) || !scale(&u->n, &t) || !(n = answer(s, u, out, 0)) || n + 2 > MAXOUT) goto done;
        out[n++] = T_EQ; out[n++] = 0x30;
        v = g;
        nstep(s, SR_DIV, &v, 1, out, n);
    }
    if (mg) {                                   /* X³-X: X(X²-1) */
        divmono(&u->n, mg);
        out[0] = s->var[NV - 1 - j];
        n = putpow(out, 1, mg >> 4 * j & 15);
        if (n && n < MAXOUT) out[n++] = T_LPAR;
        if (!n || !(n = answer(s, u, out, n)) || n >= MAXOUT) goto done;
        out[n++] = T_RPAR;
        stept(s, SR_COMMON, 0, 0, out, n);
    }
    rrsteps(s, fl, u, j, 1, out);
done:
    s->top = top;
}
/* (p)², bracketed unless p is a bare variable or a positive whole number */
static uint8_t sqtok(const ps_t *s, poly_t *p, uint8_t *out, uint8_t n)
{
    const term_t *t = p->t;
    unsigned m = t->mono;
    uint8_t br = !(p->n == 1 && t->den == 1 && ((!m && t->num > 0) || (t->num == 1 && m && !(m & 0xEEEEEEEEu) && !(m & (m - 1)))));
    if (br) PUT(T_LPAR);
    if (!(n = emit(s, p, out, n))) return 0;
    if (br) PUT(T_RPAR);
    return putpow(out, n, 2);
}

/* FACTOR by a pattern: (a+b)², or a²-b² when the two factors are a-b and a+b (a b² added and taken away
   first when a is longer than a term). Called when no other steps came. */
NOINLINE static void sqsteps(ps_t *s, fl_t *fl, uint8_t *out)
{
    uint8_t top = s->top, k, n, m;
    uint16_t rule;
    rat_t *a = get(s), *b = get(s), *q = get(s);
    term_t g, c = fl->c;
    const poly_t *f;
    if (!s->st || !q || fl->n > 2 || !fl->n) goto done;
    for (k = 0; k < fl->n; k++)
        if (fl->e[k] != 3 - fl->n || hasrad(fl->f[k])) goto done;     /* one factor squared, or two to the first */
    a->n.n = b->n.n = 0; pconst(&a->d, 1); pconst(&b->d, 1);
    if (fl->n == 1) {
        f = fl->f[0];
        if (f->n != 2) goto done;
        addterm(&a->n, lead(f));
        addterm(&b->n, f->t + (lead(f) == f->t));
        rule = SR_SQP;
    } else {
        for (k = 0; k < 2; k++)
            for (n = 0; n < fl->f[k]->n; n++) {
                g = fl->f[k]->t[n];
                if (!addterm(&a->n, &g)) goto done;
                if (k) { if (!addterm(&b->n, &g)) goto done; }
                else { g.num = -g.num; if (!addterm(&b->n, &g)) goto done; }
            }
        g.num = 1; g.den = 2; g.mono = 0; g.rad = 1;
        if (!a->n.n || !b->n.n || !scale(&a->n, &g) || !scale(&b->n, &g)) goto done;
        for (n = 0; n < a->n.n; n++) if (a->n.t[n].den != 1) goto done;
        for (n = 0; n < b->n.n; n++) if (b->n.t[n].den != 1) goto done;
        if (lead(&b->n)->num < 0) pneg(&b->n);
        if (a->n.n > 1 && b->n.n > 1) goto done;
        rule = SR_SQD;
    }
    if (rule == SR_SQD && a->n.n > 1) {                    /* add and subtract b² */
        g = *lead(&b->n);
        g.num *= g.num; g.den *= g.den; g.mono += g.mono;
        q->n.n = 0; pconst(&q->d, 1);
        addterm(&q->n, &g);
        if (!(n = emit(s, &q->n, out, 0))) goto bad;
        if (!(m = sqtok(s, &a->n, out, n)) || m >= MAXOUT) goto bad;
        out[m++] = T_SUB;
        if (!(m = sqtok(s, &b->n, out, m))) goto bad;
        stept(s, SR_SQA, out, n, out + n, m - n);
    }
    if (!(n = emit(s, &a->n, out, 0)) || n >= MAXOUT) goto bad;
    out[n++] = T_COMMA;
    if (!(n = emit(s, &b->n, out, n))) goto bad;
    fl->c.num = fl->c.den = 1;
    m = putfl(s, fl, out, n);
    fl->c = c;
    if (!m) goto bad;
    stept(s, rule, out, n, out + n, m - n);
    goto done;
bad:
    s->st[2] |= 2;
done:
    s->top = top;
}
#endif

/* SOLVE of a polynomial of degree 2 or more: everything to the left, the discriminant of a
   quadratic, the factored form, then each linear factor set to 0. Before the roots are put out,
   so out is free for scratch. */
NOINLINE static void psteps(ps_t *s, const rat_t *p, fl_t *fl, uint8_t j, uint8_t rnz, uint8_t *out)
{
    uint8_t top = s->top, k, n, q, e, done;
    rat_t *u = get(s), *v = get(s), *r = get(s);
    term_t t;
    if (!s->st || !r) goto done;
    if (rnz) {
        u->n = p->n; pconst(&u->d, 1);
        v->n.n = 0; pconst(&v->d, 1);
        stepa(s, SR_MOVE, 0, u, v);
    }
    q = qsteps(s, &p->n, fl, j, 1, out);                /* a quadratic: how it factors, or the formula */
    if (q == 2) goto done;
    if (!q) hsteps(s, p, fl, j, out);
    t = fl->c;                                          /* both sides were divided by the number in front */
    fl->c.num = fl->c.den = 1;
    n = putfl(s, fl, out, 0);
    fl->c = t;
    if (!n || n + 2 > MAXOUT) goto bad;
    out[n++] = T_EQ; out[n++] = 0x30;
    stept(s, SR_FACTOR, 0, 0, out, n);
    t.num = t.den = 1; t.mono = 1u << 4 * j; t.rad = 1;
    for (done = 0, e = 0; e < fl->n; e++) {              /* the roots in the order the answer has them: real, then by real part, then imaginary part */
        int64_t bre = 0, bim = 0, re, im;
        uint8_t b = 0xFF;
        for (k = 0; k < fl->n; k++) {
            if (done >> k & 1 || vdeg(fl->f[k], j) != 1 || !root(s, r, fl->f[k], j)) continue;
            im = approx(&r->n, IMAG);
            re = univ(&p->n) == MULTI ? k : approx(&r->n, 0) + (im ? (int64_t)1 << 58 : 0);
            if (b == 0xFF || re < bre || (re == bre && im < bim)) { b = k; bre = re; bim = im; }
        }
        if (b == 0xFF) break;
        done |= 1 << b;
        if (!root(s, r, fl->f[b], j)) continue;
        u->n = *fl->f[b]; pconst(&u->d, 1);
        v->n.n = 0; pconst(&v->d, 1);
        addterm(&v->n, &t);
        stepa(s, SR_ZERO, u, v, r);
    }
    goto done;
bad:
    s->st[2] |= 2;
done:
    s->top = top;
}

/* FACTOR of a polynomial, o its numerator before fac(): the common factor taken out, then how the rest
   factors: a quadratic's pair of numbers or its formula, a longer one's roots found one at a time. */
NOINLINE static void fsteps(ps_t *s, fl_t *fl, const rat_t *o, uint8_t j, uint8_t *out)
{
    uint8_t top = s->top, k, n, e, q;
    unsigned m = 0;
    int32_t l = 1, c = 0;
    const term_t *x;
    rat_t *u = get(s), *v = get(s);
    term_t g;
    if (!s->st || !v) goto done;
    u->n = o->n; pconst(&u->d, 1);
    for (x = u->n.t; x < u->n.t + u->n.n; x++) m = x > u->n.t ? mgcd(m, x->mono) : x->mono;
    divmono(&u->n, m);
    if (!lg(&u->n, &l, &c) || !c) goto done;
    g.mono = 0; g.rad = 1;
    if (lead(&u->n)->num < 0) c = -c;
    if (!rset(&g, l, c) || !scale(&u->n, &g)) goto done;      /* what is left has whole coefficients, the first positive */
    if ((c != 1 || l != 1 || m) && u->n.n > 1) {
        rset(&g, c, 1); g.mono = m;             /* the denominator last: X(X+1)/2, not X/2(X+1) */
        v->n.n = 0; pconst(&v->d, 1);
        addterm(&v->n, &g);
        if (c == 1 && !m) n = 0;
        else if (c == -1 && !m) { out[0] = T_NEG; n = 1; }      /* -(X²-4), not -1(X²-4) */
        else n = answer(s, v, out, 0);
        if ((!n && (c != 1 || m)) || n + 2 > MAXOUT) goto bad;
        out[n++] = T_LPAR;
        if (!(n = answer(s, u, out, n)) || n >= MAXOUT) goto bad;
        out[n++] = T_RPAR;
        if (l != 1 && (n >= MAXOUT || !(out[n++] = T_DIV, n = number(out, n, l)))) goto bad;
        stept(s, SR_COMMON, 0, 0, out, n);
    }
    n = s->st[1];
    q = vdeg(&u->n, j);
    if (q == 2) q = qsteps(s, &u->n, fl, j, 0, out);
    else if (q >= 3 && q != 0xFF) q = rrsteps(s, fl, u, j, 0, out);
    else q = 0;
    if (!q && n == s->st[1]) sqsteps(s, fl, out);
    if (q != 3) goto done;
    for (k = 0; k < fl->n; k++) {                       /* the roots the formula gave, one factor each */
        const poly_t *f = fl->f[k];
        if (fl->e[k] & 0x80 || f->n < 2 || vdeg(f, j) != 1 || !hasrad(f) || !root(s, v, f, j)) continue;
        out[0] = s->var[NV - 1 - j]; out[1] = T_EQ;
        if (!(n = answer(s, v, out, 2))) goto bad;
        u->n = *f; pconst(&u->d, 1);
        if (!(e = answer(s, u, out, n))) goto bad;
        stept(s, SR_RR, out, n, out + n, e - n);
    }
    goto done;
bad:
    s->st[2] |= 2;
done:
    s->top = top;
}

/* FACTOR of a whole number v > 1: one prime division at a time */
static void nsteps(ps_t *s, int32_t v)
{
    uint8_t a[12], b[12], na, nb;
    int32_t p;
    for (p = 2; v > 1; p++) {
        if (p * p > v) p = v;
        for (; v % p == 0; v /= p) {
            na = number(a, 0, p);
            nb = number(b, 0, v / p);
            if (!na || !nb) { if (s->st) s->st[2] |= 2; return; }
            stept(s, SR_PDIV, a, na, b, nb);
        }
    }
}

#ifdef NOSTEPSX     /* engine_device.py's build: the ENGTEST program must fit the RAM, so the spelled-out rules go (steps.py has them) */
#define spr(...) 0
#define stf(...) ((void)0)
#define stn(...) ((void)0)
#define dsteps(s, p, j) ((void)0)
#define dord(...) ((void)0)
#else
/* ---- the calculus steps: a step's texts are written as a format and the values it holds ----
   spr() writes tokens: A..Z and digits and + - * / = ' , ( ) ^ as they are, ~ the negative sign, @ the
   variable of the command; $ the next value, & the same in brackets unless it is one plain factor
   (X^2, sin(X)), # the same in brackets always, ! in brackets only when it is a sum or negative. The tokens go to o (at most MAXOUT); 0 when they do not fit. */
static uint8_t bra(const rat_t *r)
{
    const term_t *t = r->n.t;
    return !(r->n.n == 1 && isconst(&r->d) && t->num == 1 && t->den == 1 && (t->mono || t->rad != 1));
}

NOINLINE static uint8_t spr(ps_t *s, uint8_t *o, uint8_t v, const char *f, rat_t *const **a)
{
    static const char in[] = "+-*/=',()^~", out[] = { T_ADD, T_SUB, T_MUL, T_DIV, T_EQ, T_PRIME, T_LCOMMA, T_LPAR, T_RPAR, T_POW, T_NEG };
    uint8_t n = 0, m, k;
    rat_t *r;
    char c;
    for (; (c = *f) != 0; f++) {
        if (c == '$' || c == '&' || c == '#' || c == '!') {
            r = *(*a)++;
            if (c == '#' || (c == '&' && bra(r)) || (c == '!' && (r->n.n > 1 || (r->n.n == 1 && r->n.t[0].num < 0)))) { if (n >= MAXOUT) return 0; o[n++] = T_LPAR; c = '#'; }
            if (!(m = answer(s, r, o, n))) return 0;
            n = m;
            if (c == '#') { if (n >= MAXOUT) return 0; o[n++] = T_RPAR; }
            continue;
        }
        if (n >= MAXOUT) return 0;
        if (c == '@') { o[n++] = v; continue; }
        for (k = 0; in[k] && in[k] != c; k++) ;
        o[n++] = in[k] ? (uint8_t)out[k] : (uint8_t)c;
    }
    return n;
}

/* A step: the parameter written by pf and the expression by ef (0: none), out of the values a in order.
   With fin, the step after it (rule2) shows fin as the result, when ef is not it already (or did not fit). */
NOINLINE static void stfx(ps_t *s, uint8_t v, uint16_t rule, const char *pf, const char *ef, rat_t *const *a,
                          rat_t *fin, uint16_t rule2)
{
    uint8_t top = s->top, n = 0, m = 0, l = 0, i, *o;
    rat_t *w = get(s), *q = get(s);
    if (!s->st || s->st[2] || !q) goto done;
    o = (uint8_t *)w;
    if (pf && !(n = spr(s, o, v, pf, &a))) goto done;
    if (ef && !(m = spr(s, o + MAXOUT, v, ef, &a))) { if (!fin) goto done; ef = 0; }
    if (rule != 0xFFFF && (!ef || m || !fin)) stept(s, rule, o, n, o + MAXOUT, m);
    if (fin && (l = result(s, fin, o + 2 * MAXOUT, q, 0))) {
        for (i = 0; i < l && m == l && o[MAXOUT + i] == o[2 * MAXOUT + i]; i++) ;
        if (!ef || i != l || m != l) stept(s, rule2, 0, 0, o + 2 * MAXOUT, l);
    }
done:
    s->top = top;
}
/* the usual step: no finishing rule */
NOINLINE static void stf2(ps_t *s, uint8_t v, uint16_t rule, const char *pf, const char *ef, rat_t *const *a)
{
    stfx(s, v, rule, pf, ef, a, 0, 0);
}
NOINLINE static void stn(ps_t *s, uint8_t v, uint16_t rule) { stfx(s, v, rule, 0, 0, 0, 0, 0); }

/* the factors of x that hold V (nibble j), in th: V's power, √V, each kernel of V (its power in mono,
   its root in rad); co = x less them; their number */
#define NTH 8
typedef struct { unsigned m; uint32_t r; } th_t;

NOINLINE static uint8_t csplit(const ps_t *s, const term_t *x, uint8_t j, th_t *th, term_t *co)
{
    uint8_t n = 0, k;
    uint32_t rb = (uint32_t)1 << (RVAR + j), ar = 0;
    unsigned m, am = 0;
    if (x->mono >> 4 * j & 15) { th[n].m = 15u << 4 * j; th[n++].r = 0; }
    if (x->rad & rb) { th[n].m = 0; th[n++].r = rb; }
    for (k = 0; k < s->nk; k++) {
        m = 15u << 4 * KN(s, k);
        rb = (uint32_t)1 << (RVAR + KN(s, k));
        if (((x->mono & m) || (x->rad & rb)) && kdep(s, k, j)) { th[n].m = x->mono & m; th[n++].r = x->rad & rb; }
    }
    for (k = 0; k < n; k++) { am |= th[k].m; ar |= th[k].r; }
    *co = *x;
    co->mono &= ~am;
    co->rad &= ~ar;
    return n;
}

/* r = x less the factor h (h: r = just h, coefficient 1) */
static void thr(rat_t *r, const term_t *x, const th_t *h, uint8_t only)
{
    term_t t = *x;
    if (only) { t.num = t.den = 1; t.mono &= h->m; t.rad = (t.rad & h->r) | 1; }
    else { t.mono &= ~h->m; t.rad &= ~h->r; }
    r->n.n = 0;
    addterm(&r->n, &t);
    pconst(&r->d, 1);
}

static int dr(ps_t *s, rat_t *d, rat_t *e, uint8_t j)
{
    return drat(s, d, e, j) && reduce(s, d) == 1;
}

/* out = the derivative of kernel k by its own argument: cos(u) for sin(u); 0 when not so known */
NOINLINE static int outer(ps_t *s, rat_t *o, uint8_t k)
{
    uint8_t f = s->kf[k], g;
    if (f == K_SIN || f == K_COS || f == K_EXP) {
        g = f == K_EXP ? KN(s, k) : kern(s, K_SIN ^ K_COS ^ f, s->ka[k], 0);
        if (g == 0xFF) return 0;
        rconst(o, f == K_COS ? -1 : 1);
        o->n.t[0].mono = 1u << 4 * g;
        return 1;
    }
    if (f == K_TAN) {
        rconst(o, 1);
        o->n.t[1] = o->n.t[0];
        o->n.t[1].mono = 2u << 4 * KN(s, k);
        o->n.n = 2;
        return 1;
    }
    if ((f == K_LOG && !s->kb[k]) || f == K_LABS) {
        rconst(o, 1);
        return rmulinto(s, o, s->ka[k], 1);
    }
    return f >= K_ASIN && f <= K_ATAN && oinv(s, o, k);
}

/* the kernel of th h in a term, or 0xFF */
static uint8_t thk(const ps_t *s, const th_t *h)
{
    uint8_t k;
    for (k = 0; k < s->nk; k++)
        if ((h->m & 15u << 4 * KN(s, k)) || (h->r & (uint32_t)1 << (RVAR + KN(s, k)))) return k;
    return 0xFF;
}

/* the rule for the derivative of e, one factor or a sum, shown as a single step */
NOINLINE static uint16_t krule(ps_t *s, rat_t *e, uint8_t j)
{
    th_t th[NTH];
    term_t co;
    uint8_t k, f;
    if (!dep(s, e, j)) return SR_KCONST;
    if (e->n.n != 1 || !isconst(&e->d) || csplit(s, e->n.t, j, th, &co) != 1) return SR_KANY;
    if (th->m == 15u << 4 * j) {
        f = e->n.t[0].mono >> 4 * j & 15;
        if (f > 1) return SR_KPOW;
        return co.num == 1 && co.den == 1 && !co.mono && co.rad == 1 ? SR_KLIN : SR_KCLIN;
    }
    if (!th->m) return th->r == (uint32_t)1 << (RVAR + j) ? SR_KSQRT : SR_KCHAIN1;
    if ((k = thk(s, th)) == 0xFF || th->r || (th->m >> 4 * KN(s, k) & 15) != 1) return SR_KCHAIN1;
    f = s->kf[k];
    if (f == K_POW) return !dep(s, s->kb[k], j) ? SR_KPOWC : !dep(s, s->ka[k], j) ? SR_KPOWB : SR_KPOWL;
    if (!isvar(s->ka[k], j)) return SR_KCHAIN1;
    return f == K_SIN ? SR_KSIN : f == K_COS ? SR_KCOS : f == K_TAN ? SR_KTAN : f == K_EXP ? SR_KEXP :
           f == K_ASIN ? SR_KASIN : f == K_ACOS ? SR_KACOS : f == K_ATAN ? SR_KATAN :
           (f == K_LOG && !s->kb[k]) || f == K_LABS ? SR_KLN : SR_KCHAIN1;
}

/* d = e', as one step with the rule that fits it; 0 when the derivative is not worked out */
NOINLINE static uint8_t kone(ps_t *s, rat_t *e, rat_t *d, uint8_t j, uint8_t v)
{
    if (!dr(s, d, e, j)) return 0;
    stf(s, v, krule(s, e, j), "$", "$", e, d);
    return 1;
}

/* the chain rule on a term's one function (f: a kernel k to a power) in four steps; d = f'. oo, ii, u scratch.
   1 when shown; 0 when f is a function of V itself or not so known (kone() shows it in one step). */
NOINLINE static uint8_t kchain(ps_t *s, rat_t *f, const th_t *h, uint8_t k, uint8_t j, uint8_t v, rat_t *d, rat_t *oo, rat_t *ii,
                               rat_t *u)
{
    uint8_t e;
    if (k == 0xFF || h->r) return 0;
    e = f->n.t[0].mono >> 4 * KN(s, k) & 15;
    if (e == 1) {
        if (isvar(s->ka[k], j) || !outer(s, oo, k) || !dr(s, ii, s->ka[k], j)) return 0;
        stf(s, v, SR_KCHAIN, "$,$", 0, f, s->ka[k]);
        stf(s, v, SR_KOUT, 0, "$", oo);
        stf(s, v, SR_KIN, "$", "$", s->ka[k], ii);
    } else {
        rconst(u, 1);
        u->n.t[0].mono = 1u << 4 * KN(s, k);
        rconst(oo, e);
        oo->n.t[0].mono = (unsigned)(e - 1) << 4 * KN(s, k);
        if (!dkern(s, ii, k, j) || !ii->n.n) return 0;
        stf(s, v, SR_KCHAIN, "$,$", 0, f, u);
        stf(s, v, SR_KOUT, 0, "$", oo);
        stf(s, v, SR_KIN, "$", "$", u, ii);
    }
    if (!dr(s, d, f, j)) return 0;
    stf(s, v, SR_KCMUL, "$,$", "$", oo, ii, d);
    return 1;
}

/* a factor written beside another: in brackets unless it is a plain positive whole-coefficient term (2X, sin(X)) */
static char kbr(const rat_t *r)
{
    return r->n.n == 1 && isconst(&r->d) && r->n.t[0].num > 0 && r->n.t[0].den == 1 ? '$' : '#';
}

/* the product rule's "a g + f b", a = f', b = g': a factor 1 is left out */
NOINLINE static void kput(ps_t *s, uint8_t v, rat_t *a, rat_t *g, rat_t *f, rat_t *b, rat_t *d)
{
    char e[8];
    rat_t *w[4];
    uint8_t n = 0, m = 0;
    if (!isnum(a, 1)) { e[m++] = kbr(a); w[n++] = a; e[m++] = kbr(g); } else e[m++] = '$';
    w[n++] = g;
    e[m++] = '+';
    if (!isnum(b, 1)) { e[m++] = kbr(f); w[n++] = f; e[m++] = kbr(b); w[n++] = b; } else { e[m++] = '$'; w[n++] = f; }
    e[m] = 0;
    stfx(s, v, SR_KPUT, 0, e, w, d, SR_KSIM);
}

/* the quotient rule's "(G F' - F G') / G^2", z = top, bottom, top', bottom', result: a factor 1 is left out, a term 0 too */
NOINLINE static void kqput(ps_t *s, uint8_t v, rat_t *const *z)
{
    char e[16];
    rat_t *w[5];
    uint8_t n = 0, m = 0, p1 = z[2]->n.n != 0, p2 = z[3]->n.n != 0;
    const term_t *t = z[1]->n.t;
    if (p1) e[m++] = '(';
    if (p1) {
        w[n++] = z[1];
        e[m++] = !p2 || isnum(z[2], 1) ? '$' : kbr(z[1]);
        if (!isnum(z[2], 1)) { w[n++] = z[2]; e[m++] = kbr(z[2]); }
        if (isnum(z[2], 1) && p2) e[m - 1] = kbr(z[1]);
    }
    if (p2) {
        e[m++] = p1 ? '-' : '~';
        w[n++] = z[0];
        e[m++] = isnum(z[3], 1) ? (p1 ? kbr(z[0]) : '$') : kbr(z[0]);
        if (!isnum(z[3], 1)) { w[n++] = z[3]; e[m++] = kbr(z[3]); }
    }
    if (p1) e[m++] = ')';
    e[m++] = '/';
    e[m++] = z[1]->n.n == 1 && isconst(&z[1]->d) && t->num == 1 && t->den == 1 && t->rad == 1 &&
             t->mono && !(t->mono & (t->mono - 1)) && (t->mono & 0x11111111u) ? '$' : '#';   /* X, not X^2 */
    w[n++] = z[1];
    e[m++] = '\015';
    e[m] = 0;
    stfx(s, v, SR_KQPUT, 0, e, w, z[4], SR_KSIM);
}

/* term i of p (V at nibble j) and its derivative z[5], z the scratch u f g a b d: the steps of the rules it needs */
NOINLINE static uint8_t dterm(ps_t *s, rat_t *p, uint8_t i, uint8_t j, uint8_t v, rat_t **z)
{
    uint8_t c, k;
    th_t th[NTH];
    term_t co;
    rat_t *u = z[0], *f = z[1], *g = z[2], *a = z[3], *b = z[4], *d = z[5];
    const term_t *x = &p->n.t[i];
    c = csplit(s, x, j, th, &co);
    u->n.n = 0;
    addterm(&u->n, x);
    u->d = p->d;
    if (reduce(s, u) != 1 || !dr(s, d, u, j)) return 0;
    if (c == 1) {
        thr(f, x, th, 1);
        g->n.n = 0;
        addterm(&g->n, &co);
        g->d = p->d;
        if (!isnum(g, 1) && th[0].m == 15u << 4 * j && (x->mono >> 4 * j & 15) == 1) {      /* cX: the slope */
            stf(s, v, SR_KCLIN, "$", "$", u, d);
            return 1;
        }
        if (!kchain(s, f, th, thk(s, th), j, v, a, b, z[0], a) && !kone(s, f, a, j, v)) return 0;
        if (!isnum(g, 1)) stf(s, v, SR_KCM, "$,$", "$", g, a, d);
        return 1;
    }
    thr(f, x, th + c - 1, 0);
    thr(g, x, th + c - 1, 1);
    stf(s, v, SR_KPROD, "$,$", 0, f, g);
    if (!kone(s, f, a, j, v) || !kone(s, g, b, j, v)) return 0;
    kput(s, v, a, g, f, b, d);
    return 1;
}

/* DERIV(P, V): a sum is taken a term at a time, then added; a term is a number times one thing (the power
   rule, a known function, the chain rule) or a product (the product rule), each said in words with its
   result; a term without V is the constant rule. A quotient: the quotient rule. The kernels the steps make
   are let go again, so the real run starts from the same state. */
NOINLINE static void dsteps(ps_t *s, rat_t *p, uint8_t j)
{
    uint8_t top = s->top, nk = s->nk, lim = s->lim, v = s->var[NV - 1 - j], i, nt = 0, kc = 0, cs;
    rat_t *z[6];
    th_t th[NTH];
    term_t co;
    for (i = 0; i < 6; i++)
        if (!(z[i] = get(s))) goto done;
    if (!s->st) goto done;
    if (!dep(s, p, j)) {
        rconst(z[3], 0);
        stf(s, v, SR_KCONST, "$", "$", p, z[3]);
        goto done;
    }
    if (!isconst(&p->d)) {
        z[0]->n = p->n; pconst(&z[0]->d, 1);
        z[1]->n = p->d; pconst(&z[1]->d, 1);
        stf(s, v, SR_KQUOT, "$,$", 0, z[0], z[1]);
        if (!kone(s, z[0], z[2], j, v) || !kone(s, z[1], z[3], j, v) || !dr(s, z[4], p, j)) goto bad;
        kqput(s, v, z);
        goto done;
    }
    for (i = 0; i < p->n.n; i++)
        if (csplit(s, &p->n.t[i], j, th, &co)) nt++;
        else kc++;
    if (nt + (kc != 0) > 1) stn(s, v, SR_KSPLIT);
    for (i = 0; i < p->n.n; i++)
        if (csplit(s, &p->n.t[i], j, th, &co) && !dterm(s, p, i, j, v, z)) goto bad;
    if (kc) {
        z[1]->n.n = 0;
        for (i = 0; i < p->n.n; i++)
            if (!csplit(s, &p->n.t[i], j, th, &co)) addterm(&z[1]->n, &p->n.t[i]);
        z[1]->d = p->d;
        rconst(z[3], 0);
        stf(s, v, SR_KCONST, "$", "$", z[1], z[3]);
    }
    if (nt + (kc != 0) > 1) {
        if (!dr(s, z[0], p, j)) goto bad;
        stfx(s, v, 0xFFFF, 0, 0, 0, z[0], SR_KADD);
    }
    goto done;
bad:
    s->st[2] |= 2;
done:
    s->top = top; s->nk = nk; s->lim = lim;
}

/* DERIV of order o: before the k-th derivative's steps, which one it is */
NOINLINE static void dord(ps_t *s, rat_t *p, uint8_t j, uint8_t k, uint8_t o)
{
    uint8_t top = s->top;
    rat_t *a = get(s), *b = get(s);
    if (!b) goto done;
    if (o > 1) {
        rconst(a, k); rconst(b, o);
        stf(s, s->var[NV - 1 - j], SR_KORD, "$,$", 0, a, b);
    }
    s->top = top;                               /* a, b are done: dsteps() wants the slots */
    dsteps(s, p, j);
done:
    s->top = top;
}

#endif

#ifdef NOSTEPSX
#define ilim2(...) ((void)0)
#define ic(...) ((void)0)
#define isteps(...) ((void)0)
#define rpsteps(...) 0
#define trsteps(...) 0
#else
/* the tokens t[n] with the variable v replaced by the value val[m]: in brackets unless it is one token after an operator */
NOINLINE static uint8_t subtok(const uint8_t *t, uint8_t n, uint8_t v, const uint8_t *val, uint8_t m, uint8_t *o)
{
    uint8_t i, k, w = 0, c, p = 0, br;
    for (i = 0; i < n; i++) {
        c = t[i];
        if (c != v) {
            if (w >= 90) return 0;
            o[w++] = c;
            if (two(c) && i + 1 < n) o[w++] = t[++i];
            p = c;
            continue;
        }
        br = !(m == 1 && (!p || p == T_LPAR || p == T_SQRT || p == T_ABS || (p >= T_LN && p <= T_ATAN) || p == T_ADD || p == T_SUB || p == T_DIV || p == T_EQ || p == T_MUL || p == T_POW ||
                           p == T_NEG || p == T_LCOMMA));
        if (w + m + 2 >= 90) return 0;
        if (br) o[w++] = T_LPAR;
        for (k = 0; k < m; k++) o[w++] = val[k];
        if (br) o[w++] = T_RPAR;
        p = br ? T_RPAR : val[0];
    }
    return w;
}

/* "F(lim) = F with lim put in = the value": a step of rule; o a scratch of 3 * MAXOUT */
NOINLINE static void fput(ps_t *s, uint16_t rule, const uint8_t *ft, uint8_t fn, uint8_t v, rat_t *lm, rat_t *res, uint8_t *o)
{
    uint8_t m, l, r;
    if (!(m = answer(s, lm, o, 0)) || !(l = subtok(ft, fn, v, o, m, o + MAXOUT)) || l > MAXOUT) return;
    o[MAXOUT + l] = T_EQ;
    if (!(r = answer(s, res, o + MAXOUT + l + 1, 0))) return;
    stept(s, rule, o, m, o + MAXOUT, l + 1 + r);
}

/* a definite integral's last moves, the antiderivative f from the bottom limit a to the top b: each limit put in
   (the numbers shown), then the difference */
NOINLINE static void ilim2(ps_t *s, rat_t *f, uint8_t j, rat_t *a, rat_t *b)
{
    uint8_t top = s->top, nk = s->nk, lim = s->lim, v = s->var[NV - 1 - j], n, *o;
    rat_t *x = get(s), *t = get(s), *w = get(s);
    if (!s->st || !w) goto done;
    o = (uint8_t *)w;
    if (!(n = answer(s, f, o + 2 * MAXOUT, 0))) goto done;
    *x = *f;
    if (evalat(s, x, j, b) != 1 || reduce(s, x) != 1) goto done;
    fput(s, SR_IFB, o + 2 * MAXOUT, n, v, b, x, o);
    *t = *f;
    if (evalat(s, t, j, a) != 1 || reduce(s, t) != 1) goto done;
    fput(s, SR_IFA, o + 2 * MAXOUT, n, v, a, t, o);
    *w = *x;
    pneg(&t->n);
    if (!raddinto(s, w, t) || reduce(s, w) != 1) goto done;
    pneg(&t->n);
    stf(s, v, SR_IFD, "$,$", "$-!=$", a, b, x, t, w);
done:
    s->top = top; s->nk = nk; s->lim = lim;
}

/* ---- LIMIT's steps ---- */
/* c(V-a)^e as tokens (w = V - a, ev = e) in o; 0 when it does not fit */
NOINLINE static uint8_t lterm(ps_t *s, uint8_t *o, uint8_t j, rat_t *c, rat_t *w, rat_t *ev, rat_t *a, uint8_t e)
{
    rat_t *ar[3], *const *ap = ar;
    char f[8], *q = f;
    uint8_t k = 0, v = s->var[NV - 1 - j];
    const term_t *t = c->n.t;
    if (!a->n.n) {                              /* c V^e as the engine writes it: X², 3X, X/2 */
        *ev = *c;
        return vpow(ev, j, e) && reduce(s, ev) == 1 ? answer(s, ev, o, 0) : 0;
    }
    if (e == 0 || c->n.n != 1 || t->num != 1 || t->den != 1 || t->mono || t->rad != 1 || !isconst(&c->d)) {
        if (e && c->n.n == 1 && t->num == -1 && t->den == 1 && !t->mono && t->rad == 1) *q++ = '~';
        else { ar[k++] = c; *q++ = c->n.n > 1 || (c->n.n == 1 && t->den != 1) ? '#' : '$'; }
    }
    if (e) { ar[k++] = w; *q++ = '&'; }
    if (e > 1) { rconst(ev, e); ar[k++] = ev; *q++ = '^'; *q++ = '$'; }
    *q = 0;
    return spr(s, o, v, f, &ap);
}

/* the entry in[st..en) read as N/D: N anything without a + or - outside brackets, D a bracketed group or one
   letter or number (X, X², X^2, 12); the index of the bar, else 0 */
NOINLINE static unsigned lbar(const uint8_t *in, unsigned st, unsigned en)
{
    unsigned k, at = 0;
    int d = 0;
    uint8_t c;
    for (k = st; k < en; k++) {
        c = in[k];
        if (c == T_RPAR) d--;
        else if (!d && (c == T_ADD || c == T_SUB)) return 0;
        else if (!d && c == T_DIV) { if (at) return 0; at = k; }
        else if (c == T_LPAR || c == T_SQRT || c == T_ABS || (c >= T_LN && c <= T_ATAN) ||
                 (c == T_EXT && k + 1 < en && in[k + 1] == 0x34)) d++;
        k += two(c);
    }
    if (!at || at == st || at + 1 >= en) return 0;
    k = at + 1;
    c = in[k];
    if (c == T_LPAR || c == T_SQRT || c == T_ABS || (c >= T_LN && c <= T_ATAN)) {
        for (d = 0; k < en; k++) {
            if (in[k] == T_LPAR || in[k] == T_SQRT || in[k] == T_ABS || (in[k] >= T_LN && in[k] <= T_ATAN)) d++;
            else if (in[k] == T_RPAR && !--d) break;
            k += two(in[k]);
        }
        return k == en - 1 ? at : 0;
    }
    if (c >= 0x30 && c <= 0x39) { while (k < en && in[k] >= 0x30 && in[k] <= 0x39) k++; return k == en ? at : 0; }
    if (c < T_VAR0 || c > 0x5A) return 0;
    k++;
    if (k < en && in[k] == T_SQR) k++;
    else if (k < en && in[k] == T_POW) for (k++; k < en && in[k] >= 0x30 && in[k] <= 0x39;) k++;
    return k == en ? at : 0;
}

/* r, r + 1 = the top and the bottom of the entry in[st..en) as typed, before they cancel; 0 when it is not N/D */
NOINLINE static uint8_t lraw(ps_t *s, unsigned st, unsigned en, rat_t *r)
{
    unsigned at = lbar(s->in, st, en), i = s->i, len = s->len, ok;
    if (!at) return 0;
    s->i = st; s->len = at;
    ok = expr(s, r) && s->i == at;
    s->i = at + 1; s->len = en;
    ok = ok && expr(s, r + 1) && s->i == en;
    s->i = i; s->len = len;
    return ok && isconst(&r->d) && isconst(&r[1].d);
}

/* LIMIT at a point: plugging in. Top and bottom both 0 (or the bottom alone) is said first; then, when
   the fraction reduced and direct is set, the shared factor is cancelled and the number put in */
NOINLINE static void lpoint(ps_t *s, const rat_t *raw, rat_t *p, uint8_t j, rat_t *a, uint8_t direct)
{
    uint8_t top = s->top, nk = s->nk, lim = s->lim, v = s->var[NV - 1 - j], n, *o;
    rat_t *x = get(s), *y = get(s), *g = get(s), *t = get(s), *w = get(s);
    int zero = 0;
    if (!s->st || !w) goto done;
    if (raw) {
        x->n = raw[0].n; pconst(&x->d, 1);
        y->n = raw[1].n; pconst(&y->d, 1);
        if (evalat(s, x, j, a) == 1 && evalat(s, y, j, a) == 1 && !y->n.n && reduce(s, x) == 1) {
            zero = 1;
            if (x->n.n) stf(s, v, SR_LZN0, "@=$", "$/0", a, x);
            else stf(s, v, SR_LZ00, "@=$", "0/0", a);
        }
    }
    if (!direct) goto done;
    o = (uint8_t *)w;
    *t = *p;
    if (evalat(s, t, j, a) != 1 || reduce(s, t) != 1 || !(n = answer(s, p, o + 2 * MAXOUT, 0))) goto done;
    if (zero && !x->n.n) {
        g->n = raw[1].n; pconst(&g->d, 1);
        if (divinto(s, &g->n, &p->d) == 1 && reduce(s, g) == 1) stf(s, v, SR_LCANC, "#", "$", g, p);
        else stn(s, v, SR_LCANC2);
        if (!(n = answer(s, p, o + 2 * MAXOUT, 0))) goto done;
    }
    fput(s, SR_LDIR, o + 2 * MAXOUT, n, v, a, t, o);
done:
    s->top = top; s->nk = nk; s->lim = lim;
}

/* LIMIT / DOMINANTTERM through the leading terms c(V-a)^vn over d(V-a)^vd: what each acts like near a, then the verdict */
NOINLINE static void lser(ps_t *s, rat_t *p, rat_t *c, rat_t *d, uint8_t j, rat_t *a, uint8_t vn, uint8_t vd, uint8_t lim)
{
    uint8_t top = s->top, nk = s->nk, lm = s->lim, v = s->var[NV - 1 - j], n, m, *o, vt = vn;
    rat_t *x = get(s), *w, *y, *e, *b;
    rat_t *const *ap;
    rat_t *ar[1];
    if (!s->st || !x) goto done;
    *x = *c;
    if (!c->n.n && (!lim || tfirst(s, x, &p->n, j, a, 7, &vt) != 1 || vt > 7)) goto done;     /* a top that vanishes: its real first term */
    w = get(s); y = get(s); e = get(s); b = get(s);
    if (!b) goto done;
    o = (uint8_t *)b;
    if (!vplus(w, a, j, 0)) goto done;
    ar[0] = a; ap = ar;
    if (!(n = spr(s, o, v, "@=$,", &ap))) goto done;
    if (!(m = lterm(s, o + n, j, x, w, e, a, vt))) goto done;
    n += m;
    o[n++] = T_LCOMMA;
    if (!(m = lterm(s, o + n, j, d, w, e, a, vd)) || n + m > MAXOUT) goto done;
    n += m;
    stept(s, SR_LSER, o, n, 0, 0);
    *x = *c; *y = *d;
    if (!lim) {                                 /* (c/d)(V-a)^(vn-vd) */
        if (!rmulinto(s, x, y, 1) || !powinto(s, w, (int32_t)vn - vd) || !rmulinto(s, x, w, 0) || reduce(s, x) != 1) goto done;
        ar[0] = x; ap = ar;
        stf(s, v, SR_LDT, 0, "$", x);
    } else if (vn > vd) stf(s, v, SR_LS0, 0, "0");
    else if (vn == vd) {
        if (!rmulinto(s, x, y, 1) || reduce(s, x) != 1) goto done;
        stf(s, v, SR_LSE, 0, "$", x);
    } else stn(s, v, SR_LSI);
done:
    s->top = top; s->nk = nk; s->lim = lm;
}

/* LIMIT / DOMINANTTERM at infinity: the highest power of V above and below, then what they say (c: their ratio, e: the degrees' gap) */
NOINLINE static void linf(ps_t *s, const rat_t *p, uint8_t j, int8_t e, rat_t *c, uint8_t lim)
{
    uint8_t top = s->top, nk = s->nk, lm = s->lim, v = s->var[NV - 1 - j];
    uint8_t dn = vdeg(&p->n, j), dd = vdeg(&p->d, j);
    rat_t *x = get(s), *y = get(s), *t1 = get(s), *t2 = get(s);
    if (!s->st || !t2 || dn == 0xFF || dd == 0xFF) goto done;
    slice(&x->n, &p->n, 4 * j, dn); pconst(&x->d, 1);
    slice(&y->n, &p->d, 4 * j, dd); pconst(&y->d, 1);
    *t1 = *x; *t2 = *y;
    if (!vpow(t1, j, dn) || !vpow(t2, j, dd) || reduce(s, t1) != 1 || reduce(s, t2) != 1) goto done;
    stf(s, v, SR_LI1, "$,$", 0, t1, t2);
    if (!lim) {
        *t1 = *c;
        if (vpow(t1, j, e) && reduce(s, t1) == 1) stf(s, v, SR_LDT, 0, "$", t1);
    } else if (e < 0) stf(s, v, SR_LI0, 0, "0");
    else if (e == 0) {
        if (y->n.n == 1 && y->n.t[0].num == 1 && y->n.t[0].den == 1 && !y->n.t[0].mono && y->n.t[0].rad == 1) stf(s, v, SR_LIE, 0, "$", c);
        else stf(s, v, SR_LIE, 0, "$/!=$", x, y, c);
    } else stn(s, v, SR_LIP);
done:
    s->top = top; s->nk = nk; s->lim = lm;
}

/* ---- the rest of the Calculus menu's steps ---- */
/* a step showing r, reduced on a copy (ef: how, "$" alone mostly) */
NOINLINE static void stc(ps_t *s, uint8_t v, uint16_t rule, const char *ef, const rat_t *r)
{
    if (!s->st) return;
    uint8_t top = s->top;
    rat_t *x = get(s);
    if (s->st && x) {
        *x = *r;
        if (reduce(s, x) == 1) stf(s, v, rule, 0, ef, x);
    }
    s->top = top;
}

/* SERIES: the series r (times V^vd for the bottom) as the step rule */
NOINLINE static void sesh(ps_t *s, uint8_t j, uint16_t rule, const rat_t *r, uint8_t vd)
{
    if (!s->st) return;
    uint8_t top = s->top, nk = s->nk, lim = s->lim;
    rat_t *x = get(s);
    if (s->st && x) {
        *x = *r;
        if ((!vd || vpow(x, j, vd)) && reduce(s, x) == 1) stf(s, s->var[NV - 1 - j], rule, 0, "$", x);
    }
    s->top = top; s->nk = nk; s->lim = lim;
}

/* TANGENTLINE, NORMALLINE: the slope function, the slope and the height at the point, the line, simplified */
NOINLINE static void tlsteps(ps_t *s, rat_t *p, uint8_t j, rat_t *c, uint8_t cmd)
{
    uint8_t top = s->top, nk = s->nk, lim = s->lim, v = s->var[NV - 1 - j], n, *o;
    rat_t *d = get(s), *fa = get(s), *m = get(s), *w = get(s), *l = get(s), *ns = get(s), *b = get(s);
    rat_t *ar[3];
    if (!s->st || !b) goto done;
    o = (uint8_t *)b;
    if (!dr(s, d, p, j)) goto done;
    stf(s, v, SR_TLD, 0, "$", d);
    *m = *d;
    if (evalat(s, m, j, c) != 1 || reduce(s, m) != 1 || !(n = answer(s, d, o + 2 * MAXOUT, 0))) goto done;
    fput(s, SR_TLM, o + 2 * MAXOUT, n, v, c, m, o);
    *fa = *p;
    if (evalat(s, fa, j, c) != 1 || reduce(s, fa) != 1 || !(n = answer(s, p, o + 2 * MAXOUT, 0))) goto done;
    fput(s, SR_TLY, o + 2 * MAXOUT, n, v, c, fa, o);
    if (!vplus(w, c, j, 0)) goto done;
    *ns = *m;
    if (cmd == C_NORMALLINE) {
        if (!m->n.n) goto done;
        rconst(ns, -1);
        if (!rmulinto(s, ns, m, 1) || reduce(s, ns) != 1) goto done;
        stf(s, v, SR_TLN, 0, "$", ns);
    }
    *l = *ns;
    if (!rmulinto(s, l, w, 0) || !raddinto(s, l, fa)) goto done;
    ar[0] = fa; ar[1] = ns; ar[2] = w;
    {
        char e[8] = "$+";
        rat_t *mm = ns;
        if (ns->n.n == 1 && isconst(&ns->d) && ns->n.t[0].num < 0) { *b = *ns; pneg(&b->n); mm = b; e[1] = '-'; }
        if (isnum(mm, 1)) { ar[1] = w; e[2] = '&'; e[3] = 0; }
        else { ar[1] = mm; ar[2] = w; e[2] = kbr(mm); e[3] = '&'; e[4] = 0; }
        stfx(s, v, SR_TLL, 0, e, ar, l, SR_TLS);
    }
done:
    s->top = top; s->nk = nk; s->lim = lim;
}

/* TAYLOR: the term of order k, x = the k-th derivative at the point, d = (V-a)^k/k! */
NOINLINE static void tyk(ps_t *s, uint8_t j, rat_t *x, rat_t *d, rat_t *c, uint8_t k)
{
    if (!s->st) return;
    uint8_t top = s->top, nk = s->nk, lim = s->lim;
    rat_t *t = get(s), *y = get(s), *kk = get(s);
    if (!s->st || !kk) goto done;
    *y = *x; *t = *x;
    if (reduce(s, y) != 1 || !rmulinto(s, t, d, 0) || reduce(s, t) != 1) goto done;
    rconst(kk, k);
    stf(s, s->var[NV - 1 - j], SR_TYK, "$,$,$", "$", kk, c, y, t);
done:
    s->top = top; s->nk = nk; s->lim = lim;
}

/* a token string t[m] onto o[n..] after op (n > 0), in brackets when it has a + - or / in it and wrap; the new length, 0 if too long */
NOINLINE static uint8_t sujoin(uint8_t *o, uint8_t n, const uint8_t *t, uint8_t m, uint8_t op, uint8_t wrap)
{
    uint8_t i, br = 0;
    for (i = 0; wrap && i < m; i++) if (t[i] == T_ADD || t[i] == T_SUB || t[i] == T_NEG || (t[i] == T_DIV && op == T_MUL)) br = 1;
    if (n + m + 3 > MAXOUT) return 0;
    if (n) o[n++] = op;
    if (br) o[n++] = T_LPAR;
    while (m--) o[n++] = *t++;
    if (br) o[n++] = T_RPAR;
    return n;
}

/* SUM, PRODUCT term by term (l..h): the terms with the number put in, then their values */
NOINLINE static uint8_t sustep(ps_t *s, uint8_t cmd, rat_t *p, uint8_t j, int32_t l, int32_t h)
{
    uint8_t top = s->top, nk = s->nk, lim = s->lim, v = s->var[NV - 1 - j], fn, m, nl = 0, nv = 0, op = cmd == C_SUM ? T_ADD : T_MUL;
    uint8_t *ft, *lb, *vb, *tk, ok = 0;
    rat_t *t = get(s), *w = get(s), *b1 = get(s);
    int32_t k;
    if (!s->st || !b1) goto done;
    ft = (uint8_t *)b1; lb = ft + MAXOUT; vb = lb + MAXOUT; tk = vb + MAXOUT;
    if (!(fn = answer(s, p, ft, 0))) goto done;
    for (k = l; k <= h; k++) {
        rconst(w, k);
        if (!(m = answer(s, w, tk, 0)) || !(m = subtok(ft, fn, v, tk, m, tk + 16)) || m + 16 >= 100) goto done;
        if (!(nl = sujoin(lb, nl, tk + 16, m, op, p->n.n > 1 || (op == T_MUL && !isconst(&p->d))))) goto done;
        *t = *p;
        if (evalat(s, t, j, w) != 1 || reduce(s, t) != 1 || !(m = answer(s, t, tk, 0))) goto done;
        if (!(nv = sujoin(vb, nv, tk, m, op, 1))) goto done;
    }
    stept(s, cmd == C_SUM ? SR_SUL : SR_PRL, 0, 0, lb, nl);
    stept(s, cmd == C_SUM ? SR_SUV : SR_PRV, 0, 0, vb, nv);
    ok = 1;
done:
    s->top = top; s->nk = nk; s->lim = lim;
    return ok;
}

/* FMIN, FMAX: f' (f = p, or -p for FMAX; q its derivative), then each candidate with its value */
NOINLINE static void fmsteps(ps_t *s, uint8_t cmd, rat_t *p, rat_t *q, const fl_t *fl, rat_t *a, rat_t *b, uint8_t j)
{
    uint8_t top = s->top, nk = s->nk, lim = s->lim, v = s->var[NV - 1 - j], i, n, *o;
    rat_t *f = get(s), *d = get(s), *r = get(s), *x = get(s), *bf = get(s);
    if (!s->st || !bf) goto done;
    *f = *p; *d = *q;
    if (cmd == C_FMAX) { pneg(&f->n); pneg(&d->n); }
    if (reduce(s, d) != 1 || reduce(s, f) != 1) goto done;
    stf(s, v, SR_FMD, 0, "$", d);
    o = (uint8_t *)bf;
    if (!(n = answer(s, f, o + 2 * MAXOUT, 0))) goto done;
    for (i = 0; i < fl->n + 2; i++) {
        if (cand(s, fl, i, r, a, b, j) != 1) continue;
        *x = *f;
        if (subst(s, x, j, r) != 1 || reduce(s, x) != 1) continue;
        fput(s, i < fl->n ? SR_FMR : SR_FME, o + 2 * MAXOUT, n, v, r, x, o);
    }
done:
    s->top = top; s->nk = nk; s->lim = lim;
}

/* ARCLEN: the slope function g, then the length of a tiny piece, √(1+g²) */
NOINLINE static void ari(ps_t *s, uint8_t j, rat_t *g)
{
    uint8_t top = s->top, nk = s->nk, lim = s->lim, v = s->var[NV - 1 - j];
    rat_t *x = get(s);
    if (s->st && x) {
        *x = *g;
        if (reduce(s, x) == 1) {
            stf(s, v, SR_ARD, 0, "$", x);
            stf(s, v, SR_ARI, 0, x->n.n == 1 && isconst(&x->d) && !x->n.t[0].mono && x->n.t[0].rad == 1 && x->n.t[0].num > 0 && x->n.t[0].den == 1 ? "\xBC" "1+$\x0D" ")" : "\xBC" "1+#\x0D" ")", x);
        }
    }
    s->top = top; s->nk = nk; s->lim = lim;
}

/* an indefinite integral ends with + C */
static void ic(ps_t *s, rat_t *f, uint8_t v)
{
    stf(s, v, SR_IC, 0, "$+C", f);
}

/* one term u of an integral with V (nibble j) only in the numerator, its antiderivative t (integ()'s): the rule
   that fits it and the constant in front; 0 when none does (then by parts, or just the integral) */
NOINLINE static uint8_t iterm(ps_t *s, rat_t *u, rat_t *t, uint8_t j, uint8_t v)
{
    uint8_t top = s->top, ok = 0, c, k, e, rule;
    th_t th[NTH];
    term_t co, sl;
    rat_t *f = get(s), *g = get(s), *h = get(s), *w = get(s);
    if (!w || u->n.n != 1) goto done;
    c = csplit(s, u->n.t, j, th, &co);
    if (!c) {
        stf(s, v, SR_ICONST, "$", "$", u, t);
        ok = 1;
        goto done;
    }
    if (c != 1) goto done;
    thr(f, u->n.t, th, 1);
    g->n.n = 0;
    addterm(&g->n, &co);
    g->d = u->d;
    *w = *f;
    if (integ(s, h, w, j, 0, 0, 1, 0) != 1) goto done;
    if (th->m == 15u << 4 * j) {
        e = u->n.t[0].mono >> 4 * j & 15;
        rconst(w, e + 1);
        stf(s, v, SR_IPOW, "$,$", "$", f, w, h);
    } else if (!th->m && th->r == (uint32_t)1 << (RVAR + j)) stf(s, v, SR_IROOT, "$", "$", f, h);
    else if ((k = thk(s, th)) != 0xFF && !th->r && (th->m >> 4 * KN(s, k) & 15) == 1 && !s->kb[k] &&
             (s->kf[k] == K_SIN || s->kf[k] == K_COS || s->kf[k] == K_EXP) && rate(s->ka[k], j, &sl)) {
        rule = s->kf[k] == K_SIN ? SR_ISIN : s->kf[k] == K_COS ? SR_ICOS : SR_IEXP;
        if (isvar(s->ka[k], j)) stf(s, v, rule, "$", "$", f, h);
        else {
            w->n.n = 0;
            addterm(&w->n, &sl);
            pconst(&w->d, 1);
            stf(s, v, SR_ILIN, "$,$", "$", f, w, h);
        }
    } else goto done;
    if (!isnum(g, 1)) stf(s, v, SR_ICM, "$,$", "$", g, h, t);
    ok = 1;
done:
    s->top = top;
    return ok;
}

/* by parts, a term G(V)K, K one kernel (sin, cos, e^, ln), G with V, or any ln: u and dv, du and v, the integral
   left, then uv less it. 1 when shown. */
NOINLINE static uint8_t ipart(ps_t *s, rat_t *u, rat_t *t, uint8_t j, uint8_t v)
{
    uint8_t top = s->top, ok = 0, k, f = 0;
    unsigned vk = vkern(s, j), km = u->n.t[0].mono & vk;
    rat_t *g = get(s), *h = get(s), *a = get(s), *b = get(s), *w = get(s);
    term_t x;
    if (!w || u->n.n != 1 || !km) goto done;
    for (k = 0; k < s->nk; k++)
        if (km & 15u << 4 * KN(s, k)) f = s->kf[k];
    x = u->n.t[0];
    x.mono -= km;
    if (f == K_LOG || f == K_LABS) {                 /* u = ln, dv = G */
        g->n.n = h->n.n = 0;
        addterm(&h->n, &x);
        x.num = x.den = 1; x.mono = km; x.rad = 1;
        addterm(&g->n, &x);
    } else if ((f == K_SIN || f == K_COS || f == K_EXP) && (x.mono >> 4 * j & 15)) {   /* u = G, dv = K */
        g->n.n = h->n.n = 0;
        addterm(&g->n, &x);
        x.num = x.den = 1; x.mono = km; x.rad = 1;
        addterm(&h->n, &x);
    } else goto done;
    pconst(&g->d, 1); pconst(&h->d, 1);
    if (!dr(s, a, g, j)) goto done;
    *w = *h;
    if (integ(s, b, w, j, 0, 0, 1, 0) != 1) goto done;
    stf(s, v, SR_IPU, "$,$", 0, g, h);
    stf(s, v, SR_IPV, "$,$", 0, a, b);
    if (!rmulinto(s, a, b, 0) || reduce(s, a) != 1) goto done;
    *w = *a;
    if (integ(s, h, w, j, 0, 0, 1, 0) != 1) goto done;
    stf(s, v, SR_IPS, "$", "$", a, h);
    stf(s, v, SR_IPF, 0, "$", t);
    ok = 1;
done:
    s->top = top;
    return ok;
}

/* INTEGRAL(f,V[,a,b]) of a sum with V only in the numerator: each term integrated by integ() alone (so a printed
   antiderivative is the engine's own), the sum, and for a definite integral the limits put in. cc: end an
   indefinite one with + C. The kernels integ() makes are let go, as csum() does, so the real run starts from
   the same state. */
NOINLINE static void isteps(ps_t *s, const rat_t *p, uint8_t j, rat_t *a, rat_t *b, uint8_t cc)
{
    uint8_t top = s->top, nk = s->nk, lim = s->lim, i, k, v = s->var[NV - 1 - j], kf[NV], sv1 = s->st[1], pn, en;
    unsigned sv2 = s->st[68] | s->st[69] << 8;
    rat_t *u = get(s), *w = get(s), *f = get(s), *t = get(s), *z = get(s), *ka[NV], *kb[NV], *la = s->la, *lb = s->lb;
    for (i = 0; i < NV; i++) { kf[i] = s->kf[i]; ka[i] = s->ka[i]; kb[i] = s->kb[i]; }      /* lin() renumbers them */
    if (!s->st || !t || !z) goto done;
    z->n.n = z->d.n = 0;
    s->la = s->lb = z;                          /* no limits to keep in step: π and e stay */
    rconst(f, 0);
    if (p->n.n > 1) stn(s, v, SR_ISPLIT);
    for (i = 0; i < p->n.n; i++) {
        u->n.n = 0;
        if (!addterm(&u->n, &p->n.t[i])) goto bad;
        u->d = p->d;
        if (reduce(s, u) != 1) goto bad;
        *w = *u;
        if (integ(s, t, w, j, 0, 0, 1, 0) != 1 || !raddinto(s, f, t)) goto bad;
        for (k = 0; k < nk && s->kf[k] == kf[k] && s->ka[k] == ka[k] && s->kb[k] == kb[k]; k++) ;
        if (k < nk) {                           /* sin²cos: integ() rebuilt the kernels, so u and t can not be written together: the whole integrand in one step */
            s->st[1] = sv1; s->st[68] = (uint8_t)sv2; s->st[69] = (uint8_t)(sv2 >> 8);
            for (k = 0; k < nk; k++) { s->kf[k] = kf[k]; s->ka[k] = ka[k]; s->kb[k] = kb[k]; }
            s->nk = nk;
            *w = *p;
            if (!(pn = answer(s, w, (uint8_t *)u, 0))) goto bad;
            *w = *p;
            if (integ(s, f, w, j, 0, 0, 1, 0) != 1 || reduce(s, f) != 1 || !(en = answer(s, f, (uint8_t *)t, 0))) goto bad;
            stept(s, SR_IANY, (uint8_t *)u, pn, (uint8_t *)t, en);
            goto fin;
        }
        if (!iterm(s, u, t, j, v) && !ipart(s, u, t, j, v)) stf(s, v, SR_IANY, "$", "$", u, t);
    }
    if (reduce(s, f) != 1) goto bad;
    if (p->n.n > 1) stfx(s, v, 0xFFFF, 0, 0, 0, f, SR_ISUM);
fin:
    if (a && b) ilim2(s, f, j, a, b);
    else if (cc) ic(s, f, v);
    goto done;
bad:
    s->st[2] |= 2;
done:
    s->top = top; s->nk = nk; s->lim = lim; s->la = la; s->lb = lb;
    for (i = 0; i < NV; i++) { s->kf[i] = kf[i]; s->ka[i] = ka[i]; s->kb[i] = kb[i]; }
}
/* a one-term value: the monomial, coefficient num/den, over 1 */
static void oneterm(rat_t *r, unsigned mono, int32_t num, int32_t den)
{
    r->n.n = 1;
    r->n.t[0].num = num; r->n.t[0].den = den; r->n.t[0].mono = mono; r->n.t[0].rad = 1;
    pconst(&r->d, 1);
}

/* INTEGRAL of √V/V^e (1/√V, 1/(V√V)..): written as the power V^(1/2-e), then the power rule. 1 when logged. */
NOINLINE static uint8_t rpsteps(ps_t *s, rat_t *p, uint8_t j, rat_t *a, rat_t *b, uint8_t cc)
{
    uint8_t top = s->top, nk = s->nk, lim = s->lim, k, e, v = s->var[NV - 1 - j], n, m, ok = 0, *o, *c;
    const term_t *x = p->n.t, *y = p->d.t;
    rat_t *w = get(s), *t = get(s), *u = get(s), *f = get(s);
    if (!s->st || !f || p->n.n != 1 || p->d.n != 1 || x->num != 1 || x->den != 1 || x->rad != ((1u << (RVAR + j)) | 1) || x->mono || y->num != 1 || y->den != 1 || y->rad != 1) goto done;
    e = y->mono >> 4 * j & 15;
    if (!e || y->mono != (unsigned)e << 4 * j) goto done;
    o = (uint8_t *)w; c = (uint8_t *)t;
    m = 0;                                                      /* what was asked: 1/√(X), 1/(X√(X)) */
    c[m++] = 0x31; c[m++] = T_DIV;
    if (e > 1) { c[m++] = T_LPAR; c[m++] = v; m = putpow(c, m, e - 1); }
    c[m++] = T_SQRT; c[m++] = v; c[m++] = T_RPAR;
    if (e > 1) c[m++] = T_RPAR;
    o[0] = v; o[1] = T_POW; o[2] = T_LPAR;
    if (!(n = ftok(o, 3, 1 - 2 * (int32_t)e, 2)) || n + 2 >= MAXOUT) goto bad;
    o[n++] = T_RPAR;
    stept(s, SR_IRP, c, m, o, n);
    *u = *p;
    if (integ(s, f, u, j, 0, 0, 1, 0) != 1 || reduce(s, f) != 1) goto bad;
    for (k = 0; k < n; k++) c[k] = o[k];                        /* the power rule: the power, the new power, then the answer */
    c[n++] = T_COMMA;
    if (!(n = ftok(c, n, 3 - 2 * (int32_t)e, 2)) || !(m = answer(s, f, o, 0))) goto bad;
    stept(s, SR_IPOW, c, n, o, m);
    if (a && b) ilim2(s, f, j, a, b);
    else if (cc) ic(s, f, v);
    ok = 1;
    goto done;
bad:
    s->st[2] |= 2;
done:
    s->top = top; s->nk = nk; s->lim = lim;
    return ok;
}

/* INTEGRAL of sin(V)^m cos(V): u = sin(V) with the slope cos(V) the rest, the power rule in u, u put back. The
   calculator's own answer may be written in multiple angles: shown last when it differs. 1 when logged. */
NOINLINE static uint8_t trsteps(ps_t *s, rat_t *p, uint8_t j, rat_t *a, rat_t *b, uint8_t cc)
{
    uint8_t top = s->top, nk = s->nk, lim = s->lim, k, ks = 0xFF, kc = 0xFF, m = 0, e, v = s->var[NV - 1 - j], kf[NV], n, en, ok = 0, *o, *c;
    const term_t *x = p->n.t;
    rat_t *us = get(s), *sl = get(s), *pu = get(s), *pr = get(s), *f = get(s), *ka[NV], *kb[NV];
    for (k = 0; k < nk; k++) { kf[k] = s->kf[k]; ka[k] = s->ka[k]; kb[k] = s->kb[k]; }   /* integ() may rebuild the kernels */
    if (!s->st || !f || p->n.n != 1 || !isconst(&p->d) || x->num != 1 || x->den != 1 || x->rad != 1) goto done;
    for (k = 0; k < nk; k++) {
        if (!(e = x->mono >> 4 * KN(s, k) & 15)) continue;
        if (!isvar(s->ka[k], j)) goto done;
        if (s->kf[k] == K_SIN) { ks = k; m = e; }
        else if (s->kf[k] == K_COS && e == 1) kc = k;
        else goto done;
    }
    if (kc == 0xFF || ks == 0xFF || x->mono != ((unsigned)m << 4 * KN(s, ks) | 1u << 4 * KN(s, kc))) goto done;
    oneterm(us, 1u << 4 * KN(s, ks), 1, 1);
    oneterm(sl, 1u << 4 * KN(s, kc), 1, 1);
    oneterm(pu, (unsigned)m << 4 * j, 1, 1);
    oneterm(pr, (unsigned)(m + 1) << 4 * KN(s, ks), 1, m + 1);
    stf(s, v, SR_IUSUB, "$,$", 0, us, sl);
    s->var[NV - 1 - j] = 0x55;
    stf(s, 0x55, SR_IUINT, 0, "$", pu);
    isteps(s, pu, j, 0, 0, 0);
    s->var[NV - 1 - j] = v;
    stf(s, v, SR_IUBACK, "$", "$", us, pr);
    o = (uint8_t *)us; c = (uint8_t *)pu;
    if (!(n = answer(s, pr, o, 0))) goto bad;                   /* sin(X)^3/3, in the kernels as they stand */
    *sl = *p;
    if (integ(s, f, sl, j, 0, 0, 1, 0) != 1 || reduce(s, f) != 1 || !(en = answer(s, f, c, 0))) goto bad;
    for (k = 0; k < n && k < en && o[k] == c[k]; k++) ;
    if (n != en || k < n) stept(s, SR_ISAME, 0, 0, c, en);
    if (a && b) ilim2(s, f, j, a, b);
    else if (cc) ic(s, f, v);
    ok = 1;
    goto done;
bad:
    s->st[2] |= 2;
done:
    s->top = top; s->nk = nk; s->lim = lim;
    for (k = 0; k < nk; k++) { s->kf[k] = kf[k]; s->ka[k] = ka[k]; s->kb[k] = kb[k]; }
    return ok;
}
#endif

NOINLINE static uint8_t apart(ps_t *s, rat_t *p, uint8_t j, uint8_t *out);
#ifdef NOSTEPSX
#define pfsteps(s, p, j)
#define pfdone(s, f, r, j, a, b)
#else
/* INTEGRAL of a fraction with V below the bar: apart()'s split first, then the antiderivative (f + r) */
NOINLINE static void pfsteps(ps_t *s, const rat_t *p, uint8_t j)
{
    uint8_t top = s->top, nk = s->nk, lim = s->lim, n, out[MAXOUT + 4];
    rat_t *w = get(s);
    if (!s->st || !w) goto done;
    *w = *p;
    if ((n = apart(s, w, j, out)) && n <= MAXOUT) stept(s, SR_IPFR, 0, 0, out, n);
done:
    s->top = top; s->nk = nk; s->lim = lim;
    { uint8_t q; for (q = top; q < lim; q++) s->pool[q].n.n = s->pool[q].d.n = 0; }
}

NOINLINE static void pfdone(ps_t *s, const rat_t *f, rat_t *r, uint8_t j, rat_t *a, rat_t *b)
{
    uint8_t top = s->top, v = s->var[NV - 1 - j];
    rat_t *x = get(s);
    if (!s->st || !x) goto done;
    *x = *f;
    if (r->n.n && !raddinto(s, x, r)) goto done;
    if (reduce(s, x) != 1) goto done;
    stf(s, v, SR_IPFI, 0, "$", x);
    if (a) ilim2(s, x, j, a, b);
    else ic(s, x, v);
done:
    s->top = top;
}
#endif

/* The part of p = R/D that belongs to fq = F₂V²+F₁V+F₀, an irreducible factor of D (to the first power): (uV+w)/fq, with
   (uV+w)·g ≡ R mod fq, g = D/fq (a 2 by 2 system). Its integral, added to out, is
   u/(2F₂)·ln(fq) + (w - u·F₁/(2F₂))·2/√Δ·tan⁻¹((2F₂V+F₁)/√Δ), Δ = 4F₂F₀ - F₁² (F₂ > 0 by a sign).
   0 when the numbers are not plain, or pass the limits. */
NOINLINE static int qpart(ps_t *s, rat_t *out, const rat_t *p, const poly_t *fq, uint8_t j)
{
    rat_t *z = get(s), *y = get(s), *k = get(s);
    term_t F[3], G[2], R[2], be, ga, H, det, u0, u1, t, w, kf, dl, c, two = fri(2), four = fri(4);
    uint32_t r;
    uint8_t top = s->top - 3, neg;
    if (!k || !fcoef(fq, j, F, 2) || !F[2].num) return 0;
    z->n = p->d; y->n.n = 0;
    if (!vrem(&z->n, fq, &y->n, j) || z->n.n) return 0;     /* y->n = g */
    z->n = y->n;
    if (!vrem(&z->n, fq, 0, j) || !fcoef(&z->n, j, G, 1)) return 0;
    y->n = p->n;
    if (!vrem(&y->n, fq, 0, j) || !fcoef(&y->n, j, R, 1)) return 0;
    neg = F[2].num < 0;
    /* G₁u₀ + H·u₁ = R₁,  G₀u₀ + w·u₁ = R₀,  H = G₀ - (F₁/F₂)G₁,  w = -(F₀/F₂)G₁ */
    if (!frd(&be, &F[1], &F[2]) || !frd(&ga, &F[0], &F[2]) || !frm(&t, &be, &G[1])) return 0;
    t.num = -t.num;
    if (!fra(&H, &G[0], &t) || !frm(&w, &ga, &G[1])) return 0;
    w.num = -w.num;
    if (!frm(&kf, &G[1], &w) || !frm(&t, &H, &G[0])) return 0;
    t.num = -t.num;
    if (!fra(&det, &kf, &t) || !det.num) return 0;
    if (!frm(&t, &R[1], &w) || !frm(&kf, &H, &R[0])) return 0;
    kf.num = -kf.num;
    if (!fra(&c, &t, &kf) || !frd(&u0, &c, &det)) return 0;
    if (!frm(&t, &G[1], &R[0]) || !frm(&kf, &G[0], &R[1])) return 0;
    kf.num = -kf.num;
    if (!fra(&c, &t, &kf) || !frd(&u1, &c, &det)) return 0;
    if (neg) { F[0].num = -F[0].num; F[1].num = -F[1].num; F[2].num = -F[2].num; u0.num = -u0.num; u1.num = -u1.num; }
    if (!frm(&t, &F[2], &F[0]) || !frm(&t, &t, &four) || !frm(&w, &F[1], &F[1])) return 0;
    w.num = -w.num;
    if (!fra(&dl, &t, &w) || dl.num <= 0) return 0;
    y->n = *fq;
    if (neg) pneg(&y->n);
    pconst(&y->d, 1);
    if (u1.num) {                               /* u/(2F₂)·ln(fq) */
        if (!kvar(s, k, K_LOG, y, 0) || !frd(&t, &u1, &F[2]) || !frd(&t, &t, &two)) return 0;
        if (!scale(&k->n, &t) || !raddinto(s, out, k)) return 0;
    }
    /* w - u·F₁/(2F₂), times 2/√Δ = 2·kf·√r */
    if (!frd(&t, &F[1], &F[2]) || !frm(&t, &t, &u1) || !frd(&t, &t, &two)) return 0;
    t.num = -t.num;
    if (!fra(&w, &u0, &t)) return 0;
    if (w.num) {
        if (!fsq(&dl, &kf, &r) || !frm(&w, &w, &kf) || !frm(&w, &w, &two)) return 0;
        y->n.n = 0;                             /* (2F₂V+F₁)/√Δ */
        if (!frm(&c, &F[2], &kf) || !frm(&c, &c, &two)) return 0;
        c.rad = r; c.mono = 1u << 4 * j;
        if (!addterm(&y->n, &c)) return 0;
        if (F[1].num) {
            if (!frm(&c, &F[1], &kf)) return 0;
            c.rad = r; c.mono = 0;
            if (!addterm(&y->n, &c)) return 0;
        }
        if (!kvar(s, k, K_ATAN, y, 0)) return 0;
        w.rad = r; w.mono = 0;
        if (!scale(&k->n, &w)) return 0;
        stf(s, s->var[NV - 1 - j], SR_IATAN, 0, "$", k);
        if (!raddinto(s, out, k)) return 0;
    }
    s->top = top;
    return 1;
}

#ifdef NOSTEPSX
#define usteps(s, p, j, ku)
#else
/* INTEGRAL with a u-substitution (lsub): u = aV+b, the integrand in u (the letter U), its terms */
NOINLINE static void usteps(ps_t *s, rat_t *p, uint8_t j, uint8_t ku)
{
    uint8_t v0 = s->var[NV - 1 - j], top = s->top;
    rat_t *m = get(s);
    term_t c;
    if (!s->st || !m || !rate(s->ka[ku], j, &c)) goto done;
    m->n.n = 0;
    addterm(&m->n, &c);
    pconst(&m->d, 1);
    stf(s, v0, SR_IUSUB, "$,$", 0, s->ka[ku], m);
    s->var[NV - 1 - j] = 0x55;
    stf(s, 0x55, SR_IUINT, 0, "$", p);
    if (!vdeg(&p->d, j) && !used(&p->d, vkern(s, j)) && p->n.n) isteps(s, p, j, 0, 0, 0);
    s->var[NV - 1 - j] = v0;
done:
    s->top = top;
}
#endif

/* ---- INTEGRAL by a general u-substitution: p = F(w)·w', ∫p dV = ∫F(u) du at u = w ----
   w is a root's argument (X√(X²+1)), the argument of a kernel (X·sin(X²)), a factor of
   the denominator (X/(X²+1)), or a kernel of V (ln(X)/X, cos(X)/(1+sin(X)): w = sin(X)).
   q = p/w' is written in a new variable that takes V's nibble: its V-parts in w's powers
   (bexp()), each kernel of V over with the argument in the new variable (sin(X²) is sin(V), its argument
   aw+b is aV+b), √w as √V; a kernel w is V by moving its nibble. integ() does the rest, ubak() puts
   w back. Right by construction: no check of the derivative. */

/* p's nibble f moved to the nibble t, which p does not use: its powers and its root bit */
static void mvnib(poly_t *p, uint8_t f, uint8_t t)
{
    term_t *x;
    unsigned e;
    for (x = p->t; x < p->t + p->n; x++) {
        e = x->mono >> 4 * f & 15;
        x->mono = x->mono - (e << 4 * f) + (e << 4 * t);
        if (x->rad >> (RVAR + f) & 1) x->rad ^= (uint32_t)1 << (RVAR + f) | (uint32_t)1 << (RVAR + t);
    }
}

/* out = g, a polynomial in V (nibble j, the other nibbles coefficients), in w's powers: Σ cᵢWⁱ, W in
   nibble j and cᵢ free of V; 0 when g is not so */
NOINLINE static int bexp(ps_t *s, poly_t *out, const poly_t *g, const poly_t *w, uint8_t j)
{
    poly_t *a = tmp(s), *q = tmp(s);
    term_t t;
    uint8_t i, x;
    if (!q || !vdeg(w, j) || vdeg(w, j) == 0xFF) return 0;
    *a = *g;
    out->n = 0;
    for (i = 0; a->n; i++) {
        q->n = 0;
        if (!vrem(a, w, q, j)) return 0;
        for (x = 0; x < a->n; x++) {
            t = a->t[x];
            if (t.mono >> 4 * j & 15 || t.rad >> (RVAR + j) & 1 || !madd(&t.mono, (unsigned)i << 4 * j) || !addterm(out, &t)) return 0;
        }
        *a = *q;
    }
    s->top -= 2;
    return 1;
}

/* the nibbles of kernel k are in q, or its root bit */
static unsigned inq(const ps_t *s, const rat_t *q, uint8_t k, uint32_t ro)
{
    unsigned m = 15u << 4 * KN(s, k);
    return used(&q->n, m) | used(&q->d, m) | (ro >> (RVAR + KN(s, k)) & 1);
}

/* out = q with V gone, in the new variable at V's nibble: w's powers (kp 0xFF), or the kernel at nibble kp
   as V. Every kernel of V in q is made over with its argument in the new variable; 0 when one cannot be */
NOINLINE static int uconv(ps_t *s, rat_t *out, const rat_t *q, const rat_t *w, uint8_t j, uint8_t kp)
{
    unsigned vk = vkern(s, j), km, nm;
    uint32_t rk = 0, ro = 0, r;
    uint8_t mp[NV], k, e, nk = s->nk, n, top = s->top;
    rat_t *a = get(s);
    poly_t *c = tmp(s), *g = tmp(s), *h = tmp(s);
    const term_t *x;
    uint8_t i;
    if (!h || vdeg(&q->n, j) == 0xFF || vdeg(&q->d, j) == 0xFF) return 0;
    for (x = q->n.t; x < q->n.t + q->n.n; x++) ro |= x->rad;
    for (k = 0; k < nk; k++) {
        mp[k] = 0xFF;
        if (s->kf[k] == K_ROOT && kdep(s, k, j)) rk |= (uint32_t)1 << (RVAR + KN(s, k));
    }
    for (k = 0; k < nk; k++) {
        n = KN(s, k);
        if (!kdep(s, k, j) || !inq(s, q, k, ro)) continue;
        if (n == kp) { mp[k] = j; continue; }
        if (s->kf[k] == K_ROOT) {
            if (kp != 0xFF || !same2(s->ka[k], w)) goto no;
            mp[k] = j;
            continue;
        }
        *a = *s->ka[k];
        if (kp == 0xFF) {
            if (!isconst(&a->d) || !bexp(s, c, &a->n, &w->n, j)) goto no;
            a->n = *c;
        } else {
            if (vdeg(&a->n, j) || vdeg(&a->d, j)) goto no;
            mvnib(&a->n, kp, j);
            mvnib(&a->d, kp, j);
        }
        if (used(&a->n, vk) || used(&a->d, vk) || (n = kern(s, s->kf[k], a, s->kb[k])) == 0xFF) goto no;
        mp[k] = n;
    }
    out->n.n = 0;
    *c = q->n;
    while (c->n) {
        km = c->t[0].mono & vk;
        r = c->t[0].rad & rk;
        g->n = 0;
        for (i = 0; i < c->n; )
            if ((c->t[i].mono & vk) == km && (c->t[i].rad & rk) == r) {
                g->t[g->n] = c->t[i];
                g->t[g->n].mono -= km;
                g->t[g->n++].rad &= ~rk;
                c->t[i] = c->t[--c->n];
            } else i++;
        for (nm = 0, k = 0; k < nk; k++)
            if ((e = km >> 4 * KN(s, k) & 15)) {
                if (mp[k] == 0xFF || s->kf[k] == K_ROOT) goto no;
                nm += (unsigned)e << 4 * mp[k];
            }
        if (r && (r & (r - 1))) goto no;                    /* two roots in one term */
        if (kp == 0xFF) { if (!bexp(s, h, g, &w->n, j)) goto no; }
        else if (vdeg(g, j)) goto no;
        else *h = *g;
        for (i = 0; i < h->n; i++) {
            term_t t = h->t[i];
            if (!madd(&t.mono, nm)) goto no;
            if (r) t.rad |= (uint32_t)1 << (RVAR + j);
            if (!addterm(&out->n, &t)) goto no;
        }
    }
    if (kp == 0xFF) {
        if (used(&q->d, vk) || !bexp(s, &out->d, &q->d, &w->n, j)) goto no;
    } else {
        if (used(&q->d, vk & ~(15u << 4 * kp)) || vdeg(&q->d, j)) goto no;
        out->d = q->d;
        mvnib(&out->d, kp, j);
    }
    s->top = top;
    return reduce(s, out) != 0;
no:
    s->top = top;
    return 0;
}

/* F, in the new variable, back in V: its kernels with the argument in w, then V := w */
NOINLINE static int ubak(ps_t *s, rat_t *F, const rat_t *w, uint8_t j, uint8_t kp)
{
    rat_t *a = get(s), *c = get(s);
    uint8_t k, nk = s->nk, top = s->top;
    unsigned pm = 0;
    if (!c) return 0;
    for (k = 0; k < nk; k++)
        if (kdep(s, k, j) && (used(&F->n, 15u << 4 * KN(s, k)) || used(&F->d, 15u << 4 * KN(s, k)))) pm |= 1u << k;
    for (k = 0; k < nk; k++) {
        if (!(pm >> k & 1)) continue;
        *a = *s->ka[k];
        if (kp == 0xFF) { if (subst(s, a, j, w) != 1) return 0; }
        else { mvnib(&a->n, j, kp); mvnib(&a->d, j, kp); }
        if (!kvar(s, c, s->kf[k], a, s->kb[k]) || subst(s, F, KN(s, k), c) != 1) return 0;
    }
    if (kp == 0xFF) { if (subst(s, F, j, w) != 1) return 0; }
    else {
        if (vdeg(&F->n, j) == 0xFF || vdeg(&F->d, j) == 0xFF) return 0;
        mvnib(&F->n, j, kp);
        mvnib(&F->d, j, kp);
    }
    s->top = top;
    return 1;
}

/* ∫p dV with u = w: w a rat in V, or (kp not 0xFF) the kernel at that nibble, w its monomial. 1, out;
   0 when p is not F(w)·w' (kernels made on the way are let go). lo: a definite integral, which the
   poles of F could spoil */
NOINLINE static int utry(ps_t *s, rat_t *out, const rat_t *p, uint8_t j, rat_t *w, uint8_t kp, uint8_t lo)
{
    uint8_t top = s->top, nk = s->nk, lim = s->lim, v0 = s->var[NV - 1 - j];
    uint8_t sv[4] = { 0, 0, 0, 0 };
    rat_t *d = get(s), *q = get(s), *t = get(s);
    if (!t) goto no;
#ifndef NOSTEPSX
    if (s->st) { sv[0] = s->st[1]; sv[1] = s->st[2]; sv[2] = s->st[68]; sv[3] = s->st[69]; }
#endif
    if (!drat(s, d, w, j) || reduce(s, d) != 1 || !dep(s, d, j)) goto no;
    *q = *p;
    if (!rmulinto(s, q, d, 1) || !uconv(s, t, q, w, j, kp)) goto no;
    if (lo && (vdeg(&t->d, j) || used(&t->d, vkern(s, j))) && !(s->pl && t->d.n == 1)) goto no;
#ifndef NOSTEPSX
    stf(s, v0, SR_IUSUB, "$,$", 0, w, d);
    s->var[NV - 1 - j] = 0x55;
    stf(s, 0x55, SR_IUINT, 0, "$", t);
    *q = *t;
#endif
    if (integ(s, out, t, j, 0, 0, 1, 0) != 1) goto no;
#ifndef NOSTEPSX
    stf(s, 0x55, SR_IANY, "$", "$", q, out);
    s->var[NV - 1 - j] = v0;
#endif
    if (!ubak(s, out, w, j, kp)) goto no;
#ifndef NOSTEPSX
    stf(s, v0, SR_IUBACK, "$", "$", w, out);
#endif
    s->top = top;
    return 1;
no:
    s->var[NV - 1 - j] = v0;
#ifndef NOSTEPSX
    if (s->st) { s->st[1] = sv[0]; s->st[2] = sv[1]; s->st[68] = sv[2]; s->st[69] = sv[3]; }
#endif
    s->top = top; s->nk = nk; s->lim = lim;
    return 0;
}

/* ∫c·a^b dV, a power kernel alone: a number to a linear b, a^b/(b'·ln(a)); a linear a to a number b,
   a^(b+1)/(a'·(b+1)). 1 and out, or 0 */
NOINLINE static int ipow(ps_t *s, rat_t *out, const rat_t *p, uint8_t j, uint8_t lo)
{
    uint8_t k, nk = s->nk, top = s->top, neg = 0;
    unsigned m;
    term_t a, c;
    rat_t *t = get(s), *u = get(s), *q = get(s), *b;
    if (!q) return 0;
    for (k = 0; k < nk; k++) if (s->kf[k] == K_POW && kdep(s, k, j)) break;
    if (k == nk || p->n.n != 1 || (m = 1u << 4 * KN(s, k)) != p->n.t[0].mono || p->n.t[0].rad != 1 ||
        used(&p->d, vkern(s, j)) || vdeg(&p->d, j) == 0xFF || vdeg(&p->d, j)) goto no;
    *q = *p;
    q->n.t[0].mono = 0;
    if (!dep(s, s->ka[k], j)) {
        if (!rate(s->kb[k], j, &a) || !mklog(s, t, s->ka[k], 0) || !t->n.n) goto no;
        rinv(&c, &a);
        rconst(out, 1);
        if (!kvar(s, u, K_POW, s->ka[k], s->kb[k]) || !rmulinto(s, out, u, 0) || !scale(&out->n, &c) || !rmulinto(s, out, t, 1)) goto no;
    } else {
        if (dep(s, s->kb[k], j) || !rate(s->ka[k], j, &a)) goto no;
        if (lo && !(s->kb[k]->n.n == 1 && s->kb[k]->n.t[0].num > 0 && isconst(&s->kb[k]->d))) goto no;
        *t = *s->kb[k];
        rconst(u, 1);
        if (!raddinto(s, t, u) || !t->n.n) goto no;
        neg = t->n.n == 1 && !t->n.t[0].mono && t->n.t[0].num < 0;
        if (neg) pneg(&t->n);
        if (!(b = keep(s, t)) || !kvar(s, u, K_POW, s->ka[k], b)) goto no;
        rinv(&c, &a);
        rconst(out, 1);
        if (!rmulinto(s, out, u, neg) || !scale(&out->n, &c)) goto no;
        if (neg) pneg(&t->n);
        if (!rmulinto(s, out, t, 1)) goto no;
    }
    if (!rmulinto(s, out, q, 0)) goto no;
    s->top = top;
    return 1;
no:
    s->top = top;
    return 0;
}

/* ∫k/√U dV, U = -A·V² + b·V + c with A > 0 and b² + 4Ac > 0: p = k'·√U/W with W = λ·U, k = k'/λ. That is
   k/√A·sin⁻¹((2A·V - b)/√(b²+4Ac)). 1 and out, or 0 */
NOINLINE static int iasin(ps_t *s, rat_t *out, const rat_t *p, uint8_t j)
{
    uint8_t k, top = s->top;
    term_t U[3], W[3], l, kk, A, t, d, c, two = fri(2), four = fri(4);
    uint32_t r1, r2;
    rat_t *a = get(s), *u = get(s);
    if (!u || p->n.n != 1 || p->n.t[0].mono || vdeg(&p->d, j) != 2) goto no;
    for (k = 0; k < s->nk; k++)
        if (s->kf[k] == K_ROOT && !s->kb[k] && (p->n.t[0].rad & ~RNUM) == (uint32_t)1 << (RVAR + KN(s, k))) break;
    if (k == s->nk || !isconst(&s->ka[k]->d) || !fcoef(&s->ka[k]->n, j, U, 2) || !fcoef(&p->d, j, W, 2)) goto no;
    if (U[2].num >= 0 || !frd(&l, &W[2], &U[2])) goto no;
    for (k = 0; k < 2; k++)                     /* W = λU */
        if (!frm(&t, &l, &U[k]) || t.num != W[k].num || t.den != W[k].den) goto no;
    c = fri(p->n.t[0].num);
    c.den = p->n.t[0].den;
    if (!frd(&kk, &c, &l)) goto no;
    A = U[2]; A.num = -A.num;
    if (!frm(&t, &U[1], &U[1]) || !frm(&d, &A, &U[0]) || !frm(&d, &d, &four) || !fra(&d, &d, &t) || d.num <= 0) goto no;
    if (!fsq(&A, &t, &r1) || !frm(&kk, &kk, &t) || !fsq(&d, &t, &r2)) goto no;
    a->n.n = 0;                                 /* (2AV - b)/√D = (2AV - b)·t·√r₂ */
    if (!frm(&c, &A, &t) || !frm(&c, &c, &two)) goto no;
    c.rad = r2; c.mono = 1u << 4 * j;
    if (!addterm(&a->n, &c)) goto no;
    if (U[1].num) {
        if (!frm(&c, &U[1], &t)) goto no;
        c.num = -c.num; c.rad = r2; c.mono = 0;
        if (!addterm(&a->n, &c)) goto no;
    }
    pconst(&a->d, 1);
    if (!kvar(s, u, K_ASIN, a, 0)) goto no;
    kk.rad = r1; kk.mono = 0;
    if (!scale(&u->n, &kk)) goto no;
    kk = fri(1); kk.rad = p->n.t[0].rad & RNUM;     /* a content √2 of the root's argument */
    if (!scale(&u->n, &kk)) goto no;
    *out = *u;
    s->top = top;
    return 1;
no:
    s->top = top;
    return 0;
}

/* w, a polynomial in V, is positive for every V */
static int upos(const rat_t *w, uint8_t j)
{
    return isconst(&w->d) && vdeg(&w->n, j) != 0xFF && noroot(&w->n, j) && lead(&w->n)->num > 0;
}

/* ∫p dV by a u-substitution; the candidates for u, in turn: a root's argument, a kernel's nonlinear argument,
   a factor of the denominator, a kernel. 1 and out, or 0 */
NOINLINE static int usub(ps_t *s, rat_t *out, const rat_t *p, uint8_t j, uint8_t lo)
{
    uint8_t k, f, nk = s->nk, top = s->top, i, ok = 0;
    rat_t *w = get(s), *x = get(s);
    fl_t fl;
    term_t t;
    if (!x) return 0;
    for (k = 0; k < nk && !ok; k++) {
        f = s->kf[k];
        if (!kdep(s, k, j) || (f != K_ROOT && (f > K_LOG && f != K_LABS))) continue;
        if (f != K_ROOT && (rate(s->ka[k], j, &t) || haskern(s, s->ka[k], 0) || vdeg(&s->ka[k]->n, j) < 2)) continue;
        *w = *s->ka[k];
        s->pl = f == K_ROOT || upos(w, j);
        ok = utry(s, out, p, j, w, 0xFF, lo);
    }
    if (!ok && vdeg(&p->d, j) && vdeg(&p->d, j) != 0xFF && !used(&p->d, vkern(s, j)) && !hasrad(&p->d) && p->d.n > 1) {
        get(s); get(s);
        if (!get(s)) goto done;
        pconst(&w->d, 1);
        flinit(&fl, s->pool + s->top - 3);
        x->n = p->d;
        pconst(&x->d, 1);
        if (fac(s, &fl, &x->n, 0)) {
            s->top -= 3;
            w->n = x->n;                        /* the whole denominator first: X²/(X³+1) */
            s->pl = upos(w, j);
            ok = vdeg(&w->n, j) > 1 && utry(s, out, p, j, w, 0xFF, lo);
            for (i = 0; i < fl.n && !ok; i++) {
                if (vdeg(fl.f[i], j) < 2) continue;
                w->n = *fl.f[i];
                pconst(&w->d, 1);
                s->pl = upos(w, j);
                ok = utry(s, out, p, j, w, 0xFF, lo);
            }
        }
    }
    for (k = 0; k < nk && !ok; k++) {
        f = s->kf[k];
        if (!kdep(s, k, j) || (f > K_LOG && f != K_LABS)) continue;
        rconst(w, 1);
        w->n.t[0].mono = 1u << 4 * KN(s, k);
        s->pl = f == K_EXP;
        ok = utry(s, out, p, j, w, KN(s, k), lo);
    }
done:
    s->pl = 0;
    s->top = top;
    return ok;
}

/* ---- LIMIT where lcore() stops: a root (its conjugate), abs (each side), tan⁻¹ sin cos e^ ln (by strength:
   e^ beats powers, powers beat ln), a power with a variable in its exponent (b^c is e^(c ln b)). Each
   makes a simpler f and takes its limit again. lv(): lcore()'s codes, the value in f; q: no steps. ---- */
NOINLINE static uint8_t lv(ps_t *s, rat_t *f, uint8_t j, rat_t *a, int dir, int8_t inf, uint8_t q);

static int nsg(const ps_t *s, const rat_t *r)
{
    int v = nsign(r);
    return v == 3 ? nsignk(s, r) : v;
}

/* f with the root of kernel k := w (and its radicand := the kernel's argument) */
static int krsub(ps_t *s, rat_t *f, uint8_t k, const rat_t *w)
{
    rat_t *n = get(s), *d = get(s);
    if (!d || !hpoly(s, n, &f->n, KN(s, k), s->ka[k], w) || !hpoly(s, d, &f->d, KN(s, k), s->ka[k], w) ||
        !rmulinto(s, n, d, 1)) return 0;
    *f = *n;
    s->top -= 2;
    return 1;
}

/* the sign of S near a (side d) or at ±∞: 1, -1; 0 when it is 0 there and d does not say, or cannot be told */
NOINLINE static int ssg(ps_t *s, rat_t *S, uint8_t j, rat_t *a, int d, int8_t inf)
{
    rat_t *t = get(s), *u = get(s);
    int r = 0;
    uint8_t e;
    if (!u) return 0;
    if (inf) {
        e = vdeg(&S->n, j);
        if (e != 0xFF && S->n.n) r = lead(&S->n)->num > 0 ? (inf < 0 && e & 1 ? -1 : 1) : (inf < 0 && e & 1 ? 1 : -1);
    } else {
        *t = *S;
        if (evalat(s, t, j, a) != 1) goto done;
        if ((r = nsg(s, t)) > 1) r = 0;
        if (!r && d) {
            if (!drat(s, u, S, j)) goto done;
            *t = *u;
            if (evalat(s, t, j, a) != 1) goto done;
            if ((r = nsg(s, t)) > 1) r = 0;
            r *= d;
        }
    }
done:
    s->top -= 2;
    return r;
}

/* one side of abs(S): g = f with |S| := ±S, then its limit */
NOINLINE static uint8_t lside(ps_t *s, rat_t *g, const rat_t *f, uint8_t k, uint8_t j, rat_t *a, int dir, int8_t inf, uint8_t q)
{
    rat_t *w = get(s), *t = get(s);
    int sg;
    *g = *f;
    if (!t || !(sg = ssg(s, s->kb[k], j, a, dir, inf))) return 0;
    *w = *s->kb[k];
    if (sg < 0) pneg(&w->n);
    if (!krsub(s, g, k, w)) return 0;
    *t = *w;
    rconst(w, dir ? dir : inf);
    stf(s, s->var[NV - 1 - j], SR_LABS, "$,$", 0, w, t);
    return lv(s, g, j, a, dir, inf, q);
}

NOINLINE static uint8_t labs_(ps_t *s, rat_t *f, uint8_t k, uint8_t j, rat_t *a, int dir, int8_t inf, uint8_t q)
{
    uint8_t top = s->top, rc, rd, nb = s->st ? s->st[1] : 0, ns = 1;
    rat_t *g = get(s), *h = get(s);
    if (!h) return 0;
    if (!inf && !dir && !ssg(s, s->kb[k], j, a, 0, 0)) {        /* 0 at a: both sides */
        ns = 2;
        rc = lside(s, g, f, k, j, a, 1, 0, q);
        rd = lside(s, h, f, k, j, a, -1, 0, q);
        if (rc && rd && (rc != rd || (rc == 1 && !same2(g, h)))) rc = 4;
        else if (!rd) rc = 0;
    } else rc = lside(s, g, f, k, j, a, dir, inf, q);
    if (rc) *f = *g;
    if (rc == 1 && s->st && s->st[1] == nb + ns) stf(s, s->var[NV - 1 - j], SR_LANS, 0, "$", f);
    s->top = top;
    return rc;
}

/* (A + B√U)/D: B²U - A² over D(B√U - A); √U at a is a number, at ±∞ its leading term */
NOINLINE static uint8_t lconj(ps_t *s, rat_t *f, uint8_t k, uint8_t j, rat_t *a, int dir, int8_t inf, uint8_t q)
{
    uint8_t top = s->top, g = KN(s, k), rc = 0, m, ns;
    uint32_t bit = (uint32_t)1 << (RVAR + g);
    rat_t *A = get(s), *B = get(s), *E = get(s), *N = get(s), *t = get(s), *l = get(s), *D = B;
    const term_t *x;
    term_t y;
    if (!l) return 0;
    A->n.n = B->n.n = 0;
    for (x = f->n.t; x < f->n.t + f->n.n; x++) {
        y = *x;
        y.rad &= ~bit;
        if (!addterm(x->rad & bit ? &B->n : &A->n, &y)) goto done;
    }
    for (x = f->d.t; x < f->d.t + f->d.n; x++) if (x->rad & bit) goto done;
    if (!B->n.n) goto done;
    pconst(&A->d, 1); pconst(&B->d, 1);
    N->n = A->n;
    t->n = B->n;
    if (!mulinto(s, &N->n, &A->n) || !mulinto(s, &t->n, &B->n) || !addmono(&N->n, &t->n, 1u << 4 * g, 1) ||
        !unroot(s, &N->n)) goto done;
    pconst(&N->d, 1);
    E->n = A->n;                                /* A - B√U */
    for (x = B->n.t; x < B->n.t + B->n.n; x++) {
        y = *x;
        y.num = -y.num;
        y.rad |= bit;
        if (!addterm(&E->n, &y)) goto done;
    }
    pconst(&E->d, 1);
    if (inf) {                                  /* √U ~ √c·V^m, U = cV^2m + ... */
        x = lead(&s->ka[k]->n);
        m = vdeg(&s->ka[k]->n, j);
        if (m == 0xFF || m < 2 || m & 1 || !isconst(&s->ka[k]->d) || univ(&s->ka[k]->n) != 15u << 4 * j || x->num < 0 || x->rad != 1) goto done;
        rconst(l, 1);
        l->n.t[0].num = x->num; l->n.t[0].den = x->den;
        if (!sqrtinto(s, l) || !vpow(l, j, m / 2)) goto done;
        if (inf < 0 && m & 2) pneg(&l->n);
        *t = *B;
        if (!rmulinto(s, t, l, 0)) goto done;
        pneg(&t->n);
        *l = *A;
        if (!raddinto(s, l, t) || !l->n.n || vdeg(&l->n, j) != (vdeg(&A->n, j) > vdeg(&B->n, j) + m / 2 ? vdeg(&A->n, j) : vdeg(&B->n, j) + m / 2)) goto done;
    } else {
        *t = *s->ka[k];
        if (evalat(s, t, j, a) != 1 || nsg(s, t) != 1) goto done;
        *l = *E;
        if (evalat(s, l, j, a) != 1 || !l->n.n) goto done;
    }
    pneg(&N->n); pneg(&E->n); pneg(&l->n);     /* the conjugate B√U - A, so N is B²U - A² */
    D->n = f->d; pconst(&D->d, 1);
    if (isnum(D, 1)) stf(s, s->var[NV - 1 - j], SR_LCJ, "#", "$/#", E, N, E);
    else stf(s, s->var[NV - 1 - j], SR_LCJ, "#", "$/(&&)", E, N, D, E);
    N->d = f->d;
    *t = *N;
    if (!rmulinto(s, t, l, 1) || reduce(s, t) != 1) goto done;
    ns = s->st ? s->st[1] : 0;
    if ((rc = lv(s, t, j, a, dir, inf, q))) *f = *t;
    if (rc == 1 && s->st && s->st[1] == ns) stf(s, s->var[NV - 1 - j], SR_LANS, 0, "$", t);
done:
    s->top = top;
    return rc;
}

/* b^c with b, c kernel k's: e^(c ln b), or e^(c(b-1)) when b goes to 1; the limit of the exponent L, then e^L */
NOINLINE static uint8_t lpow(ps_t *s, rat_t *f, uint8_t k, uint8_t j, rat_t *a, int dir, int8_t inf, uint8_t q)
{
    uint8_t top = s->top, rc = 0, v = s->var[NV - 1 - j];
    rat_t *b = get(s), *g = get(s), *c = get(s), *t = get(s);
    int e;
    if (!t || f->n.n != 1 || !isconst(&f->d) || f->n.t[0].mono != 1u << 4 * KN(s, k) || (f->n.t[0].rad & ~RNUM)) goto done;
    c->n = f->n;
    c->n.t[0].mono = 0;
    pconst(&c->d, 1);
    *b = *s->ka[k];
    rc = lv(s, b, j, a, dir, inf, 1);
    rconst(t, -1);
    if (rc == 1 && isnum(b, 1)) {               /* 1 to the infinity */
        *g = *s->ka[k];
        if (!raddinto(s, g, t) || !rmulinto(s, g, s->kb[k], 0)) goto zero;
        stf(s, v, SR_LP1, 0, "$", g);
    } else if (rc == 2 || (rc == 1 && ((e = nsg(s, b)) > 0 || (!e && (dir > 0 || inf > 0))))) {
        if (!kvar(s, g, K_LOG, s->ka[k], 0) || !rmulinto(s, g, s->kb[k], 0)) goto zero;
        stf(s, v, SR_LPOW, 0, "$", g);
    } else goto zero;
    if ((rc = lv(s, g, j, a, dir, inf, 1)) == 1) {
        if (!g->n.n) rconst(t, 1);
        else if (!apply(s, t, K_EXP, g, b, 0)) goto zero;
        if (!rmulinto(s, t, c, 0)) goto zero;
        *f = *t;
        stf(s, v, SR_LEXP, 0, "$", f);
    } else if (rc == 2) {
        rc = nsg(s, c) > 0 ? 2 : 3;
    } else if (rc == 3) {
        rconst(f, 0);
        rc = 1;
    } else goto zero;
    goto done;
zero:
    rc = 0;
done:
    s->top = top;
    return rc;
}

/* how strong a term is at the end it goes to: its e^ rate, its power of V (in halves; 1/V counts for -2,
   and at a = 0 the sign is turned), its power of ln */
typedef struct { term_t r; int8_t p, l; } sk_t;
/* a kernel's kind: 1 e^(cV), 2 ln(cV^m) weighing w, 3 sin or cos that keeps swinging */
typedef struct { uint8_t c; int8_t w; term_t sl; } kc_t;

static int skcmp(const sk_t *a, const sk_t *b)
{
    int64_t d = (int64_t)a->r.num * b->r.den - (int64_t)b->r.num * a->r.den;
    if (d) return d < 0 ? -1 : 1;
    if (a->p != b->p) return a->p < b->p ? -1 : 1;
    return a->l == b->l ? 0 : a->l < b->l ? -1 : 1;
}

/* the strongest terms of p: their key, their coefficients added in cf, the first of them in lt; *osc: one holds a swinging kernel */
NOINLINE static uint8_t skey(ps_t *s, const poly_t *p, uint8_t j, int8_t inf, const kc_t *cl, sk_t *best, rat_t *cf, term_t *lt, uint8_t *osc)
{
    const term_t *x;
    sk_t kk;
    term_t c;
    uint8_t k, e, m, os, first = 1;
    int8_t w;
    int cmp;
    cf->n.n = 0;
    pconst(&cf->d, 1);
    for (x = p->t; x < p->t + p->n; x++) {
        kk.r.num = kk.l = os = 0; kk.r.den = 1; kk.r.mono = 0; kk.r.rad = 1;
        c = *x;
        e = x->mono >> 4 * j & 15;
        kk.p = 2 * e + (x->rad >> (RVAR + j) & 1);
        if (inf < 0) {
            if (kk.p & 1) return 0;
            if (e & 1) c.num = -c.num;
        }
        c.mono &= ~(15u << 4 * j);
        c.rad &= ~((uint32_t)1 << (RVAR + j));
        if (!inf) kk.p = -kk.p;
        for (k = 0; k < s->nk; k++) {
            m = x->mono >> 4 * KN(s, k) & 15;
            if (!m || !cl[k].c) continue;
            c.mono &= ~(15u << 4 * KN(s, k));
            if (cl[k].c == 1) { if (!radd(&kk.r, m * cl[k].sl.num, cl[k].sl.den)) return 0; }
            else if (cl[k].c == 2) {
                kk.l += m;
                for (w = cl[k].w; m--; ) {
                    if (!cmul(&c, w < 0 ? -w : w, 1)) return 0;
                    if (w < 0) c.num = -c.num;
                }
            } else os = 1;
        }
        cmp = first ? 1 : skcmp(&kk, best);
        first = 0;
        if (cmp > 0) { *best = kk; *lt = *x; cf->n.n = 0; *osc = 0; }
        else if (cmp) continue;
        *osc |= os;
        if (!addterm(&cf->n, &c)) return 0;
    }
    return 1;
}

/* f by strength at +∞, -∞ or 0 from the right: the kernels with a limit are put in (tan⁻¹(V) goes to ±π/2), the
   rest are e^(cV), ln(cV^m) or sin and cos of something that has none; then N and D by their strongest terms */
NOINLINE static uint8_t lscale(ps_t *s, rat_t *f, uint8_t j, rat_t *a, int dir, int8_t inf, uint8_t q)
{
    uint8_t top = s->top, k, h, rc = 0, lr, ch = 0, ub = 0, v = s->var[NV - 1 - j], on = 0, od = 0;
    kc_t cl[NV];
    rat_t *x = get(s), *y = get(s), *z = get(s), *g = get(s), *cn = x, *cd = y;
    sk_t kn, kd;
    term_t ln, ld;
    uint8_t *sv;
    int sg, df;
    if (!g || (!inf && (a->n.n || (s->nk && dir < 0)))) return 0;
    *g = *f;
    for (k = 0; k < s->nk; k++) {
        cl[k].c = 0;
        h = s->kf[k];
        if (!kdep(s, k, j) || !(vdeg(&g->n, KN(s, k)) || vdeg(&g->d, KN(s, k)))) continue;
        if ((h > K_LOG && h < K_ASIN) || h == K_E || s->kb[k]) goto done;
        *x = *s->ka[k];                         /* x, y, z side by side: lcore's p, p[1], p[2] */
        sv = s->st; s->st = 0;
        lr = lcore(s, C_LIMIT, x, j, a, dir, inf, 0);
        s->st = sv;
        if (lr == 5) lr = 1;
        if (lr == 1) *x = *y;
        sg = lr == 1 ? nsg(s, x) : 2;
        if (lr == 2 || lr == 3) {
            if (h == K_ATAN) {                  /* ±π/2 */
                if (!kvar(s, y, K_PI, 0, 0)) goto done;
                y->n.t[0].den = 2;
                if (lr == 3) y->n.t[0].num = -1;
                goto put;
            }
            if (h == K_EXP) {
                if (!inf || s->ka[k]->n.n != 1 || !rate(s->ka[k], j, &cl[k].sl)) goto done;
                if (inf < 0) cl[k].sl.num = -cl[k].sl.num;
                cl[k].c = 1;
                ub = 1;
                continue;
            }
        }
        if (h == K_LOG && ((lr == 2 && inf > 0) || (lr == 1 && !sg && (inf > 0 || dir > 0)))) {   /* ln of c·V^m to 0 or ∞ */
            const rat_t *u = s->ka[k];
            if (u->n.n != 1 || u->d.n != 1 || u->n.t[0].num < 0 || u->n.t[0].rad != 1 || u->d.t[0].rad != 1 ||
                univ(&u->n) & ~(15u << 4 * j) || univ(&u->d) & ~(15u << 4 * j)) goto done;
            cl[k].w = (int8_t)((u->n.t[0].mono >> 4 * j & 15) - (u->d.t[0].mono >> 4 * j & 15));
            if (!inf) cl[k].w = -cl[k].w;
            cl[k].c = 2;
            ub = 1;
            continue;
        }
        if ((h == K_SIN || h == K_COS) && lr && lr != 1) { cl[k].c = 3; ub = 1; continue; }
        if (lr != 1 || (h == K_LOG && sg < 1)) goto done;
        if (!apply(s, y, h, x, z, 0)) goto done;
put:
        rconst(z, 1);
        z->n.t[0].mono = 1u << 4 * KN(s, k);
        stf(s, v, SR_LSIN, "$,$", 0, z, y);
        if (subst(s, g, KN(s, k), y) != 1) goto done;
        ch = 1;
    }
    rc = 0;
    if (!ch && !ub) goto done;
    if (!ub) {
        if ((rc = lv(s, g, j, a, dir, inf, q))) *f = *g;
        goto done;
    }
    if (!skey(s, &g->n, j, inf, cl, &kn, cn, &ln, &on) || !skey(s, &g->d, j, inf, cl, &kd, cd, &ld, &od) || od ||
        !cn->n.n || !cd->n.n) goto done;
    z->n.n = z->d.n = 1;
    z->n.t[0] = ln; z->d.t[0] = ld;
    stf(s, v, SR_LGROW, "$", 0, z);
    df = skcmp(&kn, &kd);
    if (df < 0) {
        rconst(f, 0);
        rc = 1;
        stf(s, v, on ? SR_LSQ : SR_LGZ, 0, "0");
    } else if (on) {
        rc = 4;
        stn(s, v, SR_LOSC);
    } else if (df > 0) {
        if ((sg = nsg(s, cn)) > 1 || (df = nsg(s, cd)) > 1 || (!inf && !dir)) goto done;
        rc = sg * df > 0 ? 2 : 3;
        stn(s, v, SR_LGI);
    } else if (rmulinto(s, cn, cd, 1)) {
        *f = *cn;
        rc = 1;
        stf(s, v, SR_LGE, 0, "$", f);
    }
done:
    s->top = top;
    return rc;
}

NOINLINE static uint8_t lalt(ps_t *s, rat_t *f, uint8_t j, rat_t *a, int dir, int8_t inf, uint8_t q)
{
    uint8_t k, rc = 0;
    const term_t *x, *e;
    for (k = 0; k < s->nk && !rc; k++) {
        if (!kdep(s, k, j)) continue;
        if (s->kf[k] == K_ROOT) {               /* √ and abs sit in the rad bit */
            for (x = f->n.t, e = f->n.t + f->n.n; x < e && !(x->rad >> (RVAR + KN(s, k)) & 1); x++);
            if (x == e) continue;
        } else if (!(vdeg(&f->n, KN(s, k)) || vdeg(&f->d, KN(s, k)))) continue;
        if (s->kf[k] == K_POW) rc = lpow(s, f, k, j, a, dir, inf, q);
        else if (s->kf[k] == K_ROOT) rc = s->kb[k] ? labs_(s, f, k, j, a, dir, inf, q) : lconj(s, f, k, j, a, dir, inf, q);
    }
    return rc ? rc : lscale(s, f, j, a, dir, inf, q);
}

NOINLINE static uint8_t lv(ps_t *s, rat_t *f, uint8_t j, rat_t *a, int dir, int8_t inf, uint8_t q)
{
    uint8_t top = s->top, *st = s->st, rc = 0;
    rat_t *p = get(s);
    if (get(s) && get(s)) {
        *p = *f;
        if (q) s->st = 0;
        rc = lcore(s, C_LIMIT, p, j, a, dir, inf, 0);
        s->st = st;
        if (rc) { *f = p[1]; if (rc == 5) rc = 1; }
    }
    s->top = top;
    return rc ? rc : lalt(s, f, j, a, dir, inf, q);
}


NOINLINE static uint8_t calc2(ps_t *s, uint8_t cmd, const char *name, uint8_t *out, rat_t *p, uint8_t j,
                              rat_t *a, rat_t *b, uint8_t o, int8_t inf, const rat_t *raw)
{
    rat_t *r = p + 2, *u = 0;
    int v;
    uint8_t n = 0, k = 0, nk = s->nk, ku = 0, pf, top0 = s->top;
#ifdef NOLS
    if (cmd == C_LIMIT || cmd == C_DOMINANTTERM || cmd == C_SUM || cmd == C_PRODUCT) return 0;
#endif
#ifdef NOINT
    if (cmd == C_INTEGRAL) return 0;
#endif
again:
    for (; k < nk; k++)                         /* √(P(V)): LIMIT at a point, INTEGRAL at P = aV+b */
        if (s->kf[k] == K_ROOT && cmd != C_LIMIT && dep(s, s->ka[k], j)) {
            if (cmd != C_INTEGRAL) return 0;
            if (u || !(u = lsub(s, p, j, ku = k))) goto us;
        }
    k = 0;
    if (u) usteps(s, p, j, ku);
    pf = cmd == C_INTEGRAL && s->st && !u && vdeg(&p->d, j) && !used(&p->n, vkern(s, j)) && vdeg(&p->n, j) != 0xFF && p->n.n;
    if (pf) pfsteps(s, p, j);
    if (cmd == C_LIMIT || cmd == C_DOMINANTTERM) {
        n = clim(s, cmd, name, out, p, j, a, b, inf, raw);
        if (n || cmd != C_LIMIT) return n;
        s->top = s->lt;                         /* the other cases: f again, its steps over, the cancelling fraction's slots free */
        s->i = s->i0;
        if (s->st) streset(s->st);
        if (!expr(s, p) || !reduce(s, p)) return 0;
        v = b ? nsign(b) : 0;
        if (!(n = lv(s, p, j, a, v, inf, 0))) return 0;
        if (n == 1) return result(s, p, out, p + 1, inf != 0);
        return error(out, n == 4 ? "UNDEFINED" : n == 2 ? "+INFINITY" : "-INFINITY", name, "");
    }
    if (cmd == C_SUM || cmd == C_PRODUCT) return csum(s, cmd, name, out, p, j, a, b, inf);
    if (cmd == C_FMIN || cmd == C_FMAX) return cfmin(s, cmd, name, out, p, j, a, b);
    if (cmd == C_ARCLEN) return carc(s, name, out, p, j, a, b);
    if (cmd == C_SERIES) return cser(s, out, p, j, a, o);
    if (cmd == C_INTEGRAL && s->st && !u && !rpsteps(s, p, j, a, b, 1) && !trsteps(s, p, j, a, b, 1) &&
        !vdeg(&p->d, j) && !used(&p->d, vkern(s, j)) && p->n.n) isteps(s, p, j, a, b, 1);
    rconst(r, 0);
    s->la = a; s->lb = b;
    v = integ(s, p + 1, p, j, u ? 0 : a, u ? 0 : b, !(a && (kdepx(s, a) || kdepx(s, b))), a && !u ? 0 : r);
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
    if (pf && v == 1) pfdone(s, p + 1, r, j, a, b);
#ifndef NOSTEPSX
    if (u && v == 1 && s->st) { stf(s, s->var[NV - 1 - j], SR_IUBACK, "$", "$", s->ka[ku], p + 1); if (a) ilim2(s, p + 1, j, a, b); else ic(s, p + 1, s->var[NV - 1 - j]); }
#endif
    if (v == 1 && a && defint(s, p + 1, j, a, b) != 1) return 0;  /* ln(X) from 0: improper, refused */
    if (v == 0 && cmd == C_INTEGRAL && !u) goto us;
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
us:                                             /* no tabular answer: a u-substitution */
    s->top = top0;                              /* f again: integ() ate it */
    s->i = s->i0;
    if (!expr(s, p) || !reduce(s, p)) return 0;
    rconst(r, 0);
    if (s->st) streset(s->st);
    if (ipow(s, p + 1, p, j, a != 0)) stf(s, s->var[NV - 1 - j], SR_IANY, "$", "$", p, p + 1);
    else if (iasin(s, p + 1, p, j)) stf(s, s->var[NV - 1 - j], SR_IASIN, 0, "$", p + 1);
    else if (!usub(s, p + 1, p, j, a != 0)) {
        if (s->q || cmd != C_INTEGRAL) return 0;
        s->q = 1;                               /* the rational ones left, with an irreducible quadratic: ln and tan⁻¹ */
        k = ku = 0; u = 0; nk = s->nk; n = 0;
        goto again;
    }
    if (s->st) { if (a) ilim2(s, p + 1, j, a, b); else ic(s, p + 1, s->var[NV - 1 - j]); }
    if (a && defint(s, p + 1, j, a, b) != 1) return 0;
    return result(s, p + 1, out, r, 0);
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
    rat_t *p, *q, *r, *w, *z, *c, *e[2] = {0, 0}, *raw = 0;
    term_t f;
#ifndef NOSTEPSX
    unsigned st0, en0;
#endif
    uint8_t imp = 0;
    uint8_t j, y = 0xFF, m, k, o = 1, lo, taylor = cmd == C_TAYLOR || cmd == C_SERIES;
    int8_t inf = 0;
    int v;

    s->fn = C_TOLN;
    s->q = 0;
    s->la = s->lb = 0;
    s->kn = cmd == C_DERIV ? KX_NEST | KX_INV | KX_POW : cmd == C_INTEGRAL ? KX_NEST | KX_INV | KX_POW : cmd == C_LIMIT ? KX_INV | KX_POW : cmd == C_SUM ? KX_INV | KX_POW | KX_ALT : 0;
    if (!(p = get(s)) || !(q = get(s)) || !(r = get(s)) || !(w = get(s))) return 0;
#ifndef NOSTEPSX
    st0 = s->i;
#endif
    s->i0 = s->i;
    if (!expr(s, p)) return 0;
#ifndef NOSTEPSX
    en0 = s->i;
#endif
    if (cmd == C_IMPDIF && peek(s) == T_EQ) {   /* f = g is f - g = 0 */
        imp = 1;
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
        else if (m == 1 && cmd == C_SUM && (inf = infty(s))) { if (inf < 0) goto bad; rconst(e[m], 0); }
        else if (!expr(s, e[m]) || !reduce(s, e[m])) return 0;
    }
    if (peek(s) == T_RPAR) s->i++;
    if (peek(s) == T_COMMA) goto bad;
    if (s->i != s->len) return 0;
    s->lt = s->top;
#ifndef NOSTEPSX
    if (s->st && cmd == C_LIMIT && !inf && (raw = get(s)) && (!get(s) || !lraw(s, st0, en0, raw))) raw = 0;   /* LIMIT's steps: the fraction before it cancels */
#endif
    if (!reduce(s, p)) return 0;
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
            if ((s->kf[k] <= K_TAN || s->kf[k] >= K_ASIN) && s->ka[k] && (dep(s, s->ka[k], j) || (y != 0xFF && dep(s, s->ka[k], y))))
                return error(out, "MODE", "USE RADIAN", "");
    if (cmd >= C_INTEGRAL)
        return calc2(s, cmd, name, out, p, j, taylor ? c : e[0], taylor ? 0 : e[1], o, inf, raw);
    v = 0;
    f.mono = 0; f.rad = 1; f.num = 1;

    if (cmd == C_CENTRALDIFF) {                 /* (f(V+h) - f(V-h))/(2h) */
        if (!c->n.n) return error(out, "DIVIDE BY 0", "", "");
        *q = *p;
        if (!vplus(w, c, j, 1) || (v = evalat(s, q, j, w)) != 1 ||
            !vplus(w, c, j, 0) || (v = evalat(s, p, j, w)) != 1) goto at;
        stc(s, 0, SR_CDP, "$", q);
        stc(s, 0, SR_CDM, "$", p);
        pneg(&p->n);
        f.den = 2;
        if (!raddinto(s, p, q) || !rmulinto(s, p, c, 1) || !scale(&p->n, &f)) return 0;
        stc(s, 0, SR_CDF, "$", p);
    } else if (cmd == C_IMPDIF) {               /* y' = -F_V/F_Y; y'' = dy'/dV + dy'/dY·y' */
        if (imp) stc(s, 0, SR_IMF, "$=0", p);
        if (!drat(s, q, p, j) || !drat(s, r, p, y)) return 0;
        stc(s, 0, SR_IMX, "$", q);
        stc(s, 0, SR_IMY, "$", r);
        if (!r->n.n) return error(out, "DIVIDE BY 0", "", "");
        if (!rmulinto(s, q, r, 1)) return 0;
        pneg(&q->n);
        *p = *q;
        stc(s, 0, SR_IMD, "$", p);
        while (--o) {
            if (!drat(s, r, p, j) || !drat(s, w, p, y) || !rmulinto(s, w, q, 0) || !raddinto(s, r, w))
                return 0;
            *p = *r;
            stc(s, 0, SR_IMN, "$", p);
        }
    } else if (taylor) {                        /* the sum of f⁽ᵏ⁾(a)·(V-a)ᵏ/k! */
        rat_t *d = get(s), *x = get(s);
        if (!x || !vplus(w, c, j, 0)) return 0;
        rconst(r, 0);
        rconst(d, 1);
        for (k = 0; ; k++) {
            *x = *p;
            if ((v = evalat(s, x, j, c)) != 1) goto at;
            tyk(s, j, x, d, c, k);
            if (!rmulinto(s, x, d, 0) || !raddinto(s, r, x)) return 0;
            if (k == o) break;
            f.den = k + 1;
            if (!drat(s, q, p, j) || !rmulinto(s, d, w, 0) || !scale(&d->n, &f)) return 0;
            *p = *q;
        }
        *p = *r;
        stc(s, 0, SR_TYS, "$", p);
    } else if (cmd == C_TANGENTLINE || cmd == C_NORMALLINE) {  /* f(a) + f'(a)(V-a), f(a) - (V-a)/f'(a) */
        *q = *p;
        if (s->st) tlsteps(s, p, j, c, cmd);
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
            if (s->st && !s->st[2]) dord(s, p, j, k + 1, o);
            if (!drat(s, q, p, j)) return 0;
            *p = *q;
        }
        if (cmd == C_DERIVAT && (v = evalat(s, p, j, c)) != 1) goto at;
        if (cmd == C_DERIVAT) stf(s, s->var[NV - 1 - j], SR_KAT, "@=$", "$", c, p);
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

#ifdef NOSTEPSX     /* the ENGTEST build keeps none of the step code */
#define gsteps(...) ((void)0)
#define gfinal(...) ((void)0)
#define gput(...) ((void)0)
#else
/* ---- stepsgeo: steps( for GEOMETRY and MATRIX. A step is a rule (menu.c's text says the formula) and an
   expression with this entry's numbers put in, then worked out. The steps are computed on copies, before the
   command's own code runs, so its answer is untouched; and cost nothing when s->st is 0. ---- */
#define GFAIL 0xFF

/* The tokens of format f at e + n, or GFAIL when they do not fit. Digits, X Y R as they are; ( ) , + - * / = as
   tokens, ^ squared, $ sqrt(, ~ negative, | abs(, # sin(, @ cos(; r the next rat, q the same in brackets unless it is a
   plain number, g + or - and its size, h the other way round (X-3, X+3); n the next int; a ';' ends the
   parameter (*pn is the length so far). */
NOINLINE static uint8_t vgf(const ps_t *s, uint8_t *e, uint8_t n, const char *f, va_list ap, uint8_t *pn)
{
    static const char sym[] = "(),+-*/=^$~|#";
    static const uint8_t tok[] = { T_LPAR, T_RPAR, T_LCOMMA, T_ADD, T_SUB, T_MUL, T_DIV, T_EQ, T_SQR, T_SQRT, T_NEG,
                                   T_ABS, T_SIN };
    static const uint8_t cosl[7] = { T_BB, 0xB2, T_BB, 0xBF, T_BB, 0xC3, T_LPAR };
    rat_t *r;
    const char *y;
    uint8_t c, m, i, ng, gh;
    for (; n != GFAIL && (c = (uint8_t)*f); f++) {
        if (c == 'r' || c == 'q' || c == 'g' || c == 'h') {
            r = va_arg(ap, rat_t *);
            gh = c == 'g' || c == 'h';
            ng = gh && nsign(r) < 0;
            if (gh) {
                if (n >= MAXOUT) return GFAIL;
                e[n++] = (ng ^ (c == 'h')) ? T_SUB : T_ADD;
            }
            if (ng) pneg(&r->n);
            m = answer(s, r, e, n);
            if (ng) pneg(&r->n);
            if (!m) return GFAIL;
            if (c != 'r') {                     /* brackets round all but a plain number */
                for (i = n; i < m && e[i] != T_ADD && e[i] != T_SUB && e[i] != T_NEG && (gh || (e[i] != T_DIV &&
                            e[i] != T_SQRT)); i++) ;
                if (i < m) {
                    if (m + 2 > MAXOUT) return GFAIL;
                    for (i = m; i > n; i--) e[i] = e[i - 1];
                    e[n] = T_LPAR; e[m + 1] = T_RPAR; m += 2;
                }
            }
            n = m;
        } else if (c == 'n') {
            m = (uint8_t)va_arg(ap, int);
            if (n + 2 > MAXOUT) return GFAIL;
            if (m > 9) e[n++] = (uint8_t)(0x30 + m / 10);
            e[n++] = (uint8_t)(0x30 + m % 10);
        } else if (c == ';') *pn = n;
        else if (c == '@') {
            if (n + 7 > MAXOUT) return GFAIL;
            for (i = 0; i < 7; i++) e[n++] = cosl[i];
        } else {
            if (n >= MAXOUT) return GFAIL;
            for (y = sym; *y && *y != (char)c; y++) ;
            e[n++] = *y ? tok[y - sym] : c >= 'A' && c <= 'Z' ? (uint8_t)(c - 'A' + T_VAR0) : c;
        }
    }
    return n;
}

/* the tokens of a format appended at e + n */
NOINLINE static uint8_t gfm(const ps_t *s, uint8_t *e, uint8_t n, const char *f, ...)
{
    va_list ap;
    uint8_t pn;
    va_start(ap, f);
    n = vgf(s, e, n, f, ap, &pn);
    va_end(ap);
    return n;
}

/* A step: the rule, the parameter p[pn], and the expression e[en] (tokens; "a=a" is a). The room for
   the answer behind it is kept. */
NOINLINE static void gput(ps_t *s, uint16_t rule, const uint8_t *p, uint8_t pn, const uint8_t *e, uint8_t en)
{
    uint8_t *st = s->st, i, k;
    if (!st || st[2]) return;
    if ((st[68] | st[69] << 8) + pn + en + MAXOUT + 8 > STCAP) { st[2] |= 1; return; }
    for (i = 0; i < en && e[i] != T_EQ; i++) ;
    for (k = 0; i < en && en == 2 * i + 1 && k < i && e[k] == e[i + 1 + k]; k++) ;
    if (i < en && en == 2 * i + 1 && k == i) en = i;
    stlog(st, rule, p, pn, e, en, 1);
}

/* a step from a format: the parameter first, then ';', then the expression */
NOINLINE static void gst(ps_t *s, uint16_t rule, const char *f, ...)
{
    uint8_t e[MAXOUT], n, pn = 0;
    va_list ap;
    if (!s->st || s->st[2]) return;
    va_start(ap, f);
    n = vgf(s, e, 0, f, ap, &pn);
    va_end(ap);
    if (n == GFAIL) s->st[2] |= 1;
    else gput(s, rule, e, pn, e + pn, n - pn);
}
#define GS(r, ...) gst(s, SR_G##r, __VA_ARGS__)

/* l = c0 + c1X + c2Y [+ c3X² + c4Y²], gcoef()'s coefficients as one polynomial to print */
NOINLINE static int gsum(ps_t *s, rat_t *l, rat_t **c, uint8_t nc)
{
    uint8_t k, i;
    term_t t;
    l->n.n = 0;
    pconst(&l->d, 1);
    for (k = 0; k < nc; k++)
        for (i = 0; i < c[k]->n.n; i++) {
            t = c[k]->n.t[i];
            t.mono += (unsigned)((k == 1) + 2 * (k == 3)) << 4 * JX(s) | (unsigned)((k == 2) + 2 * (k == 4)) << 4 * JY(s);
            if (!addterm(&l->n, &t)) return 0;
        }
    return 1;
}

/* the line c0 + c1X + c2Y as "aX+bY+c=0" at e + n: no zero terms, no 1 in front of a letter */
NOINLINE static uint8_t gabc(const ps_t *s, uint8_t *e, uint8_t n, rat_t **c)
{
    uint8_t k, m, i, v, f = 1;
    for (k = 1; k < 4 && n != GFAIL; k++) {
        v = k < 3 ? (uint8_t)(T_VAR0 + 0x16 + k) : 0;       /* X, Y, then the plain number */
        if (!c[k % 3]->n.n) continue;
        m = n;
        if ((n = gfm(s, e, n, "g", c[k % 3])) == GFAIL) break;
        if (v && n == m + 2 && e[m + 1] == 0x31) n--;       /* 1X is X */
        if (v) {
            if (n >= MAXOUT) return GFAIL;
            e[n++] = v;
        }
        if (f && e[m] == T_SUB) e[m] = T_NEG;               /* a first term has no sign but a minus */
        else if (f) {
            for (i = m; ++i < n; ) e[i - 1] = e[i];
            n--;
        }
        f = 0;
    }
    return n == GFAIL ? n : gfm(s, e, n, "=0");
}

/* a step with the line c (and d, after a comma) as aX+bY+c=0 */
NOINLINE static void glog(ps_t *s, uint16_t rule, rat_t **c, rat_t **d)
{
    uint8_t e[MAXOUT], n;
    if (!s->st || s->st[2]) return;
    n = gabc(s, e, 0, c);
    if (d && n != GFAIL) n = gabc(s, e, gfm(s, e, n, ","), d);
    if (n == GFAIL) s->st[2] |= 1;
    else gput(s, rule, 0, 0, e, n);
}

/* e = the line of the points as far from P_i as from P_j: 2(xj-xi)X + 2(yj-yi)Y + |Pi|²-|Pj|² */
NOINLINE static int gbis(ps_t *s, rat_t **e, rat_t **a, rat_t **b, uint8_t i, uint8_t j)
{
    rat_t *t = get(s);
    int ok;
    if (!t) return 0;
    rconst(t, 2);
    ok = radd3(s, e[1], a[j], a[i], 1) && rmulinto(s, e[1], t, 0) && radd3(s, e[2], b[j], b[i], 1) &&
         rmulinto(s, e[2], t, 0) && sumsq(s, e[0], a[i], b[i]) && sumsq(s, t, a[j], b[j]) && radd3(s, e[0], e[0], t, 1);
    s->top--;
    return ok;
}

/* e = the line through corner i at right angles to side jl: (xl-xj)(X-xi) + (yl-yj)(Y-yi) */
NOINLINE static int galt(ps_t *s, rat_t **e, rat_t **a, rat_t **b, uint8_t i, uint8_t j, uint8_t l)
{
    rat_t *t = get(s);
    int ok;
    if (!t) return 0;
    ok = radd3(s, e[1], a[l], a[j], 1) && radd3(s, e[2], b[l], b[j], 1);
    *t = *e[1];
    *e[0] = *e[2];
    ok = ok && rmulinto(s, t, a[i], 0) && rmulinto(s, e[0], b[i], 0) && raddinto(s, e[0], t);
    pneg(&e[0]->n);
    s->top--;
    return ok;
}

/* (u, v) where two perpendicular bisectors (alt 0), or two altitudes, of the triangle meet: the steps, and 1 */
NOINLINE static int gcentre(ps_t *s, rat_t **a, rat_t **b, uint8_t alt, rat_t *u, rat_t *v)
{
    rat_t *c[3], *d[3], **e = c;
    uint8_t k;
    for (k = 0; k < 2; k++, e = d) {
        if (!(e[0] = get(s)) || !(e[1] = get(s)) || !(e[2] = get(s))) return 0;
        if (!(alt ? galt(s, e, a, b, k, (k + 1) % 3, (k + 2) % 3) : gbis(s, e, a, b, k, k + 1))) return 0;
        glog(s, alt ? SR_GALT : SR_GBIS, e, 0);
    }
    if (gsolve(s, u, v, c, d) != 1) return 0;
    GS(CROSS, "(r,r)", u, v);
    return 1;
}

/* lines and points: DISTANCE .. REFLECT, MIDPOINT .. PARTITION, HYPOT, LEG */
NOINLINE static void gline(ps_t *s, uint8_t cmd, unsigned sg, rat_t **a, rat_t **b)
{
    rat_t *c[3], *d[3], *z[3], *p, *q, *r, *u, *v, *w, **e;
    uint8_t top = s->top, k;
    if (!(p = get(s)) || !(q = get(s)) || !(r = get(s)) || !(u = get(s)) || !(v = get(s)) || !(w = get(s))) goto full;
    switch (cmd) {
    case C_DISTANCE:
        if (sg == G2(G_PT, G_PT)) {
            if (!radd3(s, p, a[1], a[0], 1) || !radd3(s, q, b[1], b[0], 1) || !sumsq(s, r, p, q)) goto full;
            GS(DIST, "$(q-q)^+(q-q)^)", a[1], a[0], b[1], b[0]);
            GS(DSUB, "$q^+q^)", p, q);
            GS(SQ, "$r)", r);
        } else if (sg == G2(G_PT, G_EQ) && gcoef(s, a[1], c, 3)) {
            if (!lval(s, p, c, a[0], b[0]) || !sumsq(s, q, c[1], c[2])) goto full;
            glog(s, SR_GSTD, c, 0);
            GS(PL, "|q*q+q*q+q)/$q^+q^)", c[1], a[0], c[2], b[0], c[0], c[1], c[2]);
            GS(CALC, "|r)/$r)", p, q);
        }
        break;
    case C_SLOPE:
        if (sg == G2(G_PT, G_PT)) {
            if (!radd3(s, p, b[1], b[0], 1) || !radd3(s, q, a[1], a[0], 1) || !q->n.n) goto full;
            *r = *p;
            if (!rmulinto(s, r, q, 1)) goto full;
            GS(SLOPE, "(q-q)/(q-q)=r", b[1], b[0], a[1], a[0], r);
        } else if (sg == G_EQ && gcoef(s, a[0], c, 3) && c[2]->n.n) {
            *r = *c[1];
            pneg(&r->n);
            if (!rmulinto(s, r, c[2], 1)) goto full;
            glog(s, SR_GSTD, c, 0);
            GS(LSL, "~q/q=r", c[1], c[2], r);
        }
        break;
    case C_PARALLEL: case C_PERPENDICULAR:
        if (sg != G2(G_PT, G_EQ) || !gcoef(s, a[1], c, 3)) break;
        glog(s, SR_GSTD, c, 0);
        k = cmd == C_PERPENDICULAR;
        if (!c[2]->n.n) {                       /* a vertical line */
            if (k) GS(VPERP, "Y=r", b[0]);
            else GS(VPAR, "X=r", a[0]);
            break;
        }
        if (k && !c[1]->n.n) { GS(HPERP, "X=r", a[0]); break; }
        *p = *c[1];
        pneg(&p->n);
        if (!rmulinto(s, p, c[2], 1)) goto full;
        GS(LSL, "~q/q=r", c[1], c[2], p);
        if (k) {
            rconst(q, -1);
            if (!rmulinto(s, q, p, 1)) goto full;
            GS(PERP, "~1/q=r", p, q);
            p = q;
        }
        if (k) GS(PTS, "Yh=q*(Xh)", b[0], p, a[0]);
        else GS(PTSP, "Yh=q*(Xh)", b[0], p, a[0]);
        break;
    case C_INTERSECT:
        if (sg != G2(G_EQ, G_EQ) || !gcoef(s, a[0], c, 3) || !gcoef(s, a[1], d, 3) || gsolve(s, u, v, c, d) != 1) break;
        glog(s, SR_GINT, c, d);
        if (!cross(s, p, c[1], d[2], d[1], c[2]) || !cross(s, q, c[0], d[2], d[0], c[2])) goto full;
        rconst(w, 0);
        z[0] = q; z[1] = p; z[2] = w;
        glog(s, SR_GELIM, z, 0);
        GS(SOLV, "X=r", u);
        e = c[2]->n.n ? c : d;                  /* a line with a Y in it */
        *p = *e[1];
        *q = *e[0];
        if (!rmulinto(s, p, u, 0) || !raddinto(s, q, p)) goto full;
        z[0] = q; z[1] = w; z[2] = e[2];
        glog(s, SR_GBACK, z, 0);
        *p = *q;
        pneg(&p->n);
        if (!rmulinto(s, p, e[2], 1)) goto full;
        GS(SOLV, "Y=r", p);
        break;
    case C_REFLECT:
        if (sg != G2(G_PT, G_EQ) || !gcoef(s, a[1], c, 3) || !lval(s, p, c, a[0], b[0]) ||
            !sumsq(s, q, c[1], c[2])) break;
        glog(s, SR_GSTD, c, 0);
        GS(REFV, "q*q+q*q+q=r", c[1], a[0], c[2], b[0], c[0], p);
        GS(REFN, "q^+q^=r", c[1], c[2], q);
        *r = *p;
        rconst(u, 2);
        if (!rmulinto(s, r, q, 1) || !rmulinto(s, r, u, 0)) goto full;
        GS(REFP, "(r,r)-q*(r,r)", a[0], b[0], r, c[1], c[2]);
        break;
    case C_MIDPOINT:
        if (sg == G2(G_PT, G_PT)) GS(MID, "((qg)/2,(qg)/2)", a[0], a[1], b[0], b[1]);
        break;
    case C_PARTITION:
        if (sg != (G2(G_PT, G_PT) | G2(G_NUM, G_NUM) << 4) || !radd3(s, r, a[2], a[3], 0) || !r->n.n) break;
        *p = *a[2];
        if (!rmulinto(s, p, r, 1)) goto full;
        GS(SECT, "q/(q+q)=r", a[2], a[2], a[3], p);
        GS(SECP, "(q+q*(q-q),q+q*(q-q))", a[0], p, a[1], a[0], b[0], p, b[1], b[0]);
        break;
    case C_LINE:
        if (sg == G2(G_PT, G_NUM)) { GS(PTS, "Yh=q*(Xh)", b[0], a[1], a[0]); break; }
        if (sg != G2(G_PT, G_PT) || !radd3(s, p, a[1], a[0], 1) || !radd3(s, q, b[1], b[0], 1)) break;
        if (!p->n.n) {
            if (q->n.n) GS(VERT, "X=r", a[0]);
            break;
        }
        *r = *q;
        if (!rmulinto(s, r, p, 1)) goto full;
        GS(SLOPE, "(q-q)/(q-q)=r", b[1], b[0], a[1], a[0], r);
        GS(PTS, "Yh=q*(Xh)", b[0], r, a[0]);
        break;
    case C_PERPBISECTOR:
        if (sg != G2(G_PT, G_PT) || !radd3(s, p, a[1], a[0], 1) || !radd3(s, q, b[1], b[0], 1) ||
            (!p->n.n && !q->n.n)) break;
        GS(MID, "((qg)/2,(qg)/2)", a[0], a[1], b[0], b[1]);
        rconst(r, 1);
        r->n.t[0].den = 2;
        if (!radd3(s, u, a[0], a[1], 0) || !rmulinto(s, u, r, 0) || !radd3(s, v, b[0], b[1], 0) ||
            !rmulinto(s, v, r, 0)) goto full;
        GS(CALC, "(r,r)", u, v);
        if (!q->n.n) { GS(BV, "X=r", u); break; }
        if (!p->n.n) { GS(BH, "Y=r", v); break; }
        *w = *q;
        if (!rmulinto(s, w, p, 1)) goto full;
        GS(SLOPE, "(q-q)/(q-q)=r", b[1], b[0], a[1], a[0], w);
        rconst(r, -1);
        if (!rmulinto(s, r, w, 1)) goto full;
        GS(PERP, "~1/q=r", w, r);
        GS(PTS, "Yh=q*(Xh)", v, r, u);
        break;
    case C_HYPOT: case C_LEG:
        if (sg != G2(G_NUM, G_NUM)) break;
        k = cmd == C_LEG;
        *p = *a[0];
        *q = *a[1];
        if (!rmulinto(s, p, a[0], 0) || !rmulinto(s, q, a[1], 0) || !radd3(s, r, p, q, k)) goto full;
        if (k) { GS(LEG, "$q^-q^)", a[0], a[1]); GS(SQM, "$r)", r); }
        else { GS(HYP, "$q^+q^)", a[0], a[1]); GS(SQ, "$r)", r); }
        break;
    }
    goto done;
full:
    s->st[2] |= 1;
done:
    s->top = top;
}

/* AREA, PERIMETER, CENTROID, INCENTER: the corners (a, b), m of them */
NOINLINE static void gpgon(ps_t *s, uint8_t cmd, unsigned sg, rat_t **a, rat_t **b, uint8_t m)
{
    uint8_t e[MAXOUT], top = s->top, k, i, n = 0;
    rat_t *p, *q, *r, *u, *c[3];
    if (!(p = get(s)) || !(q = get(s)) || !(r = get(s)) || !(u = get(s))) goto full;
    if (cmd == C_INCENTER) {
        if (sg != G3(G_PT, G_PT, G_PT)) goto done;
        rconst(r, 0);
        for (k = 0; k < 3; k++)
            if (!(c[k] = get(s)) || !dist2(s, c[k], a, b, (k + 1) % 3, (k + 2) % 3) || !sqrtinto(s, c[k]) ||
                !radd3(s, r, r, c[k], 0)) goto full;
        GS(INSD, "r,r,r", c[0], c[1], c[2]);
        for (i = 0; i < 2; i++) {               /* x, then y */
            rat_t **z = i ? b : a;
            rconst(p, 0);
            for (k = 0; k < 3; k++) {
                *u = *c[k];
                if (!rmulinto(s, u, z[k], 0) || !radd3(s, p, p, u, 0)) goto full;
            }
            *q = *p;
            if (!rmulinto(s, q, r, 1)) goto full;
            GS(INSW, "(q*q+q*q+q*q)/(q+q+q)=r", c[0], z[0], c[1], z[1], c[2], z[2], c[0], c[1], c[2], q);
        }
        goto done;
    }
    if (m < 3 || sg != (0xAAAu & ((1u << 2 * m) - 1))) goto done;
    if (cmd == C_CENTROID) {
        n = gfm(s, e, 0, "((");
        for (k = 0; k < m; k++) n = gfm(s, e, n, k ? "g" : "q", a[k]);
        n = gfm(s, e, n, ")/n,(", m);
        for (k = 0; k < m; k++) n = gfm(s, e, n, k ? "g" : "q", b[k]);
        n = gfm(s, e, n, ")/n)", m);
        if (n == GFAIL) goto full;
        gput(s, SR_GCEN, 0, 0, e, n);
        goto done;
    }
    rconst(p, 0);
    for (k = 0; k < m; k++) {
        i = k + 1 < m ? k + 1 : 0;
        if (cmd == C_AREA) {
            if (!cross(s, r, a[k], b[i], a[i], b[k])) goto full;
            GS(SHOE, "q*q-q*q=r", a[k], b[i], a[i], b[k], r);
        } else {
            if (!dist2(s, r, a, b, k, i) || !sqrtinto(s, r)) goto full;
            GS(SIDE, "$(q-q)^+(q-q)^)=r", a[i], a[k], b[i], b[k], r);
        }
        *u = *r;
        if (!raddinto(s, p, u)) goto full;
        n = gfm(s, e, n, k ? "g" : "q", r);
    }
    n = gfm(s, e, n, "=r", p);
    if (n == GFAIL) goto full;
    gput(s, cmd == C_AREA ? SR_GSHSUM : SR_GPER, 0, 0, e, n);
    if (cmd == C_AREA) {                        /* half the size of the total */
        rconst(r, 2);
        *q = *p;
        if (!rmulinto(s, q, r, 1) || nsign(q) > 1) goto full;
        if (nsign(q) < 0) pneg(&q->n);
        GS(SHA, "|q)/2=r", p, q);
    }
    goto done;
full:
    s->st[2] |= 1;
done:
    s->top = top;
}

/* CIRCLE */
NOINLINE static void gcir(ps_t *s, unsigned sg, rat_t **a, rat_t **b)
{
    rat_t *c[5], *g[5], *p, *q, *r, *u, *v, *w, *t, *z;
    uint8_t top = s->top;
    if (!(p = get(s)) || !(q = get(s)) || !(r = get(s)) || !(u = get(s)) || !(v = get(s))) goto full;
    if (sg == G2(G_PT, G_NUM)) {
        GS(CIR, "(Xh)^+(Yh)^=q^", a[0], b[0], a[1]);
    } else if (sg == G2(G_PT, G_PT)) {
        if (!dist2(s, r, a, b, 0, 1)) goto full;
        GS(CIRR, "(q-q)^+(q-q)^=r", a[1], a[0], b[1], b[0], r);
        GS(CIR, "(Xh)^+(Yh)^=r", a[0], b[0], r);
    } else if (sg == G3(G_PT, G_PT, G_PT)) {
        if (!gcentre(s, a, b, 0, u, v)) goto done;
        if (!radd3(s, p, a[0], u, 1) || !radd3(s, q, b[0], v, 1) || !sumsq(s, r, p, q)) goto full;
        GS(CIRR, "(q-q)^+(q-q)^=r", a[0], u, b[0], v, r);
        GS(CIR, "(Xh)^+(Yh)^=r", u, v, r);
    } else if ((sg == G_EQ || sg == G_NUM) && gcoef(s, a[0], c, 5)) {
        if (!(w = get(s)) || !(t = get(s)) || !(z = get(s))) goto full;
        *p = *c[1]; *q = *c[2]; *r = *c[0];     /* each over the X² coefficient */
        if (!rmulinto(s, p, c[3], 1) || !rmulinto(s, q, c[3], 1) || !rmulinto(s, r, c[3], 1)) goto full;
        rconst(u, 1);
        rconst(v, 0);
        g[3] = g[4] = u; g[1] = p; g[2] = q;
        if (!isnum(c[3], 1)) {
            g[0] = r;
            if (!gsum(s, w, g, 5)) goto full;
            GS(CN1, "r=0", w);
        }
        g[0] = v;
        *z = *r;
        pneg(&z->n);                            /* the plain number across: -r */
        if (!gsum(s, w, g, 5)) goto full;
        GS(CN2, "r=r", w, z);
        rconst(t, 2);
        *w = *p; *c[0] = *q;                    /* halves in w, c[0]; c[] is not needed again */
        if (!rmulinto(s, w, t, 1) || !rmulinto(s, c[0], t, 1) || !sumsq(s, t, w, c[0])) goto full;
        *r = *z;
        if (!raddinto(s, t, r)) goto full;      /* t = -r + w² + c0² */
        GS(CN3, "r+q^+q^=r", z, w, c[0], t);
        GS(CN4, "(Xg)^+(Yg)^=r", w, c[0], t);
    }
    goto done;
full:
    s->st[2] |= 1;
done:
    s->top = top;
}

/* ROTATE and DILATE about a centre, the origin if none */
NOINLINE static void grot(ps_t *s, uint8_t cmd, unsigned sg, rat_t **a, rat_t **b)
{
    rat_t *p, *q, *r, *u, *v, *w, *x, *cx, *cy;
    uint8_t top = s->top, k, i, z;
    int32_t g;
    if (!(p = get(s)) || !(q = get(s)) || !(r = get(s)) || !(u = get(s)) || !(v = get(s)) || !(w = get(s)) ||
        !(cx = get(s)) || !(cy = get(s))) goto full;
    rconst(cx, 0);
    rconst(cy, 0);
    if (sg == G3(G_PT, G_NUM, G_PT)) { cx = a[2]; cy = b[2]; }
    else if (sg != G2(G_PT, G_NUM)) goto done;
    z = cx->n.n || cy->n.n;
    if (!radd3(s, p, a[0], cx, 1) || !radd3(s, q, b[0], cy, 1)) goto full;
    if (z) GS(ROT0, "(q-q,q-q)=(r,r)", a[0], cx, b[0], cy, p, q);
    if (cmd == C_DILATE) {
        *u = *p; *v = *q;
        if (!rmulinto(s, u, a[1], 0) || !rmulinto(s, v, a[1], 0)) goto full;
        GS(DIL, "(q*q,q*q)=(r,r)", a[1], p, a[1], q, u, v);
    } else {
        if (!intval(a[1], &g)) goto done;
        g %= 360;
        if (g < 0) g += 360;
        if ((i = g % 90) % 30 && i != 45) goto done;
        cosd(u, i); cosd(v, 90 - i);            /* cos and sin of the angle, then a quarter turn each */
        for (k = g / 90; k; k--) { x = u; u = v; v = x; pneg(&u->n); }
        GS(ROTCS, "@r)=r,#r)=r", a[1], u, a[1], v);
        if (!cross(s, r, p, u, q, v)) goto full;
        GS(ROTX, "q*q-q*q=r", p, u, q, v, r);
        if (!(x = get(s))) goto full;
        *w = *p; *x = *q;
        if (!rmulinto(s, w, v, 0) || !rmulinto(s, x, u, 0) || !raddinto(s, w, x)) goto full;
        GS(ROTY, "q*q+q*q=r", p, v, q, u, w);
        u = r; v = w;
    }
    if (z) {
        if (!radd3(s, p, u, cx, 0) || !radd3(s, q, v, cy, 0)) goto full;
        GS(ROTC, "(q+q,q+q)=(r,r)", u, cx, v, cy, p, q);
    }
    goto done;
full:
    s->st[2] |= 1;
done:
    s->top = top;
}

/* the steps of a Geometry command, before it runs; the pool is as it was after */
NOINLINE static void gsteps(ps_t *s, uint8_t cmd, unsigned sg, rat_t **a, rat_t **b, uint8_t m)
{
    uint8_t top = s->top;
    rat_t *u, *v;
    if (cmd == C_CIRCLE) gcir(s, sg, a, b);
    else if (cmd == C_ROTATE || cmd == C_DILATE) grot(s, cmd, sg, a, b);
    else if (cmd == C_AREA || cmd == C_PERIMETER || cmd == C_CENTROID || cmd == C_INCENTER) gpgon(s, cmd, sg, a, b, m);
    else if (cmd == C_CIRCUMCENTER || cmd == C_ORTHOCENTER) {
        if (sg == G3(G_PT, G_PT, G_PT) && (u = get(s)) && (v = get(s))) gcentre(s, a, b, cmd == C_ORTHOCENTER, u, v);
    } else gline(s, cmd, sg, a, b);
    s->top = top;
}

/* the last step is the answer, or ends in it? else it is "Write the answer" (an equation) or "= the answer" */
NOINLINE static void gfinal(ps_t *s, const uint8_t *out, uint8_t n)
{
    uint8_t *st = s->st, *w, i, k, c, pl;
    unsigned u = STHDR, l = 0;
    if (!st || st[2] || !st[1]) return;
    for (i = 0; i < st[1]; i++) { l = u; pl = st[u + 1] & 127; u += 3 + pl + st[u + 2 + pl]; }
    w = st + l + 3 + (st[l + 1] & 127);                 /* the last step's expression */
    c = st[l + 2 + (st[l + 1] & 127)];
    if (c >= n) {
        for (k = 0; k < n && w[c - n + k] == (out[k] == T_LCOMMA ? T_COMMA : out[k]); k++) ;
        if (k == n && (c == n || w[c - n - 1] == T_EQ)) return;
    }
    for (k = 0; k < n && out[k] != T_EQ; k++) ;
    gput(s, k < n ? SR_GSIMP : SR_GVAL, 0, 0, out, n);
}

#endif

NOINLINE static uint8_t geo0(ps_t *s, uint8_t cmd, const char *name, uint8_t *out)
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

    if (s->st) gsteps(s, cmd, sg, a, b, m);
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

#ifdef NOSTEPSX
#define geo geo0
#else
NOINLINE static uint8_t geo(ps_t *s, uint8_t cmd, const char *name, uint8_t *out)
{
    uint8_t n = geo0(s, cmd, name, out);
    if (s->st && n && n != SYMCE_ERR) gfinal(s, out, n);
    return n;
}
#endif

/* SOLVE with a root: isolate it (the terms with the root bit rb on the left), square both sides */
#ifdef NOSTEPSX
#define rsiso(s, p, rb)
#define rssq(s, x, y)
#else
NOINLINE static void rsiso(ps_t *s, const rat_t *p, uint32_t rb)
{
    uint8_t top = s->top, i;
    rat_t *l = get(s), *r = get(s);
    term_t t;
    if (!s->st || !r || !isconst(&p->d)) goto done;
    l->n.n = r->n.n = 0;
    for (i = 0; i < p->n.n; i++) {
        t = p->n.t[i];
        if (t.rad & rb) addterm(&l->n, &t);
        else { t.num = -t.num; addterm(&r->n, &t); }
    }
    pconst(&l->d, 1); pconst(&r->d, 1);
    stepa(s, SR_RISO, 0, l, r);
done:
    s->top = top;
}

NOINLINE static void rssq(ps_t *s, const poly_t *x, const poly_t *y)
{
    uint8_t top = s->top;
    rat_t *l = get(s), *r = get(s);
    if (!s->st || !r) goto done;
    l->n = *x; r->n = *y;
    pconst(&l->d, 1); pconst(&r->d, 1);
    stepa(s, SR_RSQ, 0, l, r);
    lsteps(s, l, r);
done:
    s->top = top;
}
#endif

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
    rsiso(s, p, rb);
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
    if (isconst(&a[3].n)) rssq(s, &p->n, b);
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

/* gcd of a >= 0 and b */
static int64_t g64(int64_t a, int64_t b)
{
    int64_t z;
    if (b < 0) b = -b;
    while (b) { z = a % b; a = b; b = z; }
    return a;
}

/* M, nn rows by nn+1 columns: Gauss-Jordan without fractions (Bareiss). On
   success every diagonal entry is the determinant, and M[i][nn] over it the
   solution. 0 when singular, or when an entry passes 2^31 (the products
   then still fit 64 bits). */
NOINLINE static int pfsolve(int64_t (*M)[9], uint8_t nn)
{
    int64_t pv = 1, d, e;
    uint8_t i, k, t;
    for (k = 0; k < nn; k++) {
        for (i = k; i < nn && !M[i][k]; i++) ;
        if (i == nn) return 0;
        for (t = 0; i != k && t <= nn; t++) { d = M[i][t]; M[i][t] = M[k][t]; M[k][t] = d; }
        for (i = 0; i < nn; i++) {
            if (i == k) continue;
            d = M[i][k];
            for (t = 0; t <= nn; t++) {
                e = (M[i][t] * M[k][k] - d * M[k][t]) / pv;
                if (e >= (int64_t)1 << 31 || e <= -((int64_t)1 << 31)) return 0;
                M[i][t] = e;
            }
        }
        pv = M[k][k];
    }
    return 1;
}

/* PARTFRAC(N/D,V): N/D in V alone, rational coefficients, as the polynomial
   part and one term r/f^k for each irreducible factor f of D and each power k
   up to its multiplicity, r of lower degree than f. The coefficients of the
   r's are the unknowns of one linear system, a row per power of V in the
   remainder's numerator, solved in whole numbers by pfsolve(). The terms are
   written as FACTOR writes a fraction: 1/(2(X-1))-1/(2(X+1)), (2X+3)/(X²+1). */
NOINLINE static uint8_t apart(ps_t *s, rat_t *p, uint8_t j, uint8_t *out)
{
    fl_t fl, ft;
    rat_t *r;
    poly_t *q, *a, *c, *rr;
    int64_t (*M)[9], u, d, ln = 1, wd[8];
    uint8_t n = 0, nn = 0, k, m, i, t, kk, col, o[MAXF], dg[MAXF], cb[MAXF];
    const term_t *x;
    term_t f;
    int32_t l, g;
    unsigned sh = 4 * j;

    for (k = 0; k < 2; k++)
        for (x = (k ? &p->d : &p->n)->t; x < (k ? &p->d : &p->n)->t + (k ? &p->d : &p->n)->n; x++)
            if (x->rad != 1 || x->mono & ~(15u << sh)) return 0;
    if (!p->n.n) { PUT(0x30); return n; }
    if (isconst(&p->d) || !whole(p)) return answer(s, p, out, 0);
    if (!(q = tmp(s)) || !(a = tmp(s)) || !(c = tmp(s)) || !(rr = tmp(s))) return 0;
    q->n = 0; *a = p->n;
    if (!rem(a, &p->d, q)) return 0;            /* N = Q·D + a, a of lower degree */
    *c = p->d;
    fl.n = 0; fl.split = 0;
    fl.c.num = fl.c.den = 1; fl.c.mono = 0; fl.c.rad = 1;
    for (k = 0; k < MAXF; k += 2) {
        if (!(r = get(s))) return 0;
        fl.f[k] = &r->n; fl.f[k + 1] = &r->d;
    }
    if (!fac(s, &fl, c, 0) || !(r = get(s))) return 0;
    M = (void *)r;
    for (k = 0; k < fl.n; k++) { dg[k] = vdeg(fl.f[k], j); cb[k] = nn; nn += dg[k] * (fl.e[k] & 0x7F); }
    if (nn > 8 || nn != vdeg(&p->d, j)) return 0;
    for (k = 0; k < nn; k++) for (i = 0; i <= nn; i++) M[k][i] = 0;
    for (k = 0; k < fl.n; k++)                  /* column of factor k, power kk, term t */
        for (kk = 1; kk <= (fl.e[k] & 0x7F); kk++) {
            pconst(c, 1);                       /* D / f^kk, D = c·(product of f^e) */
            for (i = 0; i < fl.n; i++)
                for (m = i == k ? (fl.e[i] & 0x7F) - kk : fl.e[i] & 0x7F; m--; )
                    if (!mulinto(s, c, fl.f[i])) return 0;
            for (t = 0; t < dg[k]; t++)
                for (x = c->t; x < c->t + c->n; x++) {
                    i = (x->mono >> sh & 15) + t;
                    if (x->den != 1) return 0;
                    if (i < nn) M[i][cb[k] + (kk - 1) * dg[k] + t] = x->num;
                }
        }
    for (k = 0; k < nn; k++) wd[k] = 1;         /* a/c over a common denominator ln */
    for (x = a->t; x < a->t + a->n; x++) {
        k = x->mono >> sh & 15;
        u = (int64_t)x->num * fl.c.den;
        d = (int64_t)x->den * fl.c.num;
        if (d < 0) { d = -d; u = -u; }
        t = (g = (int32_t)g64(u < 0 ? -u : u, d)) != 0;
        if (k >= nn || !t || u / g >= (int64_t)1 << 31 || u / g <= -((int64_t)1 << 31) || d / g >= (int64_t)1 << 31)
            return 0;
        M[k][nn] = u / g;
        wd[k] = d / g;
    }
    for (k = 0; k < nn; k++) {                  /* ln = lcm of wd */
        ln = ln / g64(ln, wd[k]) * wd[k];
        if (ln >= (int64_t)1 << 31) return 0;
    }
    for (k = 0; k < nn; k++) M[k][nn] *= ln / wd[k];
    if (!pfsolve(M, nn)) return 0;

    if (q->n && !(n = emit(s, q, out, n))) return 0;
    for (k = 0; k < fl.n; k++) {                /* FACTOR's order */
        o[k] = k;
        for (i = k; i && after(&fl, o[i - 1], o[i]); i--) { t = o[i]; o[i] = o[i - 1]; o[i - 1] = t; }
    }
    ft.c.mono = 0; ft.c.rad = 1; ft.split = 0; ft.f[0] = rr;
    for (m = 0; m < fl.n; m++)
        for (k = o[m], kk = 1; kk <= (fl.e[k] & 0x7F); kk++) {
            rr->n = 0;
            col = cb[k] + (kk - 1) * dg[k];
            for (t = 0; t < dg[k]; t++) {       /* r's coefficient: M[col+t][nn] / (det·ln) */
                u = M[col + t][nn];
                d = M[col + t][col + t] * ln;
                if (d < 0) { d = -d; u = -u; }
                if (!u) continue;
                g = (int32_t)g64(u < 0 ? -u : u, d);
                u /= g; d /= g;
                if (u > LIM || u < -LIM || d > LIM) return 0;
                f.mono = (unsigned)t << sh; f.rad = 1; f.num = (int32_t)u; f.den = (int32_t)d;
                if (!addterm(rr, &f)) return 0;
            }
            if (!rr->n) continue;
            l = 1; g = 0;
            if (!lg(rr, &l, &g)) return 0;
            u = lead(rr)->num < 0;              /* the sign goes between the terms */
            if (n) PUT(u ? T_SUB : T_ADD); else if (u) PUT(T_NEG);
            f.mono = 0; f.rad = 1;
            if (!rset(&f, u ? -l : l, g) || !scale(rr, &f)) return 0;
            ft.c.den = l;
            if (isconst(rr)) { ft.c.num = g; ft.n = 1; ft.f[0] = fl.f[k]; ft.e[0] = 0x80 | kk; }
            else {
                ft.c.num = 1; ft.n = 2; ft.f[0] = rr; ft.e[0] = 1; ft.f[1] = fl.f[k]; ft.e[1] = 0x80 | kk;
                if (!rset(&f, g, 1) || !scale(rr, &f)) return 0;
            }
            if (!(n = putfl(s, &ft, out, n))) return 0;
        }
    return n;
}

/* An inequality with one abs(S) in it, A + B abs(S) over a polynomial D, A and B
   numbers: its sign is B times that of S² - c², c = -A/B, when c >= 0; B's alone when
   c < 0 (abs(S) is never below it). p becomes that, a polynomial over D, for ineq().
   0 when it is not that shape (abs(S) times X, two kernels). */
NOINLINE static int absfix(ps_t *s, rat_t *p)
{
    uint32_t rb = 0, m;
    uint8_t i, k, kr = 0;
    term_t a, b, t;
    int32_t x, y;
    for (i = 0; i < p->n.n; i++) {
        if (!(m = p->n.t[i].rad & ~(IMAG | RNUM)) || m == rb) continue;
        for (k = 0; k < s->nk && m != (uint32_t)1 << (RVAR + KN(s, k)); k++) ;
        if (k == s->nk || s->kf[k] != K_ROOT || !s->kb[k] || rb) return 0;
        rb = m; kr = k;
    }
    if (!rb) return 1;
    a.num = 0; a.den = 1; b = a;
    for (i = 0; i < p->n.n; i++) {
        t = p->n.t[i];
        if (t.mono) return 0;
        if (t.rad & rb) { t.rad &= ~rb; b = t; }
        else a = t;
        if (t.rad != 1) return 0;
    }
    if (hasrad(&p->d) || !b.num) return 0;
    if (a.num && (a.num > 0) == (b.num > 0)) { pconst(&p->n, b.num < 0 ? -1 : 1); return 1; }
    p->n = s->ka[kr]->n;
    if (a.num) {
        if (!mulc(a.num, a.num, &x) || !mulc(b.den, b.den, &y) || !mulc(x, y, &x) ||
            !mulc(a.den, a.den, &y) || !mulc(b.num, b.num, &t.den) || !mulc(y, t.den, &y) ||
            !rset(&t, -x, y)) return 0;
        t.mono = 0; t.rad = 1;
        if (!addterm(&p->n, &t)) return 0;
    }
    if (b.num < 0) pneg(&p->n);
    return 1;
}

/* SOLVE(A op B,V), op one of < > ≤ ≥ ≠ (the TI test tokens 6B..6F): p = A-B, a
   fraction in V alone with rational coefficients. A sign chart over the exact
   real roots of numerator and denominator, as fac() finds them: right of the
   last root p has the sign of its leading terms, and each root of odd power
   flips it. Atoms 2i (the interval left of root i) and 2i+1 (root i itself,
   in the set only where p is 0, never at a pole) are merged into runs:
   X<-2 or X>2, -1≤X≤3, X≠1, X=1. Everything is ALWAYS TRUE, nothing NO
   SOLUTION. A factor cancelled by reduce() is lost (its hole too). */
NOINLINE static uint8_t ineq(ps_t *s, rat_t *p, uint8_t j, uint8_t op, uint8_t *out)
{
    fl_t fl;
    struct { int64_t re; uint8_t k; } key[MAXF], kt;
    rat_t *r;
    unsigned inc = 0, all, a, b;
    uint8_t n = 0, k, m, cnt = 0, neg, runs = 0;
    const term_t *x;
    int eq = op == T_EQ + 3 || op == T_EQ + 4;  /* ≤ ≥ keep the zeros */
#define PROOT(i) do { if (!root(s, r, fl.f[key[i].k], j) || !(n = putroot(s, r, out, n))) return 0; } while (0)

    for (k = 0; k < 2; k++)
        for (x = (k ? &p->d : &p->n)->t; x < (k ? &p->d : &p->n)->t + (k ? &p->d : &p->n)->n; x++)
            if (x->rad != 1 || x->mono & ~(15u << 4 * j)) return 0;
    if (!p->n.n) return eq ? error(out, "ALWAYS TRUE", "", "") : error(out, "NO SOLUTION", "", "");
    neg = (lead(&p->n)->num < 0) ^ (lead(&p->d)->num < 0);
    fl.n = 0; fl.split = 2;
    fl.c.num = fl.c.den = 1; fl.c.mono = 0; fl.c.rad = 1;
    for (k = 0; k < MAXF; k += 2) {
        if (!(r = get(s))) return 0;
        fl.f[k] = &r->n; fl.f[k + 1] = &r->d;
    }
    if (!fac(s, &fl, &p->n, 0) || (!isconst(&p->d) && !fac(s, &fl, &p->d, 1))) return 0;
    if (!(r = get(s))) return 0;
    for (k = 0; k < fl.n; k++) {
        if (!(m = vdeg(fl.f[k], j))) continue;
        if (m == 1) {
            if (!root(s, r, fl.f[k], j)) return 0;
            kt.k = k; kt.re = approx(&r->n, 0);
            for (m = cnt++; m && key[m - 1].re > kt.re; m--) key[m] = key[m - 1];
            key[m] = kt;
        } else if (!noroot(fl.f[k], j)) return 0;
    }
    all = (1u << (2 * cnt + 1)) - 1;
    for (k = cnt; ; k--) {
        inc |= (unsigned)(op == T_EQ + 5 || (op == T_EQ + 1 || op == T_EQ + 3 ? neg : !neg)) << 2 * k;
        if (!k) break;
        m = fl.e[key[k - 1].k];
        neg ^= m & 1;
        inc |= (unsigned)(eq && !(m & 0x80)) << (2 * k - 1);
    }
    if (inc == all) return error(out, "ALWAYS TRUE", "", "");
    if (!inc) return error(out, "NO SOLUTION", "", "");
    a = all & ~inc;
    if (!(a & (a - 1)) && a & 0xAAAAAA) {       /* one point missing: X≠r */
        for (b = 1; !(a >> b & 1); b += 2) ;
        PUT(s->var[NV - 1 - j]); PUT(T_EQ + 5);
        PROOT(b >> 1);
        return n;
    }
    for (a = 0; a <= 2 * cnt; a = b + 1) {
        if (!(inc >> a & 1)) { b = a; continue; }
        for (b = a; b < 2 * cnt && inc >> (b + 1) & 1; b++) ;
        if (runs++) PUT(T_OR);
        if (a == b && a & 1) {                  /* a lone zero */
            PUT(s->var[NV - 1 - j]); PUT(T_EQ);
            PROOT(a >> 1);
        } else if (!a) {                        /* X<hi, X≤hi */
            PUT(s->var[NV - 1 - j]); PUT(b & 1 ? T_EQ + 3 : T_EQ + 1);
            PROOT(b >> 1);
        } else if (b == 2 * cnt) {              /* X>lo, X≥lo */
            PUT(s->var[NV - 1 - j]); PUT(a & 1 ? T_EQ + 4 : T_EQ + 2);
            PROOT((a - 1) >> 1);
        } else {                                /* lo<X<hi, lo≤X≤hi */
            PROOT((a - 1) >> 1);
            PUT(a & 1 ? T_EQ + 3 : T_EQ + 1);
            PUT(s->var[NV - 1 - j]);
            PUT(b & 1 ? T_EQ + 3 : T_EQ + 1);
            PROOT(b >> 1);
        }
    }
    return n;
}
#undef PROOT

/* ---- SOLVE({A=B,..},{X,..}): a system ----
   Every letter not in the second list is a stored value (see the top), else
   a letter that stays. Linear in the unknowns with numbers for coefficients:
   Gauss-Jordan over the rationals, 1..6 unknowns, any number of equations;
   X=2 and Y=1, an unknown left free is the others' X=(3-Y)/2, no answer is
   NO SOLUTION. Else two equations in two unknowns where one is linear in an
   unknown with no other unknown beside it: that one solved and put into the
   other, which is then one SOLVE in the remaining unknown (letters among
   the coefficients stay: {AX+Y=1,X-Y=2}; X²+Y²=25 with X-Y=1 gives both
   points, by the first unknown's value). A set too wide for " and " is
   written {X=2,Y=1}, sets apart by commas. */
#define MAXU 6
typedef char mat_fits[MAXU * (MAXU + 1) * sizeof(term_t) <= sizeof(rat_t) ? 1 : -1];

/* The values v[k] (0: free) of the unknowns uj[k], `V=value` apiece */
static uint8_t putset(const ps_t *s, uint8_t *out, uint8_t n, const uint8_t *uj, uint8_t nu,
                      rat_t *const *v, uint8_t lst)
{
    uint8_t k, more = 0;
    if (lst) PUT(T_LBRACE);
    for (k = 0; k < nu; k++) {
        if (!v[k]) continue;
        if (more) PUT(lst ? T_LCOMMA : T_AND);
        more = 1;
        PUT(s->var[NV - 1 - uj[k]]);
        PUT(T_EQ);
        if (!(n = putroot(s, v[k], out, n))) return 0;
    }
    if (lst) PUT(T_RBRACE);
    return n;
}

/* a linear system: the row r of M (nu unknowns) as a step, `rule` on equation r+1 */
#ifdef NOSTEPSX
#define rowstep(s, rule, M, r, nu, uj)
#else
NOINLINE static void rowstep(ps_t *s, uint16_t rule, term_t (*M)[MAXU + 1], uint8_t r, uint8_t nu, const uint8_t *uj)
{
    uint8_t top = s->top, c;
    rat_t *p, *a, *b;
    term_t t;
    if (!s->st || s->st[2] || !(p = get(s)) || !(a = get(s)) || !(b = get(s))) return;
    t.num = r + 1; t.den = 1; t.mono = 0; t.rad = 1;
    p->n.n = a->n.n = b->n.n = 0;
    addterm(&p->n, &t);
    for (c = 0; c < nu; c++) {
        t = M[r][c]; t.mono = (unsigned)1 << 4 * uj[c]; t.rad = 1;
        if (t.num) addterm(&a->n, &t);
    }
    t = M[r][nu]; t.mono = 0; t.rad = 1;
    addterm(&b->n, &t);
    pconst(&p->d, 1); pconst(&a->d, 1); pconst(&b->d, 1);
    stepa(s, rule, p, a, b);
    s->top = top;
}
#endif

/* numbers only: the rows of e (ne equations in nu unknowns) as a matrix */
NOINLINE static uint8_t linsys(ps_t *s, uint8_t *out, rat_t *const *e, uint8_t ne, uint8_t nu, const uint8_t *uj)
{
    term_t (*M)[MAXU + 1] = (term_t (*)[MAXU + 1])get(s), f, h, g, *t;
    rat_t *v[MAXU];
    uint8_t r, c, k, pr = 0, pc[MAXU], n, lst;
    if (!M) return 0;
    for (r = 0; r < ne; r++) {
        for (c = 0; c <= nu; c++) { M[r][c].num = 0; M[r][c].den = 1; M[r][c].mono = 0; M[r][c].rad = 1; }
        for (t = e[r]->n.t; t < e[r]->n.t + e[r]->n.n; t++) {
            for (c = 0; c < nu && t->mono != (unsigned)1 << 4 * uj[c]; c++) ;
            if (!radd(&M[r][c], c == nu ? -t->num : t->num, t->den)) return 0;   /* c == nu: the constant, to the right */
        }
    }
    for (c = 0; c < nu && pr < ne; c++) {
        for (r = pr; r < ne && !M[r][c].num; r++) ;
        if (r == ne) continue;
        for (k = 0; r != pr && k <= nu; k++) { f = M[r][k]; M[r][k] = M[pr][k]; M[pr][k] = f; }
        if (r != pr) rowstep(s, SR_RPIV, M, pr, nu, uj);
        rinv(&f, &M[pr][c]);
        f.mono = 0;
        for (k = 0; k <= nu; k++) if (!rmul(&M[pr][k], &M[pr][k], &f)) return 0;
        if (f.num != 1 || f.den != 1) rowstep(s, SR_RDIV, M, pr, nu, uj);
        for (r = 0; r < ne; r++) {
            if (r == pr || !M[r][c].num) continue;
            g = M[r][c];
            for (k = 0; k <= nu; k++) {
                h.mono = 0;
                if (!rmul(&h, &M[pr][k], &g) || !radd(&M[r][k], -h.num, h.den)) return 0;
            }
            rowstep(s, SR_RELIM, M, r, nu, uj);
        }
        pc[pr++] = c;
    }
    for (r = pr; r < ne; r++) if (M[r][nu].num) return error(out, "NO SOLUTION", "", "");
    if (!pr) return error(out, "ALWAYS TRUE", "", "");
    for (k = 0; k < nu; k++) {                  /* each pivot unknown: its row's right side less the free ones */
        for (r = 0; r < pr && pc[r] != k; r++) ;
        v[k] = 0;
        if (r == pr) continue;
        if (!(v[k] = get(s))) return 0;
        v[k]->n.n = 0;
        pconst(&v[k]->d, 1);
        h.mono = 0; h.rad = 1;
        for (c = 0; c <= nu; c++) {
            for (n = 0; n < pr && pc[n] != c; n++) ;
            if (c < nu && n < pr) continue;     /* a pivot's own column */
            h.num = c == nu ? M[r][c].num : -M[r][c].num; h.den = M[r][c].den;
            h.mono = c == nu ? 0 : (unsigned)1 << 4 * uj[c];
            if (!addterm(&v[k]->n, &h)) return 0;
        }
    }
    for (lst = 0; lst < 2; lst++)
        if ((n = putset(s, out, 0, uj, nu, v, lst)) && (width(out, n, s->mp) <= SCR_WIDTH || lst)) return n;
    return 0;
}

/* p is linear in unknown ja, with no other unknown (jb) beside it, and has no root */
static int solvable(const rat_t *p, uint8_t ja, uint8_t jb)
{
    const term_t *x;
    uint8_t found = 0;
    if (!isconst(&p->d) || hasrad(&p->n)) return 0;
    for (x = p->n.t; x < p->n.t + p->n.n; x++) {
        if ((x->mono >> 4 * ja & 15) > 1) return 0;
        if (x->mono >> 4 * ja & 15) { found = 1; if (x->mono >> 4 * jb & 15) return 0; }
    }
    return found;
}

#ifdef NOSTEPSX
#define ssteps(s, v, g, j, o)
#else
/* two equations, one of them solvable for the unknown at nibble j (equation o+1): that, then put in the other */
NOINLINE static void ssteps(ps_t *s, rat_t *v, rat_t *g, uint8_t j, uint8_t o)
{
    uint8_t top = s->top;
    rat_t *p = get(s), *x = get(s), *z = get(s);
    term_t t;
    if (!s->st || !z) goto done;
    t.num = o + 1; t.den = 1; t.mono = 0; t.rad = 1;
    p->n.n = x->n.n = z->n.n = 0;
    addterm(&p->n, &t);
    t.num = 1; t.mono = 1u << 4 * j;
    addterm(&x->n, &t);
    pconst(&p->d, 1); pconst(&x->d, 1); pconst(&z->d, 1);
    stepa(s, SR_NSOL, p, x, v);
    stepa(s, SR_NSUB, 0, g, z);
done:
    s->top = top;
}
#endif

NOINLINE static uint8_t nonlin(ps_t *s, uint8_t cmd, uint8_t *out, rat_t *const *e, const uint8_t *uj)
{
    rat_t *v, *g, *r, *w, *vals[2];
    poly_t *a;
    fl_t fl;
    struct { int64_t re, im; uint8_t k; } key[MAXF], kt;
    uint8_t ia, k, m, cnt = 0, lst, n, o = 0;
    if (s->nk || !isconst(&e[0]->d) || !isconst(&e[1]->d)) return 0;
    for (ia = 1; ia < 2; ia--) {                /* uj[1] first: the answer is then sorted by the first unknown */
        for (o = 0; o < 2 && !solvable(e[o], uj[ia], uj[1 - ia]); o++) ;
        if (o < 2) break;
    }
    if (ia > 1) return 0;
    if (!(v = get(s)) || !(g = get(s)) || !root(s, v, &e[o]->n, uj[ia])) return 0;
    *g = *e[1 - o];
    if (subst(s, g, uj[ia], v) != 1) return 0;
    a = &g->n;
    ssteps(s, v, g, uj[ia], o);
    vals[ia] = v; vals[1 - ia] = 0;
    if (!a->n) {                                /* the same equation twice: one is free */
        for (lst = 0; lst < 2; lst++)
            if ((n = putset(s, out, 0, uj, 2, vals, lst)) && (width(out, n, s->mp) <= SCR_WIDTH || lst)) return n;
        return 0;
    }
    if (!(m = vdeg(a, uj[1 - ia])) && isconst(a)) return error(out, "NO SOLUTION", "", "");
    if (!m || m == 0xFF) return 0;
    fl.n = 0;
    fl.c.num = fl.c.den = 1; fl.c.mono = 0; fl.c.rad = 1;
    fl.split = cmd == C_CSOLVE ? 6 : 2;
    for (k = 0; k < MAXF; k += 2) {
        if (!(r = get(s))) return 0;
        fl.f[k] = &r->n; fl.f[k + 1] = &r->d;
    }
    if (m == 1) { *fl.f[0] = *a; fl.e[0] = 1; fl.n = 1; }
    else if (!fac(s, &fl, a, 0)) return 0;
    if (!(r = get(s)) || !(w = get(s))) return 0;
    if (s->st && vdeg(a, uj[1 - ia]) > 1) psteps(s, g, &fl, uj[1 - ia], 0, out);
    for (k = 0; k < fl.n; k++) {
        if (!(m = vdeg(fl.f[k], uj[1 - ia]))) continue;
        if (m == 1) {
            if (!root(s, r, fl.f[k], uj[1 - ia])) return 0;
            kt.k = k;
            kt.im = univ(a) == MULTI ? 0 : approx(&r->n, IMAG);
            kt.re = univ(a) == MULTI ? k : approx(&r->n, 0) + (kt.im ? (int64_t)1 << 58 : 0);
            for (m = cnt++; m && (key[m - 1].re > kt.re || (key[m - 1].re == kt.re && key[m - 1].im > kt.im)); m--)
                key[m] = key[m - 1];
            key[m] = kt;
        } else if (fl.split & 4 || univ(fl.f[k]) == MULTI || !noroot(fl.f[k], uj[1 - ia])) return 0;
    }
    if (!cnt) { if (s->st && fl.n) s->st[67] = 1; return error(out, "NO SOLUTION", "", ""); }
    vals[1 - ia] = r; vals[ia] = w;
    for (lst = 0; lst < 2; lst++) {
        n = 0;
        for (k = 0; k < cnt; k++) {
            if (!root(s, r, fl.f[key[k].k], uj[1 - ia])) return 0;
            *w = *v;
            if (evalat(s, w, uj[1 - ia], r) != 1) return 0;
            if (k) { if (n >= MAXOUT) return 0; out[n++] = lst ? T_LCOMMA : T_OR; }
            if (!(n = putset(s, out, n, uj, 2, vals, lst))) return 0;
        }
        if (width(out, n, s->mp) <= SCR_WIDTH || lst) return n;
    }
    return 0;
}

NOINLINE static uint8_t systems(ps_t *s, uint8_t cmd, const char *name, uint8_t *out)
{
    rat_t *e[MAXU], *q;
    const term_t *t;
    uint8_t ne = 0, nu = 0, uj[MAXU], i, k;
    int c;
    s->i++;                                     /* the { */
    do {
        if (ne == MAXU || !(e[ne] = get(s)) || !expr(s, e[ne])) return 0;
        if (peek(s) != T_EQ) goto syntax;
        s->i++;
        if (!(q = get(s)) || !expr(s, q)) return 0;
        pneg(&q->n);
        if (!raddinto(s, e[ne++], q)) return 0;
        s->top--;
        if ((c = peek(s)) < 0) goto syntax;
        s->i++;
    } while (c == T_COMMA);
    if (c != T_RBRACE || peek(s) != T_COMMA || (s->i++, peek(s)) != T_LBRACE) goto syntax;
    do {
        s->i++;
        if (!is_var(c = peek(s))) return error(out, "ARGUMENT", name, "(LETTERS)");
        for (i = 0; i < nu; i++) if (uj[i] == NV - 1 - s->rank[c - T_VAR0]) return error(out, "ARGUMENT", name, "(TWICE)");
        if (nu == MAXU) return 0;
        uj[nu++] = NV - 1 - s->rank[c - T_VAR0];
        s->i++;
    } while ((c = peek(s)) == T_COMMA);
    if (c != T_RBRACE) goto syntax;
    s->i++;
    if (peek(s) == T_RPAR) s->i++;
    if (s->i != s->len) goto syntax;
    for (k = 0; k < ne; k++)
        if (reduce(s, e[k]) != 1) return 0;
    for (k = 0; k < ne; k++) {                  /* numbers times the unknowns, and a number? */
        if (!isconst(&e[k]->d) && e[k]->n.n) goto not_linear;
        for (t = e[k]->n.t; t < e[k]->n.t + e[k]->n.n; t++) {
            if (t->rad != 1) goto not_linear;
            for (i = 0; i < nu && t->mono != (unsigned)1 << 4 * uj[i]; i++) ;
            if (i == nu && t->mono) goto not_linear;
        }
    }
    return linsys(s, out, e, ne, nu, uj);
not_linear:
    return ne == 2 && nu == 2 ? nonlin(s, cmd, out, e, uj) : 0;
syntax:
    return error(out, "SYNTAX", name, "({A=B},{X})");
}

/* The command's arguments are the entry now: expr [= expr | , expr]
   [, variable] [)]. The answer's length, SYMCE_ERR with the message written,
   or 0: past what SymCE can do. */
NOINLINE static uint8_t command(ps_t *s, uint8_t cmd, const char *name, uint8_t *out, uint8_t split)
{
    rat_t *p = get(s), *q = 0, *r, *w = 0, *o = 0;
    poly_t *a, *b;
    fl_t fl;
    struct { int64_t re, im; uint8_t k; } key[MAXF], kt;
    int32_t l = 1, g = 0;
    term_t f;
    uint8_t n = 0, j = 0, k, m, cnt = 0, op = T_EQ, rnz = 0, rej = 0;
    int c;

    if (!p || !expr(s, p)) return 0;
    if (cmd <= C_CSOLVE) {                      /* A=B, and SOLVE(A=B,V) all of it */
        op = (uint8_t)peek(s);
        if (cmd == C_SOLVE ? op < T_EQ || op > T_EQ + 5 : op != T_EQ) goto syntax;
        s->i++;
        if (!(q = get(s)) || !expr(s, q)) return 0;
        if (cmd == C_RIGHT) p = q;
        else if (cmd >= C_SOLVE) {
            if (cmd == C_SOLVE && op == T_EQ) { rnz = q->n.n != 0; lsteps(s, p, q); }
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
#ifdef NOSIDE
    if (cmd == C_PARTFRAC) return 0;
#else
    if (cmd == C_PARTFRAC) return apart(s, p, j, out);
#endif
    if (op != T_EQ) return (cmd != C_SOLVE || !s->nk || absfix(s, p)) ? ineq(s, p, j, op, out) : 0;
    if (cmd == C_SOLVE) { if (!rsplit(s, p, j, &w)) return 0; }
    else for (k = 0; k < s->nk; k++)            /* √(P(V)) is no polynomial in V: DERIV is calc()'s */
        if (s->kf[k] == K_ROOT && dep(s, s->ka[k], j)) return 0;
    if (cmd == C_DERIV) {
        dsteps(s, p, j);
        r = get(s); a = tmp(s); b = tmp(s);
        if (!b || !xd(a, &p->n, j) || !mulinto(s, a, &p->d) || !xd(b, &p->d, j) || !mulinto(s, b, &p->n))
            return 0;
        pneg(b);
        r->n = *a;
        for (k = 0; k < b->n; k++) if (!addterm(&r->n, &b->t[k])) return 0;
        r->d = p->d;
        if (!mulinto(s, &r->d, &p->d)) return 0;
        for (k = 0; k < r->d.n; k++)
            if (!madd(&r->d.t[k].mono, 1u << 4 * j)) return 0;
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
            if (s->st && (cmd == C_FACTOR || cmd == C_CFACTOR) && a->t[0].den == 1) nsteps(s, a->t[0].num < 0 ? -a->t[0].num : a->t[0].num);
            if (a->t[0].num < 0) PUT(T_NEG);
            if (!(n = primes(out, n, a->t[0].num < 0 ? -a->t[0].num : a->t[0].num))) return 0;
            if (a->t[0].den == 1) return n;
            PUT(T_DIV);
            if (!ppow(a->t[0].den)) PUT(T_LPAR);
            if (!(n = primes(out, n, a->t[0].den))) return 0;
            if (!ppow(a->t[0].den)) PUT(T_RPAR);
            return n;
        }
        if (s->st && (cmd == C_FACTOR || cmd == C_CFACTOR) && isconst(&p->d) && (w = get(s))) w->n = p->n;
        if (!fac(s, &fl, &p->n, 0) || (!isconst(&p->d) && !fac(s, &fl, &p->d, 1))) return 0;
        if (w) fsteps(s, &fl, w, j, out);
        return putfl(s, &fl, out, 0);
    }

    /* The zeros of the numerator; reduce() left none in common with the
       denominator. Degree 1 in V: V = -B/A, whatever else is in it. Over C
       (split & 4) pushquad() split the quadratics with no real root too. */
    a = &p->n;
    if (!a->n) return error(out, "ALWAYS TRUE", "", "");
    if (cmd == C_NROOTS) return nroots(s, a, j, out);
    if (!(m = vdeg(a, j)) && !isconst(a)) return 0;
    if (m == 1) { *fl.f[0] = *a; fl.e[0] = 1; fl.n = 1; }
    else if (m && m != 0xFF && m > 1 && s->st && cmd >= C_SOLVE && cmd <= C_POLYROOTS && (o = get(s))) { o->n = p->n; pconst(&o->d, 1); }
    if (m != 1 && m && (m == 0xFF || !fac(s, &fl, a, 0))) return 0;
    r = get(s);
    for (k = 0; k < fl.n; k++) {
        if (!(m = vdeg(fl.f[k], j))) continue;
        if (m == 1) {                           /* in order: real, then by real part */
            if (!r || !root(s, r, fl.f[k], j)) return 0;
            if (w && (c = rok(s, w, j, r)) != 1) {  /* A+B√P squared: its extra roots out */
                if (c) return 0;
                if (k < 8) rej |= 1 << k;
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
    if (o) psteps(s, o, &fl, j, rnz || w, out);
    for (k = 0; k < 8; k++)
        if (rej >> k & 1 && root(s, r, fl.f[k], j)) stepa(s, SR_RBAD, r, 0, 0);
    if (!cnt && cmd < C_CZEROS) { if (o && s->st) s->st[67] = 1; return error(out, "NO SOLUTION", "", ""); }
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

static uint8_t engine(const uint8_t *in, unsigned len, uint8_t *out, void *work,
                      const uint8_t *ans, uint8_t mode, const uint8_t *(*look)(uint8_t));
static uint8_t cmdat(const uint8_t *in, unsigned len, uint8_t *nl);

#if !defined(NODESOLVE) && !defined(NOSTEPSX)
/* ---- SOLVE of exponential and logarithmic equations ----
   Run when command() gave up, since it reads no functions: e^(X)=5, 2^X=8, ln(X)=3, log(X)+log(X-3)=1.
   Powers: every kernel is B^(aX) for one base B (e, or K_POW's), and with c the gcd of the slopes
   a they are all G^m, G = B^(cX): the equation is a polynomial in G. It is solved as one in "u"
   (the unknown's letter is swapped while its steps are written). Each root u = r > 0 gives
   X = ln(r)/(c ln B), or the whole number q/c when r is B^q; r <= 0 is dropped.
   Logs: every kernel is a log to one base. The equation is scaled to whole coefficients, the logs
   are combined, the base is raised to both sides (or equal logs are dropped), and the polynomial
   that is left is solved; a root that puts 0 or a negative number inside a log is dropped.
   Exact only: anything else is 0 (SYMCE LIMIT), trigonometry too. */

/* One step, written with the unknown's letter ul in a and b (par keeps the real one: it may hold
   e^(X)); par is a list of values, 0 last; w is scratch. */
NOINLINE static void xs(ps_t *s, uint16_t rule, rat_t *const *par, rat_t *a, rat_t *b, uint8_t j, uint8_t ul, rat_t *w)
{
    uint8_t *o = (uint8_t *)w, *v = &s->var[NV - 1 - j], sv = *v, n = 0, m = 0;
    if (!s->st || s->st[2]) return;
    for (; par && *par; par++) {
        if (n) { if (n >= MAXOUT - 1) goto bad; o[n++] = T_LCOMMA; }
        if (!(n = answer(s, *par, o, n))) goto bad;
    }
    if (ul) *v = ul;
    if (a && !(m = answer(s, a, o + MAXOUT, 0))) goto bad;
    if (b) {
        if (m >= MAXOUT - 1) goto bad;
        o[MAXOUT + m++] = T_EQ;
        if (!(m = putroot(s, b, o + MAXOUT, m))) goto bad;      /* as the answer reads: (e^(3)-1)/2 */
    }
    *v = sv;
    stept(s, rule, o, n, o + MAXOUT, m);
    return;
bad:
    *v = sv;
    s->st[2] |= 2;
}

/* The real roots of fl's linear factors, as indexes into fl, by size; 0 when a factor is out of scope. */
static int fxkeys(ps_t *s, fl_t *fl, rat_t *v, uint8_t j, uint8_t *key, uint8_t *cnt)
{
    int64_t re[MAXF], z;
    uint8_t k, m, d;
    *cnt = 0;
    for (k = 0; k < fl->n; k++) {
        if (!(d = vdeg(fl->f[k], j))) continue;
        if (d == 1) {
            if (!root(s, v, fl->f[k], j)) return 0;
            z = approx(&v->n, 0);
            for (m = (*cnt)++; m && re[m - 1] > z; m--) { re[m] = re[m - 1]; key[m] = key[m - 1]; }
            re[m] = z;
            key[m] = k;
        } else if (!noroot(fl->f[k], j)) return 0;
    }
    return 1;
}

/* a, a polynomial in the unknown (nibble j), factored into fl, and its steps: a one-step linear
   equation l = r, else a moved to the left of 0 when rnz. The unknown reads ul meanwhile. */
NOINLINE static int fxfac(ps_t *s, fl_t *fl, rat_t *a, uint8_t j, uint8_t rnz, rat_t *l, rat_t *r, uint8_t ul, uint8_t *out)
{
    uint8_t m = vdeg(&a->n, j), k, *v = &s->var[NV - 1 - j], sv = *v, el = 0;
    rat_t *o = get(s), *f;
    if (!o || !m || m == 0xFF) return 0;
    fl->n = 0; fl->split = 2;
    fl->c.num = fl->c.den = 1; fl->c.mono = 0; fl->c.rad = 1;
    for (k = 0; k < MAXF; k += 2) {
        if (!(f = get(s))) return 0;
        fl->f[k] = &f->n; fl->f[k + 1] = &f->d;
    }
    if (ul) *v = ul;
    if (m == 1) {
        *fl->f[0] = a->n; fl->e[0] = 1; fl->n = 1;
        if (s->st) { el = s->st[3]; s->st[3] = 0; }             /* the entry's brackets (1.05)^X are not to be multiplied out */
        lsteps(s, l, r);
        if (s->st) s->st[3] = el;
    } else {
        o->n = a->n; pconst(&o->d, 1);
        if (!fac(s, fl, &a->n, 0)) { *v = sv; return 0; }
        psteps(s, o, fl, j, rnz, out);
    }
    *v = sv;
    return 1;
}

/* r = rn/rd is B^q for B = bn/bd (lowest terms, positive), 1 <= |q| <= 20: q; else 0 */
static int8_t fxpow(int32_t rn, int32_t rd, int32_t bn, int32_t bd)
{
    int32_t pn, pd;                                     /* LIM times a base under 1000 fits */
    int8_t q, f;
    if (bn > 999 || bd > 999) return 0;
    for (f = 0; f < 2; f++) {
        pn = pd = 1;
        for (q = 1; q <= 20; q++) {
            pn *= f ? bd : bn; pd *= f ? bn : bd;
            if (pn > LIM || pd > LIM) break;
            if (pn == rn && pd == rd) return f ? -q : q;
        }
    }
    return 0;
}

/* NO SOLUTION; the steps end there (st[67]) */
static uint8_t fxnone(ps_t *s, uint8_t *out)
{
    if (s->st) s->st[67] = 1;
    return error(out, "NO SOLUTION", "", "");
}

/* "or X=root" onto the answer, "X=root" for the first (nres 0) */
static uint8_t fxans(const ps_t *s, rat_t *v, uint8_t j, uint8_t nres, uint8_t *out, uint8_t n)
{
    if (nres) PUT(T_OR);
    PUT(s->var[NV - 1 - j]);
    PUT(T_EQ);
    return putroot(s, v, out, n);
}

/* a = l - r, polynomials over 1 */
static int fxdiff(rat_t *a, const rat_t *l, const rat_t *r)
{
    term_t z;
    uint8_t k;
    a->n = l->n; pconst(&a->d, 1);
    for (k = 0; k < r->n.n; k++) {
        z = r->n.t[k];
        z.num = -z.num;
        if (!addterm(&a->n, &z)) return 0;
    }
    return 1;
}

/* The power of G in the term x, where kernel k is G^mm[k]; 99 when x is more than that. */
static int fxe(const ps_t *s, const term_t *x, const int8_t *mm, unsigned varm)
{
    uint8_t k;
    int e = 0;
    if (x->rad != 1 || (x->mono & varm)) return 99;
    for (k = 0; k < s->nk; k++) e += (int)(x->mono >> 4 * KN(s, k) & 15) * mm[k];
    return e;
}

/* out = the term t, alone */
static void fxt(rat_t *out, const term_t *t)
{
    out->n.n = 0;
    addterm(&out->n, t);
    pconst(&out->d, 1);
}

/* The bases bk[k] of the power kernels in `used` are whole powers B^q of one number B (4^X = 2^(X+3), B = 2):
   *bs = B, and the slopes sn and whole offsets of are those of B^(..) */
NOINLINE static int fxbase(ps_t *s, rat_t *const *bk, unsigned used, int8_t *of, int *sn, rat_t **bs)
{
    int32_t bn, bd;
    int8_t qq[NV], p;
    uint8_t m, k, ok = 0;
    term_t z;
    rat_t *t = get(s);
    if (!t) return 0;
    for (m = 0; m < 8 + s->nk && !ok; m++) {
        if (m < 8) { bn = m + 2; bd = 1; }
        else if (used >> (m - 8) & 1) { bn = bk[m - 8]->n.t[0].num; bd = bk[m - 8]->n.t[0].den; if (bn < bd) { p = (int8_t)bn; bn = bd; bd = p; } }
        else continue;
        for (ok = 1, k = 0; ok && k < s->nk; k++)
            if (used >> k & 1) {
                qq[k] = fxpow(bk[k]->n.t[0].num, bk[k]->n.t[0].den, bn, bd);
                ok = qq[k] && of[k] * qq[k] >= -9 && of[k] * qq[k] <= 9;
            }
    }
    if (!ok) return 0;
    for (k = 0; k < s->nk; k++)
        if (used >> k & 1) { sn[k] *= qq[k]; of[k] *= qq[k]; }
    rset(&z, bn, bd);
    z.mono = 0; z.rad = 1;
    fxt(t, &z);
    return (*bs = keep(s, t)) != 0;
}

/* B^(aX) for one B, the kernels in `used`: an equation in G */
NOINLINE static uint8_t fxexp(ps_t *s, rat_t *l, rat_t *r, uint8_t j, unsigned used, uint8_t *out)
{
    rat_t *lp = get(s), *rp = get(s), *a = get(s), *gr = get(s), *ca = get(s), *w = get(s), *cc = get(s), *bs = 0;
    rat_t *v, *x, *t, *y;
    fl_t fl;
    const rat_t *u;
    const term_t *tt;
    term_t c, z;
    poly_t *pl;
    int g = 0, lc = 1, sn[NV], sd[NV], q, fn[NV], fd[NV], cn;   /* slopes under 1000: all of it fits 24 bits */
    int8_t mm[NV], emin = 99, emax = -99, of[NV];
    uint8_t k, i, m, kind = 0, key[MAXF], cnt, nres = 0, ul, n = 0, top, rev, multi = 0;
    rat_t *bk[NV];
    unsigned varm = s->nv ? 0xFFFFFFu << 4 * (NV - s->nv) & 0xFFFFFFu : 0;
    int e;
    if (!w) return 0;
    for (k = 0; k < NV; k++) { mm[k] = of[k] = 0; fn[k] = fd[k] = 1; }
    for (k = 0; k < s->nk; k++) {
        if (!(used >> k & 1)) continue;
        if (kind && s->kf[k] != kind) return 0;
        kind = s->kf[k];
        u = kind == K_EXP ? s->ka[k] : s->kb[k];
        if (u->n.n < 1 || u->n.n > 2 || !isconst(&u->d)) return 0;
        for (tt = 0, i = 0; i < u->n.n; i++) {          /* aX, or aX + b with a whole b in a power B^(aX+b) */
            if (u->n.t[i].rad != 1) return 0;
            if (u->n.t[i].mono == 1u << 4 * j) tt = &u->n.t[i];
            else if (!u->n.t[i].mono && kind == K_POW && u->n.t[i].den == 1 && u->n.t[i].num >= -9 && u->n.t[i].num <= 9) of[k] = (int8_t)u->n.t[i].num;
            else return 0;
        }
        if (!tt) return 0;
        if (kind == K_POW) {
            bk[k] = s->ka[k];
            if (dep(s, bk[k], j) || haskern(s, bk[k], 0) || nsign(bk[k]) != 1 || isnum(bk[k], 1) || bk[k]->n.n != 1 || bk[k]->n.t[0].mono || bk[k]->n.t[0].rad != 1 || !isconst(&bk[k]->d)) return 0;
            if (bs && !same2(bs, bk[k])) multi = 1;
            else if (!bs) bs = bk[k];
        }
        if (tt->num > 999 || tt->num < -999 || tt->den > 999) return 0;
        sn[k] = tt->num; sd[k] = tt->den;
    }
    if (multi && !fxbase(s, bk, used, of, sn, &bs)) return 0;
    for (k = 0; k < s->nk; k++)                         /* B^b, whole b: a number in front of the power */
        if ((used >> k & 1) && of[k])
            for (m = of[k] < 0 ? -of[k] : of[k], cn = 1, q = 1; m--; ) {
                if ((cn *= of[k] < 0 ? bs->n.t[0].den : bs->n.t[0].num) > LIM || (q *= of[k] < 0 ? bs->n.t[0].num : bs->n.t[0].den) > LIM) return 0;
                fn[k] = cn; fd[k] = q;
            }
    for (k = 0; k < s->nk; k++)
        if (used >> k & 1) {
            g = g ? gcd(iabs(sn[k]), g) : iabs(sn[k]);
            lc = lc / gcd(lc, sd[k]) * sd[k];
        }
    if (!kind || !rset(&c, g, lc)) return 0;
    c.mono = 0; c.rad = 1;                                      /* c = g/lc, the gcd of the slopes */
    for (k = 0; k < s->nk; k++)
        if (used >> k & 1) {
            q = sn[k] / g * (lc / sd[k]);
            if (q > 8 || q < -8) return 0;
            mm[k] = (int8_t)q;
        }
    for (i = 0; i < 2; i++)
        for (pl = &(i ? r : l)->n, k = 0; k < pl->n; k++) {
            if ((e = fxe(s, &pl->t[k], mm, varm)) == 99) return 0;
            if (e < emin) emin = (int8_t)e;
            if (e > emax) emax = (int8_t)e;
        }
    if (emin > 0) emin = 0;                                     /* u > 0 stays a root test, never a shift away */
    if (emax - emin > 8) return 0;
    for (i = 0; i < 2; i++) {                                   /* each side as a polynomial in u, the powers of G */
        y = i ? rp : lp;
        y->n.n = 0;
        pconst(&y->d, 1);
        for (k = 0; k < (i ? r : l)->n.n; k++) {
            z = (i ? r : l)->n.t[k];
            for (m = 0; m < s->nk; m++)
                for (cn = (used >> m & 1) ? z.mono >> 4 * KN(s, m) & 15 : 0; cn--; )
                    if (!cmul(&z, fn[m], fd[m])) return 0;
            z.mono = (unsigned)(fxe(s, &z, mm, varm) - emin) << 4 * j;
            if (!addterm(&y->n, &z)) return 0;
        }
    }
    if (!fxdiff(a, lp, rp) || !a->n.n) return 0;
    if (a->n.n == 1 && !a->n.t[0].mono) return fxnone(s, out);
    /* G = B^(cX) */
    z = c; z.mono = 1u << 4 * j;
    fxt(ca, &z);
    if (kind == K_EXP) { if (!kvar(s, gr, K_EXP, ca, 0)) return 0; }
    else if (!(x = keep(s, ca)) || !kvar(s, gr, K_POW, bs, x)) return 0;
    fxt(cc, &c);
    ul = s->var[NV - 1 - j] == 0x55 ? 0x56 : 0x55;
    xs(s, SR_XLET, (rat_t *[]){ gr, 0 }, lp, rp, j, ul, w);
    if (!fxfac(s, &fl, a, j, rp->n.n != 0, lp, rp, ul, out)) return 0;
    if (!fxkeys(s, &fl, w, j, key, &cnt)) return 0;
    rev = kind == K_POW && bs->n.t[0].num < bs->n.t[0].den;    /* B < 1: ln(B) < 0 */
    top = s->top;
    for (i = 0; i < cnt; i++) {
        s->top = top;
        v = lp; x = rp;                                         /* free by now */
        if (!(t = get(s)) || !(y = get(s)) || !root(s, v, fl.f[key[rev ? cnt - 1 - i : i]], j)) return 0;
        e = nsign(v);
        if (e == 2 || e == 3) return 0;
        if (e <= 0) { xs(s, SR_XNEG, (rat_t *[]){ v, 0 }, 0, 0, j, 0, w); continue; }
        xs(s, SR_XBACK, (rat_t *[]){ gr, 0 }, gr, v, j, 0, w);
        tt = v->n.t;
        q = kind == K_POW && v->n.n == 1 && !tt->mono && tt->rad == 1 && isconst(&v->d) ?
            fxpow(tt->num, tt->den, bs->n.t[0].num, bs->n.t[0].den) : 0;
        if (!mklog(s, x, v, 0)) return 0;                       /* ln(r) */
        z.mono = 1u << 4 * j; z.num = z.den = 1;
        fxt(t, &z);                                             /* X */
        if (kind == K_POW) {
            if (!mklog(s, y, bs, 0)) return 0;                  /* ln(B) */
            *a = *ca;
            if (!rmulinto(s, a, y, 0)) return 0;                /* cX ln(B) */
            xs(s, SR_XLB, 0, a, x, j, 0, w);
            *a = *cc;
            if (!rmulinto(s, a, y, 0) || !rmulinto(s, x, a, 1)) return 0;
            xs(s, SR_DIV, (rat_t *[]){ a, 0 }, t, x, j, 0, w);  /* X = ln(r) / (c ln(B)) */
            if (q) {
                rconst(x, q);
                if (!rmulinto(s, x, cc, 1)) return 0;
                rconst(a, q);
                xs(s, SR_XEXACT, (rat_t *[]){ v, bs, a, 0 }, t, x, j, 0, w);
            }
        } else {
            xs(s, SR_XLN, 0, ca, x, j, 0, w);                   /* cX = ln(r) */
            if (c.num != 1 || c.den != 1) {
                if (!rmulinto(s, x, cc, 1)) return 0;
                if (c.den != 1) { rinv(&z, &c); z.mono = 0; fxt(y, &z); xs(s, SR_MUL, (rat_t *[]){ y, 0 }, t, x, j, 0, w); }
                else xs(s, SR_DIV, (rat_t *[]){ cc, 0 }, t, x, j, 0, w);
            }
        }
        if (!(n = fxans(s, x, j, nres++, out, n))) return 0;
    }
    return nres ? n : fxnone(s, out);
}

/* every u in ux (a polynomial in the unknown) is positive at v: 1; some is not: 0; not told: -1.
   single: the equation is ln(u) = K, so u is B^K at the root, positive whatever it looks like. */
static int fxok(ps_t *s, rat_t *const *ux, uint8_t nx, rat_t *t, const rat_t *v, uint8_t j, uint8_t single)
{
    uint8_t i;
    int g;
    for (i = 0; i < nx; i++) {
        *t = *ux[i];
        if (subst(s, t, j, v) != 1) return -1;
        g = nsign(t);
        if (g == 3 && single) g = 1;
        if (g == 2 || g == 3) return -1;
        if (g <= 0) return 0;
    }
    return 1;
}

/* the kernel of coefficient a (a whole number) added into the polynomial p */
static int fxadd(const ps_t *s, poly_t *p, uint8_t k, int32_t a, int32_t d)
{
    term_t z;
    z.num = a; z.den = d; z.mono = 1u << 4 * KN(s, k); z.rad = 1;
    return addterm(p, &z);
}

/* a rat is one log with the number 1 in front */
static int fxunit(const rat_t *r)
{
    return r->n.n == 1 && r->n.t[0].mono && r->n.t[0].num == 1 && r->n.t[0].den == 1 && r->n.t[0].rad == 1;
}

/* lp (c X + d, numbers) = rp (no X, but maybe e^(3) in it): the two moves lsteps cannot write, as steps */
NOINLINE static void fxlin(ps_t *s, const rat_t *lp, const rat_t *rp, uint8_t j, rat_t *w, rat_t *a, rat_t *b, rat_t *x, unsigned um)
{
    term_t c1, c0, z;
    const term_t *t;
    uint8_t k;
    c1.num = 0; c1.den = 1; c1.mono = 0; c1.rad = 1;
    c0 = c1;
    if (!s->st || s->st[2] || dep(s, rp, j)) return;
    for (k = 0; k < lp->n.n; k++) {
        t = &lp->n.t[k];
        if (t->rad != 1 || (t->mono && t->mono != um)) return;
        if (!radd(t->mono ? &c1 : &c0, t->num, t->den)) return;
    }
    if (!c1.num) return;
    *b = *rp;
    if (c0.num) {
        z = c0; z.num = -z.num;
        fxt(x, &z);
        if (!raddinto(s, b, x)) return;
        if (z.num < 0) z.num = -z.num;
        fxt(a, &z);
        z = c1; z.mono = um;
        fxt(x, &z);
        xs(s, c0.num > 0 ? SR_SUB : SR_ADD, (rat_t *[]){ a, 0 }, x, b, j, 0, w);
    }
    if (c1.num != 1 || c1.den != 1) {
        z = c1; z.mono = 0;
        b->d.n = 0;                                             /* b / c1, written as root() would */
        if (!addterm(&b->d, &z) || !rationalize(s, b) || reduce(s, b) != 1) return;
        if (c1.den != 1 || c1.num == -1) { rinv(&z, &c1); z.mono = 0; fxt(a, &z); }
        else fxt(a, &c1);
        z.mono = um; z.num = z.den = 1; z.rad = 1;
        fxt(x, &z);
        xs(s, c1.den != 1 || c1.num == -1 ? SR_MUL : SR_DIV, (rat_t *[]){ a, 0 }, x, b, j, 0, w);
    }
}

/* the whole n-th root of v >= 0, else -1 */
static int fxir(uint8_t n, int32_t v)
{
    int32_t r, p;
    uint8_t i;
    for (r = 0; r <= 1000; r++) {
        for (p = 1, i = 0; i < n; i++) p *= r;
        if (p == v) return r;
        if (p > v) break;
    }
    return -1;
}

/* X^n = number, or e^(K): X = the root of it, and minus that for even n. Steps and the answer; 0 when it is not that shape.
   x, y, a: scratch; the root in y */
NOINLINE static uint8_t fxroot(ps_t *s, const rat_t *lp, const rat_t *rp, uint8_t j, rat_t *x, rat_t *y, rat_t *a, rat_t *w, uint8_t *out)
{
    const term_t *t = &rp->n.t[0], *l = &lp->n.t[0];
    uint8_t n = l->mono >> 4 * j, m, o;
    int p, q;
    term_t z;
    uint8_t buf[2 * MAXOUT];
    if (lp->n.n != 1 || l->num != 1 || l->den != 1 || l->mono != (unsigned)n << 4 * j || n < 2 || n > 4) return 0;
    if (rp->n.n != 1 || t->rad != 1 || !isconst(&rp->d) || !isconst(&lp->d)) return 0;
    if (!t->mono) {                                     /* a whole number or a fraction of them */
        if (t->num < 0 || (p = fxir(n, t->num)) < 0 || (q = fxir(n, t->den)) < 1) return 0;
        rset(&z, p, q);
        z.mono = 0; z.rad = 1;
        fxt(y, &z);
    } else {                                            /* e^(K): e^(K/n) */
        for (m = 0; m < s->nk && t->mono != 1u << 4 * KN(s, m); m++) ;
        if (m == s->nk || s->kf[m] != K_EXP || t->num != 1 || t->den != 1) return 0;
        rconst(a, n);
        *x = *s->ka[m];
        if (!rmulinto(s, x, a, 1) || !apply(s, y, K_EXP, x, a, 0)) return 0;
    }
    o = fxans(s, y, j, 0, buf, 0);
    if (!o) return 0;
    if (!(n & 1)) {
        z.num = -1; z.den = 1; z.mono = 0; z.rad = 1;
        fxt(a, &z);
        if (!rmulinto(s, a, y, 0)) return 0;
        if (!(o = fxans(s, a, j, 1, buf, o))) return 0;
    }
    stept(s, SR_XROOT, 0, 0, buf, o);
    return 1;
}

/* the terms of v with the unknown in xp, those without in cp */
static void fxsplit(const rat_t *v, uint8_t j, rat_t *xp, rat_t *cp)
{
    const term_t *t;
    xp->n.n = cp->n.n = 0;
    xp->d = cp->d = v->d;
    for (t = v->n.t; t < v->n.t + v->n.n; t++) addterm(t->mono >> 4 * j & 15 ? &xp->n : &cp->n, t);
}

/* "subtract p from both sides" (add -p when it is negative), p = what moves, q = r the sides after */
static void fxmove(ps_t *s, rat_t *p, rat_t *q, rat_t *r, uint8_t j, rat_t *w, rat_t *m)
{
    term_t z = { -1, 1, 0, 1 };
    uint8_t neg = p->n.t[0].num < 0;
    if (neg) { fxt(m, &z); if (!rmulinto(s, p, m, 0)) return; }
    xs(s, neg ? SR_ADD : SR_SUB, (rat_t *[]){ p, 0 }, q, r, j, 0, w);
}

/* B1^(a1 X + b1) = B2^(a2 X + b2), two bases that are no powers of one number: ln both sides, X on one side, divide */
NOINLINE static uint8_t fxtwo(ps_t *s, rat_t *l, rat_t *r, uint8_t j, unsigned used, uint8_t *out)
{
    rat_t *sd[2] = { l, r }, *e[2], *xp[2], *cp[2], *w = get(s), *p = get(s), *q = get(s), *u = get(s), *v = get(s), *y = get(s);
    const term_t *t;
    term_t z;
    uint8_t i, k;
    for (i = 0; i < 2; i++) if (!(e[i] = get(s)) || !(xp[i] = get(s)) || !(cp[i] = get(s))) return 0;
    if (!y) return 0;
    for (i = 0; i < 2; i++) {
        t = sd[i]->n.t;
        if (sd[i]->n.n != 1 || t->num != 1 || t->den != 1 || t->rad != 1 || !isconst(&sd[i]->d) || !t->mono) return 0;
        for (k = 0; k < s->nk && t->mono != 1u << 4 * KN(s, k); k++) ;
        if (k == s->nk || !(used >> k & 1) || s->kf[k] != K_POW || !dep(s, s->kb[k], j) || vdeg(&s->kb[k]->n, j) != 1 || haskern(s, s->kb[k], 0)) return 0;
        if (dep(s, s->ka[k], j) || haskern(s, s->ka[k], 0) || nsign(s->ka[k]) != 1 || isnum(s->ka[k], 1) || !mklog(s, y, s->ka[k], 0)) return 0;
        *e[i] = *s->kb[k];
        if (!rmulinto(s, e[i], y, 0)) return 0;                 /* (a X + b) ln(B) */
        fxsplit(e[i], j, xp[i], cp[i]);
    }
    xs(s, SR_XLNB, 0, e[0], e[1], j, 0, w);
    *p = *xp[1];
    if (!p->n.n || !fxdiff(q, e[0], xp[1]) || !fxdiff(v, xp[0], xp[1])) return 0;
    fxmove(s, p, q, cp[1], j, w, y);                            /* X ln(B2) over to the left */
    if (cp[0]->n.n) {
        *p = *cp[0];
        if (!fxdiff(u, cp[1], cp[0])) return 0;
        fxmove(s, p, v, u, j, w, y);                            /* the number over to the right */
    } else *u = *cp[1];
    p->n.n = 0; p->d = v->d;
    for (t = v->n.t; t < v->n.t + v->n.n; t++) {                /* v = D X: D */
        z = *t; z.mono -= 1u << 4 * j;
        if (!addterm(&p->n, &z)) return 0;
    }
    if (!p->n.n) return 0;
    *q = *u;
    if (!rmulinto(s, q, p, 1)) return 0;
    z.num = z.den = 1; z.rad = 1; z.mono = 1u << 4 * j;
    fxt(y, &z);
    xs(s, SR_DIV, (rat_t *[]){ p, 0 }, y, q, j, 0, w);
    return fxans(s, q, j, 0, out, 0);
}

/* Logs to one base: a1 ln(u1) + .. + d1 ln(w1) + c0 = 0, the u in the unknown, the w numbers. */
NOINLINE static uint8_t fxlog(ps_t *s, rat_t *l, rat_t *r, uint8_t j, unsigned used, uint8_t *out)
{
    rat_t *lp = get(s), *rp = get(s), *a = get(s), *w = get(s), *b = get(s), *x = get(s), *y = get(s), *ux[3], *cw[2];
    rat_t *bb = 0, *v;
    fl_t fl;
    term_t cf[NV], c0, ft, kt;
    poly_t *pl;
    const term_t *t;
    int ax[3], cd[2], lcd = 1, G = 0, q;                /* coefficients under 1000 and a common denominator under 1000: 24 bits do */
    uint8_t ik[3], jk[2], nx = 0, nc = 0, nb = 0, k, m, i, n = 0, cnt, key[MAXF], nres = 0, top, single, uni;
    int e;
    if (!y) return 0;
    for (k = 0; k < NV; k++) { cf[k].num = 0; cf[k].den = 1; cf[k].mono = 0; cf[k].rad = 1; }
    c0 = cf[0];
    for (i = 0; i < 2; i++) {                           /* l - r: terms of c0 and of the logs */
        pl = &(i ? r : l)->n;
        if (!isconst(&(i ? r : l)->d)) return 0;
        for (k = 0; k < pl->n; k++) {
            t = &pl->t[k];
            if (t->rad != 1) return 0;
            if (!t->mono) { if (!radd(&c0, i ? -t->num : t->num, t->den)) return 0; continue; }
            for (m = 0; m < s->nk && t->mono != 1u << 4 * KN(s, m); m++) ;
            if (m == s->nk || !radd(&cf[m], i ? -t->num : t->num, t->den)) return 0;
        }
    }
    for (m = 0; m < s->nk; m++) {
        if (!(used >> m & 1) || !cf[m].num) continue;
        if (s->kf[m] != K_LOG || (nb++ && s->kb[m] != bb)) return 0;
        bb = s->kb[m];
        v = s->ka[m];
        if (dep(s, v, j)) {                             /* a polynomial in the unknown alone */
            if (nx == 3 || !isconst(&v->d) || haskern(s, v, 0)) return 0;
            for (k = 0; k < v->n.n; k++)
                if (v->n.t[k].rad != 1 || (v->n.t[k].mono & ~(15u << 4 * j))) return 0;
            ux[nx] = v; ik[nx++] = m;
        } else {                                        /* a positive rational */
            if (nc == 2 || v->n.n != 1 || v->n.t[0].mono || v->n.t[0].rad != 1 || !isconst(&v->d)) return 0;
            cw[nc] = v; jk[nc++] = m;
        }
    }
    if (!nx) return 0;
    /* whole coefficients with no common factor, the first in front of a u positive: times ft */
    for (m = 0; m < s->nk; m++)
        if ((used >> m & 1) && cf[m].num) {
            if (cf[m].num > 999 || cf[m].num < -999 || (lcd = lcd / gcd(lcd, cf[m].den) * cf[m].den) > 999) return 0;
        }
    for (m = 0; m < s->nk; m++)
        if ((used >> m & 1) && cf[m].num) { q = cf[m].num * (lcd / cf[m].den); G = G ? gcd(iabs(q), G) : iabs(q); }
    ft.mono = 0; ft.rad = 1;
    if (!rset(&ft, cf[ik[0]].num < 0 ? -lcd : lcd, G)) return 0;
    for (k = 0; k < nx; k++) ax[k] = cf[ik[k]].num * (lcd / cf[ik[k]].den) / G * (cf[ik[0]].num < 0 ? -1 : 1);
    for (k = 0; k < nc; k++) cd[k] = cf[jk[k]].num * (lcd / cf[jk[k]].den) / G * (cf[ik[0]].num < 0 ? -1 : 1);
    kt = c0; kt.num = -kt.num;                          /* K, the number the logs add up to */
    if (!cmul(&kt, ft.num, ft.den)) return 0;
    if (bb && (kt.den != 1 || kt.num > 9 || kt.num < -9)) return 0;     /* B^K a whole number: ln, log to a base */
    for (k = 0; k < nx; k++) if (ax[k] > 6 || ax[k] < -6) return 0;
    for (k = 0; k < nc; k++) if (cd[k] > 6 || cd[k] < -6) return 0;
    /* the logs with a positive coefficient to the left, the others (made positive) to the right, K there too */
    lp->n.n = rp->n.n = 0;
    pconst(&lp->d, 1); pconst(&rp->d, 1);
    for (k = 0; k < nx; k++)
        if (!fxadd(s, ax[k] > 0 ? &lp->n : &rp->n, ik[k], ax[k] > 0 ? ax[k] : -ax[k], 1)) return 0;
    for (k = 0; k < nc; k++)
        if (!fxadd(s, cd[k] > 0 ? &lp->n : &rp->n, jk[k], cd[k] > 0 ? cd[k] : -cd[k], 1)) return 0;
    if (kt.num) { kt.mono = 0; if (!addterm(&rp->n, &kt)) return 0; }
    if (ft.num != 1 || ft.den != 1) {
        if (ft.num == 1) rconst(a, ft.den);
        else fxt(a, &ft);
        xs(s, ft.num == 1 ? SR_DIV : SR_MUL, (rat_t *[]){ a, 0 }, lp, rp, j, 0, w);
    }
    single = nx == 1 && ax[0] == 1;                     /* u times positive numbers = positive numbers: u > 0 */
    /* the products: lp the arguments of the logs on the left, rp those on the right */
    pconst(&lp->n, 1); pconst(&rp->n, 1);
    for (k = 0; k < nx; k++)
        for (q = ax[k] > 0 ? ax[k] : -ax[k]; q--; ) if (!mulinto(s, ax[k] > 0 ? &lp->n : &rp->n, &ux[k]->n)) return 0;
    for (k = 0; k < nc; k++)
        for (q = cd[k] > 0 ? cd[k] : -cd[k]; q--; ) if (!mulinto(s, cd[k] > 0 ? &lp->n : &rp->n, &cw[k]->n)) return 0;
    /* ln(left) = ln(right) + K, unless the scaling step showed it already, or it was typed so */
    uni = nx == 1 && ax[0] == 1 && (!nc || (nc == 1 && cd[0] == -1));
    if (uni ? ft.num == 1 && ft.den == 1 && !(fxunit(l) && (!r->n.n || (r->n.n == 1 && (!r->n.t[0].mono || fxunit(r))))) : 1) {
        if (!mklog(s, a, lp, bb) || !mklog(s, b, rp, bb)) return 0;
        if (kt.num) { fxt(x, &kt); if (!raddinto(s, b, x)) return 0; }
        xs(s, SR_XCOMB, 0, a, b, j, 0, w);
    }
    if (bb) { *y = *bb; if (!powinto(s, y, kt.num)) return 0; }
    else { fxt(x, &kt); if (!kt.num) rconst(x, 0); if (!apply(s, y, K_EXP, x, a, 0)) return 0; }
    if (!rmulinto(s, rp, y, 0)) return 0;                  /* times B^K */
    if (nc && !kt.num) xs(s, SR_XSAME, 0, lp, rp, j, 0, w);
    else {
        if (bb) *x = *bb; else if (!kvar(s, x, K_E, 0, 0)) return 0;
        xs(s, SR_XRAISE, (rat_t *[]){ x, 0 }, lp, rp, j, 0, w);
    }
    if (nx == 1 && !nc && ax[0] == 1 && fxroot(s, lp, rp, j, x, y, a, w, out)) {   /* ln(X^2) = 4 */
        n = fxans(s, y, j, 0, out, 0);
        if (vdeg(&lp->n, j) % 2 == 0) { fxt(x, &(term_t){ -1, 1, 0, 1 }); rmulinto(s, x, y, 0); n = fxans(s, x, j, 1, out, n); }
        return n;
    }
    if (!fxdiff(a, lp, rp) || !a->n.n) return 0;
    if (!vdeg(&a->n, j)) return fxnone(s, out);
    top = s->st ? s->st[1] : 0;
    if (!fxfac(s, &fl, a, j, 1, lp, rp, 0, out)) return 0;
    if (s->st && s->st[1] == top && vdeg(&a->n, j) == 1) fxlin(s, lp, rp, j, w, a, b, x, 1u << 4 * j);
    if (!fxkeys(s, &fl, w, j, key, &cnt)) return 0;
    top = s->top;
    for (i = 0; i < cnt; i++) {
        s->top = top;
        v = y;                                                  /* x, y: scratch no more */
        if (!root(s, v, fl.f[key[i]], j)) return 0;
        if ((e = fxok(s, ux, nx, x, v, j, single)) < 0) return 0;
        if (!e) { xs(s, SR_XDOM, (rat_t *[]){ v, 0 }, 0, 0, j, 0, w); continue; }
        if (!(n = fxans(s, v, j, nres++, out, n))) return 0;
    }
    return nres ? n : fxnone(s, out);
}

/* ---- SOLVE of sin( cos( tan( equations: the unit circle, one period, then every turn (N is any whole number) ---- */
#define T_PI 0xAC

/* f(v), v = n/d √r, on the unit circle: its first angle in degrees (sin, tan from -90 to 90, cos from 0 to 180); 999 when
   v is no such value */
static int fxang(uint8_t f, int32_t n, int32_t d, int32_t r)
{
    static const uint8_t t[] = { 0, 0, 0, 1, 2, 1, 1, 2, 2, 1, 2, 3, 1, 1, 1 },     /* sin, cos of 0, 30, 45, 60, 90: |v| */
                         u[] = { 0, 0, 0, 1, 3, 3, 1, 1, 1, 1, 1, 3 },              /* tan of 0, 30, 45, 60 */
                         a[] = { 0, 30, 45, 60, 90 };
    const uint8_t *p = f == K_TAN ? u : t;
    uint8_t i, m = f == K_TAN ? 4 : 5, g = n < 0;
    if (g) n = -n;
    for (i = 0; i < m && !(n == p[3 * i] && (!n || (d == p[3 * i + 1] && r == p[3 * i + 2]))); i++) ;
    if (i == m) return 999;
    return f == K_COS ? 90 + (g ? a[i] : -a[i]) : g ? -a[i] : a[i];
}

/* the angles in [0, a turn) with f = the one of angle a, sorted, in o; the families that leave (each plus whole periods p):
   1 when they are half a turn apart, or the same, or tan */
static uint8_t fxfams(uint8_t f, int a, int *o, int *p)
{
    int b;
    *p = f == K_TAN ? 180 : 360;
    o[0] = (a + *p) % *p;
    o[1] = o[0];
    if (f == K_TAN) return 1;
    o[1] = f == K_SIN ? (540 - a) % 360 : (360 - a) % 360;
    if (o[1] < o[0]) { b = o[0]; o[0] = o[1]; o[1] = b; }
    if (o[0] == o[1]) return 1;
    if (o[1] - o[0] == 180) { *p = 180; return 1; }
    return 2;
}

static uint8_t fxnum(uint8_t *out, uint8_t n, int v)
{
    uint8_t d[6], k = 0;
    do d[k++] = (uint8_t)(v % 10); while ((v /= 10) && k < 6);
    while (k--) PUT(0x30 + d[k]);
    return n;
}

/* the term P N: P = pp/qq of π, or a number in degrees */
static uint8_t fxper(uint8_t *out, uint8_t n, int pp, int qq, uint8_t rad)
{
    if (pp != 1 && !(n = fxnum(out, n, pp))) return 0;
    if (rad) PUT(T_PI);
    PUT('N');
    if (qq != 1) { PUT(T_DIV); n = fxnum(out, n, qq); }
    return n;
}

/* dg degrees as an angle: dg/180 π, or the number */
static void fxth(rat_t *out, int dg, unsigned pm)
{
    term_t z;
    int g = pm && dg ? gcd(dg < 0 ? -dg : dg, 180) : 1;
    out->n.n = 0; pconst(&out->d, 1);
    if (!dg) return;
    z.num = dg / g; z.den = pm ? 180 / g : 1; z.mono = pm; z.rad = 1;
    addterm(&out->n, &z);
}

/* a + bπ (a number over a number) scaled by 10^9; sg its sign, 9 when it is no such number. Exact when it has one of the two. */
static int64_t fxv(const rat_t *v, int *sg)
{
    term_t a, b;
    int64_t x;
    uint8_t k;
    a.num = b.num = 0; a.den = b.den = 1;
    for (k = 0; k < v->n.n; k++)
        if (v->n.t[k].rad != 1 || !radd(v->n.t[k].mono ? &b : &a, v->n.t[k].num, v->n.t[k].den)) { *sg = 9; return 0; }
    x = a.num * 1000000000LL / a.den + b.num * 3141592654LL / b.den;
    if (isconst(&v->d) && v->d.n) x = x * v->d.t[0].den / v->d.t[0].num;
    *sg = !b.num ? (a.num > 0) - (a.num < 0) : !a.num ? (b.num > 0) - (b.num < 0) : (x > 0) - (x < 0);
    if (isconst(&v->d) && v->d.n && v->d.t[0].num < 0) *sg = -*sg;
    return x;
}

typedef struct { int dg, p; uint8_t ak; } fm_t;         /* an angle in degrees (ak 0), or an arc form, and its period */
typedef struct {
    ps_t *s;
    rat_t *u, *th, *lx, *c0, *av, *c;                      /* an angle; the unknown; the number in the argument, b; f(c) of an arc */
    fm_t all[6];
    uint8_t na, f, arcok;
    int bn, bd;                                         /* the argument is bX + c0, b = bn/bd */
    unsigned pm;                                        /* the mono of π, 0 in degrees */
} tr_t;

/* "u = angle + P N", more of them with "or"; arc: 1 f^-1(c), 2 half a turn - f^-1(c), 3 -f^-1(c) */
static uint8_t fxfam(ps_t *s, uint8_t *out, uint8_t n, rat_t *lhs, rat_t *th, uint8_t arc, uint8_t f, rat_t *c, int pp, int qq)
{
    uint8_t body = 0;
    if (n) PUT(T_OR);
    if (!(n = answer(s, lhs, out, n))) return 0;
    PUT(T_EQ);
    if (arc) {
        if (arc == 2) {
            if (s->deg) { if (!(n = fxnum(out, n, 180))) return 0; } else PUT(T_PI);
            PUT(T_SUB);
        } else if (arc == 3) PUT(T_NEG);
        PUT(f == K_SIN ? 0xC3 : f == K_COS ? 0xC5 : 0xC7);
        if (!(n = answer(s, c, out, n))) return 0;
        PUT(T_RPAR);
        body = 1;
    } else if (th->n.n) {
        if (!(n = putroot(s, th, out, n))) return 0;
        body = 1;
    }
    if (pp) { if (body) PUT(T_ADD); n = fxper(out, n, pp, qq, !(s->deg)); }
    else if (!body) PUT('0');
    return n;
}

/* the value of family m for the unknown: (angle - c0) / b */
static int fxA(tr_t *t, const fm_t *m, rat_t *out)
{
    term_t z;
    fxth(t->th, m->dg, t->pm);
    if (!fxdiff(out, t->th, t->c0)) return 0;
    z.num = t->bn; z.den = t->bd; z.mono = 0; z.rad = 1;
    fxt(t->th, &z);
    return rmulinto(t->s, out, t->th, 1);
}

/* one step over the families it[0..cnt): per adds the period times N, x is for the unknown itself, not the angle; the
   tokens go to *copy too */
NOINLINE static uint8_t fxst(tr_t *t, uint16_t rule, const fm_t *it, uint8_t cnt, uint8_t per, uint8_t x, uint8_t *copy)
{
    ps_t *s = t->s;
    uint8_t eb[MAXOUT], n = 0, i;
    int pp, qq, g;
    rat_t *lhs = x ? t->lx : t->u;
    for (i = 0; i < cnt; i++) {
        if (x ? !fxA(t, &it[i], t->av) : (fxth(t->th, it[i].dg, t->pm), 0)) return 0;
        pp = qq = 0;
        if (per) {
            pp = it[i].p * (x ? t->bd : 1); qq = (x ? iabs(t->bn) : 1) * (s->deg ? 1 : 180);
            g = gcd(pp, qq); pp /= g; qq /= g;
        }
        if (!(n = fxfam(s, eb, n, lhs, x ? t->av : t->th, it[i].ak, t->f, t->c, pp, qq))) return 0;
    }
    if (rule && s->st && !s->st[2]) stept(s, rule, 0, 0, eb, n);
    if (copy) for (i = 0; i < n; i++) copy[i] = eb[i];
    return n;
}

/* f = v: the steps and the families in t->all. 1 done, 2 no angle has it, 0 out of scope */
NOINLINE static uint8_t fxval(tr_t *t, int32_t vn, int32_t vd, int32_t vr)
{
    ps_t *s = t->s;
    fm_t pr, fam[2], two[2];
    int o[2], a, p, np;
    uint8_t nf, i, f = t->f;
    term_t z;
    if (f != K_TAN && vn * vn * vr > vd * vd) { if (s->st && !s->st[2]) stept(s, SR_CYRNG, 0, 0, 0, 0); return 2; }
    a = fxang(f, vn, vd, vr);
    pr.p = 0; pr.ak = 0;
    if (a == 999) {                                     /* an arcsin form, when the angle is the unknown */
        if (!t->arcok) return 0;
        z.num = vn; z.den = vd; z.mono = 0; z.rad = (uint32_t)vr;
        fxt(t->c, &z);
        pr.dg = 0; pr.ak = 1;
        p = f == K_TAN ? 180 : 360;
        nf = np = f == K_TAN ? 1 : 2;
        fam[0].dg = two[0].dg = 0; fam[0].ak = two[0].ak = 1; fam[0].p = fam[1].p = two[0].p = p;
        fam[1].dg = two[1].dg = 0; fam[1].ak = two[1].ak = f == K_SIN ? 2 : 3; two[1].p = p;
    } else {
        pr.dg = a;
        nf = fxfams(f, a, o, &p);
        np = f == K_TAN || o[0] == o[1] ? 1 : 2;
        for (i = 0; i < 2; i++) { two[i].dg = fam[i].dg = o[i]; two[i].ak = fam[i].ak = 0; two[i].p = fam[i].p = p; }
    }
    if (t->na + nf > 6) return 0;
    if (!fxst(t, SR_CYIS + f - K_SIN, &pr, 1, 0, 0, 0)) return 0;
    if (np == 2 && !fxst(t, SR_CYAS + (f == K_COS), two, 2, 0, 0, 0)) return 0;
    if (!fxst(t, SR_CYPER, fam, nf, 1, 0, 0)) return 0;
    for (i = 0; i < nf; i++) t->all[t->na++] = fam[i];
    return 1;
}

/* family i: its first answer for the unknown (t->av) and its period (y) */
NOINLINE static uint8_t fxfp(tr_t *t, uint8_t i, rat_t *y)
{
    term_t z;
    int pp = t->all[i].p * t->bd, qq = iabs(t->bn) * (t->s->deg ? 1 : 180), g;
    if (!fxA(t, &t->all[i], t->av)) return 0;
    g = gcd(pp, qq);
    z.num = pp / g; z.den = qq / g; z.mono = t->pm; z.rad = 1;
    fxt(y, &z);
    return 1;
}

/* the rest of the scratch values, taken late: a quadratic's factoring needs the pool before them */
NOINLINE static uint8_t fxget(ps_t *s, tr_t *t)
{
    return (t->th = get(s)) && (t->lx = get(s)) && (t->av = get(s)) && (t->c = get(s));
}

/* a sin(bX + c) + ... = ..., or a quadratic in sin(bX + c) (cos, tan too); lo, hi: only the answers in [lo, hi] */
NOINLINE static uint8_t fxtrig(ps_t *s, rat_t *l, rat_t *r, uint8_t j, unsigned used, uint8_t *out, rat_t *lo, rat_t *hi)
{
    rat_t *w = get(s), *lp = get(s), *rp = get(s), *a = get(s), *gr = get(s), *v, *x, *y;
    tr_t t;
    fm_t tm;
    fl_t fl;
    rat_t *u;
    const term_t *tt;
    term_t cf[3], cq, z;
    int32_t vn, vd, e;
    uint8_t k, m, i, d = 0, n = 0, crad = 1, key[MAXF], cnt, top, roots = 0, sd;
    int64_t va;
    if (!gr || !(t.c0 = get(s))) return 0;
    for (k = 0; k < s->nk && !(used >> k & 1); k++) ;
    if (k == s->nk || used != 1u << k) return 0;
    t.s = s; t.f = s->kf[k]; t.na = 0; t.pm = 0; t.bn = 0; t.bd = 1;
    u = t.u = s->ka[k];
    if (!isconst(&u->d) || u->d.n != 1 || u->d.t[0].num != 1 || u->d.t[0].den != 1) return 0;
    if (!s->deg) {                                      /* π as an angle: its kernel, made if the entry had none */
        if (!kvar(s, w, K_PI, 0, 0)) return 0;
        t.pm = w->n.t[0].mono;
    }
    t.c0->n.n = 0; pconst(&t.c0->d, 1);
    for (tt = u->n.t; tt < u->n.t + u->n.n; tt++) {     /* bX + c0 */
        if (tt->rad != 1) return 0;
        if (tt->mono == 1u << 4 * j) { t.bn = tt->num; t.bd = tt->den; }
        else if (!tt->mono || (t.pm && tt->mono == t.pm)) { if (!addterm(&t.c0->n, tt)) return 0; }
        else return 0;
    }
    if (!t.bn || t.bn > 999 || t.bn < -999 || t.bd > 999) return 0;
    t.arcok = t.bn == 1 && t.bd == 1 && !t.c0->n.n;
    cq.num = 0; cq.den = 1;                             /* the coefficients of f^0, f^1, f^2; a root of a number in f^0 */
    for (i = 0; i < 3; i++) { cf[i].num = 0; cf[i].den = 1; }
    for (i = 0; i < 2; i++)
        for (tt = (i ? r : l)->n.t; tt < (i ? r : l)->n.t + (i ? r : l)->n.n; tt++) {
            e = tt->mono >> 4 * KN(s, k) & 15;
            if (tt->mono != (unsigned)e << 4 * KN(s, k) || e > 2) return 0;
            if (tt->rad == 1) { if (!radd(&cf[e], i ? -tt->num : tt->num, tt->den)) return 0; }
            else if (!e && (crad == 1 || crad == tt->rad)) { crad = (uint8_t)tt->rad; if (!radd(&cq, i ? -tt->num : tt->num, tt->den)) return 0; }
            else return 0;
        }
    if (!isconst(&l->d) || !isconst(&r->d)) return 0;
    d = cf[2].num ? 2 : cf[1].num ? 1 : 0;
    if (!d || (crad != 1 && (d == 2 || cf[0].num))) return 0;
    if (d == 1) {
        if (r->n.n && r->n.t[0].mono) { fxdiff(lp, l, r); rp->n.n = 0; pconst(&rp->d, 1); }
        else { *lp = *l; *rp = *r; }
        if (!fxget(s, &t)) return 0;
        fxlin(s, lp, rp, j, w, t.th, t.av, t.lx, 1u << 4 * KN(s, k));
        if (crad != 1) { vn = -cq.num * cf[1].den; vd = cq.den * cf[1].num; }
        else { vn = -cf[0].num * cf[1].den; vd = cf[0].den * cf[1].num; }
        if (vd < 0) { vn = -vn; vd = -vd; }
        e = gcd(vn < 0 ? -vn : vn, vd); if (e) { vn /= e; vd /= e; }
        if (!vn) vd = 1;
        if (!fxval(&t, vn, vd, crad)) return 0;
        roots = 1;
    } else {
        for (i = 0; i < 2; i++) {
            y = i ? rp : lp;
            y->n.n = 0; pconst(&y->d, 1);
            for (tt = (i ? r : l)->n.t; tt < (i ? r : l)->n.t + (i ? r : l)->n.n; tt++) {
                z = *tt; z.mono = (unsigned)(tt->mono >> 4 * KN(s, k) & 15) << 4 * j;
                if (!addterm(&y->n, &z)) return 0;
            }
        }
        if (!fxdiff(a, lp, rp) || !a->n.n) return 0;
        rconst(gr, 1); gr->n.t[0].mono = 1u << 4 * KN(s, k);
        sd = s->var[NV - 1 - j] == 0x55 ? 0x56 : 0x55;
        xs(s, SR_CYLET, (rat_t *[]){ gr, 0 }, lp, rp, j, sd, w);
        if (!fxfac(s, &fl, a, j, rp->n.n != 0, lp, rp, sd, out) || !fxkeys(s, &fl, w, j, key, &cnt) || !fxget(s, &t)) return 0;
        top = s->top;
        for (i = 0; i < cnt; i++) {
            s->top = top;
            v = lp;
            if (!root(s, v, fl.f[key[i]], j)) return 0;
            tt = v->n.t;
            if (v->n.n > 1 || (v->n.n && (tt->mono || !isconst(&v->d) || v->d.n != 1 || v->d.t[0].num != 1 || v->d.t[0].den != 1))) return 0;
            xs(s, SR_CYBACK, (rat_t *[]){ gr, 0 }, gr, v, j, 0, w);
            m = v->n.n ? fxval(&t, tt->num, tt->den, (int32_t)tt->rad) : fxval(&t, 0, 1, 1);
            if (!m) return 0;
            roots += m == 1;
        }
    }
    if (!t.na) return fxnone(s, out);
    /* the unknown itself */
    z.num = z.den = 1; z.mono = 1u << 4 * j; z.rad = 1;
    fxt(t.lx, &z);
    x = lp; y = rp;                                     /* free by now */
    s->wide = 1;
    for (i = 1; i < t.na; i++) {                        /* by angle */
        tm = t.all[i];
        for (m = i; m && t.all[m - 1].dg > tm.dg; m--) t.all[m] = t.all[m - 1];
        t.all[m] = tm;
    }
    for (i = 0; i < t.na; i++)                          /* a and a + half a period: a + half the period times N */
        for (m = i + 1; m < t.na; m++)
            if (!t.all[i].ak && !t.all[m].ak && t.all[i].p == t.all[m].p && t.all[m].dg - t.all[i].dg == t.all[i].p / 2) {
                t.all[i].p /= 2;
                for (t.na--; m < t.na; m++) t.all[m] = t.all[m + 1];
                break;
            }
    if (!lo) {
        n = fxst(&t, !t.arcok ? SR_CYX : roots > 1 ? SR_CYSUM : 0, t.all, t.na, 1, !t.arcok, out);
        return n;
    }
    {                                                   /* the answers between lo and hi, smallest first */
        struct { int64_t v; uint8_t fam; int8_t N; } en[12], tmp;
        int64_t vb;
        int sg, sg2, N, n0, n1, ne = 0;
        uint8_t pbuf[MAXOUT], pn;
        if (t.arcok && (t.all[0].ak)) return 0;
        for (i = 0; i < t.na; i++) {
            if (!fxfp(&t, i, y)) return 0;              /* its first answer, and the period of the unknown */
            va = fxv(t.av, &sg);
            vb = fxv(y, &sg2);
            if (sg == 9 || !vb) return 0;
            n0 = (int)((fxv(lo, &sg) - va) / vb); n1 = (int)((fxv(hi, &sg) - va) / vb);
            if (sg == 9 || n1 - n0 > 20) return 0;
            for (N = n0 - 1; N <= n1 + 1; N++) {
                rconst(x, N);
                if (!rmulinto(s, x, y, 0) || !raddinto(s, x, t.av)) return 0;
                if (!fxdiff(a, x, lo)) return 0;
                if (fxv(a, &sg), sg < 0) continue;
                if (!fxdiff(a, hi, x)) return 0;
                if (fxv(a, &sg), sg < 0) continue;
                if (ne == 12) return 0;
                tmp.v = fxv(x, &sg); tmp.fam = i; tmp.N = (int8_t)N;
                for (m = ne++; m && en[m - 1].v > tmp.v; m--) en[m] = en[m - 1];
                en[m] = tmp;
            }
        }
        if (!ne) return fxnone(s, out);
        for (i = 0; i < ne; i++) {
            fxfp(&t, en[i].fam, y);
            rconst(x, en[i].N);
            if (!rmulinto(s, x, y, 0) || !raddinto(s, x, t.av) || !(n = fxans(s, x, j, i, out, n))) return 0;
        }
        pn = answer(s, lo, pbuf, 0);
        if (pn && pn < MAXOUT - 1) { pbuf[pn++] = T_LCOMMA; pn = answer(s, hi, pbuf, pn); }
        if (s->st && !s->st[2]) stept(s, SR_CYBND, pbuf, pn, out, n);
    }
    return n;
}

/* SOLVE(L=R,V) with e^(), a^X or ln( log( in it: engine()'s, when command() gave up. 0 when it is none of
   those, or out of scope. */
NOINLINE static uint8_t fxsolve(ps_t *s, uint8_t *out)
{
    rat_t *l, *r, *lo = 0, *hi = 0;
    unsigned used = 0;
    uint8_t j, k, m, ne = 0, nl = 0, no = 0, nt = 0, res = 0, tp, nk;
    unsigned lm;
    const term_t *t;
    s->fn = C_TEXPAND; s->noexp = s->home = 1; s->nk = 0; s->lim = NPOOL;
    s->i = 0; s->top = 0; s->depth = 0;
    streset(s->st);
    l = get(s); r = get(s);
    if (!l || !r || !expr(s, l) || peek(s) != T_EQ) goto done;
    s->i++;
    if (!expr(s, r) || peek(s) != T_COMMA || s->i + 1 >= s->len || !is_var(s->in[s->i + 1])) goto done;
    j = NV - 1 - s->rank[s->in[s->i + 1] - T_VAR0];
    s->i += 2;
    if (peek(s) == T_COMMA) {                           /* SOLVE(eq,X,lo,hi): the answers from lo to hi */
        s->i++;
        lo = get(s); hi = get(s);
        if (!hi || !expr(s, lo) || peek(s) != T_COMMA) goto done;
        s->i++;
        if (!expr(s, hi) || reduce(s, lo) != 1) goto done;
        if (reduce(s, hi) != 1) goto done;
    }
    if (peek(s) == T_RPAR) s->i++;
    if (s->i != s->len || reduce(s, l) != 1 || reduce(s, r) != 1) goto done;
    for (m = 0; m < 2; m++)
        for (t = (m ? r : l)->n.t; t < (m ? r : l)->n.t + (m ? r : l)->n.n; t++)
            for (k = 0; k < s->nk; k++) if (t->mono >> 4 * KN(s, k) & 15) used |= 1u << k;
    for (k = 0; k < s->nk; k++)
        if (used >> k & 1) {
            if (s->kf[k] == K_EXP || s->kf[k] == K_POW) ne++;
            else if (s->kf[k] == K_LOG) nl++;
            else if (s->kf[k] <= K_TAN) nt++;
            else no++;
        }
    if (!no && nt && !ne && !nl) res = fxtrig(s, l, r, j, used, out, lo, hi);
    else if (!no && !nt && !lo && !ne != !nl) {
        tp = s->top; lm = s->lim; nk = s->nk;
        res = ne ? fxexp(s, l, r, j, used, out) : fxlog(s, l, r, j, used, out);
        if (!res && ne == 2) {                          /* two bases that are no powers of one number */
            s->top = tp; s->lim = lm; s->nk = nk;
            streset(s->st);
            res = fxtwo(s, l, r, j, used, out);
        }
    }
done:
    s->fn = s->noexp = s->home = 0; s->nk = 0; s->lim = NPOOL;
    return res;
}
#endif

#ifndef NODESOLVE      /* engine_device.py builds without it: the engine and its vectors must fit 150 KB of RAM */
/* ---- DESOLVE: a driver over the engine's own commands ----
   It never touches the rational pool: every step is a token string run through
   engine() (MODE_EVAL) one call at a time, so each call has the whole pool and
   NV = 6 to itself. A string is [length, tokens...]. */
#define DL 160                        /* the longest expression built */
#define TL 38                         /* the longest piece kept */
typedef uint8_t ts_t[TL + 2];
typedef struct { void *w; uint8_t *st, mode, x, y, c[2], q; } ds_t;

static uint8_t vfm(uint8_t *o, const ds_t *d, const char *f, va_list a)
{
    uint8_t n = 0, c, k;
    const uint8_t *p;
    for (; (c = (uint8_t)*f) && n < DL - 1; f++) {
        if (c == '$') {
            p = va_arg(a, const uint8_t *);
            for (k = 0; k < p[0] && n < DL - 1; k++) o[1 + n++] = p[1 + k];
            continue;
        }
        if (c == '&' || c == ';') {   /* & a factor after a letter: nothing for 1, in brackets unless one piece; ; "+ value", or "- value" when it is negative */
            p = va_arg(a, const uint8_t *);
            if (c == ';') {
                o[1 + n++] = p[1] == T_NEG ? T_SUB : T_ADD;
                for (k = 1 + (p[1] == T_NEG); k <= p[0] && n < DL - 1; k++) o[1 + n++] = p[k];
            } else if (p[0] != 1 || p[1] != 0x31) {
                uint8_t dp = 0, br = p[1] == T_NEG, dg = 1;
                for (k = 1; k <= p[0]; k++) {
                    dp += p[k] == T_LPAR || p[k] == T_SQRT || p[k] == T_ABS || (p[k] >= T_LN && p[k] <= T_ATAN);
                    dp -= p[k] == T_RPAR;
                    dg &= is_digit(p[k]);
                    br |= !dp && (p[k] == T_ADD || p[k] == T_SUB || p[k] == T_DIV);
                }
                br |= is_digit(p[1]) && !dg;
                if (br) o[1 + n++] = T_LPAR;
                for (k = 1; k <= p[0] && n < DL - 2; k++) o[1 + n++] = p[k];
                if (br) o[1 + n++] = T_RPAR;
            }
            continue;
        }
        if (c == 'o') { o[1 + n++] = T_OR; continue; }
        if (c == '@') {                  /* cos( as an answer spells it: the letters c o s and the bracket */
            static const uint8_t lc[7] = { T_BB, 0xB2, T_BB, 0xBF, T_BB, 0xC3, T_LPAR };
            for (k = 0; k < 7 && n < DL - 7; k++) o[1 + n++] = lc[k];
            continue;
        }
        if (c >= 'A' && c <= 'Z' && f[1] >= 'A' && f[1] <= 'Z') {   /* a command's name: lowercase tokens, as the menus type it */
            for (; *f >= 'A' && *f <= 'Z' && n < DL - 2; f++) {
                o[1 + n++] = T_BB;
                o[1 + n++] = (uint8_t)(0xB0 + (*f - 'A') + (*f >= 'K'));
            }
            f--;
            continue;
        }
        c = c >= '0' && c <= '9' ? c : c >= 'A' && c <= 'Z' ? (uint8_t)(c - 'A' + T_VAR0) :
            c == '+' ? T_ADD : c == '-' ? (!n || (o[n] != T_RPAR && !is_digit(o[n]) && !is_var(o[n])) ? T_NEG : T_SUB) : c == '*' ? T_MUL : c == '/' ? T_DIV :
            c == '^' ? T_POW : c == '(' ? T_LPAR : c == ')' ? T_RPAR : c == ',' ? T_COMMA :
            c == '\'' ? T_PRIME : c == '=' ? T_EQ : c == 'x' ? d->x : c == 'y' ? d->y : c == 'c' ? d->c[0] :
            c == 'd' ? d->c[1] : c == 'e' ? T_EXP : c == 's' ? T_SIN : c == 'k' ? 0xC4 :
            c == 'n' ? T_NEG : c == 'i' ? T_I : c == 't' ? T_TAN : c == 'l' ? T_LN : T_SQRT;
        o[1 + n++] = c;
    }
    o[0] = *f ? 0 : n;
    return o[0];
}

/* steps( : one step, its parameter P (a piece, or 0) and the expression the format spells */
static void dsl(const ds_t *d, uint16_t rule, const uint8_t *P, const char *f, ...)
{
    uint8_t e[DL + 1];
    va_list a;
    if (!d->st) return;
    va_start(a, f);
    vfm(e, d, f, a);
    va_end(a);
    if (e[0]) stlog(d->st, rule, P ? P + 1 : 0, P ? P[0] : 0, e + 1, e[0], 1);
}

static void dslr(const ds_t *d, uint16_t rule, const uint8_t *e, uint8_t n)
{
    if (d->st && n) stlog(d->st, rule, 0, 0, e, n, 1);
}

/* r = engine(format), 1 when it answered in cap tokens (64 at most) */
static uint8_t qn(const ds_t *d, uint8_t *r, uint8_t cap, const char *f, ...)
{
    uint8_t e[DL + 1], o[MAXOUT + 2], n, i;
    va_list a;
    va_start(a, f);
    n = vfm(e, d, f, a);
    va_end(a);
    r[0] = 0;
    if (!n) return 0;
    n = engine(e + 1, n, o, d->w, 0, d->mode, 0);
    if (n == SYMCE_ERR || n > cap) return 0;
    for (i = 0; i < n; i++) r[1 + i] = o[i];
    r[0] = n;
    return n != 0;
}
#define q(d, r, ...) qn(d, r, TL, __VA_ARGS__)

static uint8_t zero(const uint8_t *r) { return r[0] == 1 && r[1] == 0x30; }

/* a plain rational: -, digits, / digits. Fills n/d. */
static uint8_t rd(const uint8_t *r, int32_t *n, int32_t *dn)
{
    uint8_t i = 1, neg = r[1] == T_NEG;
    int32_t v = 0, w = 1;
    i += neg;
    for (; i <= r[0] && is_digit(r[i]); i++) v = v * 10 + r[i] - 0x30;
    if (i <= r[0] && r[i] == T_DIV)
        for (w = 0, i++; i <= r[0] && is_digit(r[i]); i++) w = w * 10 + r[i] - 0x30;
    *n = neg ? -v : v;
    *dn = w;
    return i > r[0] && w > 0 && r[0] > neg;
}

/* o = s with every token v replaced by (r) */
static uint8_t svar(uint8_t *o, const uint8_t *s, uint8_t v, const uint8_t *r)
{
    uint8_t n = 0, i, k;
    for (i = 1; i <= s[0]; i++) {
        if (s[i] != v) { if (n >= DL) return o[0] = 0; o[1 + n++] = s[i]; continue; }
        if (n + r[0] + 2 > DL) return o[0] = 0;
        o[1 + n++] = T_LPAR;
        for (k = 1; k <= r[0]; k++) o[1 + n++] = r[k];
        o[1 + n++] = T_RPAR;
    }
    return o[0] = n;
}

/* r = f(order-th derivative) at x = v */
static uint8_t at(const ds_t *d, uint8_t *r, const uint8_t *f, uint8_t order, const uint8_t *v)
{
    ts_t a, b;
    uint8_t e[DL + 1], i;
    for (i = 0; i <= f[0]; i++) a[i] = f[i];
    for (; order; order--) {
        if (!q(d, b, "DERIV($,x)", a)) return 0;
        for (i = 0; i <= b[0]; i++) a[i] = b[i];
    }
    if (!svar(e, a, d->x, v)) return 0;
    return q(d, r, "$", e);
}

static uint8_t opens(uint8_t c)
{
    return c == T_LPAR || c == T_EXP || c == T_SIN || c == 0xC4 || c == T_SQRT || c == T_LN ||
           c == T_ABS || c == T_TAN || c == T_LOG || c == T_ASIN || c == T_ACOS || c == T_ATAN;
}

/* the n-th (0..) top-level term of r, with its sign, into t; 0 when there is none */
static uint8_t nth(const uint8_t *r, uint8_t n, uint8_t *t)
{
    uint8_t i = 1, s, dp;
    for (;; n--) {
        if (i > r[0]) return 0;
        s = i;
        dp = 0;
        if (r[i] == T_ADD || r[i] == T_SUB) i++;
        for (; i <= r[0] && (dp || (r[i] != T_ADD && r[i] != T_SUB)); i++)
            if (r[i] == T_BB) i++;                              /* two-byte: the letters of cos( */
            else dp += opens(r[i]) - (r[i] == T_RPAR);
        if (!n) break;
    }
    if (!t) return 1;
    t[0] = 0;
    if (r[s] == T_SUB) t[++t[0]] = T_NEG;
    else if (r[s] == T_ADD) ;
    else t[++t[0]] = r[s];
    for (s++; s < i; s++) t[++t[0]] = r[s];
    return 1;
}

/* only digits, signs, / and parentheses: a plain rational */
static uint8_t num(const uint8_t *r)
{
    uint8_t i;
    for (i = 1; i <= r[0]; i++)
        if (!is_digit(r[i]) && r[i] != T_DOT && r[i] != T_NEG && r[i] != T_DIV && r[i] != T_ADD &&
            r[i] != T_SUB && r[i] != T_MUL && r[i] != T_LPAR && r[i] != T_RPAR) return 0;
    return 1;
}

static void cp(uint8_t *to, const uint8_t *from)
{
    uint8_t i;
    for (i = 0; i <= from[0]; i++) to[i] = from[i];
}


/* ---- gaps-de: exponentials of integrals, by hand where the engine holds no kernel ---- */

/* the token closing the opener at i */
static uint8_t mcl(const uint8_t *t, uint8_t i)
{
    uint8_t dp = 0;
    for (; i <= t[0]; i++) {
        if (t[i] == T_BB) { i++; continue; }
        dp += opens(t[i]) - (t[i] == T_RPAR);
        if (!dp) break;
    }
    return i;
}

/* b + t as a sum: " + t", or " - t" when t starts with a minus; 0 when it passes cap tokens */
static uint8_t sadd(uint8_t *b, const uint8_t *t, uint8_t cap)
{
    uint8_t i = 1, ng = t[1] == T_NEG;
    if (b[0] + t[0] + (b[0] && !ng) > cap) return 0;
    if (b[0]) { b[++b[0]] = ng ? T_SUB : T_ADD; i += ng; }
    for (; i <= t[0]; i++) b[++b[0]] = t[i];
    return 1;
}

/* r = the tokens f spells: counted at r[0], r[1..] when sh is 1, else plain from r[0]; their number, 0 past cap */
NOINLINE static uint8_t dsfmt(const ds_t *d, uint8_t *r, uint8_t cap, uint8_t sh, const char *f, ...)
{
    uint8_t e[DL + 1], n, i;
    va_list a;
    va_start(a, f);
    n = vfm(e, d, f, a);
    va_end(a);
    if (n > cap) n = 0;
    for (i = 0; i < n; i++) r[sh + i] = e[1 + i];
    if (sh) r[0] = n;
    return n;
}

/* a term c ln(abs(u)) / n, or c ln(u) / n: the coefficient c/n (signed, 1 when none) to cf, u to u */
static uint8_t lnsplit(const uint8_t *t, uint8_t *cf, uint8_t *u)
{
    uint8_t p = 1, e, a, k;
    while (p <= t[0] && (is_digit(t[p]) || t[p] == T_NEG)) p++;
    if (p > t[0] || t[p] != T_LN) return 0;
    e = mcl(t, p);
    if (e > t[0]) return 0;
    if (e < t[0]) {
        if (t[e + 1] != T_DIV || e + 2 > t[0]) return 0;
        for (k = e + 2; k <= t[0]; k++) if (!is_digit(t[k])) return 0;
    }
    a = t[p + 1] == T_ABS && mcl(t, p + 1) == e - 1;
    for (cf[0] = 0, k = 1; k < p; k++) cf[++cf[0]] = t[k];
    if (p == 1 || (p == 2 && t[1] == T_NEG)) cf[++cf[0]] = 0x31;
    for (k = e + 1; k <= t[0]; k++) cf[++cf[0]] = t[k];
    for (u[0] = 0, k = p + 1 + a; k < e - a; k++) u[++u[0]] = t[k];
    return u[0] != 0;
}

/* r = a b (r may be a or b) */
static uint8_t dsmul(const ds_t *d, uint8_t *r, const uint8_t *a, const uint8_t *b)
{
    ts_t t;
    if (a[0] == 1 && a[1] == 0x31) { cp(r, b); return 1; }
    if (b[0] == 1 && b[1] == 0x31) { cp(r, a); return 1; }
    if (q(d, t, "($)*($)", a, b)) { cp(r, t); return 1; }                  /* (a failed q leaves t empty, r untouched) */
    return dsfmt(d, t, TL, 1, "&&", a, b) && (cp(r, t), 1);
}

/* r = e^(m A): each term c ln(abs(u)) of A gives u^(m c) and the others go together under one e^(; the
   engine holds no e^(cos(X)), so that factor is written out. 1: only the e^(, 2: a power of a u too, 0: not built */
NOINLINE static uint8_t eexp(const ds_t *d, uint8_t *r, const uint8_t *A, const uint8_t *m)
{
    ts_t t, cf, u, N;
    uint8_t i, ln = 0, one = m[0] == 1 && m[1] == 0x31;
    r[0] = 1; r[1] = 0x31; N[0] = 0;
    for (i = 0; nth(A, i, t); i++) {
        if (!lnsplit(t, cf, u)) { if (!sadd(N, t, TL)) return 0; continue; }
        if (!q(d, cf, "($)*($)", m, cf)) return 0;
        if (!(cf[0] == 1 && cf[1] == 0x31) && !q(d, u, "($)^($)", u, cf)) return 0;
        if (!dsmul(d, r, r, u)) return 0;
        ln = 1;
    }
    if (N[0]) {
        if (!one && !q(d, N, "($)*($)", m, N)) return 0;
        if (N[0] + 2 > TL) return 0;
        for (i = N[0]; i; i--) N[i + 1] = N[i];
        N[1] = T_EXP; N[0] += 2; N[N[0]] = T_RPAR;
        if (!dsmul(d, r, r, N)) return 0;
    }
    return 1 + ln;
}

/* r = mu W, a sum of its terms times mu. When pat (e^(-A) written out) is there and mu is the whole e^(A), a term of
   W that has pat as a factor loses it, which is what e^(A) e^(-A) is */
NOINLINE static uint8_t emul(const ds_t *d, uint8_t *r, const uint8_t *mu, const uint8_t *W, const uint8_t *pat)
{
    ts_t t, tm;
    uint8_t i, k, j, dp, ng, f;
    r[0] = 0;
    for (i = 0; nth(W, i, t); i++) {
        for (k = 1, dp = 0, f = 0; pat[0] && k + pat[0] - 1 <= t[0]; k++) {
            if (!dp) {
                for (j = 0; j < pat[0] && t[k + j] == pat[1 + j]; j++) ;
                if (j == pat[0]) { f = k == 1 || t[k - 1] != T_DIV; break; }
            }
            if (t[k] == T_BB) k++;
            else dp += opens(t[k]) - (t[k] == T_RPAR);
        }
        if (f) {                                            /* cancelled: what is left of the term */
            j = k + pat[0];
            for (tm[0] = 0, ng = 1; ng < k; ng++) tm[++tm[0]] = t[ng];
            if ((tm[0] == 0 || (tm[0] == 1 && tm[1] == T_NEG)) && (j > t[0] || t[j] == T_DIV)) tm[++tm[0]] = 0x31;
            for (; j <= t[0]; j++) tm[++tm[0]] = t[j];
        } else {
            ng = t[1] == T_NEG;
            for (tm[0] = 0, j = 1 + ng; j <= t[0]; j++) tm[++tm[0]] = t[j];
            if (!dsmul(d, tm, mu, tm) || (ng && tm[0] >= TL)) return 0;
            if (ng) { for (j = tm[0]; j; j--) tm[j + 1] = tm[j]; tm[1] = T_NEG; tm[0]++; }
        }
        if (!sadd(r, tm, MAXOUT)) return 0;
    }
    return r[0] != 0;
}

/* A = the integral of a. tan(u) is first written sin(u)/cos(u), whose integral comes out as ln|cos|, not ln(tan^2+1)/2 */
static uint8_t dsint(const ds_t *d, uint8_t *A, const uint8_t *a)
{
    ts_t t;
    uint8_t i, j, k, n = 0;
    for (i = 1; i <= a[0]; i++) {
        if (a[i] != T_TAN) { if (n >= TL) return 0; t[++n] = a[i]; continue; }
        j = mcl(a, i);
        if (n + 2 * (j - i) + 8 > TL) return 0;
        t[++n] = T_LPAR; t[++n] = T_SIN;
        for (k = i + 1; k < j; k++) t[++n] = a[k];
        t[++n] = T_RPAR; t[++n] = T_DIV; t[++n] = 0xC4;
        for (k = i + 1; k < j; k++) t[++n] = a[k];
        t[++n] = T_RPAR; t[++n] = T_RPAR;
        i = j;
    }
    t[0] = n;
    return q(d, A, "INTEGRAL($,x)", t);
}

/* Q <- (P - c1 Q' - c2 Q'')/c0, m times from 0: the derivatives die out, so it is exact */
static uint8_t piter(const ds_t *d, uint8_t *Q, const uint8_t *P, const uint8_t *c0, const uint8_t *c1,
                     const uint8_t *c2, uint8_t m)
{
    ts_t a, b;
    Q[0] = 1; Q[1] = 0x30;
    for (; m; m--)
        if (!q(d, a, "DERIV($,x)", Q) || !q(d, b, "DERIV($,x)", a) ||
            !q(d, Q, "(($)-($)*($)-($)*($))/($)", P, c1, a, c2, b, c0)) return 0;
    return 1;
}

/* yp = a particular solution of c2 y'' + c1 y' + c0 y = t, t one term: P(x), P e^(kx), P cos(wx),
   P sin(wx); c2 may be 0 (then c1 is not) */
NOINLINE static uint8_t dspart(const ds_t *d, const uint8_t *t, const uint8_t *c2, const uint8_t *c1,
                               const uint8_t *c0, uint8_t *yp)
{
    ts_t P, k, A, B, a, b, c, e;
    uint8_t i, j = 0, h = 0, kind = 0, cs = 0, m, dp, x0[2] = { 1, 0x30 };
    for (i = 1; i <= t[0] && !kind; i++) {
        if (t[i] == T_EXP) { kind = 1; h = i; j = i + 1; }
        else if (t[i] == T_SIN || t[i] == 0xC4) { kind = 2; cs = t[i] == 0xC4; h = i; j = i + 1; }
        else if (t[i] == T_BB && i + 6 <= t[0] && t[i + 1] == 0xB2 && t[i + 3] == 0xBF && t[i + 5] == 0xC3) {
            kind = 2; cs = 1; h = i; j = i + 7;
        } else if (t[i] == T_LN || t[i] == T_TAN || t[i] == T_LOG || t[i] == T_BB) return 0;
    }
    if (kind) {
        for (dp = 1, i = j; i <= t[0]; i++) {
            dp += opens(t[i]) - (t[i] == T_RPAR);
            if (!dp) break;
        }
        if (dp) return 0;
        for (c[0] = 0, m = h; m <= i; m++) c[++c[0]] = t[m];       /* the call */
        for (a[0] = 0, m = j; m < i; m++) a[++a[0]] = t[m];        /* its argument */
        if (!q(d, P, "($)/($)", t, c) || !q(d, k, "($)/x", a) || !num(k)) return 0;
    } else { cp(P, t); cp(k, x0); }
    for (i = 1; i <= P[0]; i++)                                   /* P: a polynomial in x */
        if (P[i] == T_EXP || P[i] == T_SIN || P[i] == 0xC4 || P[i] == T_BB || P[i] == T_LN ||
            (is_var(P[i]) && P[i] != d->x)) return 0;
    cp(a, P);
    for (m = 0; !zero(a) && m < 9; m++)
        if (!q(d, b, "DERIV($,x)", a)) return 0;
        else cp(a, b);
    if (m == 9) return 0;
    if (kind != 2) {                                              /* e^(kx): c2 Q'' + (2 c2 k + c1) Q' + L(k) Q = P */
        if (!q(d, a, "($)*($)*($)+($)*($)+($)", c2, k, k, c1, k, c0) ||     /* L(k) */
            !q(d, b, "2*($)*($)+($)", c2, k, c1)) return 0;                 /* L'(k) */
        if (!zero(a)) { if (!piter(d, A, P, a, b, c2, m)) return 0; }
        else {
            if (zero(b)) { if (!q(d, c, "INTEGRAL(($)/($),x)", P, c2)) return 0; cp(e, c); }
            else if (!piter(d, e, P, b, c2, x0, m)) return 0;
            if (!q(d, A, "INTEGRAL($,x)", e)) return 0;
        }
        return kind ? q(d, yp, "($)*e($)*x)", A, k) : (cp(yp, A), 1);
    }
    /* w = k; y = A cos + B sin gives al A + be B + (c2 A'' + c1 A' + 2 c2 w B') = Pc,
       al B - be A + (c2 B'' + c1 B' - 2 c2 w A') = Ps, al = c0 - c2 w², be = c1 w */
    cp(c, cs ? P : x0);                                           /* Pc */
    cp(e, cs ? x0 : P);                                           /* Ps */
    if (!q(d, a, "($)-($)*($)*($)", c0, c2, k, k) || !q(d, b, "($)*($)", c1, k)) return 0;   /* al, be */
    if (!q(d, B, "($)*($)+($)*($)", a, a, b, b)) return 0;        /* al²+be² */
    cp(A, x0);
    if (!zero(B)) {
        ts_t r, s, u, v, w;
        cp(w, B);
        cp(B, x0);
        for (; m; m--)
            if (!q(d, r, "DERIV($,x)", A) || !q(d, s, "DERIV($,x)", r) || !q(d, u, "DERIV($,x)", B) ||
                !q(d, v, "DERIV($,x)", u)) return 0;
            else {
                ts_t rc, rs;
                if (!q(d, rc, "($)-($)*($)-($)*($)-2*($)*($)*($)", c, c2, s, c1, r, c2, k, u) ||
                    !q(d, rs, "($)-($)*($)-($)*($)+2*($)*($)*($)", e, c2, v, c1, u, c2, k, r) ||
                    !q(d, A, "(($)*($)-($)*($))/($)", a, rc, b, rs, w) ||
                    !q(d, B, "(($)*($)+($)*($))/($)", b, rc, a, rs, w)) return 0;
            }
    } else {                                                      /* resonance (c2 = 1, c1 = 0): the x-multiplied forms */
        ts_t r, s, w;
        cp(B, x0);
        if (!q(d, w, "2*($)*($)", c2, k)) return 0;
        for (; m; m--)
            if (!q(d, r, "DERIV($,x)", A) || !q(d, b, "(($)-($)*($))/($)", c, c2, r, w) ||
                !q(d, s, "DERIV($,x)", b) || !q(d, A, "(($)*($)-($))/($)", c2, s, e, w)) return 0;
            else cp(B, b);
        cp(a, A);
        if (!q(d, A, "INTEGRAL($,x)", a) || !q(d, a, "INTEGRAL($,x)", B)) return 0;
        cp(B, a);
    }
    return q(d, yp, "($)*k($)*x)+($)*s($)*x)", A, k, B, k);
}

/* o = (lhs)-(rhs) with y'', y', y standing for the pieces v2, v1, v0 (in parentheses) */
static uint8_t gb(uint8_t *o, const ds_t *d, const uint8_t *in, unsigned e0, unsigned eq,
                  const uint8_t *v2, const uint8_t *v1, const uint8_t *v0)
{
    unsigned i;
    uint8_t n = 0, k, c;
    const uint8_t *v;
#define EM(t) do { if (n >= DL) return o[0] = 0; o[1 + n++] = (uint8_t)(t); } while (0)
    EM(T_LPAR);
    for (i = 0; i < e0; i++) {
        if (i == eq) { EM(T_RPAR); EM(T_SUB); EM(T_LPAR); continue; }
        if (in[i] != d->y) { EM(in[i]); continue; }
        for (c = 0; i + 1 < e0 && (in[i + 1] == T_PRIME || in[i + 1] == 0x2A); i++) c += in[i + 1] == T_PRIME ? 1 : 2;
        if (c > 2) return o[0] = 0;
        v = c == 0 ? v0 : c == 1 ? v1 : v2;
        EM(T_LPAR);
        for (k = 1; k <= v[0]; k++) EM(v[k]);
        EM(T_RPAR);
    }
    EM(T_RPAR);
    return o[0] = n;
}

typedef struct { uint8_t o; ts_t x, v; } ic_t;       /* an initial condition: y^(o)(x) = v */

static uint8_t bf(const ds_t *d, uint8_t *o, const char *f, ...)
{
    va_list a;
    uint8_t n;
    va_start(a, f);
    n = vfm(o, d, f, a);
    va_end(a);
    return n;
}

/* a starting value's step: the condition e is its parameter, "lhs = rhs" (or "lhs = rhs + c", rhs the number c is added to) the equation it gives */
static void dsv(const ds_t *d, const ic_t *e, const uint8_t *lhs, const uint8_t *rhs, uint8_t withc)
{
    uint8_t c[DL + 1], m[DL + 1];
    if (!d->st || !lhs[0]) return;
    if (!bf(d, c, e->o ? "y'($)=$" : "y($)=$", e->x, e->v)) return;
    if (!(withc ? (zero(rhs) ? bf(d, m, "$=c", lhs) : bf(d, m, "$=$+c", lhs, rhs)) : bf(d, m, "$=$", lhs, rhs))) return;
    stlog(d->st, SR_DYV, c + 1, c[0], m + 1, m[0], 1);
}

/* the engine's SOLVE-style answer to out: Y=expr */
static uint8_t dsfin(const ds_t *d, uint8_t *out, const uint8_t *ex)
{
    uint8_t i;
    if (!ex[0] || ex[0] + 2 > MAXOUT) return 0;
    out[0] = d->y; out[1] = T_EQ;
    for (i = 1; i <= ex[0]; i++) out[1 + i] = ex[i];
    return (uint8_t)(ex[0] + 2);
}

/* Y = ex, with e^(kx) factored out when it is common to all: e^(2X)(C+DX), Ce^(2X) */
static uint8_t dsfac(const ds_t *d, uint8_t *out, const uint8_t *ex, const uint8_t *kf)
{
    uint8_t e[MAXOUT + 2], f[MAXOUT + 2], i, n;
    if (kf && !zero(kf) && qn(d, e, MAXOUT, "e($)*x)", kf) && qn(d, f, MAXOUT, "($)/($)", ex, e)) {
        for (i = 1; i <= f[0] && f[i] != T_EXP; i += 1 + (f[i] == T_BB)) ;
        if (i > f[0] && nth(f, 1, 0)) {       /* two terms or more: e^(kx)(...) */
            if (e[0] + f[0] + 4 > MAXOUT) return 0;
            out[0] = d->y; out[1] = T_EQ;
            n = 2;
            for (i = 1; i <= e[0]; i++) out[n++] = e[i];
            out[n++] = T_LPAR;
            for (i = 1; i <= f[0]; i++) out[n++] = f[i];
            out[n++] = T_RPAR;
            return n;
        }
    }
    return dsfin(d, out, ex);
}

/* c1 g1 [+ c2 g2] + yp, the constants c1, c2 from the initial conditions or left as letters */
NOINLINE static uint8_t dscomb(const ds_t *d, uint8_t *out, uint8_t ng, const uint8_t *g1, const uint8_t *g2,
                               const uint8_t *yp, const ic_t *ic, uint8_t nic, const uint8_t *kf)
{
    uint8_t fin[MAXOUT + 2];
    ts_t m[4], rr[2], c1, c2, t;
    uint8_t i, j;
    const uint8_t *g;
    if (!d->q && !zero(yp) && !(ng == 1 && g1[0] == 1 && g1[1] == 0x31))
        dsl(d, SR_DYA, 0, ng == 1 ? "y=c&;" : "y=c&+d&;", g1, ng == 1 ? yp : g2, yp);
    if (!nic) {
        if (ng == 1 ? qn(d, fin, MAXOUT, "c*($)+($)", g1, yp) : qn(d, fin, MAXOUT, "c*($)+d*($)+($)", g1, g2, yp))
            return dsfac(d, out, fin, kf);
        /* more kernels than the engine holds at once: the pieces as they are, c g1 + d g2 + yp */
        fin[0] = 0;
        for (i = 0; i < ng; i++) {
            g = i ? g2 : g1;
            if (fin[0] + g[0] + 2 > MAXOUT - 2) return 0;
            if (i) fin[++fin[0]] = T_ADD;
            fin[++fin[0]] = d->c[i];
            for (j = 1; j <= g[0]; j++) fin[++fin[0]] = g[j];
        }
        if (!zero(yp)) {
            if (fin[0] + yp[0] + 1 > MAXOUT - 2) return 0;
            fin[++fin[0]] = yp[1] == T_NEG ? T_SUB : T_ADD;
            for (j = 1 + (yp[1] == T_NEG); j <= yp[0]; j++) fin[++fin[0]] = yp[j];
        }
        return dsfin(d, out, fin);
    }
    if (nic != ng) return 0;
    for (i = 0; i < ng; i++) {                  /* order i's condition: o must be i */
        const ic_t *e = &ic[ic[0].o == i ? 0 : 1];
        if (e->o != i) return 0;
        if (!at(d, t, yp, i, e->x) || !q(d, rr[i], "($)-($)", e->v, t)) return 0;
        if (!at(d, m[2 * i], g1, i, e->x)) return 0;
        if (ng == 2 && !at(d, m[2 * i + 1], g2, i, e->x)) return 0;
        if (d->st && (ng == 1 ? qn(d, t, TL, "($)*c", m[0]) : qn(d, t, TL, "($)*c+($)*d", m[2 * i], m[2 * i + 1])))
            dsv(d, e, t, rr[i], 0);
    }
    if (ng == 1) { if (!q(d, c1, "($)/($)", rr[0], m[0])) return 0; }
    else {
        if (!q(d, t, "($)*($)-($)*($)", m[0], m[3], m[1], m[2]) ||
            !q(d, c1, "(($)*($)-($)*($))/($)", rr[0], m[3], m[1], rr[1], t) ||
            !q(d, c2, "(($)*($)-($)*($))/($)", m[0], rr[1], rr[0], m[2], t)) return 0;
    }
    dsl(d, SR_DYC, 0, "c=$", c1);
    if (ng == 2) dsl(d, SR_DYC, 0, "d=$", c2);
    if (!(ng == 1 ? qn(d, fin, MAXOUT, "($)*($)+($)", c1, g1, yp) :
                    qn(d, fin, MAXOUT, "($)*($)+($)*($)+($)", c1, g1, c2, g2, yp))) return 0;
    return dsfac(d, out, fin, kf);
}

/* second order: y'' + p y' + q y = r, constant coefficients */
NOINLINE static uint8_t dssecond(const ds_t *d, uint8_t *out, const uint8_t *in, unsigned e0, unsigned eq,
                                 const ic_t *ic, uint8_t nic)
{
    static const uint8_t z[2] = { 1, 0x30 }, one[2] = { 1, 0x31 }, two[2] = { 1, 0x32 },
                         three[2] = { 1, 0x33 }, five[2] = { 1, 0x35 };
    uint8_t e[DL + 1], i, rad = 0;
    ts_t g0, c0, c1, c2, p, qq, rn, g1, g2, tm;
    uint8_t yp[MAXOUT + 2];
    const uint8_t *kf = 0;
    int32_t n, dn;
    if (!gb(e, d, in, e0, eq, z, z, z) || !q(d, g0, "$", e)) return 0;
    if (!gb(e, d, in, e0, eq, one, z, z) || !q(d, c2, "$", e) || !q(d, c2, "($)-($)", c2, g0)) return 0;
    if (!gb(e, d, in, e0, eq, z, one, z) || !q(d, c1, "$", e) || !q(d, c1, "($)-($)", c1, g0)) return 0;
    if (!gb(e, d, in, e0, eq, z, z, one) || !q(d, c0, "$", e) || !q(d, c0, "($)-($)", c0, g0)) return 0;
    if (!num(c2) || !num(c1) || !num(c0) || zero(c2)) return 0;
    if (!gb(e, d, in, e0, eq, two, three, five) || !q(d, tm, "$", e) ||     /* it must be linear */
        !q(d, tm, "($)-(2*($)+3*($)+5*($)+($))", tm, c2, c1, c0, g0) || !zero(tm)) return 0;
    if (!q(d, p, "($)/($)", c1, c2) || !q(d, qq, "($)/($)", c0, c2) || !q(d, rn, "-($)/($)", g0, c2)) return 0;
    if (!q(d, tm, "($)^2-4*($)", p, qq) || !rd(tm, &n, &dn)) return 0;
    if (d->st && d->x != 'R' - 'A' + T_VAR0 && d->y != 'R' - 'A' + T_VAR0 && qn(d, yp, MAXOUT, "($)*R^2+($)*R+($)", c2, c1, c0))
        dsl(d, SR_DYX, 0, "$=0", yp);
    if (n > 0) {
        if (!q(d, c0, "q$)", tm) || !num(c0) || !q(d, c1, "(-($)+($))/2", p, c0) ||
            !q(d, c2, "(-($)-($))/2", p, c0) || !q(d, g1, "e($)*x)", c1) || !q(d, g2, "e($)*x)", c2)) return 0;
        dsl(d, SR_DYR, 0, "R=$", c1);
        dsl(d, SR_DYR, 0, "R=$", c2);
    } else if (!n) {
        if (!q(d, c1, "-($)/2", p) || !q(d, g1, "e($)*x)", c1) || !q(d, g2, "x*e($)*x)", c1)) return 0;
        dsl(d, SR_DYD, 0, "R=$", c1);
        cp(g0, c1);
        kf = g0;
    } else {
        if (!q(d, c1, "-($)/2", p) || !q(d, c0, "qn($))/2", tm)) return 0;
        rad = !num(c0);
        if (rad) {                              /* R = a +- (sqrt(m)/2) i: written out, for the plain homogeneous case */
            if (!zero(rn) || nic || !q(d, c2, "-($)", tm) || !q(d, g1, "($)*x", c1) ||
                !dsfmt(d, g2, TL, 1, "q$)x/2", c2)) return 0;
        } else if (!q(d, g1, "e($)*x)*k($)*x)", c1, c0) || !q(d, g2, "e($)*x)*s($)*x)", c1, c0)) return 0;
        if (zero(c1)) dsl(d, SR_DYZ, 0, "R=&ioR=n&i", c0, c0);
        else dsl(d, SR_DYZ, 0, "R=$+&ioR=$-&i", c1, c0, c1, c0);
        cp(g0, c1);
        kf = g0;
    }
    if (rad) {
        i = zero(c1) ? dsfmt(d, out, MAXOUT, 0, "y=c@$)+ds$)", g2, g2) : dsfmt(d, out, MAXOUT, 0, "y=e$)(c@$)+ds$))", g1, g2, g2);
        dslr(d, SR_DYH, out, i);
        return i;
    }
    yp[0] = 1; yp[1] = 0x30;
    for (i = 0; !zero(rn) && nth(rn, i, c1); i++)
        if (!dspart(d, c1, one, p, qq, c2) || !qn(d, yp, MAXOUT, "($)+($)", yp, c2)) return 0;
    dsl(d, SR_DYH, 0, "y=c&+d&", g1, g2);
    if (!zero(yp)) dsl(d, SR_DYP, 0, "y=$", yp);
    return dscomb(d, out, 2, g1, g2, yp, ic, nic, kf);
}

/* y' = f(x) g(y): (c0/F(xs,y)) dy = F(x,ys) dx, H(y) = K(x) + c, solved for y when SOLVE can */
NOINLINE static uint8_t dssep(const ds_t *d, uint8_t *out, const uint8_t *F, const ic_t *ic, uint8_t nic,
                              uint8_t *H, uint8_t *K, uint8_t *c0)
{
    static const uint8_t pts[5][2] = { { 0x31, 0x31 }, { 0x32, 0x31 }, { 0x31, 0x32 }, { 0x32, 0x33 }, { 0x33, 0x32 } };
    uint8_t e[DL + 1], s[MAXOUT + 2], i, n, j, k;
    ts_t t1, fx, fy, xs, ys;
    if (!q(d, t1, "DERIV($,x)", F) || !q(d, fx, "DERIV($,y)", F) || !q(d, fy, "DERIV($,y)", t1) ||
        !q(d, t1, "($)*($)-($)*($)", F, fy, t1, fx) || !zero(t1)) return 0;     /* F Fxy - Fx Fy = 0 */
    for (i = 0; i < 5; i++) {
        xs[0] = ys[0] = 1; xs[1] = pts[i][0]; ys[1] = pts[i][1];
        if (svar(e, F, d->y, ys) && q(d, fx, "$", e) && svar(e, F, d->x, xs) && q(d, fy, "$", e) &&
            svar(e, fy, d->y, ys) && q(d, c0, "$", e) && !zero(c0) && !zero(fy)) break;
    }
    if (i == 5 || !q(d, H, "INTEGRAL(($)/($),y)", c0, fy) || !q(d, K, "INTEGRAL($,x)", fx)) return 0;
    if (nic) {                                  /* c = H(y0) - K(x0) */
        if (!svar(e, H, d->y, ic[0].v) || !q(d, t1, "$", e) || !svar(e, K, d->x, ic[0].x) ||
            !q(d, fx, "$", e) || !q(d, c0, "($)-($)", t1, fx)) return 0;
    } else { c0[0] = 1; c0[1] = d->c[0]; }
    dsl(d, SR_DYS, 0, "$=$+c", H, K);
    if (nic) { dsv(d, &ic[0], t1, fx, 1); dsl(d, SR_DYC, 0, "c=$", c0); }
    if (qn(d, s, MAXOUT, "SOLVE(($)=($)+($),y)", H, K, c0)) {
        if (!nic) {
            for (i = 0; i < s[0]; i++) out[i] = s[1 + i];
            if (d->st && s[0] <= TL) { for (i = 0; i < s[0]; i++) t1[1 + i] = s[1 + i]; t1[0] = s[0]; dsl(d, SR_DYY, 0, "$", t1); }
            return s[0];
        }
        for (i = 3; i <= s[0]; i = (uint8_t)(j + 1)) {      /* y=u or y=v: the branch through the point */
            for (j = i; j <= s[0] && s[j] != T_OR; j++) ;
            for (t1[0] = 0, k = i; k < j && t1[0] < TL; k++) t1[++t1[0]] = s[k];
            if (j - i <= TL && svar(e, t1, d->x, ic[0].x) && q(d, fx, "($)-($)", e, ic[0].v) && zero(fx)) {
                out[0] = d->y; out[1] = T_EQ;
                for (k = i; k < j; k++) out[k - i + 2] = s[k];
                dsl(d, SR_DYY, 0, "y=$", t1);
                return (uint8_t)(j - i + 2);
            }
        }
    }
    /* H = al y² + h0: y = ±√((K + c - h0)/al), the sign the point's */
    if (q(d, t1, "DERIV($,y)", H) && q(d, fx, "DERIV($,y)", t1) && num(fx) && !zero(fx) &&
        q(d, fy, "($)-($)*y", t1, fx) && zero(fy)) {
        ys[0] = 1; ys[1] = 0x30;
        if (svar(e, H, d->y, ys) && q(d, fy, "$", e) &&
            q(d, t1, "(($)+($)-($))/(($)/2)", K, c0, fy, fx) && q(d, fx, "q$)", t1) && q(d, fy, "nq$)", t1)) {
            if (nic) {
                int32_t v, w;
                if (!rd(ic[0].v, &v, &w)) return 0;
                if (v < 0) cp(fx, fy);
                if (v) { dsl(d, SR_DYY, 0, "y=$", fx); out[0] = d->y; out[1] = T_EQ; for (i = 1; i <= fx[0]; i++) out[1 + i] = fx[i]; return (uint8_t)(fx[0] + 2); }
            } else if (2 * (fx[0] + 2) + 1 <= MAXOUT) {
                dsl(d, SR_DYY, 0, "y=$", fx);
                dsl(d, SR_DYY, 0, "y=$", fy);
                n = 0;
                out[n++] = d->y; out[n++] = T_EQ;
                for (i = 1; i <= fx[0]; i++) out[n++] = fx[i];
                out[n++] = T_OR; out[n++] = d->y; out[n++] = T_EQ;
                for (i = 1; i <= fy[0]; i++) out[n++] = fy[i];
                return n;
            }
        }
    }
    return 255;                                 /* neither solved nor a square root */
}

/* implicit: H = K + c */
static uint8_t dsimp(uint8_t *out, const uint8_t *H, const uint8_t *K, const uint8_t *c0)
{
    uint8_t n = 0, i;
    if (H[0] + K[0] + c0[0] + 2 > MAXOUT) return 0;
    for (i = 1; i <= H[0]; i++) out[n++] = H[i];
    out[n++] = T_EQ;
    for (i = 1; i <= K[0]; i++) out[n++] = K[i];
    if (!zero(c0)) {
        out[n++] = c0[1] == T_NEG ? T_SUB : T_ADD;
        for (i = 1 + (c0[1] == T_NEG); i <= c0[0]; i++) out[n++] = c0[i];
    }
    return n;
}

/* y = ... from H(y) = K(x) + c0. Through the point (0,0), H is tan^-1(y) or -1/e^(y) for y' = f(x)(1+y^2) or f(x)e^(y)
   (the sample point of dssep gives constants times those); else H is a sum of c ln|u| */
NOINLINE static uint8_t dsexpl(const ds_t *d, uint8_t *out, const uint8_t *F, const uint8_t *H, const uint8_t *K,
                               const uint8_t *c0, const ic_t *ic, uint8_t nic)
{
    static const uint8_t one[2] = { 1, 0x31 }, z[2] = { 1, 0x30 };
    ts_t acc, cf, u, g, E, t;
    uint8_t s[MAXOUT + 2], e[DL + 1], i, n, w;
    if (svar(e, F, d->y, z) && q(d, t, "$", e) && q(d, u, "INTEGRAL($,x)", t) &&      /* u = K through (0,0) */
        svar(e, F, d->x, z) && q(d, g, "$", e) && svar(e, g, d->y, z) && q(d, cf, "$", e) && !zero(cf) &&
        q(d, acc, "INTEGRAL(($)/($),y)", cf, g)) {                                    /* acc = H */
        w = acc[0] == 3 ? (acc[1] == T_ATAN && acc[2] == d->y && acc[3] == T_RPAR) :
            acc[0] == 6 ? (acc[1] == T_NEG && acc[2] == 0x31 && acc[3] == T_DIV && acc[4] == T_EXP && acc[5] == d->y && acc[6] == T_RPAR) * 2 : 0;
        if (w && nic && (!svar(e, acc, d->y, ic[0].v) || !q(d, t, "$", e) || !svar(e, u, d->x, ic[0].x) ||
                         !q(d, g, "$", e) || !q(d, cf, "($)-($)", t, g))) w = 0;      /* cf = c from the point */
        if (w && !nic) { cf[0] = 1; cf[1] = d->c[0]; }
        if (w == 2 && nic && !q(d, cf, "-($)", cf)) w = 0;                            /* -1/e^(y) = K + c: y = -ln(C - K) */
        if (w) {                                                                      /* tan^-1(y) = K + c: y = tan(K + c) */
            n = w == 1 ? (zero(cf) ? dsfmt(d, out, MAXOUT, 0, "y=t$)", u) : dsfmt(d, out, MAXOUT, 0, "y=t$;)", u, cf)) :
                         (zero(cf) ? dsfmt(d, out, MAXOUT, 0, "y=nl-&)", u) : dsfmt(d, out, MAXOUT, 0, "y=nl$-&)", cf, u));
            dslr(d, SR_DSI, out, n);
            return n;
        }
    }
    for (acc[0] = 1, acc[1] = 0x31, i = 0; nth(H, i, t); i++) {          /* g Sum ln|u| = K + c: Prod u^(c/g) = C e^(K/g) */
        if (!lnsplit(t, cf, u)) return 0;
        if (!i) cp(g, cf);
        if (!q(d, cf, "($)/($)", cf, g)) return 0;
        if (!(cf[0] == 1 && cf[1] == 0x31) && !q(d, u, "($)^($)", u, cf)) return 0;
        if (!dsmul(d, acc, acc, u)) return 0;
    }
    if (!(g[0] == 1 && g[1] == 0x31) ? !q(d, t, "($)/($)", K, g) : (cp(t, K), 0)) return 0;
    if (!eexp(d, E, t, one)) return 0;
    if (nic) {                                                         /* C = acc(y0)/E(x0) */
        if (!svar(e, acc, d->y, ic[0].v) || !q(d, t, "$", e) || !svar(e, E, d->x, ic[0].x) ||
            !q(d, g, "$", e) || !q(d, cf, "($)/($)", t, g)) return 0;
    } else { cf[0] = 1; cf[1] = d->c[0]; }
    if (!dsmul(d, t, cf, E)) return 0;                                 /* t = C E */
    for (w = 0;; w++) {
        if (!qn(d, s, MAXOUT, "SOLVE(($)=d,y)", acc)) return 0;
        for (i = 1; i <= s[0]; i++) if (s[i] == T_OR) return 0;
        if (!w) dsl(d, SR_DSX, 0, "$=$", acc, t);
        for (n = 0, i = 1; i <= s[0]; i++) {                           /* D := C E */
            if (s[i] != d->c[1]) { if (n >= DL) return 0; e[1 + n++] = s[i]; continue; }
            if (n + t[0] + 2 > DL) return 0;
            if (s[0] > 3) e[1 + n++] = T_LPAR;
            for (g[0] = 1; g[0] <= t[0]; g[0]++) e[1 + n++] = t[g[0]];
            if (s[0] > 3) e[1 + n++] = T_RPAR;
        }
        if (n > MAXOUT || n < 3) return 0;
        for (s[0] = n - 2, i = 0; i < s[0]; i++) s[1 + i] = e[3 + i];  /* the right side, for the engine to tidy */
        if (qn(d, e, MAXOUT - 2, "$", s)) for (i = 0; i <= e[0]; i++) s[i] = e[i];
        if (w || nic || s[1] != T_NEG) break;                          /* -1/(C E - 1): C absorbs the sign, 1/(C E + 1) */
        cf[0] = 2; cf[1] = T_NEG; cf[2] = d->c[0];
        if (!dsmul(d, t, cf, E)) return 0;
    }
    out[0] = d->y; out[1] = T_EQ;
    for (i = 0; i < s[0]; i++) out[2 + i] = s[1 + i];
    s[0] += 2;
    dslr(d, SR_DYY, out, s[0]);
    return s[0];
}

/* the setup of a first order equation: F = y', each of r0 r1 a clobbered */
NOINLINE static uint8_t dsfs(const ds_t *d, const uint8_t *in, unsigned e0, unsigned eq, uint8_t *F, uint8_t *r0,
                             uint8_t *r1, uint8_t *a)
{
    static const uint8_t z[2] = { 1, 0x30 }, one[2] = { 1, 0x31 }, two[2] = { 1, 0x32 };
    uint8_t e[DL + 1], yv[2] = { 1, d->y };
    ts_t b;
    if (!gb(e, d, in, e0, eq, z, z, yv) || !q(d, r0, "$", e) ||
        !gb(e, d, in, e0, eq, z, one, yv) || !q(d, r1, "$", e) ||
        !gb(e, d, in, e0, eq, z, two, yv) || !q(d, a, "$", e) ||
        !q(d, b, "($)-($)", r1, r0) ||                              /* the coefficient of y' */
        !q(d, a, "($)-2*($)+($)", a, r1, r0) || !zero(a) ||         /* affine in y' */
        !q(d, F, "-($)/($)", r0, b)) return 0;
    return 1;
}

/* y' = a y + b by an integrating factor; 255: not of that kind (r0 r1 a are only outputs then) */
NOINLINE static uint8_t dslin(const ds_t *d, uint8_t *out, uint8_t *F, uint8_t *r0, uint8_t *r1, uint8_t *a,
                              const ic_t *ic, uint8_t nic)
{
    static const uint8_t z[2] = { 1, 0x30 }, one[2] = { 1, 0x31 }, m1[3] = { 2, T_NEG, 0x31 }, none[1] = { 0 };
    uint8_t e[DL + 1], i;
    ts_t b, A, mu;
    uint8_t yp[MAXOUT + 2];
    if (q(d, a, "DERIV($,y)", F) && q(d, b, "DERIV($,y)", a) && zero(b)) {      /* y' = a y + b */
        uint8_t pure = 1;
        if (!svar(e, F, d->y, z) || !q(d, b, "$", e)) return 0;
        yp[0] = 1; yp[1] = 0x30;
        if (zero(a)) { mu[0] = 1; mu[1] = 0x31; }                       /* y' = b: the integral */
        else if (!dsint(d, A, a)) return 255;
        else if (num(a)) { if (!q(d, mu, "e$)", A)) return 255; }
        else if (!(pure = eexp(d, mu, A, one))) return 255;
        if (zero(a)) { if (!zero(b) && !q(d, yp, "INTEGRAL($,x)", b)) return 255; }
        else if (num(a)) {                                              /* constant a: y' - a y = b by terms */
            if (!q(d, A, "-($)", a)) return 0;
            for (i = 0; !zero(b) && nth(b, i, r1); i++)
                if (!dspart(d, r1, z, one, A, r0) || !qn(d, yp, MAXOUT, "($)+($)", yp, r0)) return 255;
        } else {                       /* y' + P y = Q: U = e^(integral of P), (U y)' = U Q, y = (c + integral of U Q)/U */
            ds_t dd = *d;
            if (!eexp(d, r0, A, m1)) return 255;
            if (!zero(b) && (!dsmul(d, r1, b, r0) || !dsint(d, A, r1) ||
                             !emul(d, yp, mu, A, pure == 1 ? r0 : none))) return 255;
            if (q(d, F, "-($)", a)) dsl(d, SR_DLP, 0, "y'+&y=$", F, b);
            dsl(d, SR_DLM, 0, "$", r0);
            if (zero(b)) { dsl(d, SR_DLX, 0, "(&y)'=0", r0); dsl(d, SR_DLI, 0, "&y=c", r0); }
            else { dsl(d, SR_DLX, 0, "(&y)'=$", r0, r1); dsl(d, SR_DLI, 0, "&y=$+c", r0, A); }
            if (mu[0] > 2 && mu[1] == 0x31 && mu[2] == T_DIV) {            /* c/U, not c(1/U) */
                for (F[0] = 0, i = 3; i <= mu[0]; i++) F[++F[0]] = mu[i];
                dsl(d, SR_DIV, r0, zero(yp) ? "y=c/&" : "y=c/&;", F, yp);
            } else dsl(d, SR_DIV, r0, zero(yp) ? "y=c&" : "y=c&;", mu, yp);
            dd.q = 1;
            return dscomb(&dd, out, 1, mu, mu, yp, ic, nic, 0);
        }
        if (!zero(a)) dsl(d, SR_DYH, 0, "y=c&", mu);
        if (!zero(yp)) dsl(d, zero(a) ? SR_DYI : SR_DYP, 0, zero(a) ? "y=$+c" : "y=$", yp);
        return dscomb(d, out, 1, mu, mu, yp, ic, nic, 0);
    }
    return 255;
}

/* first order: y' = F(x, y); linear (integrating factor), else separable */
NOINLINE static uint8_t dsfirst(const ds_t *d, uint8_t *out, const uint8_t *in, unsigned e0, unsigned eq,
                                const ic_t *ic, uint8_t nic)
{
    uint8_t i;
    ts_t F, r0, r1, a;
    if (nic > 1 || (nic && ic[0].o) || !dsfs(d, in, e0, eq, F, r0, r1, a)) return 0;
    dsl(d, SR_DYF, 0, "y'=$", F);
    if ((i = dslin(d, out, F, r0, r1, a, ic, nic)) != 255) return i;
    i = dssep(d, out, F, ic, nic, r0, r1, a);
    if (i != 255) return i;
    return (i = dsexpl(d, out, F, r0, r1, a, ic, nic)) ? i : dsimp(out, r0, r1, a);
}



/* DESOLVE(y'=f and y(x0)=a, x, y), DESOLVE(y''+p y'+q y=r and y(x0)=a and y'(x1)=b, x, y): in is the
   entry after the '(' */
NOINLINE static uint8_t desolve(const uint8_t *in, unsigned len, uint8_t *out, void *work, uint8_t mode)
{
    ds_t d;
    ic_t ic[2];
    ts_t raw;
    uint8_t used[NVTOK], nic = 0, ord = 0, c, n, k;
    unsigned i, e0 = 0, eq = 0, s, t, u, dp;
    d.w = work;
    d.q = 0;
    d.st = mode & MODE_STEP ? (uint8_t *)work + STOFF : 0;
    d.mode = (mode & (MODE_DEG | MODE_MP)) | MODE_EVAL;
    out[MAXOUT] = 0;
    if (len && in[len - 1] == T_RPAR) len--;
    if (len < 6 || in[len - 2] != T_COMMA || in[len - 4] != T_COMMA || !is_var(in[len - 1]) || !is_var(in[len - 3]))
        goto syn;
    d.x = in[len - 3]; d.y = in[len - 1]; len -= 4;
    for (k = 0; k < NVTOK; k++) used[k] = 0;
    for (i = 0; i < len; i++) if (is_var(in[i])) used[in[i] - T_VAR0] = 1;
    used[d.x - T_VAR0] = used[d.y - T_VAR0] = 1;
    for (c = 0, k = 2; k < 26 && c < 2; k++)           /* the constants: the first two letters from C not in use */
        if (!used[k]) d.c[c++] = (uint8_t)(T_VAR0 + k);
    if (c < 2) goto syn;
    for (dp = 0, i = 0; i < len; i++) {                /* the equation ends at the first top-level " and " */
        if (!dp && in[i] == T_AND) break;
        if (!dp && !eq && in[i] == T_EQ) eq = i;
        dp += opens(in[i]) - (in[i] == T_RPAR);
    }
    e0 = i;
    if (!eq) goto syn;
    for (i = 0; i < e0; i++)
        if (in[i] == d.y) {
            for (c = 0; i + 1 < e0 && (in[i + 1] == T_PRIME || in[i + 1] == 0x2A); i++) c += in[i + 1] == T_PRIME ? 1 : 2;
            if (c > ord) ord = c;
        }
    if (!ord || ord > 2) goto syn;
    for (s = e0; s < len; s = t) {                     /* and y(x0)=a, and y'(x0)=a */
        for (u = s + 1, t = u, dp = 0; t < len && (dp || in[t] != T_AND); t++) dp += opens(in[t]) - (in[t] == T_RPAR);
        if (nic == 2 || in[u] != d.y) goto syn;
        for (i = u + 1, c = 0; i < t && in[i] == T_PRIME; i++) c++;
        if (c > 1 || i >= t || in[i] != T_LPAR) goto syn;
        ic[nic].o = c;
        for (u = i + 1, dp = 0; i < t && (dp || in[i] != T_EQ); i++) dp += opens(in[i]) - (in[i] == T_RPAR);
        if (i >= t || in[i - 1] != T_RPAR) goto syn;
        for (raw[0] = 0, k = 0; u + k < i - 1 && raw[0] < TL; k++) raw[++raw[0]] = in[u + k];
        if (u + k < i - 1 || !q(&d, ic[nic].x, "$", raw)) goto lim;
        for (raw[0] = 0, u = i + 1; u < t && raw[0] < TL; u++) raw[++raw[0]] = in[u];
        if (u < t || !q(&d, ic[nic].v, "$", raw)) goto lim;
        nic++;
    }
    n = ord == 1 ? dsfirst(&d, out, in, e0, eq, ic, nic) : dssecond(&d, out, in, e0, eq, ic, nic);
    if (!n) goto lim;
    if (width(out, n, mode & MODE_MP) > SCR_WIDTH && out[1] == T_EQ && out[0] == d.y && width(out + 2, n - 2, mode & MODE_MP) <= SCR_WIDTH) {   /* Y= is left off, if that fits */
        for (i = 0; i + 2 < n; i++) out[i] = out[i + 2];
        n -= 2;
    }
    return n;                           /* past the screen: symce_engine hands it to the answer view */
lim:
    return error(out, "SYMCE LIMIT", "DESOLVE", "");
syn:
    return error(out, "SYNTAX", "DESOLVE", "(EQ,X,Y)");
}

/* stepsalg: the commands whose engine code logs no steps are explained afterwards, with engine()
   as a calculator on the pieces of the entry (the log keeps it at st+4). Every line shown is a
   real exact intermediate result. A bf_t is [length, tokens...]; one that outgrew DL holds DL + 1
   and is never logged. */
typedef uint8_t bf_t[DL + 2];

static void bput(uint8_t *b, const uint8_t *s, uint8_t n)
{
    if (b[0] + n > DL) b[0] = DL + 1;
    if (b[0] > DL) return;
    while (n--) b[1 + b[0]++] = *s++;
}

static void bt(uint8_t *b, uint8_t c) { bput(b, &c, 1); }

/* t, in parentheses when it is a sum or negative */
static void bw(uint8_t *b, const uint8_t *t)
{
    uint8_t w = t[0] > 1 && (nth(t, 1, 0) || t[1] == T_NEG);
    if (w) bt(b, T_LPAR);
    bput(b, t + 1, t[0]);
    if (w) bt(b, T_RPAR);
}

static uint8_t bl(const ds_t *d, uint16_t rule, const uint8_t *pb, const uint8_t *eb)
{
    if ((eb && (!eb[0] || eb[0] > DL)) || (pb && (!pb[0] || pb[0] > DL))) return 0;
    return stlog(d->st, rule, pb ? pb + 1 : 0, pb ? pb[0] : 0, eb ? eb + 1 : 0, eb ? eb[0] : 0, 1);
}

/* one step from two length-prefixed pieces (or 0) */
static uint8_t slg(const ds_t *d, uint16_t rule, const uint8_t *P, const uint8_t *E)
{
    return stlog(d->st, rule, P ? P + 1 : 0, P ? P[0] : 0, E ? E + 1 : 0, E ? E[0] : 0, 1);
}

/* w = a,b[,c] (length first); 0 when it outgrows 3 pieces */
static uint8_t pj(uint8_t *w, const uint8_t *a, const uint8_t *b, const uint8_t *c)
{
    uint8_t i, n = 0;
    for (i = 1; i <= a[0]; i++) w[1 + n++] = a[i];
    w[1 + n++] = T_COMMA;
    for (i = 1; i <= b[0]; i++) w[1 + n++] = b[i];
    if (c) {
        w[1 + n++] = T_COMMA;
        for (i = 1; i <= c[0]; i++) w[1 + n++] = c[i];
    }
    return w[0] = n;
}

/* the pieces of in[0..len) between top-level commas, at most max, each at most TL tokens */
static uint8_t targs(const uint8_t *in, uint8_t len, ts_t *v, uint8_t max)
{
    uint8_t i, n = 0, dp = 0;
    v[0][0] = 0;
    for (i = 0; i < len; i++) {
        if (!dp && in[i] == T_COMMA) {
            if (++n == max) return 0;
            v[n][0] = 0;
            continue;
        }
        if (v[n][0] >= TL) return 0;
        v[n][++v[n][0]] = in[i];
        if (in[i] == T_BB && i + 1 < len) { v[n][++v[n][0]] = in[++i]; continue; }
        dp += opens(in[i]) - (in[i] == T_RPAR);
    }
    return n + 1;
}

/* t without the parentheses that wrap all of it */
static void unwrap(uint8_t *t)
{
    uint8_t i, dp = 0;
    if (t[0] < 3 || t[1] != T_LPAR || t[t[0]] != T_RPAR) return;
    for (i = 1; i < t[0]; i++) {
        dp += opens(t[i]) - (t[i] == T_RPAR);
        if (!dp) return;
    }
    for (i = 2; i < t[0]; i++) t[i - 1] = t[i];
    t[0] -= 2;
}

static int8_t degof(const ds_t *d, const uint8_t *P)
{
    ts_t r;
    if (!q(d, r, "POLYDEGREE($,x)", P) || r[0] != 1 || r[1] < 0x30 || r[1] > 0x39) return -1;
    return (int8_t)(r[1] - 0x30);
}

/* the number of top-level terms of t */
static uint8_t nterms(const uint8_t *t)
{
    uint8_t n = 0;
    while (nth(t, n, 0)) n++;
    return n;
}

/* one factor f of a product: (G)^k becomes k copies of (G) */
static uint8_t addf(ts_t *fs, uint8_t *m, const uint8_t *f, uint8_t *pw)
{
    uint8_t k = 1, g = f[0], i;
    if (!g) return 1;
    if (g > 4 && f[1] == T_LPAR && f[g - 2] == T_RPAR && f[g - 1] == T_POW && f[g] >= 0x32 && f[g] <= 0x34) {
        k = (uint8_t)(f[g] - 0x30);
        g -= 2;
        *pw = 1;
    }
    for (; k; k--) {
        if (*m >= 6) return 0;
        fs[*m][0] = g;
        for (i = 1; i <= g; i++) fs[*m][i] = f[i];
        (*m)++;
    }
    return 1;
}

/* t to the power p at b (a plain 1, or a power 0, leaves nothing); 1 when something was written */
static uint8_t bpw(uint8_t *b, const uint8_t *t, uint8_t p)
{
    if (!p || (t[0] == 1 && t[1] == 0x31)) return 0;
    if (p == 1) bw(b, t);
    else {
        if (t[0] > 1) bt(b, T_LPAR);
        bput(b, t + 1, t[0]);
        if (t[0] > 1) bt(b, T_RPAR);
        bt(b, T_POW);
        bt(b, 0x30 + p);
    }
    return 1;
}

/* EXPAND of (a+b)^n, n 5 to 7, a and b one term each: a row of Pascal's triangle, the terms it gives, the sum.
   f is the factor "(a+b)^n"; 1 when the steps are logged */
NOINLINE static uint8_t apbin(const ds_t *d, const uint8_t *f)
{
    ts_t B, ta, tb;
    bf_t e, r;
    uint8_t n = f[f[0]] - 0x30, i, k, w;
    uint8_t row[8];
    B[0] = f[0] - 4;
    for (i = 1; i <= B[0]; i++) B[i] = f[i + 1];
    if (nterms(B) != 2 || !nth(B, 0, ta) || !nth(B, 1, tb)) return 0;
    row[0] = 1;
    for (k = 1; k <= n; k++) row[k] = (uint8_t)(row[k - 1] * (n - k + 1) / k);
    r[0] = 0;
    for (k = 0; k <= n; k++) {
        if (k) bt(r, T_COMMA);
        if (row[k] > 99) bt(r, 0x30 + row[k] / 100);
        if (row[k] > 9) bt(r, 0x30 + row[k] / 10 % 10);
        bt(r, 0x30 + row[k] % 10);
    }
    e[0] = 1; e[1] = 0x30 + n;
    bt(e, T_COMMA); bput(e, ta + 1, ta[0]); bt(e, T_COMMA); bput(e, tb + 1, tb[0]);
    if (!bl(d, SR_XBIN, e, r)) return 0;
    e[0] = 0;
    for (k = 0; k <= n; k++) {
        if (k) bt(e, T_ADD);
        w = 0;
        if (row[k] > 1) {
            if (row[k] > 99) bt(e, 0x30 + row[k] / 100);
            if (row[k] > 9) bt(e, 0x30 + row[k] / 10 % 10);
            bt(e, 0x30 + row[k] % 10);
            w = 1;
        }
        if (w && !(ta[0] == 1 && ta[1] == 0x31) && n - k) bt(e, T_MUL);
        w |= bpw(e, ta, n - k);
        if (w && !(tb[0] == 1 && tb[1] == 0x31) && k && (n - k || row[k] > 1)) bt(e, T_MUL);
        w |= bpw(e, tb, k);
        if (!w) bt(e, 0x31);
    }
    if (e[0] > DL || !bl(d, SR_XBT, 0, e)) return 0;
    return 2;
}

/* EXPAND: a product of brackets one bracket at a time, a sum of products one term at a time.
   1: steps logged and the last shows the answer; 2: the caller adds the combining of the terms */
NOINLINE static uint8_t ap_exp(const ds_t *d, const uint8_t *A)
{
    ts_t fs[6], acc, nw, ta, tb, f, tc;
    bf_t e;
    uint8_t m = 0, i, j, k, na, nb, c, dp = 0, pw = 0, did = 0, nt = nterms(A);
    if (nt > 1) {
        if (nt > 6) return 0;
        for (k = 0; k < nt; k++) {
            nth(A, k, ta);
            for (pw = 0, i = 1; i <= ta[0]; i++) pw |= ta[i] == T_LPAR || ta[i] == T_MUL;
            if (!pw) continue;
            if (!q(d, nw, "EXPAND($)", ta)) return 0;
            if (!slg(d, SR_XTERM, ta, nw)) return 0;
            did = 1;
        }
        return did ? 2 : 0;
    }
    f[0] = 0;
    for (i = 1; i <= A[0]; i++) {
        c = A[i];
        if (!dp && c == T_MUL) {
            if (!addf(fs, &m, f, &pw)) return 0;
            f[0] = 0;
            continue;
        }
        if (!dp && c == T_LPAR && f[0] && f[f[0]] != T_POW && f[f[0]] != T_NEG) {
            if (!addf(fs, &m, f, &pw)) return 0;
            f[0] = 0;
        }
        if (f[0] >= TL) return 0;
        f[++f[0]] = c;
        dp += opens(c) - (c == T_RPAR);
    }
    if (!m && f[0] > 4 && f[1] == T_LPAR && f[f[0] - 2] == T_RPAR && f[f[0] - 1] == T_POW && f[f[0]] >= 0x35 && f[f[0]] <= 0x37)
        return apbin(d, f);
    if (!addf(fs, &m, f, &pw) || m < 2) return 0;
    if (pw) {
        e[0] = 0;
        for (k = 0; k < m; k++) bw(e, fs[k]);
        if (!bl(d, SR_XPOW, 0, e)) return 0;
    }
    for (acc[0] = fs[0][0], i = 1; i <= acc[0]; i++) acc[i] = fs[0][i];
    for (k = 1; k < m; k++) {
        for (ta[0] = acc[0], i = 1; i <= ta[0]; i++) ta[i] = acc[i];
        for (tb[0] = fs[k][0], i = 1; i <= tb[0]; i++) tb[i] = fs[k][i];
        unwrap(ta);
        unwrap(tb);
        na = nterms(ta);
        nb = nterms(tb);
        if (!q(d, nw, "EXPAND(($)*($))", acc, fs[k])) return 0;
        if (na > 1 || nb > 1) {
            if (na > 4 || nb > 4) return 0;
            e[0] = 0;
            for (i = 0; i < na; i++)
                for (j = 0; j < nb; j++) {
                    nth(ta, i, f);
                    nth(tb, j, tc);
                    if (i || j) bt(e, T_ADD);
                    bw(e, f);
                    bt(e, T_MUL);
                    bw(e, tc);
                }
            if (!bl(d, SR_XDIST, 0, e) || !slg(d, SR_COMB, 0, nw)) return 0;
            did = 1;
        }
        for (acc[0] = nw[0], i = 1; i <= acc[0]; i++) acc[i] = nw[i];
    }
    return did;
}

/* t, in parentheses unless it is one token (or already wrapped): a bottom */
static void bwd(uint8_t *b, const uint8_t *t)
{
    ts_t u;
    cp(u, t);
    unwrap(u);
    if (u[0] > 1) bt(b, T_LPAR);
    bput(b, u + 1, u[0]);
    if (u[0] > 1) bt(b, T_RPAR);
}

/* num times m, leaving out a 1 */
static void bpr(uint8_t *b, const uint8_t *nm, const uint8_t *m)
{
    if (nm[0] == 1 && nm[1] == 0x31) bw(b, m);
    else if (m[0] == 1 && m[1] == 0x31) bw(b, nm);
    else { bw(b, nm); bt(b, T_MUL); bw(b, m); }
}

/* COMDENOM of a sum of fractions: the common denominator, each fraction over it, the tops added */
NOINLINE static uint8_t ap_cd(const ds_t *d, const uint8_t *A)
{
    ts_t nm[3], dn[3], L, t, m;
    bf_t e;
    uint8_t nt = nterms(A), k, i, dp, pd, poly = 0, w;
    if (nt < 2 || nt > 3) return 0;
    for (k = 0; k < nt; k++) {
        nth(A, k, t);
        for (pd = 0, dp = 0, i = 1; i <= t[0]; i++) {
            if (!dp && t[i] == T_DIV) pd = i;
            dp += opens(t[i]) - (t[i] == T_RPAR);
        }
        if (pd) {
            nm[k][0] = pd - 1;
            for (i = 1; i < pd; i++) nm[k][i] = t[i];
            dn[k][0] = t[0] - pd;
            for (i = 1; i <= dn[k][0]; i++) dn[k][i] = t[pd + i];
            unwrap(dn[k]);
        } else {
            cp(nm[k], t);
            dn[k][0] = 1; dn[k][1] = 0x31;
        }
        if (!num(dn[k])) poly = 1;
    }
    if (!poly) return 0;
    cp(L, dn[0]);
    for (k = 1; k < nt; k++)
        if (!q(d, m, "POLYGCD($,$,x)", L, dn[k]) || !q(d, t, "FACTOR(($)*($)/($))", L, dn[k], m)) return 0;
        else cp(L, t);
    bf(d, e, "$", L);
    if (!bl(d, SR_LCD, 0, e)) return 0;
    for (w = 0; w < 2; w++) {
        e[0] = 0;
        if (w) bt(e, T_LPAR);
        for (k = 0; k < nt; k++) {
            if (!q(d, m, "FACTOR(($)/($))", L, dn[k])) return 0;
            if (k) bt(e, T_ADD);
            bpr(e, nm[k], m);
            if (!w) { bt(e, T_DIV); bwd(e, L); }
        }
        if (w) { bt(e, T_RPAR); bt(e, T_DIV); bwd(e, L); }
        if (!bl(d, w ? SR_CDADD : SR_CDRW, 0, e)) return 0;
    }
    return 2;
}

/* the term of P with the highest power (g of it), into t: EXPAND does not always write them in order */
static uint8_t leadterm(const ds_t *d, const uint8_t *P, int8_t g, uint8_t *t)
{
    uint8_t k;
    for (k = 0; k < 10 && nth(P, k, t); k++)
        if (degof(d, t) == g) return 1;
    return 0;
}

/* POLYQUOTIENT / POLYREMAINDER by long division, one round at a time */
NOINLINE static uint8_t ap_div(const ds_t *d, const uint8_t *A, const uint8_t *B, uint8_t rem)
{
    ts_t R, R2, Q, t, lr, ld, Be;
    uint8_t w[3 * TL + 4];
    int8_t db;
    uint8_t k;
    if ((db = degof(d, B)) <= 0 || !q(d, Be, "EXPAND($)", B) || !q(d, R, "EXPAND($)", A)) return 0;
    if (!leadterm(d, Be, db, ld)) return 0;
    Q[0] = 0;
    for (k = 0; k < 6 && !zero(R) && degof(d, R) >= db; k++) {
        if (!leadterm(d, R, degof(d, R), lr)) return 0;
        if (!q(d, t, "($)/($)", lr, ld)) return 0;
        if (Q[0]) { if (!q(d, R2, "($)+($)", Q, t)) return 0; cp(Q, R2); }
        else cp(Q, t);
        pj(w, lr, ld, t);
        if (!stlog(d->st, SR_LDIV, w + 1, w[0], Q + 1, Q[0], 1)) return 0;
        if (!q(d, R2, "EXPAND(($)-($)*($))", R, t, Be) || !slg(d, SR_LSUBT, t, R2)) return 0;
        cp(R, R2);
    }
    if (!zero(R) && degof(d, R) >= db) return 0;
    if (!Q[0]) { Q[0] = 1; Q[1] = 0x30; }
    if (!q(d, t, "EXPAND(($)*($)+($)-($))", Q, Be, R, A) || !zero(t)) return 0;     /* quotient x divisor + remainder = dividend */
    return slg(d, rem ? SR_LREM : SR_LQUO, 0, rem ? R : Q);
}

/* POLYGCD: Euclid's remainders until one is 0 */
NOINLINE static uint8_t ap_gcd(const ds_t *d, const uint8_t *A, const uint8_t *B)
{
    ts_t a, b, r;
    uint8_t k, w[2 * TL + 4];
    cp(a, A); cp(b, B);
    for (k = 0; k < 6 && !zero(b); k++) {
        if (!q(d, r, "POLYREMAINDER($,$,x)", a, b)) return 0;
        pj(w, a, b, 0);
        if (!stlog(d->st, SR_EUC, w + 1, w[0], r + 1, r[0], 1)) return 0;
        cp(a, b); cp(b, r);
    }
    return zero(b) && k;
}

/* which trig identities the entry's sin( and cos( ask for: bit 0 sum, 1 double, 2 triple, 3 power, 4 product */
static uint8_t tkinds(const uint8_t *in, uint8_t len, uint8_t *funcs)
{
    uint8_t i, j, dp, r = 0;
    *funcs = 0;
    for (i = 0; i < len; i++) {
        if (in[i] != T_SIN && in[i] != 0xC4) continue;
        (*funcs)++;
        for (dp = 1, j = i + 1; j < len && dp; j++) {
            dp += opens(in[j]) - (in[j] == T_RPAR);
            if (dp == 1 && j > i + 1 && (in[j] == T_ADD || in[j] == T_SUB)) r |= 1;
        }
        if (in[i + 1] == 0x32 && i + 2 < len && !is_digit(in[i + 2])) r |= 2;
        if (in[i + 1] == 0x33 && i + 2 < len && !is_digit(in[i + 2])) r |= 4;
        if (j < len && in[j] == T_POW) r |= 8;
    }
    if (*funcs > 1) r |= 16;
    return r;
}

/* steps( of the commands past the engine's own steps: st holds the entry, the answer is out[0..n) */
NOINLINE static void asteps(void *work, const uint8_t *out, uint8_t n, uint8_t mode)
{
    ts_t v[3];
    ds_t d;
    uint8_t *st = (uint8_t *)work + STOFF;
    const uint8_t *e = st + 4;
    uint8_t len = st[3], cmd, nl, na, i, kinds, funcs, ok = 0, sv1 = st[1], sv2 = st[68], sv3 = st[69], b[2];
    if (!len || (cmd = cmdat(e, len, &nl)) < C_LEFT) return;
    e += nl; len -= nl;
    if (len && e[len - 1] == T_RPAR) len--;
    d.w = work; d.st = st; d.mode = (mode & (MODE_DEG | MODE_MP)) | MODE_EVAL;
    d.y = T_VAR0 + 24; d.c[0] = d.c[1] = 0;
    for (i = 0; i < len && !is_var(e[i]); i++) ;
    d.x = i < len ? e[i] : T_VAR0 + 23;
    if (!(na = targs(e, len, v, 3))) return;
    if (na > 1 && v[na - 1][0] == 1 && is_var(v[na - 1][1])) d.x = v[na - 1][1];
    switch (cmd) {
    case C_EXPAND:
        ok = ap_exp(&d, v[0]);
        if (ok == 2) ok = stlog(st, SR_COMB, 0, 0, out, n, 1);
        break;
    case C_COMDENOM:
        ok = ap_cd(&d, v[0]);
        if (ok == 2) ok = stlog(st, SR_COMB, 0, 0, out, n, 1);
        break;
    case C_POLYQUOTIENT: case C_POLYREMAINDER:
        ok = na >= 2 && ap_div(&d, v[0], v[1], cmd == C_POLYREMAINDER);
        break;
    case C_POLYGCD:
        ok = na >= 2 && ap_gcd(&d, v[0], v[1]) && stlog(st, SR_EUCZ, 0, 0, out, n, 1);
        break;
    case C_POLYDEGREE: case C_POLYCOEFFS:
        for (ok = 0, i = 1; i <= v[0][0]; i++) ok |= v[0][i] == T_LPAR || v[0][i] == T_MUL;
        if (ok) {
            if (!q(&d, v[1], "EXPAND($)", v[0])) return;
            if (!slg(&d, SR_EXP, 0, v[1])) goto undo;
        }
        ok = stlog(st, cmd == C_POLYDEGREE ? SR_PDEG : SR_PCO, &d.x, 1, out, n, 1);
        break;
    case C_LEFT: case C_RIGHT:
        ok = stlog(st, cmd == C_LEFT ? SR_LEFT : SR_RIGHT, 0, 0, out, n, 1);
        break;
    case C_NROOTS: {
        int8_t g = degof(&d, v[0]);
        if (g > 1) {
            b[0] = (uint8_t)(0x30 + g);
            ok = stlog(st, SR_NRQ, b, 1, 0, 0, 1) && stlog(st, SR_NRA, 0, 0, out, n, 1);
        }
        break;
    }
    case C_PARTFRAC: {
        uint8_t pd = 0, dp = 0;
        for (i = 1; i <= v[0][0]; i++) {
            if (!dp && v[0][i] == T_DIV) pd = i;
            dp += opens(v[0][i]) - (v[0][i] == T_RPAR);
        }
        if (pd && nterms(v[0]) == 1) {
            v[1][0] = v[0][0] - pd;
            for (i = 1; i <= v[1][0]; i++) v[1][i] = v[0][pd + i];
            unwrap(v[1]);
            if (q(&d, v[2], "FACTOR($)", v[1])) {
                ok = slg(&d, SR_PFD, 0, v[2]) && stlog(st, SR_PFSOL, 0, 0, out, n, 1);
            }
        }
        break;
    }
    case C_TEXPAND: case C_TCOLLECT: case C_TOEXP: case C_TOSIN: case C_TOCOS: case C_TOLN: case C_TOLOGBASE:
        kinds = tkinds(e, len, &funcs);
        if (cmd == C_TEXPAND) {
            ok = 1;
            if (kinds & 1) ok &= stlog(st, SR_TSUM, 0, 0, 0, 0, 0);
            if (kinds & 2) ok &= stlog(st, SR_TDBL, 0, 0, 0, 0, 0);
            if (kinds & 4) ok &= stlog(st, SR_TTRI, 0, 0, 0, 0, 0);
            if (!(kinds & 7)) goto nop;
        } else if (cmd == C_TCOLLECT) {
            ok = 1;
            if (kinds & 8) ok &= stlog(st, SR_TPOW, 0, 0, 0, 0, 0);
            else if (kinds & 16) ok &= stlog(st, SR_TPRD, 0, 0, 0, 0, 0);
            else goto nop;
        } else if (cmd == C_TOEXP) {
            ok = funcs ? stlog(st, SR_TEUL, 0, 0, 0, 0, 0) : 0;
            if (!funcs) goto nop;
        } else if (cmd == C_TOSIN || cmd == C_TOCOS) {
            ok = funcs ? stlog(st, SR_TPYS, 0, 0, 0, 0, 0) : 0;
            if (!funcs) goto nop;
        } else if (cmd == C_TOLN) {
            for (i = 0; i < len && e[i] != T_LOG; i++) ;
            if (i == len) goto nop;
            ok = stlog(st, SR_TLOG, 0, 0, 0, 0, 0);
        } else {
            for (i = 0; i < len && e[i] != T_LN; i++) ;
            if (i == len) goto nop;
            ok = stlog(st, SR_TLB, 0, 0, 0, 0, 0);
        }
        if (ok) ok = stlog(st, SR_TRES, 0, 0, out, n, 1);
        break;
    nop:
        ok = stlog(st, SR_TNOP, 0, 0, out, n, 1);
        break;
    }
    if (ok || st[2] & 1) return;
undo:
    st[1] = sv1; st[68] = sv2; st[69] = sv3; st[2] = 0;
}

/* the denominators of t that have the letter in them, at most n of them, into dn (each at most TL tokens) */
static uint8_t vdens(const ds_t *d, const uint8_t *t, ts_t *dn, uint8_t n)
{
    uint8_t i, j, e, dp, m = 0, v;
    for (i = 1; i < t[0]; i++) {
        if (t[i] != T_DIV) continue;
        j = (uint8_t)(i + 1);
        e = j;
        if (t[j] == T_LPAR) {
            for (dp = 0; e <= t[0]; e++) {
                dp += opens(t[e]) - (t[e] == T_RPAR);
                if (!dp) break;
            }
            e++;
        } else {
            for (; e <= t[0] && (is_var(t[e]) || is_digit(t[e]) || t[e] == T_POW); e++) ;
        }
        for (v = 0, dn[m][0] = 0; j < e && j <= t[0]; j++) {
            if (dn[m][0] >= TL) return 0;
            dn[m][++dn[m][0]] = t[j];
            v |= t[j] == d->x;
        }
        unwrap(dn[m]);
        if (v && m < n) m++;
        else if (v) return 0;
    }
    return m;
}

/* abs( U ) rel k with k free of the letter: U and k */
static uint8_t absform(const ds_t *d, const uint8_t *a, const uint8_t *b, ts_t U, ts_t k)
{
    uint8_t i, dp = 0;
    if (a[1] != T_ABS || a[0] < 3 || a[a[0]] != T_RPAR) return 0;
    for (i = 1; i < a[0]; i++) {
        dp += opens(a[i]) - (a[i] == T_RPAR);
        if (!dp) return 0;
    }
    for (i = 1; i <= b[0]; i++) if (b[i] == d->x || b[i] == T_NEG) return 0;
    U[0] = a[0] - 2;
    for (i = 1; i <= U[0]; i++) U[i] = a[i + 1];
    cp(k, b);
    return b[0] != 0;
}

/* SOLVE: what comes before the engine's own steps (an equation with letters below a bar), or instead
   of them (|U| = k), and the whole story of an inequality, which the engine gives none for.
   1: the engine's own steps are to be dropped */
NOINLINE static uint8_t psolve(void *work, uint8_t mode)
{
    ts_t v[3], dn[3], A, B, E, t, a;
    ds_t d;
    bf_t e;
    uint8_t *st = (uint8_t *)work + STOFF;
    const uint8_t *in = st + 4;
    uint8_t len = st[3], nl, na, i, j, dp = 0, rel = 0, nd, ok = 0, abs = 0, sv1 = st[1], sv2 = st[68], sv3 = st[69];
    int8_t g;
    if (!len || cmdat(in, len, &nl) != C_SOLVE) return 0;
    in += nl; len -= nl;
    if (len && in[len - 1] == T_RPAR) len--;
    d.w = work; d.st = st; d.mode = (mode & (MODE_DEG | MODE_MP)) | MODE_EVAL;
    d.y = T_VAR0 + 24; d.c[0] = d.c[1] = 0;
    if (!(na = targs(in, len, v, 3)) || na < 2 || v[1][0] != 1 || !is_var(v[1][1])) return 0;
    d.x = v[1][1];
    for (i = 1; i <= v[0][0]; i++) {
        if (!dp && v[0][i] >= T_EQ && v[0][i] <= T_EQ + 4) { rel = v[0][i]; j = i; }
        dp += opens(v[0][i]) - (v[0][i] == T_RPAR);
    }
    if (!rel) return 0;
    A[0] = j - 1; for (i = 1; i < j; i++) A[i] = v[0][i];
    B[0] = v[0][0] - j; for (i = 1; i <= B[0]; i++) B[i] = v[0][j + i];
    if (absform(&d, A, B, E, t)) {                                  /* |U| rel k */
        if (!q(&d, a, "n($)", t)) return 0;                         /* -k */
        e[0] = 0;
        if (rel == T_EQ) { bput(e, E + 1, E[0]); bt(e, T_EQ); bput(e, t + 1, t[0]); bt(e, T_OR); bput(e, E + 1, E[0]); bt(e, T_EQ); bput(e, a + 1, a[0]); }
        else if (rel == T_EQ + 1 || rel == T_EQ + 3) {              /* < and <=: between */
            bput(e, a + 1, a[0]); bt(e, rel); bput(e, E + 1, E[0]); bt(e, rel); bput(e, t + 1, t[0]);
        } else {                                                    /* > and >=: outside */
            bput(e, E + 1, E[0]); bt(e, rel == T_EQ + 2 ? T_EQ + 1 : T_EQ + 3); bput(e, a + 1, a[0]);
            bt(e, T_OR); bput(e, E + 1, E[0]); bt(e, rel); bput(e, t + 1, t[0]);
        }
        if (!bl(&d, SR_ABSC, 0, e) || !q(&d, a, "SOLVE($,x)", v[0])) goto undo;
        bf(&d, e, "$", a);
        if (!bl(&d, SR_ABSS, 0, e)) goto undo;
        ok = 1; abs = rel == T_EQ;
    } else if ((nd = vdens(&d, v[0], dn, 3))) {                     /* letters below a bar */
        e[0] = 0;
        for (i = 0; i < nd; i++)
            if (q(&d, a, "SOLVE(($)=0,x)", dn[i])) { if (e[0]) bt(e, T_OR); bput(e, a + 1, a[0]); }
        if (!e[0] || !bl(&d, SR_EXCL, 0, e)) goto undo;
        if (rel == T_EQ) {
            cp(E, dn[0]);
            for (i = 1; i < nd; i++) {
                if (!q(&d, a, "POLYGCD($,$,x)", E, dn[i]) || !q(&d, t, "FACTOR(($)*($)/($))", E, dn[i], a)) goto undo;
                cp(E, t);
            }
            if (!q(&d, t, "EXPAND((($)-($))*($))", A, B, E)) goto undo;
            bf(&d, e, "$=0", t);
            if (!bl(&d, SR_CLEARF, 0, e)) goto undo;
        } else {
            if (!q(&d, a, "SOLVE(($)=($),x)", A, B)) goto undo;
            bf(&d, e, "$", a);
            if (!bl(&d, SR_ICRIT, 0, e) || !bl(&d, SR_ITEST, 0, 0)) goto undo;
        }
        ok = 1;
    } else if (rel != T_EQ) {                                       /* a polynomial inequality */
        if (!q(&d, E, "EXPAND(($)-($))", A, B) || (g = degof(&d, E)) < 1) goto undo;
        if (B[0] != 1 || B[1] != 0x30) {                            /* move everything to the left */
            bf(&d, e, "$", E);
            bt(e, rel); bt(e, 0x30);
            if (!bl(&d, SR_MOVE, 0, e)) goto undo;
        }
        if (g == 1) {
            if (!q(&d, A, "DERIV($,x)", E) || !q(&d, B, "EXPAND(($)-($)*x)", E, A)) goto undo;   /* aX + b */
            if (!q(&d, t, "n($)", B)) goto undo;                                                  /* c = -b */
            if (!zero(B)) {
                e[0] = 0;
                if (!q(&d, a, "($)*x", A)) goto undo;
                bput(e, a + 1, a[0]); bt(e, rel); bw(e, t);
                if (B[1] == T_NEG) { if (!q(&d, E, "n($)", B)) goto undo; ok = bl(&d, SR_ADD, E, e); }
                else ok = bl(&d, SR_SUB, B, e);
                if (!ok) goto undo;
            }
            if (!(A[0] == 1 && A[1] == 0x31)) {
                uint8_t neg = A[1] == T_NEG, r2 = neg ? (rel == T_EQ + 1 ? T_EQ + 2 : rel == T_EQ + 2 ? T_EQ + 1 : rel == T_EQ + 3 ? T_EQ + 4 : T_EQ + 3) : rel;
                if (neg && !bl(&d, SR_IFLIP, 0, 0)) goto undo;
                if (!q(&d, E, "($)/($)", t, A)) goto undo;
                e[0] = 0;
                bt(e, d.x); bt(e, r2); bput(e, E + 1, E[0]);
                if (!bl(&d, SR_DIV, A, e)) goto undo;
            }
            ok = 1;
        } else {
            if (!q(&d, a, "SOLVE(($)=0,x)", E)) goto undo;
            bf(&d, e, "$", a);
            if (!bl(&d, SR_ICRIT, 0, e) || !bl(&d, SR_ITEST, 0, 0)) goto undo;
            ok = 1;
        }
    }
    if (ok) { st[64] = st[1]; st[65] = st[68]; st[66] = st[69]; return abs; }
undo:
    st[1] = sv1; st[68] = sv2; st[69] = sv3; st[2] = 0;
    return 0;
}

#else
#define asteps(w, o, n, m)
#define psolve(w, m) 0
#endif

/* ---- MDET( MINV( MRREF( MTRANS( DOTP( CROSSP( NORM(: matrices and vectors
   with letters in them. [[A,1][2,A]] is a matrix, [1,2,X] a vector (a matrix
   of one row), each cell a polynomial: a quotient with a letter below the
   bar, or a root, is SYMCE LIMIT. The cells sit in the pool, one slot each,
   so a matrix has MAXM of them (4x4, 3x5, 4x5). The determinant is by
   cofactors, which stay polynomials with no gcd to find; MRREF treats a
   letter as any value but the one that makes a pivot 0, as the Nspire does. ---- */

#define MAXM   20
#define MWIDE  0xFE           /* matrix(): the answer is past the screen */
typedef struct { rat_t *c[MAXM]; uint8_t r, w; } mat_t;

/* acc += x*y, or -= when neg */
static int pmac(ps_t *s, poly_t *acc, const poly_t *x, const poly_t *y, uint8_t neg)
{
    poly_t *t = tmp(s);
    uint8_t k;
    if (!t) return 0;
    *t = *x;
    if (!mulinto(s, t, y)) return 0;
    if (neg) pneg(t);
    for (k = 0; k < t->n; k++) if (!addterm(acc, &t->t[k])) return 0;
    s->top--;
    return 1;
}

/* out = the determinant of the rows rm and columns cm of m (bit masks), by
   the first row; no rows left is 1 */
NOINLINE static int mdet(ps_t *s, const mat_t *m, unsigned rm, unsigned cm, poly_t *out)
{
    poly_t *t = 0;
    uint8_t r = 0, c, i = 0;
    out->n = 0;
    if (!rm) { pconst(out, 1); return 1; }
    while (!(rm >> r & 1)) r++;
    rm &= ~(1u << r);
    if (rm && !(t = tmp(s))) return 0;          /* the minors */
    for (c = 0; c < m->w; c++) {
        if (!(cm >> c & 1)) continue;
        if (m->c[r * m->w + c]->n.n) {
            if (rm) {
                if (!mdet(s, m, rm, cm & ~(1u << c), t) || !pmac(s, out, &m->c[r * m->w + c]->n, t, i & 1))
                    return 0;
            } else {                            /* the last row: the cell itself */
                *out = m->c[r * m->w + c]->n;
                if (i & 1) pneg(out);
            }
        }
        i++;
    }
    s->top -= rm != 0;
    return 1;
}

/* [[a,b][c,d]] or [a,b,c] into m, each cell a polynomial in the pool: 1; 0 past
   what SymCE does; 2 when the brackets are wrong; 3 when the rows differ in length */
NOINLINE static uint8_t rdmat(ps_t *s, mat_t *m)
{
    uint8_t row = 0, col, k = 0, nest;
    rat_t *p;
    if (peek(s) != T_LBRK) return 2;
    s->i++;
    if ((nest = peek(s) == T_LBRK)) s->i++;
    for (;;) {
        for (col = 0; ; ) {
            if (k == MAXM || !(p = get(s)) || !expr(s, p) || reduce(s, p) != 1 || !isconst(&p->d) ||
                hasrad(&p->n)) return 0;
            m->c[k++] = p;
            col++;
            if (peek(s) != T_COMMA) break;
            s->i++;
        }
        if (row++ && col != m->w) return 3;
        m->w = col;
        if (!nest) break;
        if (peek(s) != T_RBRK) return 2;
        s->i++;
        if (peek(s) != T_LBRK) break;
        s->i++;
    }
    if (peek(s) != T_RBRK) return 2;
    s->i++;
    m->r = row;
    return 1;
}

/* w cells c[0], c[cs] .. as [a,b,c] at out + n; they are fractions */
static uint8_t putrow(const ps_t *s, rat_t **c, uint8_t w, uint8_t cs, uint8_t *out, uint8_t n)
{
    uint8_t j;
    PUT(T_LBRK);
    for (j = 0; j < w; j++) {
        if (j) PUT(T_LCOMMA);
        if (!(n = answer(s, c[j * cs], out, n))) return 0;
    }
    PUT(T_RBRK);
    return n;
}

/* r rows of w (cell (i,j) at c[i*rs + j*cs]): [[a,b][c,d]] */
static uint8_t putmat(const ps_t *s, rat_t **c, uint8_t r, uint8_t w, uint8_t rs, uint8_t cs,
                      uint8_t *out, uint8_t n)
{
    uint8_t i;
    PUT(T_LBRK);
    for (i = 0; i < r; i++)
        if (!(n = putrow(s, c + i * rs, w, cs, out, n))) return 0;
    PUT(T_RBRK);
    return n;
}

#ifdef NOSTEPSX     /* the ENGTEST build keeps none of the step code */
#define msteps(...) ((void)0)
#define mdiv(...) ((void)0)
#define mrlog(...) ((void)0)
#else
/* ---- stepsgeo: steps( for the Matrix menu. A matrix in a step is written as the answer is. ---- */

/* a step: the parameter the format f spells, then the matrix of cells c (r rows of w, strides rs, cs) */
NOINLINE static void gmat(ps_t *s, uint16_t rule, rat_t **c, uint8_t r, uint8_t w, uint8_t rs, uint8_t cs,
                          const char *f, ...)
{
    uint8_t e[MAXOUT], pn, n, no;
    va_list ap;
    if (!s->st || s->st[2]) return;
    va_start(ap, f);
    pn = n = vgf(s, e, 0, f, ap, &no);
    va_end(ap);
    if (n != GFAIL) n = putmat(s, c, r, w, rs, cs, e, n);
    if (n == GFAIL || !n) s->st[2] |= 1;
    else gput(s, rule, e, pn, e + pn, n - pn);
}

/* the cofactors of a (the adjugate when tr: each one at the place across the diagonal) as a matrix at out */
NOINLINE static uint8_t mcofe(ps_t *s, mat_t *a, uint8_t tr, poly_t *t, uint8_t *out)
{
    uint8_t n = 0, i, j, k = a->r;
    unsigned rm = (1u << k) - 1;
    PUT(T_LBRK);
    for (i = 0; i < k; i++) {
        PUT(T_LBRK);
        for (j = 0; j < k; j++) {
            if (j) PUT(T_LCOMMA);
            if (!mdet(s, a, rm & ~(1u << (tr ? j : i)), rm & ~(1u << (tr ? i : j)), t)) return 0;
            if ((i + j) & 1) pneg(t);
            if (!(n = emit(s, t, out, n))) return 0;
        }
        PUT(T_RBRK);
    }
    PUT(T_RBRK);
    return n;
}

NOINLINE static void mcof(ps_t *s, mat_t *a, uint16_t rule, uint8_t tr, poly_t *t)
{
    uint8_t e[MAXOUT], n;
    if (!s->st || s->st[2]) return;
    n = mcofe(s, a, tr, t, e);
    if (n) gput(s, rule, 0, 0, e, n);
    else s->st[2] |= 1;
}

/* the determinant of a square matrix by its first row; ds 4 scratch values */
NOINLINE static void mdsteps(ps_t *s, mat_t *a, rat_t **ds)
{
    uint8_t e[MAXOUT], k, c, i, j, t = 0, n = a->r, m = 0;
    unsigned rm = (1u << n) - 1;
    rat_t *d = ds[0], *v = ds[1], *u = ds[2], *mn[MAXM];
    pconst(&d->d, 1);
    pconst(&v->d, 1);
    if (n < 2 || !mdet(s, a, rm, rm, &d->n)) return;
    if (n == 2) {
        gst(s, SR_MDET2, "q*q-q*q=r", a->c[0], a->c[3], a->c[1], a->c[2], d);
        return;
    }
    rconst(d, 0);
    for (c = 0; c < n; c++) {
        if (!a->c[c]->n.n) continue;
        for (i = 1, k = 0; i < n; i++)
            for (j = 0; j < n; j++)
                if (j != c) mn[k++] = a->c[i * n + j];
        pconst(&v->d, 1);
        if (!mdet(s, a, rm & ~1u, rm & ~(1u << c), &v->n)) goto full;
        gmat(s, SR_MDETM, mn, n - 1, n - 1, n - 1, 1, "");
        if (n == 3) gst(s, SR_MDET2, "q*q-q*q=r", mn[0], mn[3], mn[1], mn[2], v);
        else gst(s, SR_MDETV, "r", v);
        *u = *a->c[c];
        if (!rmulinto(s, u, v, 0)) goto full;
        if (c & 1) pneg(&u->n);
        gst(s, SR_MDETE, c & 1 ? "~q*q=r" : "q*q=r", a->c[c], v, u);
        *v = *u;
        if (!raddinto(s, d, v)) goto full;
        if ((m = gfm(s, e, m, "g", u)) == GFAIL) goto full;
        if (!t++ && e[0] == T_ADD) {            /* no + in front of the first term */
            for (i = 0; i < m; i++) e[i] = e[i + 1];
            m--;
        }
    }
    if (t > 1) {
        m = gfm(s, e, m, "=r", d);
        if (m == GFAIL) goto full;
        gput(s, SR_MDETS, 0, 0, e, m);
    } else if (!t) gst(s, SR_MDETV, "r", d);
    return;
full:
    s->st[2] |= 1;
}

/* DOTP, CROSSP, NORM of the vectors a (and b), p, q scratch */
NOINLINE static void mvsteps(ps_t *s, uint8_t cmd, mat_t *a, mat_t *b, rat_t *p, rat_t *q)
{
    uint8_t e[MAXOUT], k, j, n = 0, cells = a->r * a->w;
    rat_t *x = get(s);
    if (!x) goto full;
    if (cmd == C_CROSSP) {
        for (k = 0; k < 3; k++) {
            j = (k + 1) % 3;
            if (!cross(s, p, a->c[j], b->c[(k + 2) % 3], a->c[(k + 2) % 3], b->c[j])) goto full;
            gst(s, SR_MCRS, "q*q-q*q=r", a->c[j], b->c[(k + 2) % 3], a->c[(k + 2) % 3], b->c[j], p);
        }
        return;
    }
    for (k = 0; k < cells; k++)
        if ((n = gfm(s, e, n, cmd == C_DOTP ? k ? "+q*q" : "q*q" : k ? "+q^" : "$q^", a->c[k], cmd == C_DOTP ? b->c[k] : 0)) == GFAIL)
            goto full;
    if (cmd == C_NORM) n = gfm(s, e, n, ")");
    if (n == GFAIL) goto full;
    gput(s, cmd == C_DOTP ? SR_MDOT : SR_MNRM, 0, 0, e, n);
    rconst(q, 0);
    for (k = n = 0; k < cells; k++) {
        *p = *a->c[k];
        if (!rmulinto(s, p, cmd == C_DOTP ? b->c[k] : a->c[k], 0)) goto full;
        *x = *p;
        if (!raddinto(s, q, x)) goto full;
        if (cmd == C_DOTP && (n = gfm(s, e, n, k ? "g" : "q", p)) == GFAIL) goto full;
    }
    if (cmd == C_DOTP) {
        n = gfm(s, e, n, "=r", q);
        if (n == GFAIL) goto full;
        gput(s, SR_MPROD, 0, 0, e, n);
    } else gst(s, SR_GSQ, "$r)", q);
    return;
full:
    s->st[2] |= 1;
}

/* the steps of a Matrix command that leaves its matrix alone, before it runs (MINV: after its determinant p) */
NOINLINE static void msteps(ps_t *s, uint8_t cmd, mat_t *a, mat_t *b, rat_t *p, rat_t *q)
{
    uint8_t top = s->top, k;
    unsigned rm;
    rat_t *ds[3];
    if (cmd == C_MDET || cmd == C_MINV) {
        if (cmd == C_MINV && a->r > 3) gst(s, SR_MDETV, "r", p);
        else if (a->r > 1) {
            ds[0] = cmd == C_MDET ? p : q;
            for (k = 1; k < 3; k++) ds[k] = get(s);
            if (!ds[1] || !ds[2]) s->st[2] |= 1;
            else mdsteps(s, a, ds);
            if (cmd == C_MDET) {                /* p was the scratch: the determinant again */
                rm = (1u << a->r) - 1;
                mdet(s, a, rm, rm, &p->n);
                pconst(&p->d, 1);
            }
        }
        if (cmd == C_MINV && a->r > 1 && a->r < 4) {
            mcof(s, a, a->r == 2 ? SR_MISWAP : SR_MICOF, a->r == 2, &q->n);
            if (a->r == 3) mcof(s, a, SR_MITR, 1, &q->n);
        }
    } else mvsteps(s, cmd, a, b, p, q);
    s->top = top;
}

/* MINV's last step: divide by the determinant p; the answer is what it comes to */
NOINLINE static void mdiv(ps_t *s, rat_t *p, const uint8_t *out, uint8_t n)
{
    uint8_t e[MAXOUT], pn = gfm(s, e, 0, "r", p);
    if (pn == GFAIL) s->st[2] |= 1;
    else gput(s, SR_MIDIV, e, pn, out, n);
}

/* MRREF: one row operation, its parameter e[pn] made by the caller, and the matrix after it */
NOINLINE static void mrlog(ps_t *s, mat_t *a, uint16_t rule, uint8_t *e, uint8_t pn)
{
    uint8_t n;
    if (!s->st || s->st[2]) return;
    if (pn == GFAIL || !(n = putmat(s, a->c, a->r, a->w, a->w, 1, e, pn))) s->st[2] |= 1;
    else gput(s, rule, e, pn, e + pn, n - pn);
}

#endif

NOINLINE static uint8_t matrix0(ps_t *s, uint8_t cmd, const char *name, uint8_t *out)
{
    mat_t a, b;
    rat_t *p, *q, *f, *x[3];
    uint8_t n = 0, i, j, k, cells, rc, vec, fact, lg = 0, ge[MAXOUT];
    unsigned rm, cm;

    if ((rc = rdmat(s, &a)) != 1) goto fail;
    b.r = b.w = 0;
    if (cmd == C_DOTP || cmd == C_CROSSP) {
        if (peek(s) != T_COMMA) goto syntax;
        s->i++;
        if ((rc = rdmat(s, &b)) != 1) goto fail;
    }
    if (peek(s) == T_RPAR) s->i++;
    if (s->i != s->len) goto syntax;
    cells = a.r * a.w;
    vec = a.r == 1 || a.w == 1;                 /* a row or a column */
    rm = (1u << a.r) - 1; cm = (1u << a.w) - 1;
    p = get(s);                                 /* scratch; MINV wants two more */
    if (!p || (cmd != C_MDET && !(q = get(s))) || (cmd == C_MINV && !(f = get(s)))) return 0;
    pconst(&p->d, 1);
    if (cmd != C_MDET) pconst(&q->d, 1);
    if (cmd == C_MTRANS) {
        n = putmat(s, a.c, a.w, a.r, 1, a.w, out, 0);
        if (s->st && n) gput(s, SR_MTR, 0, 0, out, n);
    }
    else if (cmd == C_MDET || cmd == C_MINV) {
        if (a.r != a.w) goto dim;
        if (!mdet(s, &a, rm, cm, &p->n)) return 0;
        if (cmd == C_MDET) {
            if (s->st) msteps(s, cmd, &a, &b, p, q);
            n = answer(s, p, out, 0);
        }
        else {                                  /* p the determinant, q a cofactor */
            if (!p->n.n) return error(out, "SINGULAR MAT", "", "");
            if (s->st) msteps(s, cmd, &a, &b, p, q);
            fact = p->n.n > 1;                  /* then 1/(det)*[[cofactors]], else each over det */
            if (fact) {
                PUT(0x31); PUT(T_DIV); PUT(T_LPAR);
                if (!(n = emit(s, &p->n, out, n))) goto wide;
                PUT(T_RPAR); PUT(T_MUL);
            }
            PUT(T_LBRK);
            for (i = 0; i < a.r; i++) {
                PUT(T_LBRK);
                for (j = 0; j < a.r; j++) {     /* (-1)^(i+j) times the minor without row j and column i */
                    if (j) PUT(T_LCOMMA);
                    if (!mdet(s, &a, rm & ~(1u << j), cm & ~(1u << i), &q->n)) return 0;
                    if ((i + j) & 1) pneg(&q->n);
                    if (fact) n = emit(s, &q->n, out, n);
                    else {
                        *f = *q; f->d = p->n;
                        n = reduce(s, f) == 1 ? answer(s, f, out, n) : 0;
                    }
                    if (!n) goto wide;
                }
                PUT(T_RBRK);
            }
            PUT(T_RBRK);
            if (s->st) mdiv(s, p, out, n);
        }
    } else if (cmd == C_MRREF) {                /* Gauss-Jordan, in place */
        uint8_t pr = 0;
        for (j = 0; j < a.w && pr < a.r; j++) {
            for (i = pr; i < a.r && !a.c[i * a.w + j]->n.n; i++) ;
            if (i == a.r) continue;
            for (k = 0; k < a.w; k++) {
                f = a.c[i * a.w + k]; a.c[i * a.w + k] = a.c[pr * a.w + k]; a.c[pr * a.w + k] = f;
            }
            if (s->st && i != pr) { lg = 1; mrlog(s, &a, SR_MSWAP, ge, gfm(s, ge, 0, "Rn,Rn;", pr + 1, i + 1)); }
            *p = *a.c[pr * a.w + j];            /* row pr over its pivot, then the pivot's column out of the rest */
            for (k = 0; k < a.w; k++)
                if (a.c[pr * a.w + k]->n.n && !rmulinto(s, a.c[pr * a.w + k], p, 1)) return 0;
            if (s->st && !isnum(p, 1) && (lg = 1)) mrlog(s, &a, SR_MSCALE, ge, gfm(s, ge, 0, "Rn=Rn/q;", pr + 1, pr + 1, p));
            for (i = 0; i < a.r; i++) {
                if (i == pr || !a.c[i * a.w + j]->n.n) continue;
                *p = *a.c[i * a.w + j];
                for (k = 0; k < a.w; k++) {
                    if (!a.c[pr * a.w + k]->n.n) continue;
                    *q = *a.c[pr * a.w + k];
                    if (!rmulinto(s, q, p, 0)) return 0;
                    pneg(&q->n);
                    if (!raddinto(s, a.c[i * a.w + k], q)) return 0;
                }
                if (s->st) {                    /* R(i) = R(i) -/+ p*R(pr), p the cell it cleared */
                    lg = 1;
                    mrlog(s, &a, SR_MELIM, ge, gfm(s, ge, 0, "Rn=Rnh*Rn;", i + 1, i + 1, p, pr + 1));
                }
            }
            pr++;
        }
        if (s->st && !lg) mrlog(s, &a, SR_MRDONE, ge, 0);
        n = putmat(s, a.c, a.r, a.w, a.w, 1, out, 0);
    } else {                                    /* DOTP, CROSSP, NORM: vectors */
        if (!vec || (b.r && b.r != 1 && b.w != 1)) goto dim;
        if (b.r && cells != b.r * b.w && cmd == C_DOTP) return error(out, "DIM MISMATCH", "", "");
        if (cmd == C_CROSSP && (cells != 3 || b.r * b.w != 3)) goto dim;
        if (s->st) msteps(s, cmd, &a, &b, p, q);
        if (cmd == C_CROSSP) {                  /* a2b3-a3b2, a3b1-a1b3, a1b2-a2b1 */
            for (i = 0; i < 3; i++) {
                if (!(x[i] = get(s))) return 0;
                x[i]->n.n = 0;
                pconst(&x[i]->d, 1);
                j = (i + 1) % 3; k = (i + 2) % 3;
                if (!pmac(s, &x[i]->n, &a.c[j]->n, &b.c[k]->n, 0) ||
                    !pmac(s, &x[i]->n, &a.c[k]->n, &b.c[j]->n, 1)) return 0;
            }
            n = putrow(s, x, 3, 1, out, 0);
        } else {
            p->n.n = 0;
            for (i = 0; i < cells; i++)
                if (!pmac(s, &p->n, &a.c[i]->n, &(cmd == C_DOTP ? b.c[i] : a.c[i])->n, 0)) return 0;
            n = cmd == C_DOTP ? answer(s, p, out, 0) : groot(s, name, out, 0, p);
        }
    }
    return n == SYMCE_ERR ? n : n ? n : MWIDE;
wide:
    return MWIDE;
dim:
    return error(out, "INVALID DIM", "", "");
fail:
    if (rc == 3) goto dim;
    return rc ? error(out, "SYNTAX", name, "") : 0;
syntax:
    return error(out, "SYNTAX", name, "");
}

/* the Matrix menu's commands; steps( shows the working in the step log, the answer is the same */
#ifdef NOSTEPSX
#define matrix matrix0
#else
NOINLINE static uint8_t matrix(ps_t *s, uint8_t cmd, const char *name, uint8_t *out)
{
    uint8_t n = matrix0(s, cmd, name, out);
    if (s->st && n && n != SYMCE_ERR && n != MWIDE) gfinal(s, out, n);
    return n;
}
#endif

/* tokens a:n hold no variable, equation, "or" or list: a number the OS can evaluate */
static uint8_t scalar(const uint8_t *a, uint8_t n)
{
    uint8_t i, c;
    for (i = 0; i < n; i++) {
        c = a[i];
        if (c == T_BB || c == T_EXT) i++;           /* two-byte token: its second byte is no variable */
        else if (is_var(c) || c == T_EQ || c == T_OR || c == T_LBRACE || c == T_LCOMMA) return 0;
    }
    return 1;
}

/* The command whose name and '(' start at in (len tokens left): its number, and in *nl the
   tokens up to and past the '(' ; 0 when none. The one place a command name is recognised
   (engine() and nested()). */
static uint8_t cmdat(const uint8_t *in, unsigned len, uint8_t *nl)
{
    const char *name = cmds;
    uint8_t cmd;
    unsigned k, j;
    /* a name is lowercase tokens (BB B0 a .. j, BB BB k .. z: a gap at BB BA), which only the
       menus type; capital letters, as typed by hand, are variables, never a command */
    for (cmd = 1; cmd <= NCMD; cmd++) {
        for (k = j = 0; name[j] && k + 1 < len && in[k] == T_BB &&
                        in[k + 1] == (uint8_t)(0xB0 + (name[j] - 'A') + (name[j] >= 'K')); j++, k += 2) ;
        if (!name[j] && k < len && in[k] == T_LPAR) { *nl = (uint8_t)(k + 1); return cmd; }
        while (*name++) ;
    }
    return 0;
}

/* engine_device.py's ENGTEST must fit the RAM (the program ends before 0xD40000), so it runs three builds:
   -DNOSIDE leaves out the Geometry and Matrix menus and PARTFRAC, -DNOCALC the Calculus menu; -DNOSIDE -DNOINT
   (INTEGRAL out) is the one that holds LIMIT, SUM, PRODUCT and DOMINANTTERM, which -DNOLS leaves out of the
   first. They answer SYMCE LIMIT for what they left out; the vectors of each are only the commands it keeps. */
#ifdef NOSIDE
#define sidecmd(s, cmd, name, out) 0
#else
#define sidecmd(s, cmd, name, out) (cmd >= C_MDET ? matrix(s, cmd, name, out) : geo(s, cmd, name, out))
#endif
#ifdef NOCALC
#define calcx(s, cmd, name, out) 0
#else
#define calcx(s, cmd, name, out) calc(s, cmd, name, out)
#endif

/* The entry is `len` tokens at `in`. Writes the answer, 1..MAXOUT display
   tokens, to `out` and returns its length; 0 leaves the entry to the OS.
   `mode` is MODE_MP in MathPrint, where the screen fits more (width()), and
   MODE_DEC when the answer is to be in decimals wherever they end.
   `work` is SYMCE_WORK bytes of scratch. `ans` is the last answer as a length
   byte then its tokens, or 0; it must not overlap `out`. MODE_DEG: degrees.
   `look` (or 0) gives a letter token's stored real, 9 bytes, or 0 when
   nothing is stored: SOLVE's other letters (see the top). */
static uint8_t engine(const uint8_t *in, unsigned len, uint8_t *out, void *work,
                      const uint8_t *ans, uint8_t mode, const uint8_t *(*look)(uint8_t))
{
    uint8_t n, cmd, split, own, hard, off = 0, nest = mode & MODE_NEST;
    const char *name = cmds;
    ps_t s;
    rat_t *p;
    unsigned k = 0, wmax = mode & (MODE_EVAL | MODE_INNER) ? 999 : SCR_WIDTH;
/* An error's Goto spot, a token offset into the entry, at out[MAXOUT] (hook.c): where the
   parse stopped when it is inside the arguments, else the command's first token. */
#define ERRAT() (out[MAXOUT] = s.i && s.i < len ? (uint8_t)(off + s.i) : 0)

    if (!len) return 0;
    /* A command: its name in letters, then '(', and the rest its arguments.
       From here on it is never left to the OS, which would read it as a
       product of variables. */
    cmd = cmdat(in, len, &off);
    if (cmd) {
        for (k = 1; k < cmd; k++) while (*name++) ;
        in += off; len -= off;
    }
    if (cmd == C_STEPS) return error(out, "SYMCE LIMIT", "STEPS", "");   /* only as the whole entry */
#ifndef NODESOLVE
    if (cmd == C_DESOLVE) return desolve(in, len, out, work, mode);
#endif
    /* Ans counts only if the entry uses it: 2+2 stays the OS's even right
       after 4X. When Ans is SymCE's own answer (own), the OS's Ans was never
       set (hook.c), so the entry is answered here or is an error screen, never
       left to the OS: POLYDEGREE(X²), then +1, is 3, not the OS's stale Ans
       plus 1 (measured: 1; and 2√(2), then *3, was 3). */
    for (k = 0; k < len && in[k] != T_ANS; k++) ;
    s.ans = k < len && ans && ans[0] && (ans[0] <= MAXOUT || ans[0] == ANS_REAL) ? ans : 0;
    own = s.ans && s.ans[0] != ANS_REAL;
    /* A number SymCE answered is also the OS's Ans (symce_engine), so what this cannot
       do is left to the OS, which reads that number: sin(Ans). A symbolic Ans it never set. */
    hard = own && !scalar(ans + 1, ans[0]);
    out[MAXOUT + 1] = cmd;                  /* symce_engine: a command, not an expression */
    n = 0;
    if (len > (cmd >= C_MDET ? 2 * MAXLEN : mode & MODE_EVAL ? 250 : MAXLEN) && !((cmd == C_SOLVE || cmd == C_CSOLVE) && in[0] == T_LBRACE && len <= 2 * MAXLEN)) goto limit;   /* a system: 6 equations */
    s.in = in; s.len = len; s.i = 0;
    s.pool = (rat_t *)work; s.top = 0; s.depth = 0; s.nv = 0;
    s.dec = mode & MODE_DEC;
    s.mp = mode & MODE_MP;
    s.deg = mode & MODE_DEG; s.wide = s.noexp = 0;
    s.lim = NPOOL; s.fn = mode & MODE_EVAL && !cmd ? C_TOLN : 0; s.home = 0; s.kn = 0; s.pl = 0; s.la = s.lb = 0; s.nk = 0; s.inans = 0; s.tb = 0;
    s.look = look; s.sm = 0;
    s.st = mode & MODE_STEP && cmd && !(mode & MODE_EVAL) ? (uint8_t *)work + STOFF : 0;
    streset(s.st);
    if (cmd == C_SOLVE || cmd == C_CSOLVE) {    /* ", V" and maybe ")" close it */
        k = len - (len && in[len - 1] == T_RPAR);
        if (k >= 2 && in[k - 2] == T_COMMA && is_var(in[k - 1])) s.sm = (uint32_t)1 << (in[k - 1] - T_VAR0);
        else if (k > 2 && in[k - 1] == T_RBRACE && in[0] == T_LBRACE)   /* SOLVE({..},{X,Y}) */
            for (k--; k && in[--k] != T_LBRACE; ) if (is_var(in[k])) s.sm |= (uint32_t)1 << (in[k] - T_VAR0);
    }

    /* Ranks go out in alphabetical order, so comparing two packed monomials
       is already the tie-break the output wants. A byte in the variable range
       can also be the second half of a two-byte token; that over-counts, but
       the parse refuses the entry at the first byte of such a token anyway. */
    for (k = 0; k < NVTOK; k++) s.rank[k] = 0;
    for (k = 0; k < len; k++) if (is_var(in[k])) s.rank[in[k] - T_VAR0] = 1;
    if (cmd >= C_DISTANCE && cmd < C_MDET) s.rank[0x58 - T_VAR0] = s.rank[0x59 - T_VAR0] = 1;   /* lines, circles */
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
        n = cmd >= C_DISTANCE ? sidecmd(&s, cmd, name, out) :
            cmd > C_TCOLLECT ? calcx(&s, cmd, name, out) : fncmd(&s, cmd, name, out);
        /* a point's (3/2,1/2) as Classic counts it: width() stacks fractions
           only where parentheses are not a point's */
        if (n == SYMCE_ERR) ERRAT();
        if (n == SYMCE_ERR || (n && n != MWIDE && (cmd >= C_MDET ? s.mp ? mpwidth(out, n) : width(out, n, 0) :
                              width(out, n, s.mp && cmd < C_DISTANCE)) <= (mode & MODE_INNER ? 999u : SCR_WIDTH)))
            return n;
        goto wide;
    }
    /* A factored answer too wide for the screen is tried again without
       splitting quadratics over their roots. */
    if (cmd)
        for (split = 1; ; split = 0) {
            s.i = 0; s.top = 0; s.depth = 0; streset(s.st);
            n = (cmd == C_SOLVE || cmd == C_CSOLVE) && in[0] == T_LBRACE ? systems(&s, cmd, name, out)
                                                                         : command(&s, cmd, name, out, split);
            if (!n && cmd == C_DERIV) {         /* with functions, or an order */
                s.i = 0; s.top = 0; s.depth = 0; streset(s.st);
                n = calcx(&s, cmd, name, out);
            }
#if !defined(NODESOLVE) && !defined(NOSTEPSX)
            if (!n && cmd == C_SOLVE && in[0] != T_LBRACE) n = fxsolve(&s, out);     /* with e^( a^X ln( log( */
#endif
            if (n == SYMCE_ERR) ERRAT();
            if (n == SYMCE_ERR || (n && (width(out, n, s.mp) <= wmax || (s.wide && s.st)))) return n;
            if (cmd < C_FACTOR || !split) goto wide;
        }
    /* Plain arithmetic is the OS's job, unless a root in it stays one: √(8)
       is 2√(2), but √(4) is the OS's 2, and so is anything in DEC. */
    for (k = 0; k < len && in[k] != T_SQRT; k++) ;
    if (!own && !nest && !s.nv && !(mode & MODE_EVAL) && (k == len || mode & MODE_DEC)) return 0;

    p = get(&s);
    s.home = 1;
    if (expr(&s, p) && s.i == len && reduce(&s, p) == 1) {
        if (!own && !nest && !s.nv && !hasrad(&p->n) && !(mode & MODE_EVAL)) return 0;
        n = answer(&s, p, out, 0);
        if (n && width(out, n, s.mp) <= wmax) return n;
    }
    /* ponytail: with own, a list or an equation in Ans, or sin(Ans), is a
       SYMCE LIMIT; the OS could only get it wrong. */
    if (!hard && !nest) return 0;
wide:
    if (n && n != MWIDE && (cmd || hard || nest)) return n;         /* past the screen: symce_engine hands it to the answer view */
    if (n && (cmd || hard || nest)) return error(out, "TOO WIDE", s.mp ? "" : "TRY MATHPRINT", "");
    if (n) return 0;
limit:
    return cmd || hard || nest ? error(out, "SYMCE LIMIT", cmd ? name : nest ? "NESTED" : "ANS", "") : 0;
}

/* STO: the answer's tokens as the OS reads them, in w; their number. The engine writes
   some as the glyphs the result routine maps (T_LCOMMA, T_I, cos( in letters). */
static uint8_t totok(const uint8_t *a, uint8_t n, uint8_t *w)
{
    static const uint8_t cosl[7] = { T_BB, 0xB2, T_BB, 0xBF, T_BB, 0xC3, T_LPAR };
    uint8_t i, k, m = 0, c;
    for (i = 0; i < n; i++) {
        c = a[i];
        for (k = 0; k < 7 && i + k < n && a[i + k] == cosl[k]; k++) ;
        if (k == 7) { w[m++] = 0xC4; i += 6; continue; }
        if (c == T_BB || c == T_EXT) { w[m++] = c; if (++i < n) w[m++] = a[i]; continue; }
        w[m++] = c == T_LCOMMA ? T_COMMA : c == T_I ? 0x2C : c;
    }
    return m;
}

#define NSB 192           /* nested(): each of its two rewrite buffers, behind the pool in `work` */
typedef char nest_fits[SYMCE_WORK + 2 * NSB <= 0x4E60 ? 1 : -1];

/* One value: no equation, list, point, matrix or system, so it can stand in parentheses. */
static uint8_t onevalue(const uint8_t *a, uint8_t n)
{
    uint8_t i, c;
    for (i = 0; i < n; i++) {
        c = a[i];
        if (c == T_BB || c == T_EXT) i++;
        else if ((c >= T_EQ && c <= T_EQ + 5) || c == T_OR || c == T_AND || c == T_LBRACE || c == T_LBRK ||
                 c == T_LCOMMA || c == T_COMMA) return 0;
    }
    return 1;
}

/* 1 when position p of the entry is inside a string literal or is the second byte of a 2-byte token:
   a command name there is text, not a command. A string ends at the next ", or at -> : or newline. */
static uint8_t instr(const uint8_t *in, unsigned p)
{
    unsigned i;
    uint8_t q = 0, t;
    for (i = 0; i < p; i++) {
        t = in[i];
        if (t == 0x2A) q ^= 1;
        else if (q && (t == T_STO || t == 0x3E || t == 0x3F)) q = 0;
        else if (two(t)) i++;
    }
    return q || i > p;
}

/* A command may stand anywhere in an entry, in another command's arguments too:
   SOLVE(EXPAND((X+1)^2)=0,X). The last command name in the entry has no other
   inside it, so it is run on its own (name to its ')'), and its answer goes back into the
   entry as (answer), in the other one of two buffers behind the pool; then again, and the
   entry as it is by then goes to engine(). A loop, not a recursion: the stack stays what
   engine() needs. Only an answer that is one value goes back; anything else, or an error,
   is shown, its Goto spot the inner command's. An entry with none is engine()'s as it was. */
static uint8_t nested(const uint8_t *in, unsigned len, uint8_t *out, void *work,
                      const uint8_t *ans, uint8_t mode, const uint8_t *(*look)(uint8_t))
{
    uint8_t *buf = (uint8_t *)work + SYMCE_WORK, *nb;
    uint8_t c, nl, nl2, n, b = 0, inner = 0, tl;
    unsigned p, e, k, m, lim = 0;
    int d;
    if (mode & MODE_EVAL) return engine(in, len, out, work, ans, mode, look);
    if (len > NSB) {                    /* past 3 * MAXLEN: engine() refuses it, but a command inside must not be read as letters */
        for (p = 1; p < len; p++)
            if (cmdat(in + p, len - p, &nl) && !instr(in, p)) { error(out, "SYMCE LIMIT", "NESTED", ""); out[MAXOUT] = (uint8_t)p; return SYMCE_ERR; }
        return engine(in, len, out, work, ans, mode, look);
    }
    for (;;) {
        p = len; c = 0;
        while (p) {
            p--;
            if ((c = cmdat(in + p, len - p, &nl)) && !instr(in, p)) break;
            c = 0;
        }
        if (!c) break;
        for (k = p > 26 ? p - 26 : 0; k < p; k++)           /* inside a longer name: CFACTOR( is not C FACTOR( */
            if ((m = cmdat(in + k, len - k, &nl2)) && k + nl2 > p) { p = k; c = (uint8_t)m; nl = nl2; break; }
        for (e = p + nl, d = 1; e < len && d; e++) {        /* its ')': the openers are those of the comma finder */
            k = in[e];
            if (k == T_RPAR) d--;
            else if (k == T_LPAR || k == T_SQRT || k == T_ABS || (k >= T_LN && k <= T_ATAN) ||
                     (k == T_EXT && e + 1 < len && in[e + 1] == 0x34)) d++;
            e += two(k);
        }
        if (e > len) e = len;
        if (!p && e == len) break;                          /* the entry is that one command */
        n = engine(in + p, e - p, out, work, ans, (mode & ~(MODE_NEST | MODE_STEP)) | MODE_INNER, look);
        if (!n || n == SYMCE_ERR || !onevalue(out, n)) {
            if (n != SYMCE_ERR) error(out, "ARGUMENT", "NOT ONE VALUE", "");
            out[MAXOUT] = (uint8_t)p;
            return SYMCE_ERR;
        }
        nb = buf + b * NSB;
        if (p + n + 2 + len - e > NSB) { error(out, "SYMCE LIMIT", "NESTED", ""); out[MAXOUT] = (uint8_t)p; return SYMCE_ERR; }
        for (k = 0; k < p; k++) nb[k] = in[k];
        nb[p] = T_LPAR;
        m = p + 1 + (tl = totok(out, n, nb + p + 1));
        nb[m++] = T_RPAR;
        for (k = e; k < len; k++) nb[m++] = in[k];
        if (mode & MODE_STEP && e - p < 100) {    /* steps( : First: the inner command, its answer */
            uint8_t *st = (uint8_t *)work + STOFF;
            if (stlog(st, SR_FIRST, in + p, (uint8_t)(e - p), nb + p + 1, tl, 0)) { st[64] = st[1]; st[65] = st[68]; st[66] = st[69]; }
        }
        in = nb; len = m; b ^= 1; lim = p;
        if (!inner) inner = c;
    }
    if (inner) mode |= MODE_NEST;
    n = engine(in, len, out, work, ans, mode, look);
    if (inner) {
        if (!out[MAXOUT + 1]) out[MAXOUT + 1] = inner;
        if (n == SYMCE_ERR && out[MAXOUT] > lim) out[MAXOUT] = (uint8_t)lim;
    }
    return n;
}

#ifdef __ez80__
/* osrun(tok, n, sync): the OS parser on n tokens, as eval() in graph.c does it: 0, or the OS error code.
   With sync, a real or complex result is also Ans (StoAns). OP1..3 and the parse pointers are put back:
   this runs inside the OS's own parse of the entry (the hook's A=2). */
extern uint8_t symce_osrun(const uint8_t *tok, unsigned n, uint8_t sync, const uint8_t *dst);
__asm__(
"	.section	.text._symce_osrun,\"ax\",@progbits\n"
"	.globl	_symce_osrun\n"
"_symce_osrun:\n"
"	push	ix\n"
"	ld	ix, 0\n"
"	add	ix, sp\n"                /* ix+6 tok, +9 n, +12 sync */
"	ld	iy, 0x0D00080\n"
"	ld	hl, (0x0D02317)\n"        /* begPC, curPC, endPC */
"	push	hl\n"
"	ld	hl, (0x0D0231A)\n"
"	push	hl\n"
"	ld	hl, (0x0D0231D)\n"
"	push	hl\n"
"	ld	hl, (0x0D005F8)\n"        /* OP1 */
"	push	hl\n"
"	ld	hl, (0x0D005FB)\n"
"	push	hl\n"
"	ld	hl, (0x0D005FE)\n"
"	push	hl\n"
"	push	ix\n"
"	ld	hl, (0x0D02596)\n"
"	push	hl\n"
"	ld	hl, .Losrun_e\n"
"	call	0x0020798\n"          /* PushErrorHandler */
"	ld	a, (ix + 12)\n"
"	cp	a, 3\n"
"	jr	nz, .Losrun_v\n"
"	ld	hl, (ix + 15)\n"          /* a store to Yn or Strn: make the variable, with the tokens as its data */
"	call	.Losrun_nm\n"
"	call	0x002050C\n"          /* ChkFindSym */
"	jr	c, .Losrun_mk\n"
"	call	0x0021434\n"          /* DelVarArc: HL, DE as ChkFindSym left them */
".Losrun_mk:\n"
"	call	.Losrun_nm\n"
"	ld	hl, (ix + 9)\n"
"	ld	a, (0x0D005F8)\n"
"	cp	a, 3\n"
"	jr	nz, .Losrun_ms\n"
"	call	0x0020560\n"          /* CreateEqu */
"	jr	.Losrun_mc\n"
".Losrun_ms:\n"
"	call	0x0020554\n"          /* CreateStrng */
".Losrun_mc:\n"
"	inc	de\n"
"	inc	de\n"
"	ld	bc, (ix + 9)\n"
"	ld	hl, (ix + 6)\n"
"	ldir\n"
"	ld	a, (0x0D005F8)\n"
"	cp	a, 3\n"
"	jr	nz, .Losrun_md\n"
"	call	.Losrun_nm\n"
"	call	0x002050C\n"          /* ChkFindSym: HL the entry */
"	dec	hl\n"
"	set	0, (hl)\n"             /* selected, so it plots */
".Losrun_md:\n"
"	call	0x002079C\n"          /* PopErrorHandler */
"	xor	a, a\n"
"	jr	.Losrun_e\n"
".Losrun_nm:\n"                  /* OP1 = the destination's name: type, two bytes, 0 */
"	ld	hl, (ix + 15)\n"
"	ld	de, 0x0D005F8\n"
"	ld	bc, 3\n"
"	ldir\n"
"	xor	a, a\n"
"	ld	(de), a\n"
"	ret\n"
".Losrun_v:\n"
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
".Losrun_g:\n"
"	call	0x0020F00\n"          /* ParseInp */
"	ld	a, (ix + 12)\n"
"	cp	a, 2\n"
"	jr	nz, .Losrun_n\n"
"	ld	a, (0x0D005F8)\n"        /* a store to a letter: only a real, copied into its variable (as graph.c's stov) */
"	or	a, a\n"
"	ld	a, 0x84\n"
"	jr	nz, .Losrun_e\n"
"	ld	hl, 0x0D005F8\n"
"	ld	de, 0x0D0060C\n"
"	ld	bc, 9\n"
"	ldir\n"
"	ld	hl, (ix + 15)\n"
"	ld	de, 0x0D005F8\n"
"	ld	bc, 3\n"
"	ldir\n"
"	xor	a, a\n"
"	ld	(de), a\n"
"	call	0x002050C\n"          /* ChkFindSym */
"	jr	nc, .Losrun_lf\n"
"	call	0x0020534\n"          /* CreateReal: DE = its data */
".Losrun_lf:\n"
"	ld	hl, 0x0D0060C\n"
"	ld	bc, 9\n"
"	ldir\n"
".Losrun_n:\n"
"	call	0x002079C\n"          /* PopErrorHandler */
"	xor	a, a\n"
".Losrun_e:\n"
"	pop	de\n"
"	push	af\n"
"	call	0x0020E80\n"          /* FixTempCnt */
"	pop	af\n"
"	pop	ix\n"
"	ld	iy, 0x0D00080\n"
"	res	0, (iy + 7)\n"
"	or	a, a\n"
"	jr	nz, .Losrun_f\n"
"	ld	a, (ix + 12)\n"
"	cp	a, 1\n"
"	ld	a, 0\n"
"	jr	nz, .Losrun_f\n"
"	ld	a, (0x0D005F8)\n"
"	and	a, 0x1F\n"
"	jr	z, .Losrun_s\n"
"	cp	a, 0x0C\n"
"	ld	a, 0\n"
"	jr	nz, .Losrun_f\n"
".Losrun_s:\n"
"	call	0x0020F30\n"          /* StoAns */
"	xor	a, a\n"
".Losrun_f:\n"
"	pop	hl\n"
"	ld	(0x0D005FE), hl\n"
"	pop	hl\n"
"	ld	(0x0D005FB), hl\n"
"	pop	hl\n"
"	ld	(0x0D005F8), hl\n"
"	pop	hl\n"
"	ld	(0x0D0231D), hl\n"
"	pop	hl\n"
"	ld	(0x0D0231A), hl\n"
"	pop	hl\n"
"	ld	(0x0D02317), hl\n"
"	pop	ix\n"
"	ld	hl, 0\n"
"	ld	l, a\n"
"	ret\n"
);
#else
/* host: nothing to store into */
static uint8_t symce_osrun(const uint8_t *tok, unsigned n, uint8_t sync, const uint8_t *dst) { (void)tok; (void)n; (void)sync; (void)dst; return 0; }
#endif

/* After an answer (n tokens at out): a number is the OS's Ans too (see engine()), and a store,
   `->Y1`, `->Str1` or `->A` at d (dl bytes), is done. The OS does both through its parser, so
   they are what typing them would do: Y1 and Str1 get the answer's text, A its value.
   An answer that cannot go where it was sent is DATA TYPE, but `2X+1->A` is the OS's. */
static uint8_t finish(uint8_t *out, uint8_t n, uint8_t *w, const uint8_t *d, uint8_t dl, uint8_t prg)
{
    uint8_t m = totok(out, n, w + 1), num = scalar(out, n), cmd = out[MAXOUT + 1], i, e;
    uint8_t dst[3];
    if (dl == 1) {                              /* A: its value */
        if (!num) return cmd ? error(out, "DATA TYPE", "NOT A NUMBER", "") : 0;
        dst[0] = 0; dst[1] = d[0]; dst[2] = 0;
        if (!prg) symce_osrun(w + 1, m, 1, 0);
        e = symce_osrun(w + 1, m, 2, dst);
    } else if (dl) {                            /* Y1, Str1: the answer as text */
        for (i = 0; i < n; i++)
            if (out[i] == T_EQ || out[i] == T_OR || out[i] == T_LBRACE) return error(out, "DATA TYPE", "", "");
        dst[0] = d[0] == 0x5E ? 3 : 4; dst[1] = d[0]; dst[2] = d[1];
        if (num && !prg) symce_osrun(w + 1, m, 1, 0);
        e = symce_osrun(w + 1, m, 3, dst);
    } else {
        if (num && !prg) symce_osrun(w + 1, m, 1, 0);
        return n;
    }
    return e ? error(out, "DATA TYPE", "", "") : n;
}

/* the columns the home screen needs, as engine() counted them for a command's answer */
static unsigned wfit(const uint8_t *out, uint8_t n, uint8_t mode)
{
    uint8_t c = out[MAXOUT + 1];
    return c >= C_MDET ? mode & MODE_MP ? mpwidth(out, n) : width(out, n, 0) : width(out, n, mode & MODE_MP && c < C_DISTANCE);
}

/* engine(), and after an answer, at out[MAXOUT], the columns Classic draws
   it in: hook.c right-aligns it by them.
   The engine runs from flash, and a CE before rev M fetches each flash byte
   in 6 + (flash wait states) cycles. The OS leaves 4; for the run, 3, what
   every CEdev program has run at since 2017: FACTOR(X^4-5X^2+6) 729 -> 661 ms
   in CEmu, the same answer. Lower is faster and misreads flash now and then,
   so never. Rev M+ (serial flash, cached) ignores the port and reads it as 0;
   0, or a value some other program already lowered, is left alone. */
/* Bare SymCE command wrap in TI-BASIC: each outermost bare CMD(args) becomes expr("CMD(args)").
   End of a bare call: its matching ')'; or at depth 1: '->', ':', newline (0x3F), or end of source.
   Put missing ')' inside the string. Leave source untouched (return 0) when args contain '"',
   the source is under 5 or over WRAPMAX bytes, or nothing matched. out is NULL to only measure:
   wi->len is the copy's length either way. wi->tgt (0xFFFF: none) is an offset in the copy; wi->orig then
   gets the source offset of the first call that ends after it (0xFFFF: none), the Goto spot of an error. */
#define WRAPMAX 8000                      /* the copy is under 2x that: 16 bits end to end */
typedef struct { uint16_t len, tgt, orig; } wrapinfo;
static uint8_t wrap_bare(const uint8_t *in, unsigned len, uint8_t *out, wrapinfo *wi)
{
    unsigned i, q = 0, matched = 0;
#define WPUT(b) do { uint8_t b_ = (b); if (out) out[q] = b_; q++; } while (0)   /* b has side effects: always evaluated */
    wi->orig = 0xFFFF;
    wi->len = 0;
    if (len < 5 || len > WRAPMAX) return 0;
    for (i = 0; i < len; ) {
        if (in[i] == 0x2A) {                    /* string literal: copy verbatim */
            WPUT(in[i++]);
            while (i < len && in[i] != 0x2A && in[i] != T_STO && in[i] != 0x3E && in[i] != 0x3F) {
                WPUT(in[i]);
                if (two(in[i]) && i + 1 < len) WPUT(in[++i]);
                i++;
            }
            if (i < len && in[i] == 0x2A) WPUT(in[i++]);
            continue;
        }
        uint8_t cm = 0;
        unsigned namelen = 0;
        if (in[i] >= 'A' && in[i] <= 'Z' &&
            (i == 0 || in[i - 1] < 'A' || (in[i - 1] > 'Z' && in[i - 1] != 0x5B))) {
            const char *nm = cmds;
            for (cm = 1; cm <= NCMD; cm++) {
                unsigned j;
                for (j = 0; nm[j] && i + j + 1 < len && in[i + j] == (uint8_t)nm[j]; j++) ;
                if (!nm[j] && in[i + j] == T_LPAR) {
                    namelen = j;
                    break;
                }
                while (*nm++) ;
            }
        }
        if (cm && cm <= NCMD) {
            unsigned j = i + namelen + 1;       /* after '(' */
            int depth = 1;
            uint8_t has_close = 0;
            while (j < len) {
                uint8_t t = in[j];
                if (t == 0x2A) {                /* args contain '"' -> leave untouched */
                    wi->orig = 0xFFFF;
                    return 0;
                }
                if (t == T_RPAR) {
                    depth--;
                    if (depth == 0) {
                        has_close = 1;
                        j++;                    /* include matching ')' */
                        break;
                    }
                } else if (t == T_LPAR || t == T_SQRT || t == T_ABS || (t >= T_LN && t <= T_ATAN) ||
                           (t == T_EXT && j + 1 < len && in[j + 1] == 0x34)) {
                    depth++;
                } else if (depth == 1 && (t == T_STO || t == 0x3E || t == 0x3F)) {
                    break;                      /* depth-0 sto, colon, or newline ends bare call */
                }
                if (two(t) && j + 1 < len) j++;
                j++;
            }
            matched = 1;
            WPUT(T_BB);
            WPUT(0x2A);                          /* expr( */
            WPUT(0x2A);                          /* " */
            unsigned k;
            for (k = 0; k < j - i; k++)
                WPUT(in[i + k]);
            if (!has_close)
                WPUT(T_RPAR);                    /* missing ')' inside string */
            WPUT(0x2A);                          /* " */
            WPUT(T_RPAR);                        /* ) */
            if (wi->orig == 0xFFFF && q > wi->tgt) wi->orig = (uint16_t)i;
            i = j;
        } else {
            WPUT(in[i]);
            if (two(in[i]) && i + 1 < len) WPUT(in[++i]);
            i++;
        }
    }
    wi->len = (uint16_t)q;
    return matched;
}

#ifdef STKPAINT
#define SYMCE_ENGINE symce_engine_real
static __attribute__((noinline))
#else
#define SYMCE_ENGINE symce_engine
#endif
uint8_t SYMCE_ENGINE(const uint8_t *in, unsigned len, uint8_t *out, void *work,
                     const uint8_t *ans, uint8_t mode, const uint8_t *(*look)(uint8_t))
{
    if (mode == MODE_WRAP) return wrap_bare(in, len, out, (wrapinfo *)(void *)ans);
    uint8_t prg = (mode & MODE_PRGM) == MODE_PRGM;
    if (prg) {                          /* the parser hook: nothing unless a command stands outside the string literals */
        uint8_t nl0;
        unsigned p0, q0 = 0, i0;
        if (len > 250) return 0;
        uint8_t *tmp = (uint8_t *)work + SYMCE_WORK + 2 * NSB;      /* free behind nested()'s buffers (len <= 250 here, so <= 500 B) */
        /* A program typed with ALPHA spells FACTOR( in capital letters: copy the entry with each such name, outside the
           string literals, as the lowercase tokens cmdat() knows. A hand-typed program is the only way to get these. */
        for (i0 = 0; i0 < len; i0++) {
            const char *nm = cmds;
            uint8_t cm = NCMD + 1, j0;
            if (in[i0] >= 'A' && in[i0] <= 'Z')
                for (cm = 1; cm <= NCMD; cm++) {
                    for (j0 = 0; nm[j0] && i0 + j0 + 1 < len && in[i0 + j0] == (uint8_t)nm[j0]; j0++) ;
                    if (!nm[j0] && in[i0 + j0] == T_LPAR && !instr(in, i0)) {
                        for (j0 = 0; nm[j0]; j0++) {
                            tmp[q0++] = T_BB;
                            tmp[q0++] = (uint8_t)(0xB0 + (nm[j0] - 'A') + (nm[j0] >= 'K'));
                        }
                        i0 += j0 - 1;
                        break;
                    }
                    while (*nm++) ;
                }
            if (cm > NCMD) {
                tmp[q0++] = in[i0];
                if (two(in[i0]) && i0 + 1 < len) tmp[q0++] = in[++i0];
            }
        }
        in = tmp; len = q0;
        for (p0 = 0; p0 < len; p0++)
            if (cmdat(in + p0, len - p0, &nl0) && !instr(in, p0)) break;
        if (p0 >= len) return 0;
        mode = (mode & 0x3F) | MODE_INNER;
        ans = 0;
    }
#ifdef __ez80__
    volatile uint8_t *const ws = (uint8_t *)0xE00005;
    uint8_t old = *ws;
    if (old > 3)
        *ws = 3;
#endif
    const uint8_t *d = 0;
    uint8_t dl = 0, n, nl, *st = (uint8_t *)work + STOFF;
    unsigned w, k;
    st[0] = 0;                          /* no steps unless this entry is steps( ... ) */
    if (!(mode & 0x80) && !prg && cmdat(in, len, &nl) == C_STEPS) {
        in += nl; len -= nl;
        if (len && in[len - 1] == T_RPAR) len--;
        mode |= MODE_STEP | MODE_NEST;
        st[64] = 0; st[65] = STHDR & 255; st[66] = STHDR >> 8;    /* the base: no steps yet */
        streset(st);
        st[3] = len <= 60 ? (uint8_t)len : 0;
        for (k = 0; k < (len <= 60 ? st[3] : 60); k++) st[4 + k] = in[k];    /* st[3] 0 with an entry: too long, the screen shows its first 60 bytes and ... */
    }
    /* a trailing ->Y1 (5E 1x), ->Str1 (AA 0x) or ->A is a store of the answer */
    for (k = len; k && in[k - 1] != T_STO; k--) ;
    if (k && (len - k == 1 ? is_var(in[k]) : len - k == 2 && ((in[k] == 0x5E && in[k + 1] >= 0x10 && in[k + 1] <= 0x19) ||
                                                              (in[k] == 0xAA && in[k + 1] <= 9)))) {
        d = in + k;
        dl = (uint8_t)(len - k);
        len = k - 1;
    }
    if (mode & 0x80) {                  /* Graph Ans: no entry, the answer is Ans, to be stored at d */
        if (ans[0] == 0xFF) {
            out[0] = T_ANS;
            n = 1;
        } else {
            for (n = 0; n < ans[0]; n++) out[n] = ans[n + 1];
        }
        out[MAXOUT + 1] = 0;
    } else {
        uint8_t drop = mode & MODE_STEP && st[3] ? psolve(work, mode) : 0;     /* stepsalg */
        n = nested(in, len, out, work, ans, mode, look);
        if (drop) { st[1] = st[64]; st[2] = 0; st[68] = st[65]; st[69] = st[66]; }
    }
#ifdef __ez80__
    if (old > 3)
        *ws = old;
#endif
    if (n && n != SYMCE_ERR && mode & MODE_STEP && !(mode & 0x80) && !st[1] && !st[2]) asteps(work, out, n, mode);   /* stepsalg */
    if (n && mode & MODE_STEP && (n != SYMCE_ERR || (st[67] && st[1] && !st[2]))) {      /* the answer ends the log; steps that failed are none */
        if (st[2] & 2) st[1] = 0;
        w = st[68] | st[69] << 8;
        st[w] = n == SYMCE_ERR ? 0 : totok(out, n, st + w + 1);
        st[68] = (uint8_t)(w + 1 + st[w]); st[69] = (uint8_t)((w + 1 + st[w]) >> 8);
        st[0] = STMAGIC;
    }
    if (n && n != SYMCE_ERR && n != MWIDE && !st[0] && !(mode & (MODE_EVAL | MODE_INNER | 0x80)) && wfit(out, n, mode) > SCR_WIDTH) {
        /* An answer wider than the screen: the home screen shows what fits, and menu.c's steps_show opens the
           whole answer (the log with no steps; flag 0x80: the answer view, not a steps( run). */
        st[1] = 0; st[2] = 0x80;
        st[3] = len <= 60 ? (uint8_t)len : 0;
        for (k = 0; k < (len <= 60 ? st[3] : 60); k++) st[4 + k] = in[k];
        st[70] = totok(out, n, st + 71);
        st[68] = (uint8_t)(71 + st[70]); st[69] = 0;
        st[0] = STMAGIC;
    }
    if (n && n != SYMCE_ERR) {
        w = width(out, n, 0);
        out[MAXOUT] = w > 255 ? 255 : (uint8_t)w;
        n = finish(out, n, (uint8_t *)work, d, dl, prg);
        if (prg && n != SYMCE_ERR && n > MAXOUT) n = error(out, "SYMCE LIMIT", "WIDE", "");   /* MWIDE: no room for it in the hook's 128 B buffer */
        if (n == SYMCE_ERR) out[MAXOUT] = 0;
        else if (prg && n && !scalar(out, n)) {         /* symbolic: a string for expr( to return, 2A and the tokens */
            for (k = n; k; k--) out[k] = out[k - 1];
            out[0] = 0x2A;
            n++;
        }
    }
    return n;
}

#ifdef STKPAINT
#ifndef STKBOT
#define STKBOT 0xD19900      /* painting 0xD1987E..0xD198FF resets the calculator (measured), so stop short */
#endif
/* tools/emu/stack.py's build (-DSTKPAINT, never shipped): paint the OS stack (stackBot 0xD1987E; from
   0xD19900 up to just below this frame) with A5, run the engine, and keep at 0xD09566 the lowest byte any call
   touched: "STK", calls, then per call (9 at most) the lowest byte touched, 3 bytes, and at +31 the
   entry SP (about: the wrapper's local). The paint pointer is made from an integer or clang drops it. */
uint8_t symce_engine(const uint8_t *in, unsigned len, uint8_t *out, void *work,
                     const uint8_t *ans, uint8_t mode, const uint8_t *(*look)(uint8_t))
{
    volatile uint8_t mark;
    volatile uint8_t *const rec = (volatile uint8_t *)0xD09566;
    unsigned top = (unsigned)&mark - 128, q, bot = STKBOT, low;
    volatile uint8_t *p;
    uint8_t r;
#ifdef STKNOPAINT
    bot = 0xFFFFFF;
#endif
    if (top <= bot || (unsigned)&mark > 0xD1A87E) {       /* not on the OS stack: only say where it is */
        r = SYMCE_ENGINE(in, len, out, work, ans, mode, look);
        rec[0] = 'S'; rec[1] = 'T'; rec[2] = 'K'; rec[3] = 0xEE; rec[31] = (uint8_t)(unsigned)&mark; rec[32] = (uint8_t)((unsigned)&mark >> 8); rec[33] = (uint8_t)((unsigned)&mark >> 16);
        return r;
    }
    for (q = bot; q < top; q++) *(volatile uint8_t *)q = 0xA5;
    r = SYMCE_ENGINE(in, len, out, work, ans, mode, look);
    for (q = bot; q < top && *(volatile uint8_t *)q == 0xA5; q++) ;
    low = q;
    p = rec;
    if (p[0] != 'S' || p[1] != 'T' || p[2] != 'K') { p[0] = 'S'; p[1] = 'T'; p[2] = 'K'; p[3] = 0; }
    q = p[3];
    if (q < 9) {                        /* slot q: the lowest byte this call touched, 3 bytes */
        p[4 + 3 * q] = (uint8_t)low; p[5 + 3 * q] = (uint8_t)(low >> 8); p[6 + 3 * q] = (uint8_t)(low >> 16);
    }
    p[3]++;
    p[31] = (uint8_t)(unsigned)&mark; p[32] = (uint8_t)((unsigned)&mark >> 8); p[33] = (uint8_t)((unsigned)&mark >> 16);
    return r;
}
#endif
