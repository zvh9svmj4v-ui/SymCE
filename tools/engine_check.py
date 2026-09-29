#!/usr/bin/env python3
"""Differential fuzz of symce/src/engine.c (built on the host as engine_cli).

    python3 tools/engine_check.py [N]

Three oracles, each independent of the C:

  * minipoly_cli, the single-variable engine this replaces. Wherever it
    answers, the new engine must give the SAME BYTES.
  * sympy. Random expression trees are rendered to TI tokens, and the exact
    expected answer is computed from the tree: expanded by sympy, then put in
    canonical form by fmt() below. Any answer must match it byte for byte.
  * a second TI parser, written here as a Pratt parser, for inputs that are
    not trees (random tokens, minipoly's fuzz corpus). If the engine answers,
    this must accept the entry and agree on the value.

Every entry is asked twice: as in Classic, where the answer must fit 26
tokens, and as in MathPrint, where it must fit 26 columns as width() counts
them. The expected answer is the same; only whether it fits differs. Every
other tree is asked twice more, as for MODE ANSWERS: DEC: in decimals where
they end, as if the entry had a decimal point.

Ans stands for the engine's own last answer. An entry with Ans must answer
exactly what the same entry with (answer) in its place does, and Ans alone
must give the answer back unchanged. The OS's own Ans is never set when the
engine answers, so an entry with the engine's Ans is never refused: past what
the engine can do it is an error screen.

The function commands (TOLN .. TCOLLECT, section 7) are checked by value, not
bytes: sympy evaluates the answer and the entry at random points, the answer
must be in the command's form, and the Nspire reference's examples must come
out exactly.

Refusing is always allowed: the entry then goes to the OS. Refusals of entries
whose answer would have fit are counted, with a few shown, because each one
is a limit (term count, nesting, an intermediate over 999999) and should be
one on purpose.
"""
import random, subprocess, sys, os
from fractions import Fraction
from math import gcd, lcm
import sympy

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENGINE = os.path.join(ROOT, 'symce/bin/host/engine_cli')
MINI = os.path.join(ROOT, 'symce/bin/host/minipoly_cli')

LIM, MAXEXP, NV, WIDTH, MAXLEN, MAXOUT = 999999, 15, 6, 26, 64, 64
ADD, SUB, MUL, DIV, POW, NEG, ANS = 0x70, 0x71, 0x82, 0x83, 0xF0, 0xB0, 0x72
LP, RP, SQR, CUBE, INV, DOT, SQRT = 0x10, 0x11, 0x0D, 0x0F, 0x0C, 0x3A, 0xBC
FRAC = [0xEF, 0x2E]         # the n/d bar as the OS flattens a MathPrint fraction
LB, RB, LC, I_ = 0x08, 0x09, 0x2C, 0xD7   # answers only: { } and the glyphs , and i
TXT = {ADD: '+', SUB: '-', MUL: '*', DIV: '/', POW: '^', NEG: '~', LP: '(',
       RP: ')', SQR: '²', CUBE: '³', INV: '⁻¹', 0x5B: 'θ', 0x3A: '.', SQRT: '√('}


# What a student's keyboard can put in the edit buffer, plus a few tokens that
# must be refused. Weighted towards what actually gets typed.
ALPHABET = (list(range(0x30, 0x3A)) * 3 +          # digits
            [0x58] * 6 + [0x41, 0x59, 0x42] +      # X mostly, other letters too
            [0x70] * 4 + [0x71] * 3 +              # + -
            [0x82] * 2 + [0xF0] * 2 +              # * ^
            [0xB0, 0x0D, 0x0F, 0x0C] +             # negate, squared, cubed, x⁻¹
            [0x10, 0x11, 0x83, 0x5F, 0xBB, 0xEF] +  # ( ) / prgm and two leads
            [DOT, SQRT])                           # decimal point, √(


def structured(rnd):
    """A well-formed polynomial most of the time, so the answer path actually
    gets exercised. Pure random tokens are refused about nine times in ten."""
    out = []
    for term in range(rnd.randint(1, 4)):
        if term:
            out.append(rnd.choice([0x70, 0x71]))
            if rnd.random() < 0.15:
                out.append(0xB0)                     # 2X+-3X
        elif rnd.random() < 0.2:
            out.append(0xB0)                         # leading negate
        factors = rnd.randint(1, 3)
        for f in range(factors):
            if f and rnd.random() < 0.5:
                out.append(0x82)                     # explicit *
            if rnd.random() < 0.45:
                num = [rnd.randrange(0x30, 0x3A) for _ in range(rnd.randint(1, 3))]
                if rnd.random() < 0.2:               # a decimal: 1.5, .25, 5.
                    num.insert(rnd.randint(0, len(num)), DOT)
                out += num
            else:
                out.append(rnd.choice([0x58] * 5 + [0x41, 0x59]))
                r = rnd.random()
                if r < 0.2:
                    out += [0xF0, rnd.randrange(0x30, 0x3A)]
                elif r < 0.3:
                    out.append(0x0D)
                elif r < 0.35:
                    out.append(0x0F)
    if rnd.random() < 0.1:                           # sometimes corrupt it
        out[rnd.randrange(len(out))] = rnd.choice(ALPHABET)
    return bytes(out[:64])


def show(tokens):
    return ''.join(TXT.get(b, chr(b) if 0x20 < b < 0x7F else '<%02x>' % b)
                   for b in tokens).replace('<ef>.', '⁄')


def isvar(b):
    return 0x41 <= b <= 0x5B


def sym(b):
    return sympy.Symbol('v%02x' % b)        # sorts like the token


class Refuse(Exception):
    """The entry is valid but out of scope (divide by zero, 0^0, ...)."""


class Syntax(Exception):
    pass


class Kernel(Refuse):
    """A root this oracle cannot spell, which the engine keeps whole as a
    kernel: √(X+1), √(-X), √(XY), √(X²) (|X|). Any answer must have the
    value, checked by same_value()."""


LAX = [False]                               # root() takes a Kernel's root


def lax_value(tokens):
    """the entry's value with its kernel roots taken, or None"""
    LAX[0] = True
    try:
        return pratt(tokens)
    except (Syntax, Refuse):
        return None
    finally:
        LAX[0] = False


def same_value(o, v):
    """the answer o has the value v at random real points where v is real"""
    try:
        g = fvalue(o)
    except Exception:
        return False
    syms = sorted(v.free_symbols | g.free_symbols, key=str)
    rnd, pts = random.Random(7), []
    for _ in range(60):
        pt = {x: sympy.Rational(rnd.randint(-12, 12), rnd.randint(1, 6)) for x in syms}
        try:
            a, b = complex(v.subs(pt).evalf(30)), complex(g.subs(pt).evalf(30))
        except (TypeError, ValueError, ZeroDivisionError):
            continue
        if a == a and b == b and abs(a) < 1e12:    # b nan: a pole of the answer, X=Y=0 in Y/X
            pts.append((a, b))
    real = [(a, b) for a, b in pts if abs(a.imag) < 1e-12]
    use = real if len(real) >= 3 else pts    # √(-Y²): the principal root, as sympy's
    return len(use) >= 3 and all(abs(a - b) <= 1e-9 * (1 + abs(a)) for a, b in use)


def divide(a, b):
    """a / b in lowest terms, or Refuse: by zero."""
    if sympy.expand(b) == 0:
        raise Refuse('divide by zero')
    return sympy.cancel(a / b)


def raise_to(a, n):
    """a^n for a whole n, negative too, or Refuse."""
    if abs(n) > 31:
        raise Refuse('exponent')
    if sympy.expand(a) == 0 and n <= 0:
        raise Refuse('0^0' if n == 0 else 'divide by zero')
    return sympy.cancel(a ** n)


def has_root(e):
    return any(p.exp.is_Rational and p.exp.q != 1 for p in e.atoms(sympy.Pow))


def root(a):
    """√a, where the engine takes one: a is c·M/K, a positive number times
    monomials, with no root in it. √(c·M·K)/K, and K may not be negative, so
    at most one variable is left under the root (it says that one is not
    negative) and no even power's half is odd (√(X²) is |X|). Else Refuse."""
    a = sympy.cancel(a)
    if a == 0:
        return a
    if has_root(a):
        raise Refuse('root under a root')
    n, d = sympy.fraction(a)
    syms = sorted(a.free_symbols, key=lambda s: s.name)
    try:
        tn = sympy.Poly(n, *syms).terms() if syms else [((), n)]
        td = sympy.Poly(d, *syms).terms() if syms else [((), d)]
    except sympy.PolynomialError:               # 4^(X²): not a polynomial, so not a monomial
        if LAX[0]:
            return sympy.sqrt(a)
        raise Kernel('radicand not a polynomial')
    c = tn[0][1] / td[0][1]
    e = [x + y for x, y in zip(tn[0][0], td[0][0])]
    if len(tn) != 1 or len(td) != 1 or c < 0 or sum(k % 2 for k in e) > 1 or any(k % 4 == 2 for k in e):
        if LAX[0]:
            return sympy.sqrt(a)
        raise Kernel('radicand not one positive term, |X| or two variables under the root')
    v = sympy.sqrt(c)
    for s_, k, kd in zip(syms, e, td[0][0]):
        v *= s_ ** sympy.Rational(k, 2) / s_ ** kd
    return v


def gens(expr):
    """expr with every root a symbol: √p is R<p> for each prime p, √x is R_x;
    returns it and each symbol's square."""
    sq = {}

    def conv(p):
        b, m = p.base, p.exp.p                 # b^(m/2), m odd
        out = b ** ((m - 1) // 2)
        if b.is_Integer:
            for pr, e in sympy.factorint(b).items():
                out *= pr ** (e // 2)
                if e % 2:
                    r = sympy.Symbol('R%d' % pr)
                    sq[r] = sympy.Integer(pr)
                    out *= r
            return out
        r = sympy.Symbol('R_' + b.name)
        sq[r] = b
        return out * r
    return expr.replace(lambda p: p.is_Pow and p.exp.is_Rational and p.exp.q == 2, conv), sq


def fmt_root(expr, nvars, dec):
    """fmt() for a value with roots in it. Written from the definition, not
    from engine.c: roots are symbols R with R² = p, a denominator is rid of
    them one at a time by its conjugate (R -> -R), and lowest terms means no
    common factor of D and every part of N under one set of roots."""
    e, sq = gens(sympy.together(expr))
    n, d = sympy.fraction(sympy.together(e))

    def red(x):
        x = sympy.expand(x)
        for r, v in sq.items():
            x = sympy.expand(sympy.rem(x, r ** 2 - v, r))
        return x
    n, d = red(n), red(d)
    for r in sorted(sq, key=str):
        if d.has(r):
            c = d.subs(r, -r)
            n, d = red(n * c), red(d * c)
    rs = sorted(sq, key=str)
    syms = sorted((n.free_symbols | d.free_symbols) - set(rs), key=lambda s: s.name)
    g = d
    for c in (sympy.Poly(n, *rs).coeffs() if rs else [n]):
        g = sympy.gcd(g, c)
    n, d = sympy.expand(sympy.cancel(n / g)), sympy.expand(sympy.cancel(d / g))
    if n == 0:
        return bytes([0x30])

    def rows_r(x):
        out = []
        for ex, c in sympy.Poly(x, *syms, *rs).terms():
            mono, rx = ex[:len(syms)], ex[len(syms):]
            num, var = 1, None
            for r, k in zip(rs, rx):
                if k:
                    if sq[r].is_Integer:
                        num *= int(sq[r])
                    elif var is not None:
                        return None                  # √X√Y: the engine refuses
                    else:
                        var = sq[r]
            out.append((mono, Fraction(int(sympy.fraction(c)[0]), int(sympy.fraction(c)[1])), num, var))
        vk = lambda v: 0 if v is None else 100 - int(v.name[1:], 16)
        out.sort(key=lambda r: (-(2 * sum(r[0]) + (r[3] is not None)), tuple(-k for k in r[0]),
                                vk(r[3]), r[2]))
        return out
    if not d.free_symbols:
        rn = rows_r(n / d)
        return None if rn is None else spell_root(rn, syms, dec)
    rn, rd = rows_r(n), rows_r(d)
    if rn is None:
        return None
    cs = [r[1] for r in rn + rd]
    f = Fraction(lcm(*[c.denominator for c in cs]), gcd(*[c.numerator for c in cs]))
    if rd[0][1] < 0:
        f = -f
    rn = [(m, c * f, k, v) for m, c, k, v in rn]
    rd = [(m, c * f, k, v) for m, c, k, v in rd]
    sn, sd = spell_root(rn, syms), spell_root(rd, syms)
    if sn is None or sd is None:
        return None
    if len(rn) > 1:
        sn = [LP] + sn + [RP]
    if len(rd) > 1 or rd[0][1] != 1 or sum(1 for e in rd[0][0] if e) != 1:
        sd = [LP] + sd + [RP]
    return sn + [DIV] + sd


def spell_root(rows, syms, dec=False):
    """spell_poly() for rows (powers, coefficient, number under the root,
    variable under it or None): 3X√(2X)/2."""
    if any(k > LIM for _, _, k, _ in rows):
        return None
    plain = spell_poly([(m, c) for m, c, _, _ in rows], syms, dec, [(k, v) for _, _, k, v in rows])
    return plain


# ---- oracle 2: canonical form, straight from the definition ----

def rows(expr, syms):
    """An expanded polynomial as (powers, Fraction) pairs, in canonical order:
    total degree, highest first; ties by the earlier variable's power."""
    expr = sympy.expand(expr)
    if expr == 0:
        return []
    terms = sympy.Poly(expr, *syms).terms() if syms else [((), expr)]
    out = [(mono, Fraction(int(sympy.fraction(c)[0]), int(sympy.fraction(c)[1])))
           for mono, c in terms]
    out.sort(key=lambda r: (-sum(r[0]), tuple(-e for e in r[0])))
    return out


def spell_decimal(a):
    """A positive Fraction whose denominator divides 10^6 as a decimal: 0.25"""
    ip = a.numerator // a.denominator
    return '%d.%s' % (ip, ('%06d' % ((a - ip) * 10 ** 6)).rstrip('0'))


def spell_poly(rows, syms, dec=False, roots=None):
    """Tokens for rows, or None past the coefficient or power limits. After a
    decimal entry, coefficients that end are decimals. roots: each row's
    (number, variable) under a root, (1, None) for none."""
    if not rows:
        return [0x30]
    if any(abs(c.numerator) > LIM or c.denominator > LIM or any(e > MAXEXP for e in mono)
           for mono, c in rows):
        return None
    roots = roots or [(1, None)] * len(rows)
    if rows[0][1] < 0 and any(c > 0 for _, c in rows):
        rows, roots = rows[::-1], roots[::-1]
    out = []
    for k, (mono, c) in enumerate(rows):
        rk, rv = roots[k]
        if c < 0:
            out.append(SUB if k else NEG)
        elif k:
            out.append(ADD)
        a = abs(c.numerator)
        dot = dec and c.denominator != 1 and 10 ** 6 % c.denominator == 0
        if dot:
            out += spell_decimal(abs(c)).encode().replace(b'.', bytes([DOT]))
        elif a != 1 or (not any(mono) and rk == 1 and rv is None):
            out += str(a).encode()
        for s, e in zip(syms, mono):
            if e:
                out.append(int(s.name[1:], 16))
                if e == 2: out.append(SQR)
                elif e == 3: out.append(CUBE)
                elif e > 3: out += [POW] + list(str(e).encode())
        if rk != 1 or rv is not None:
            out.append(SQRT)
            if rk != 1:
                out += str(rk).encode()
            if rv is not None:
                out.append(int(rv.name[1:], 16))
            out.append(RP)
        if c.denominator != 1 and not dot:
            out.append(DIV)
            out += str(c.denominator).encode()
    return out


def fmt(expr, nvars, dec=False):
    """The expected answer tokens for a sympy value, or None if it is out of
    scope (a coefficient or power over the limits, too wide).

    A polynomial is its terms, coefficients as fractions: 3X/2+1/2. Anything
    with a variable below the bar is N/D in lowest terms, both expanded, whole
    coefficients with no common factor, D's first term positive; a side is in
    parentheses unless it is one term, and D unless it is one variable."""
    if nvars > NV:
        return None
    if has_root(expr):
        out = fmt_root(expr, nvars, dec)
        return bytes(out) if out is not None and len(out) <= MAXOUT else None
    n, d = sympy.fraction(sympy.cancel(sympy.together(expr)))
    syms = sorted(n.free_symbols | d.free_symbols, key=lambda s: s.name)
    if not d.free_symbols:
        out = spell_poly(rows(n / d, syms), syms, dec)
    else:
        rn, rd = rows(n, syms), rows(d, syms)
        cs = [c for _, c in rn + rd]
        f = Fraction(lcm(*[c.denominator for c in cs]), gcd(*[c.numerator for c in cs]))
        if rd[0][1] < 0:
            f = -f
        rn = [(m, c * f) for m, c in rn]
        rd = [(m, c * f) for m, c in rd]
        sn, sd = spell_poly(rn, syms), spell_poly(rd, syms)
        if sn is None or sd is None:
            return None
        if len(rn) > 1:
            sn = [LP] + sn + [RP]
        if len(rd) > 1 or rd[0][1] != 1 or sum(1 for e in rd[0][0] if e) != 1:
            sd = [LP] + sd + [RP]
        out = sn + [DIV] + sd
    return bytes(out) if out is not None and len(out) <= MAXOUT else None


def width(t, mp):
    """Home screen columns. Classic: a token each. MathPrint: '^' none, each
    A/B stacked without parentheses, as wide as its wider side plus one for
    the bar, and a column per sign."""
    if not mp:
        return len(t) + t.count(SQRT)          # √( is two characters
    cells = lambda s: sum(b not in (POW, LP, RP) for b in s)
    terms, cur, depth = [], [], 0
    for b in t:
        depth += (b in (LP, SQRT)) - (b == RP)
        if not depth and b in (ADD, SUB, NEG):
            terms.append(cur)
            cur = []
        else:
            cur.append(b)
    terms.append(cur)
    w = len(terms) - 1
    for s in terms:
        k = s.index(DIV) if DIV in s else None
        w += cells(s) if k is None else max(cells(s[:k]), cells(s[k + 1:])) + 1
    return w


# ---- oracle 3: an independent TI parser (Pratt) ----

def pratt(tokens):
    """TI tokens -> sympy expression. Raises Syntax or Refuse."""
    # The n/d bar parses exactly like / (measured on OS 5.8.4); a lone EF is
    # some other two-byte token and stays a syntax error.
    tokens = bytes(tokens).replace(bytes(FRAC), bytes([DIV]))
    pos = [0]

    def peek():
        return tokens[pos[0]] if pos[0] < len(tokens) else None

    def take():
        pos[0] += 1
        return tokens[pos[0] - 1]

    def starts_operand(t):
        return t is not None and (0x30 <= t <= 0x39 or t == DOT or isvar(t) or t in (LP, SQRT, ABS, I_))

    def atom():
        t = peek()
        if t is None:
            raise Syntax('end')
        if 0x30 <= t <= 0x39 or t == DOT:
            v, d, dot, digits = 0, 1, False, False
            while True:
                t = peek()
                if t == DOT and not dot:
                    take()
                    dot = True
                    continue
                if t is None or not 0x30 <= t <= 0x39:
                    break
                v, digits = v * 10 + take() - 0x30, True
                d *= 10 if dot else 1
                if v > LIM or d > LIM:
                    raise Refuse('literal')
            if not digits or t == DOT:
                raise Syntax('decimal point')
            return sympy.Rational(v, d)
        if isvar(t):
            take()
            return sym(t)
        if t == I_:                     # only in the answers of the C commands
            take()
            return sympy.I
        if t in (LP, SQRT, ABS):
            take()
            e = parse(0)
            if peek() == RP:
                take()
            elif peek() is not None:
                raise Syntax(')')
            return root(e) if t == SQRT else root(raise_to(e, 2)) if t == ABS else e
        raise Syntax('atom %r' % t)

    def post():
        e = atom()
        while peek() in (SQR, CUBE, INV):
            e = power(e, {SQR: 2, CUBE: 3, INV: -1}[take()])
        return e

    def power(base, n):
        return raise_to(base, sympy.Integer(n))

    # binding powers: + - 10, * / implied 20, prefix ~ 25, ^ 30
    def parse(rbp):
        if peek() == NEG:
            take()
            left = -parse(25)
        else:
            left = post()
        while True:
            t = peek()
            if t in (ADD, SUB) and rbp < 10:
                take()
                r = parse(10)
                left = sympy.cancel(left + r if t == ADD else left - r)
            elif (t in (MUL, DIV) or starts_operand(t)) and rbp < 20:
                if t in (MUL, DIV):
                    take()
                r = parse(20)
                if t == DIV:
                    left = divide(left, r)
                else:
                    left = sympy.cancel(left * r)
            elif t == POW and rbp < 30:
                take()
                neg = False
                while peek() == NEG:
                    take()
                    neg = not neg
                e = post()
                if neg:
                    e = -e
                if e.free_symbols and LAX[0] and left != 0:
                    left = left ** e            # X^X: the engine's K_POW kernel
                    continue
                if e.free_symbols:
                    raise Refuse('symbolic exponent')
                if not e.is_integer:
                    raise Refuse('fractional exponent')
                left = power(left, e)
            else:
                return left

    e = parse(0)
    if pos[0] != len(tokens):
        raise Syntax('trailing')
    return e


def expected(tokens):
    """Oracle answer for any token string: bytes, or None for out of scope."""
    if not tokens or len(tokens) > MAXLEN:
        return None
    nv = len({b for b in tokens if isvar(b)})
    if not nv and SQRT not in tokens:
        return None
    try:
        v = pratt(tokens)
        return fmt(v, nv, DOT in tokens) if nv or has_root(v) else None
    except (Syntax, Refuse):
        return None


def ti_real(v, digits=None):
    """A Fraction with a terminating decimal, as the OS keeps a real: sign and
    type, exponent 0x80 + e, 14 BCD digits. digits overrides the mantissa."""
    m = 0
    while (v * 10 ** m).denominator != 1:
        m += 1
    d = digits or str(abs(v.numerator * 10 ** m // v.denominator))
    e = len(d) - 1 - m if d.strip('0') else 0
    return bytes([0x80 if v < 0 else 0, 0x80 + e]) + bytes.fromhex(d.ljust(14, '0')[:14])


def spell(v):
    """Tokens for a decimal Fraction, as someone would type it: (~N/1000).
    Not with a decimal point: an Ans real does not make the answer decimal."""
    n = abs(v.numerator)
    m = 0
    while (v * 10 ** m).denominator != 1:
        m += 1
    t = ([NEG] if v < 0 else []) + [0x30 + int(c) for c in str(n * 10 ** m // v.denominator)]
    if m:
        t += [DIV, 0x31] + [0x30] * m
    return bytes([LP] + t + [RP])


# ---- random expression trees, rendered with TI precedence ----

VARS = [0x58] * 6 + [0x59] * 3 + [0x41, 0x42, 0x5B]


def tree(rnd, depth):
    if depth <= 0 or rnd.random() < 0.3:
        if rnd.random() < 0.5:
            return ('num', rnd.choice([0, 1, 2, 3, 4, 5, 6, 10, 12, rnd.randrange(1000)]))
        return ('var', rnd.choice(VARS))
    k = rnd.choice(['add', 'add', 'sub', 'mul', 'mul', 'imul', 'div', 'neg',
                    'pow', 'sqr', 'cube', 'recip', 'paren', 'sqrt'])
    if k == 'sqrt':                             # mostly a radicand it takes
        if rnd.random() < 0.7:
            m = ('num', rnd.choice([2, 3, 4, 5, 8, 12, 18, 20, 27, 50, 1, 9, 75]))
            for _ in range(rnd.randint(0, 2)):
                v = ('var', rnd.choice(VARS))
                m = (rnd.choice(['imul', 'mul', 'div']), m, rnd.choice([v, ('sqr', v), ('cube', v)]))
            return ('sqrt', m)
        return ('sqrt', tree(rnd, depth - 1))
    if k in ('add', 'sub', 'mul', 'imul'):
        return (k, tree(rnd, depth - 1), tree(rnd, depth - 1))
    if k == 'div':
        d = tree(rnd, depth - 1) if rnd.random() < 0.3 else ('num', rnd.randrange(0, 13))
        n = tree(rnd, depth - 1)
        if rnd.random() < 0.3:                  # a multiple of the divisor: exact
            n = ('mul', n, d)
        return ('div', n, d)
    if k == 'pow':
        e = ('num', rnd.choice([0, 1, 2, 2, 3, 4, 5, 8, 12, 16]))
        if rnd.random() < 0.1:
            e = ('neg', e)
        return ('pow', tree(rnd, depth - 1), e)
    return (k, tree(rnd, depth - 1))


PREC = {'add': 1, 'sub': 1, 'mul': 2, 'imul': 2, 'div': 2, 'neg': 3, 'pow': 4,
        'sqr': 5, 'cube': 5, 'recip': 5, 'num': 6, 'var': 6, 'paren': 6, 'sqrt': 6}


def render(node, rnd):
    k = node[0]

    def sub(n, need):
        s = render(n, rnd)
        if PREC[n[0]] < need or rnd.random() < 0.05:
            s = [LP] + s + [RP]
        return s

    if k == 'num':
        return list(str(node[1]).encode())
    if k == 'var':
        return [node[1]]
    if k == 'paren':
        return [LP] + render(node[1], rnd) + [RP]
    if k == 'sqrt':
        return [SQRT] + render(node[1], rnd) + [RP]
    if k in ('add', 'sub'):
        return sub(node[1], 1) + [ADD if k == 'add' else SUB] + sub(node[2], 2)
    if k == 'mul':
        return sub(node[1], 2) + [MUL] + sub(node[2], 3)
    if k == 'div':
        if rnd.random() < 0.3:              # an n/d box, as the OS flattens one
            return [LP] + render(node[1], rnd) + [RP] + FRAC + sub(node[2], 3)
        return sub(node[1], 2) + [DIV] + sub(node[2], 3)
    if k == 'imul':
        a, b = sub(node[1], 2), sub(node[2], 4)
        # implied only where the calculator reads it as a product: never
        # digit-then-digit (23 is a number), and b is never a negation
        if 0x30 <= b[0] <= 0x39 and 0x30 <= a[-1] <= 0x39:
            b = [LP] + b + [RP]
        return a + b
    if k == 'neg':
        return [NEG] + sub(node[1], 3)
    if k == 'pow':
        e = node[2]
        ex = [NEG] + list(str(e[1][1]).encode()) if e[0] == 'neg' else list(str(e[1]).encode())
        return sub(node[1], 4) + [POW] + ex
    if k in ('sqr', 'cube', 'recip'):
        return sub(node[1], 5) + [{'sqr': SQR, 'cube': CUBE, 'recip': INV}[k]]


def value(node):
    """sympy value of a tree, or raise Refuse where the engine must refuse."""
    k = node[0]
    if k == 'num':
        if node[1] > LIM:
            raise Refuse('literal')
        return sympy.Integer(node[1])
    if k == 'var':
        return sym(node[1])
    if k == 'paren':
        return value(node[1])
    if k == 'sqrt':
        return root(value(node[1]))
    if k == 'neg':
        return -value(node[1])
    a = value(node[1])
    if k in ('sqr', 'cube', 'recip'):
        return raise_to(a, {'sqr': 2, 'cube': 3, 'recip': -1}[k])
    b = value(node[2])
    if k == 'add': return sympy.cancel(a + b)
    if k == 'sub': return sympy.cancel(a - b)
    if k in ('mul', 'imul'): return sympy.cancel(a * b)
    if k == 'div':
        return divide(a, b)
    if k == 'pow':
        return raise_to(a, b)


# ---- 6. commands: EXPAND, FACTOR, SOLVE, DERIV, and the Algebra menu's ----
EQ, OR, COMMA = 0x6A, 0x3C, 0x2B
LIMITS = ('TOO WIDE', 'SYMCE LIMIT')          # errors that are only limits


def factors_counted(o):
    """How many factors over Q the printed FACTOR answer has: a group in
    parentheses or a variable at the top level each, a group with a root in it
    half (the quadratic was split in two), a bare answer with no groups one."""
    n, depth, rad, bare = 0, 0, False, False
    for k, b in enumerate(o):
        if b == DIV and not depth:
            break
        if b in (LP, SQRT):
            if not depth and b == LP:
                rad = False
            depth += 1
            rad |= b == SQRT
        elif b == I_:
            rad = True
        elif b == RP:
            depth -= 1
            if not depth:
                n += 0.5 if rad else 1
        elif not depth and isvar(b):
            n += 1
        elif not depth and k and b in (ADD, SUB):
            bare = True
    return 1 if bare else n


def commands(rnd, n, ask, eng, report):
    X, Y = sym(0x58), sym(0x59)
    got = {}

    def factor():
        r = rnd.random()
        if r < 0.5:
            return rnd.choice([1, 1, 1, 2, 3, -1]) * X + rnd.randint(-5, 5)
        if r < 0.7:
            return X ** 2 + rnd.randint(-4, 4) * X + rnd.randint(-6, 6)
        if r < 0.8:
            return X ** 2 - rnd.choice([2, 3, 5, 6, 7, 8, 12])
        if r < 0.9:
            return X + rnd.choice([1, -1, 2, -2]) * Y
        return X ** 2 + rnd.randint(1, 5)

    def call(name, t, mp, var=None, ok=()):
        """t: the argument, or a list of them; ok: errors that may be right"""
        t = bytes([COMMA]).join(t) if isinstance(t, list) else t
        c = bytes(name.encode() + bytes([LP]) + t + (bytes([COMMA, var]) if var else b'') + bytes([RP]))
        o = ask(eng, c, b'', mp)
        if isinstance(o, str) and o not in LIMITS + ok and not (name == 'SOLVE' and o in ('NO SOLUTION', 'SYNTAX')):
            report(name, c, o.encode(), None)
        elif isinstance(o, bytes) and not mp and len(o) + o.count(SQRT) + 3 * o.count(OR) > WIDTH:
            report(name + ' W', c, o, None)
        elif isinstance(o, bytes):
            got[name] = got.get(name, 0) + 1
        return c, o

    def items(o):
        """A list answer's items as values, or None"""
        if o[:1] != bytes([LB]) or o[-1:] != bytes([RB]):
            return None
        try:
            return [pratt(x) for x in o[1:-1].split(bytes([LC]))] if len(o) > 2 else []
        except (Syntax, Refuse):
            return None

    def order(v):                       # real first, then by real part, then imaginary
        v = complex(sympy.N(v, 30))
        return (v.imag != 0, round(v.real, 9), round(v.imag, 9))

    def listed(o, want, name, c, nv):
        """o is a list answer of the values want, each once; in one variable
        in order()"""
        vals = items(o)
        if vals is None or sorted(str(sympy.expand(v)) for v in vals) != \
                sorted(str(sympy.expand(w)) for w in want) or nv == 1 and vals != sorted(vals, key=order):
            report(name, c, o, None)

    def algebra(p, t, nv, mp, q):
        """The Polynomial Tools, COMDENOM, the C commands, LEFT and RIGHT on
        p (tokens t), a product of factor()s; q another factor() or 1."""
        P = sympy.Poly(p, X)
        roots = list(sympy.roots(P))
        # POLYROOTS: the real roots, each once; CPOLYROOTS all of them
        for name, want in (('POLYROOTS', [r for r in roots if not r.has(sympy.I)]), ('CPOLYROOTS', roots)):
            c, o = call(name, t, mp)
            if isinstance(o, bytes):
                listed(o, want, name, c, nv)
        # CZEROS is CPOLYROOTS, and CSOLVE the same items as X=.. or X=..
        if isinstance(o, bytes):
            c, o2 = call('CZEROS', t, mp)
            if isinstance(o2, bytes) and o2 != o:
                report('CZEROS', c, o2, o)
            c, o2 = call('CSOLVE', t + bytes([EQ, 0x30]), mp, 0x58)
            want = bytes([0x58, EQ]) + o[1:-1].replace(bytes([LC]), bytes([OR, 0x58, EQ]))
            if isinstance(o2, bytes) and o2 != want:
                report('CSOLVE', c, o2, want)
        # CFACTOR: the same value, a factor over Q split only over roots and i
        c, o = call('CFACTOR', t, mp)
        if isinstance(o, bytes):
            try:
                ok = sympy.expand(pratt(o) - p) == 0
            except (Syntax, Refuse):
                ok = False
            if not ok or factors_counted(o) != len(sympy.factor_list(p)[1]):
                report('CFACTOR', c, o, None)
        # NROOTS: every root once, in decimals, 4 to 10 digits
        if nv == 1 and rnd.random() < 0.3:
            c, o = call('NROOTS', t, mp)
            if isinstance(o, bytes):
                try:
                    s = o[1:-1].translate(bytes.maketrans(b'\xb0\x71\x70\x3a\xd7', b'--+.j')).decode()
                    vals = [complex(x) for x in s.split(',')] if s else []
                except ValueError:
                    vals = None
                want = sorted((complex(r) for r in P.sqf_part().nroots(n=20)),
                              key=lambda v: (v.imag != 0, v.real, v.imag))
                if vals is None or len(vals) != len(want) or any(
                        abs(v - w) > 6e-4 * abs(w) + 1e-12 or (v.imag == 0) != (abs(w.imag) < 1e-12)
                        for v, w in zip(vals, want)):
                    report('NROOTS', c, o, None)
        # POLYCOEFFS and POLYDEGREE in X
        c, o = call('POLYCOEFFS', t, mp)
        if isinstance(o, bytes):
            vals, want = items(o), P.all_coeffs()
            if vals is None or len(vals) != len(want) or any(sympy.expand(v - w) for v, w in zip(vals, want)):
                report('POLYCOEFFS', c, o, None)
        c, o = call('POLYDEGREE', t, mp)
        if isinstance(o, bytes) and o != str(P.degree()).encode():
            report('POLYDEGREE', c, o, str(P.degree()).encode())
        # COMDENOM is EXPAND; LEFT and RIGHT are each side's own answer
        c, o = call('COMDENOM', t, mp)
        if isinstance(o, bytes) and o != ask(eng, t, b'', mp):
            report('COMDENOM', c, o, ask(eng, t, b'', mp))
        if q != 1:
            tq = fmt(q, len(q.free_symbols))
            if tq is None or len(t) + len(tq) > MAXLEN - 16:
                return
            for name, side in (('LEFT', t), ('RIGHT', tq)):
                c, o = call(name, t + bytes([EQ]) + tq, mp)
                if isinstance(o, bytes) and o != ask(eng, side, b'', mp):
                    report(name, c, o, ask(eng, side, b'', mp))
            # POLYQUOTIENT and POLYREMAINDER in X, other variables as numbers
            dom = sympy.QQ[Y] if p.has(Y) or q.has(Y) else sympy.QQ
            quo, rem = (e.as_expr() for e in sympy.div(sympy.Poly(p, X, domain=dom), sympy.Poly(q, X, domain=dom)))
            for name, want in (('POLYQUOTIENT', quo), ('POLYREMAINDER', rem)):
                c, o = call(name, [t, tq], mp)
                if isinstance(o, bytes):
                    try:
                        ok = sympy.expand(pratt(o) - want) == 0
                    except (Syntax, Refuse):
                        ok = False
                    if not ok:
                        report(name, c, o, None)
            # POLYGCD(k·p, q·p2): whole coefficients, no common factor, the
            # first term positive, times gcd(numerators)/lcm(denominators)
            k = rnd.choice([1, 2, 3, -1, sympy.Rational(1, 2), sympy.Rational(-2, 3)])
            a, b = sympy.expand(k * p), sympy.expand(q * factor())
            ta, tb = fmt(a, len(a.free_symbols)), fmt(b, len(b.free_symbols))
            if ta is None or tb is None or len(ta) + len(tb) > MAXLEN - 10:
                return
            c, o = call('POLYGCD', [ta, tb], mp)
            if isinstance(o, bytes):
                cs = [sympy.Rational(x) for e in (a, b) for x in sympy.Poly(e, X, Y).coeffs()]
                cont = sympy.Rational(sympy.gcd([x.p for x in cs]), sympy.lcm([x.q for x in cs]))
                g = sympy.Poly(sympy.gcd(a, b), X, Y, domain=sympy.QQ)
                g = g.clear_denoms()[1].primitive()[1].as_expr() * cont
                try:
                    ok = o[0] != NEG and sympy.expand(pratt(o) ** 2 - g ** 2) == 0
                except (Syntax, Refuse):
                    ok = False
                if not ok:
                    report('POLYGCD', c, o, fmt(g, len(g.free_symbols)))

    for i in range(n):
        p = rnd.choice([1, 1, 1, 2, -1, sympy.Rational(1, 2)])
        for _ in range(rnd.randint(1, 3)):
            p *= factor() ** rnd.choice([1, 1, 1, 2])
        p = sympy.expand(p)
        if sympy.degree(p, X) > MAXEXP:
            continue
        nv = len(p.free_symbols)
        t = fmt(p, nv)
        if t is None or len(t) > MAXLEN - 10:
            continue
        q = factor() if rnd.random() < 0.3 else 1
        tq = fmt(p / q, nv) if q != 1 else t
        for mp in (0, 1):
            # EXPAND is the answer SymCE gives the expression alone
            c, o = call('EXPAND', t, mp)
            if isinstance(o, bytes) and o != ask(eng, t, b'', mp):
                report('EXPAND', c, o, ask(eng, t, b'', mp))
            # FACTOR: the same value, every factor over Q found
            if tq is not None and len(tq) <= MAXLEN - 10:
                c, o = call('FACTOR', tq, mp)
                if isinstance(o, bytes):
                    try:
                        ok = sympy.expand(sympy.cancel(pratt(o) - p / q)) == 0
                    except (Syntax, Refuse):
                        ok = False
                    num = sympy.factor_list(p)[1] + (sympy.factor_list(q)[1] if q != 1 else [])
                    if not ok or (q == 1 and factors_counted(o) != len(num)):
                        report('FACTOR', c, o, None)
            # DERIV: sympy's derivative
            if tq is not None and len(tq) <= MAXLEN - 10:
                c, o = call('DERIV', tq, mp, 0x58)
                if isinstance(o, bytes):
                    try:
                        ok = sympy.cancel(pratt(o) - sympy.diff(p / q, X)) == 0
                    except (Syntax, Refuse):
                        ok = False
                    if not ok:
                        report('DERIV', c, o, None)
            # SOLVE(p=0,X), in X alone: every real root once, smallest first;
            # without =0 it is ERROR: SYNTAX
            if nv == 1:
                c, o = call('SOLVE', t, 0, 0x58)
                if o != 'SYNTAX':
                    report('SOLVE', c, o if isinstance(o, bytes) else str(o).encode(), b'SYNTAX')
                c, o = call('SOLVE', t + bytes([EQ, 0x30]), mp, 0x58)
                roots = sorted(set(sympy.real_roots(sympy.Poly(p, X))), key=float)
                if o == 'NO SOLUTION' and roots:
                    report('SOLVE', c, o.encode(), None)
                if isinstance(o, bytes):
                    parts = o.split(bytes([OR]))
                    try:
                        vals = [pratt(s[2:]) for s in parts if s[:2] == bytes([0x58, EQ])]
                    except (Syntax, Refuse):
                        vals = []
                    if len(vals) != len(parts) or len(vals) != len(roots) or \
                            any(sympy.simplify(v - r) != 0 for v, r in zip(vals, roots)):
                        report('SOLVE', c, o, None)
            algebra(p, t, nv, mp, q)

    return got


def solve_vars(rnd, n, ask, eng, report):
    """SOLVE(A=B,V) and CSOLVE: every letter but V is the value stored in it,
    as in the OS's own solve(); a letter with nothing stored stays a letter,
    and V's own stored value is ignored. Fixed cases against sympy, then random
    ones that must answer what the entry with each value typed in (its digits
    in parentheses) answers, byte for byte. A stored value that is no short
    decimal, or is complex, is an error screen, never an answer."""
    X, Y, Z = 0x58, 0x59, 0x5A
    F = Fraction
    done = 0

    def typed(entry, vals):
        """the entry's tokens with each stored letter's value typed in"""
        o = ti(entry)
        for k, v in vals.items():
            o = o.replace(bytes([k]), spell(v))
        return o

    def call(entry, vals, mp=0):
        return ask(eng, ti(entry), b'', mp, 0, [(k, ti_real(v)) for k, v in vals.items()])

    def roots(entry, vals):
        """what sympy says: real roots of lhs-rhs in Y, or None when symbolic"""
        lhs, rhs = ti(entry[6:-3]).split(bytes([EQ]))
        e = pratt(lhs) - pratt(rhs)
        e = e.subs({sym(k): sympy.Rational(v.numerator, v.denominator) for k, v in vals.items() if k != Y})
        return sorted(sympy.real_roots(sympy.Poly(e, sym(Y)))) if not e.free_symbols - {sym(Y)} else None

    fixed = [('SOLVE(X+2Y=3,Y)', {X: F(5)}), ('SOLVE(X+2Y=3,Y)', {X: F(-3)}),
             ('SOLVE(X+2Y=3,Y)', {X: F(5, 2)}), ('SOLVE(X+2Y=3,Y)', {X: F(5), Y: F(7)}),
             ('SOLVE(Y²+X*Y=6,Y)', {X: F(1)}), ('SOLVE(Y²+X=Z,Y)', {X: F(1), Z: F(10)}),
             ('SOLVE(X+2Y=3,Y)', {Z: F(1)}), ('SOLVE(X+2Y=3,Y)', {}),
             ('SOLVE(X+2Y=Z,Y)', {X: F(0), Z: F(0)})]
    for entry, vals in fixed:
        o = call(entry, vals)
        want = roots(entry, vals)
        if want is None:                      # X stays: the answer has it
            if not (isinstance(o, bytes) and X in o):
                report('SOLVEV', ti(entry), o if isinstance(o, bytes) else str(o).encode(), b'symbolic')
            continue
        try:
            got = sorted(pratt(s[2:]) for s in o.split(bytes([OR]))) if isinstance(o, bytes) else None
        except (Syntax, Refuse):
            got = None
        if got is None or len(got) != len(want) or any(sympy.simplify(g - w) != 0 for g, w in zip(got, want)):
            report('SOLVEV', ti(entry), o if isinstance(o, bytes) else str(o).encode(), None)
        else:
            done += 1
    # 1/3 is refused, not turned into a huge fraction; nor is a complex value
    o = ask(eng, ti('SOLVE(X+2Y=3,Y)'), b'', 0, 0, [(X, bytes([0, 0x7F]) + bytes([0x33] * 7))])
    if o != 'SYMCE LIMIT':
        report('SOLVEV', ti('X=1/3'), o if isinstance(o, bytes) else str(o).encode(), b'SYMCE LIMIT')
    o = ask(eng, ti('SOLVE(X+2Y=3,Y)'), b'', 0, 0, [(X, bytes([0x0C, 0x80, 0x50]) + bytes(6))])
    if o != 'SYMCE LIMIT':
        report('SOLVEV', ti('X complex'), o if isinstance(o, bytes) else str(o).encode(), b'SYMCE LIMIT')
    for entry in ('CSOLVE(Y²+X=0,Y)', 'CSOLVE(Y²+2Y+X=0,Y)'):
        for v in (F(4), F(-4), F(1, 2), F(5)):
            a = call(entry, {X: v})
            b = ask(eng, typed(entry, {X: v}), b'', 0, 0)
            if a != b:
                report('CSOLVEV', ti(entry), a if isinstance(a, bytes) else str(a).encode(), b)
            done += isinstance(a, bytes)
    # random: stored values against the same entry with them typed in
    for _ in range(n):
        vals = {k: F(rnd.randint(-9, 9), rnd.choice([1, 1, 2, 4, 10])) for k in rnd.sample([X, Z, 0x41], rnd.randint(0, 3))}
        text = rnd.choice(['SOLVE(X+2Y=Z,Y)', 'SOLVE(A*Y²+X*Y=Z,Y)', 'SOLVE(Y²-X=A,Y)', 'SOLVE(X*Y+Z=A*Y,Y)',
                           'SOLVE(Y/X=Z,Y)', 'SOLVE(Y²=X²-Z,Y)'])
        a, b = call(text, vals), ask(eng, typed(text, vals), b'', 0, 0)
        if a != b:
            report('SOLVEV', ti(text), a if isinstance(a, bytes) else str(a).encode(), b)
        done += isinstance(a, bytes)
    return done


# ---- 7. functions: Convert Expression (TOLN .. TOCOS) and Trigonometry ----
SIN, COS, TAN, LN, LOG, EXP, EXT, BB, ABS = 0xC2, 0xC4, 0xC6, 0xBE, 0xC0, 0xBF, 0xEF, 0xBB, 0xB2
LCOS = bytes([BB, 0xB2, BB, 0xBF, BB, 0xC3, LP])      # an answer's cos(: the letters c o s
E_ = bytes([BB, 0x31])
# readable text <-> tokens, for the exact cases and for reports
FIN = [('logBASE(', b'\xef\x34'), ('sin(', b'\xc2'), ('cos(', b'\xc4'), ('tan(', b'\xc6'),
       ('ln(', b'\xbe'), ('log(', b'\xc0'), ('e^(', b'\xbf'), ('e', E_), ('(', b'\x10'), (')', b'\x11'),
       (',', b'\x2b'), ('²', b'\x0d'), ('³', b'\x0f'), ('^', b'\xf0'), ('-', b'\x71'), ('~', b'\xb0'),
       ('+', b'\x70'), ('*', b'\x82'), ('/', b'\x83'), ('θ', b'\x5b'), ('√(', b'\xbc'), ('=', b'\x6a'),
       ('abs(', b'\xb2'), ('ᴇ', b'\x3b')]      # ᴇ: the E of 1ᴇ99, infinity (LIMIT)
# an answer's tokens: (text, python for sympy)
FOUT = [(LCOS, 'cos(', 'cos('), (b'\xef\x34', 'logBASE(', 'logb('), (E_, 'e', 'E'),
        (b'\xb2', 'abs(', 'Abs('),
        (b'\xc2', 'sin(', 'sin('), (b'\xc6', 'tan(', 'tan('), (b'\xbe', 'ln(', 'log('),
        (b'\xc0', 'log(', 'log10('), (b'\xbf', 'e^(', 'exp('), (b'\xbc', '√(', 'sqrt('),
        (b'\x10', '(', '('), (b'\x11', ')', ')'), (b'\x2c', ',', ','), (b'\x0d', '²', '**2'),
        (b'\x0f', '³', '**3'), (b'\xf0', '^', '**'), (b'\x71', '-', '-'), (b'\xb0', '-', '-'),
        (b'\x70', '+', '+'), (b'\x82', '*', '*'), (b'\x83', '/', '/'), (b'\xd7', 'i', 'I'), (b'\x3a', '.', '.')]


def ti(s):
    """readable text -> entry tokens: 'TEXPAND(sin(2X))'"""
    o = b''
    while s:
        for k, v in FIN:
            if s.startswith(k):
                o, s = o + v, s[len(k):]
                break
        else:
            o, s = o + s[0].encode(), s[1:]
    return o


def lex(o):
    """an answer's tokens -> [(token bytes, text, python)]"""
    out = []
    while o:
        for k, txt, py in FOUT:
            if o.startswith(k):
                out.append((k, txt, py))
                o = o[len(k):]
                break
        else:
            b = o[0]
            if not (0x30 <= b <= 0x39 or isvar(b)):
                raise Syntax('token %02x' % b)
            out.append((o[:1], 'θ' if b == 0x5B else chr(b), chr(b) if b < 0x3A else 'v%02x' % b))
            o = o[1:]
    return out


def readable(o, entry=False):
    """an answer's tokens, or an entry's, as text; FMIN's X=1 or X=2 too"""
    if not entry:
        return ' or '.join('='.join(''.join(t for _, t, _ in lex(q)) for q in p.split(bytes([EQ])))
                           for p in o.split(bytes([OR])))
    s = ''
    while o:
        k = next((k for k, v in FIN if o.startswith(v)), None)
        s, o = s + (k if k else chr(o[0])), o[len(ti(k)) if k else 1:]
    return s


def fvalue(o):
    """an answer with functions in it, as sympy: TI's implied products made
    explicit, ln( log( logBASE( e^( as sympy's"""
    py, prev = [], None
    for k, _, p in lex(o):
        ends = prev is not None and (prev[-1] in ')IE' or prev[-1].isdigit() and prev != '**'
                                     or prev.startswith('v') or prev in ('**2', '**3', '.'))
        starts = p[0].isalpha() or p[0] == 'v' or p == '(' or p[0].isdigit() or p == '.'
        if ends and starts and not ((prev[-1].isdigit() or prev == '.') and (p[0].isdigit() or p == '.')):
            py.append('*')
        py.append(p)
        prev = p
    return sympy.sympify(''.join(py), locals={'logb': lambda u, b: sympy.log(u, b),
                                              'log10': lambda u: sympy.log(u, 10), 'E': sympy.E})


def fcols(o, mp):
    """engine.c's width() for an answer with functions: Classic counts each
    name (measured: right-aligned by it), MathPrint the same names and every
    parenthesis, each A/B stacked."""
    head = {BB: 1, EXT: 8, LN: 3, EXP: 3, LOG: 4, SIN: 4, TAN: 4}
    w, k = 0, 0
    if not mp:
        while k < len(o):
            w += head.get(o[k], 2 if o[k] == SQRT else 4 if o[k] in (OR, ABS) else 1)
            k += 2 if o[k] in (BB, EXT) else 1
        return w
    side = top = over = depth = 0
    while k <= len(o):
        c = o[k] if k < len(o) else ADD
        if c in head:
            side += head[c]
            depth += c != BB
            k += c in (BB, EXT)
        elif c in (LP, SQRT, ABS):
            depth += 1
            side += 1
        elif c == RP:
            depth -= 1
            side += 1
        elif c == DIV:
            top, side, over = side, 0, 1
        elif depth or c not in (ADD, SUB, NEG, EQ, OR, LC, LB, RB):
            side += c != POW
        else:
            w += max(top, side) if over else side
            w += over + (0 if k == len(o) else 4 if c == OR else 1)
            top = side = over = 0
        k += 1
    return w


def calls(o, head):
    """the arguments of every call of head (bytes) in an answer's tokens"""
    out, k = [], 0
    while True:
        k = o.find(head, k)
        if k < 0:
            return out
        j, depth = k + len(head), 1
        while depth:
            depth += (o[j] in (LP, SIN, TAN, LN, LOG, EXP, SQRT, ABS) or o[j:j + 2] == b'\xef\x34') - (o[j] == RP)
            j += 1 + (o[j] in (BB, EXT))
        out.append(o[k + len(head):j - 1])
        k = j


def ask_fn(eng, report, t, pre=''):
    """Tokens, an error's message, or None (left to the OS)"""
    eng.stdin.write('C' + pre + t.hex() + '\n')
    eng.stdin.flush()
    r = eng.stdout.readline().strip()
    if r == 'PASS':
        return None
    if r[0] == 'E':
        return bytes.fromhex(r[1:]).split(b'\0')[0].decode()
    h, c = r.split(':')
    o = bytes.fromhex(h)
    try:
        if int(c, 16) != min(fcols(o, 0), 255):
            report('COLS', t, o, None)
    except Syntax:
        pass
    return o


def functions(rnd, n, eng, report):
    """Each command's answer must have the entry's value, at random points
    where sympy evaluates both, be in the command's form, fit the screen, and
    carry its Classic columns (the hook right-aligns by them). Only a limit
    may refuse; TOEXP is ERROR: MODE in degrees, the others are identities
    there too. The TI-Nspire's own examples come out exactly."""
    got = {}

    def fask(t, pre=''):
        return ask_fn(eng, report, t, pre)

    X = [0x58, 0x58, 0x58, 0x59, 0x41, 0x42, 0x5B]

    def num(k):                         # tokens for a coefficient: 1 none, -1 a sign
        return ([NEG] if k < 0 else []) + (list(str(abs(k)).encode()) if abs(k) != 1 else [])

    def angle(one=None):
        v = one or rnd.choice(X)
        k = rnd.choice([1, 1, 1, 2, 2, 3, -1, 4])
        t, e = num(k) + [v], k * sym(v)
        r = rnd.random()
        if r < 0.25 and not one:
            w, m = rnd.choice(X), rnd.choice([1, 1, 2, -1])
            t += [ADD if m > 0 else SUB] + num(abs(m)) + [w]
            e += m * sym(w)
        elif r < 0.35:
            m = rnd.choice([1, 2, -1])
            t += [ADD if m > 0 else SUB, 0x30 + abs(m)]
            e += m
        elif r < 0.45:
            t, e = t + [DIV, 0x32], e / 2
        return t, e

    FN = {SIN: sympy.sin, COS: sympy.cos, TAN: sympy.tan}

    def trig(fs, one=None, tan=True):
        f = rnd.choice(fs if tan else [x for x in fs if x != TAN])
        a, e = angle(one)
        t, e = [f] + a + [RP], FN[f](e)
        p = rnd.choice([1, 1, 1, 1, 2, 2, 3])
        return t + {1: [], 2: [SQR], 3: [CUBE]}[p], e ** p

    def poly(atom, terms=2):
        t, e = [], 0
        for i in range(rnd.randint(1, terms)):
            c = rnd.choice([1, 1, 2, -1, 3])
            ta, ea = atom()
            if rnd.random() < 0.2:
                tb, eb = atom()
                ta, ea = ta + tb, ea * eb
            t += ([ADD] if i and c > 0 else [SUB] if i else [NEG] if c < 0 else []) + num(abs(c)) + ta
            e += c * ea
        if rnd.random() < 0.2:
            t, e = t + [ADD, 0x31], e + 1
        return t, e

    def logs():
        def arg():
            r = rnd.random()
            v = rnd.choice(X)
            if r < 0.4:
                return [v], sym(v)
            if r < 0.6:
                w = rnd.choice(X)
                return [v, w], sym(v) * sym(w)
            if r < 0.75:
                return [0x32, v], 2 * sym(v)
            if r < 0.85:
                return [v, SQR], sym(v) ** 2
            k = rnd.choice([2, 3, 5, 10, 100])
            return list(str(k).encode()), sympy.Integer(k)

        def one():
            a, e = arg()
            r = rnd.random()
            if r < 0.35:
                return [LN] + a + [RP], sympy.log(e)
            if r < 0.7:
                return [LOG] + a + [RP], sympy.log(e, 10)
            b = rnd.choice([2, 3, 5, 10])
            return [EXT, 0x34] + a + [COMMA] + list(str(b).encode()) + [RP], sympy.log(e, b)
        return poly(one)

    def pts(e):
        """two points, every variable in (0.2, 1.4): angles off the poles
        mostly, and positive where a logarithm needs it"""
        s = sorted(e.free_symbols, key=str)
        return [{v: sympy.Rational(rnd.randint(200, 1400), 1000) for v in s} for _ in range(2)]

    def at(c, a):
        """c at X = a, if that is a finite real number with every ln( and √(
        in c defined there: ln(~1)² is real, but not here"""
        c = sympy.cancel(c)                         # (X+X²)/(X+1) is X
        for u in sympy.preorder_traversal(c):
            if isinstance(u, sympy.log) or isinstance(u, sympy.Pow) and u.exp.is_Rational and u.exp.q == 2:
                v = finite(u.args[0].subs(X, a))
                if v is None or v < 0 or v == 0 and isinstance(u, sympy.log):
                    return None
        return c.subs(X, a) if finite(c.subs(X, a)) is not None else None

    def same(o, e):
        v = fvalue(o)
        for p in pts(e):
            a, b = complex(v.evalf(30, subs=p)), complex(e.evalf(30, subs=p))
            if abs(b) < 1e6 and abs(a - b) > 1e-9 * (1 + abs(b)):
                return False
        return True

    def trigpow(v, fn):
        """the largest power of fn(..) in any term of numerator or denominator"""
        top = 0
        for side in sympy.fraction(sympy.together(v)):
            for term in sympy.Add.make_args(sympy.expand(side)):
                for f in sympy.Mul.make_args(term):
                    b, x = f.as_base_exp()
                    if isinstance(b, fn):
                        top = max(top, x)
        return top

    def linear(v):
        """no term of numerator or denominator multiplies two sin/cos"""
        for side in sympy.fraction(sympy.together(v)):
            for term in sympy.Add.make_args(side):
                k = 0
                for f in sympy.Mul.make_args(term):
                    b, x = f.as_base_exp()
                    if isinstance(b, (sympy.sin, sympy.cos)):
                        k += x
                if k > 1:
                    return False
        return True

    def form(cmd, o, base):
        """o is in cmd's form"""
        v = fvalue(o)
        if cmd == 'TEXPAND':            # each angle alone: sin(X), cos(X/2), sin(1)
            args = calls(o, bytes([SIN])) + calls(o, LCOS) + calls(o, bytes([TAN]))
            return all(
                a and a[0] not in (NEG, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39) or a.isdigit()
                for a in args) and not any(ADD in a or SUB in a for a in args)
        if cmd == 'TCOLLECT':
            return TAN not in o and linear(v)
        if cmd == 'TOSIN':
            return TAN not in o and trigpow(v, sympy.cos) <= 1
        if cmd == 'TOCOS':
            return TAN not in o and trigpow(v, sympy.sin) <= 1
        if cmd == 'TOEXP':
            return not v.has(sympy.sin, sympy.cos, sympy.tan)
        if cmd == 'TOLN':
            return LOG not in o and b'\xef\x34' not in o
        # TOLOGBASE: every logarithm to the one base
        if base == E_:
            return LOG not in o and b'\xef\x34' not in o
        if base == b'10':
            return LN not in o and b'\xef\x34' not in o
        return LN not in o and LOG not in o and all(
            a.endswith(bytes([LC]) + base) for a in calls(o, b'\xef\x34'))

    for i in range(n):
        cmd = rnd.choice(['TEXPAND', 'TEXPAND', 'TCOLLECT', 'TCOLLECT', 'TOSIN', 'TOCOS',
                          'TOEXP', 'TOLN', 'TOLOGBASE'])
        base = b''
        if cmd == 'TEXPAND':
            t, e = poly(lambda: trig([SIN, SIN, COS, COS, TAN]), 2)
        elif cmd == 'TCOLLECT':
            t, e = poly(lambda: trig([SIN, COS], rnd.choice([0x58, 0x58, 0x59])), 2)
        elif cmd in ('TOSIN', 'TOCOS'):
            t, e = poly(lambda: trig([SIN, COS, COS, SIN, TAN], 0x58), 2)
        elif cmd == 'TOEXP':
            t, e = poly(lambda: trig([SIN, COS, TAN], 0x58), 1)
        else:
            t, e = logs()
            if cmd == 'TOLOGBASE':
                base = rnd.choice([b'2', b'3', b'5', b'10', E_])
                t = t + [COMMA] + list(base)
        c = cmd.encode() + bytes([LP] + t + [RP])
        if len(c) > MAXLEN:
            continue
        for pre in ('', 'M') + (('G',) if i % 5 == 0 else ()):
            o = fask(c, pre)
            if pre == 'G' and cmd == 'TOEXP':
                if o != 'MODE':
                    report('DEG', c, o if isinstance(o, bytes) else str(o).encode(), b'MODE')
            elif pre == 'G':
                if o != fask(c):
                    report('DEG', c, o if isinstance(o, bytes) else str(o).encode(), None)
            elif o is None or isinstance(o, str):
                if o not in LIMITS:
                    report(cmd, c, (o or 'PASS').encode(), None)
                got[str(o)] = got.get(str(o), 0) + 1
            else:
                try:
                    ok = same(o, e) and form(cmd, o, base) and fcols(o, pre == 'M') <= WIDTH
                except (Syntax, TypeError, ValueError, SyntaxError, sympy.SympifyError):
                    ok = False
                if not ok:
                    report(cmd + ' ' + pre, c, o, None)
                else:
                    got[cmd] = got.get(cmd, 0) + 1

    # The TI-Nspire CX II CAS reference's examples, and the errors.
    for entry, pre, want in (
            ('TEXPAND(sin(2X))', '', '2sin(X)cos(X)'),
            ('TEXPAND(cos(A+B))', '', 'cos(A)cos(B)-sin(A)sin(B)'),
            ('TEXPAND(sin(3θ))', '', '4sin(θ)cos(θ)²-sin(θ)'),
            ('TEXPAND(sin(3θ))', 'G', '4sin(θ)cos(θ)²-sin(θ)'),
            ('TEXPAND(tan(2X))', '', '2sin(X)cos(X)/(2cos(X)²-1)'),
            ('TCOLLECT(sin(X)cos(X))', '', 'sin(2X)/2'),
            ('TCOLLECT(cos(X)²)', '', '(cos(2X)+1)/2'),
            ('TCOLLECT(sin(A)cos(B))', '', '(sin(A-B)+sin(A+B))/2'),
            ('TCOLLECT(sin(X)²+cos(X)²)', '', '1'),
            ('TOSIN(cos(X)²)', '', '1-sin(X)²'),
            ('TOCOS(sin(X)²)', '', '1-cos(X)²'),
            ('TOEXP(sin(X))', '', '(e^(Xi)-e^(-Xi))/(2i)'),
            ('TOEXP(cos(X))', '', '(e^(Xi)+e^(-Xi))/2'),
            ('TOEXP(sin(X))', 'G', 'MODE'),
            ('TOLN(log(X))', '', 'ln(X)/ln(10)'),
            ('TOLN(logBASE(X,3))', '', 'ln(X)/ln(3)'),
            ('TOLOGBASE(logBASE(10,3)-logBASE(5,5),5)', 'M', 'logBASE(10,5)/logBASE(3,5)-1'),
            ('TOLOGBASE(logBASE(10,3)-logBASE(5,5),5)', '', 'TOO WIDE'),     # 28 columns
            ('TOLOGBASE(ln(X),5)', '', 'logBASE(X,5)/logBASE(e,5)'),
            ('TOLOGBASE(ln(X))', '', 'ARGUMENT'),
            ('TOLOGBASE(ln(X),1)', '', 'DOMAIN'),
            ('TOLN(log(~2))', '', 'SYMCE LIMIT'),
            ('sin(X)', '', None),                       # no command: the OS's, as ever
            ('2X+sin(X)', '', None)):
        o = fask(ti(entry), pre)
        if (readable(o) if isinstance(o, bytes) else o) != want:
            report('EXACT', ti(entry), o if isinstance(o, bytes) else str(o).encode(), str(want).encode())
    return got


# ---- 8. calculus: the Calculus menu's DERIV .. IMPDIF ----
def calculus(rnd, n, eng, report):
    """Each command's answer must have the value sympy gives the same
    calculus, at random points, and the command's form: DERIVAT a number,
    TANGENTLINE and NORMALLINE a line in X, TAYLOR a polynomial of at most the
    order in X. Where sympy has no finite real value at the command's point
    (ln(0), 1/0) the answer must be an error; limits may refuse. In degrees, a
    trig function of the variable is ERROR: MODE (CENTRALDIFF only
    substitutes, so it answers), anything else answers as in radians. The
    TI-Nspire's own examples come out exactly."""
    got = {}
    X, Y = sym(0x58), sym(0x59)
    FN = {SIN: sympy.sin, COS: sympy.cos, TAN: sympy.tan}
    EQ_ = 0x6A

    def num(k):
        return ([NEG] if k < 0 else []) + (list(str(abs(k)).encode()) if abs(k) != 1 else [])

    def atom(ys=False):
        """(tokens, sympy, has trig of a variable)"""
        r = rnd.random()
        k = rnd.choice([1, 1, 2, 3, -1])
        v = rnd.choice([0x58, 0x59]) if ys else 0x58
        if r < 0.3:
            p = rnd.choice([1, 2, 3])
            return [v] + {1: [], 2: [SQR], 3: [CUBE]}[p], sym(v) ** p, False
        if r < 0.55:
            f = rnd.choice([SIN, COS, TAN])
            return [f] + num(k) + [v, RP], FN[f](k * sym(v)), True
        if r < 0.7:
            return [EXP] + num(k) + [v, RP], sympy.exp(k * sym(v)), False
        if r < 0.8:
            return [LN, v, RP], sympy.log(sym(v)), False
        if r < 0.85:
            return [LOG, v, RP], sympy.log(sym(v), 10), False
        if r < 0.92:
            return [SQRT, v, RP], sympy.sqrt(sym(v)), False
        return [0x59 if not ys else 0x31 + rnd.randint(1, 4)], Y if not ys else 0, False

    def fn(ys=False, terms=3):
        t, e, tr = [], 0, False
        for i in range(rnd.randint(1, terms)):
            c = rnd.choice([1, 1, 2, 3, -1])
            ta, ea, g = atom(ys)
            if ta[0] in range(0x32, 0x36):          # a constant: its own value, no coefficient
                ea, c = ta[0] - 0x30, c // abs(c)
            if rnd.random() < 0.25:
                tb, eb, h = atom(ys)
                if tb[0] in range(0x32, 0x36):
                    tb, eb = [], 1
                ta, ea, g = ta + tb, ea * eb, g or h
            t += ([ADD] if i and c > 0 else [SUB] if i else [NEG] if c < 0 else []) + num(abs(c)) + ta
            e += c * ea
            tr = tr or g
        if not ys and rnd.random() < 0.15:
            t, e = [LP] + t + [RP, DIV, LP, 0x58, ADD, 0x31, RP], e / (X + 1)
        return t, e, tr

    POINTS = [([0x30], 0), ([0x31], 1), ([0x32], 2), ([NEG, 0x31], -1),
              ([0x31, DIV, 0x32], sympy.Rational(1, 2))]

    def finite(c):
        """c's value, if it is a finite real number (other variables at 0.7:
        they are constants here)"""
        try:
            v = complex(sympy.N(c.subs({w: sympy.Rational(7, 10) for w in c.free_symbols}), 30))
        except (TypeError, ValueError, ZeroDivisionError):
            return None
        return v.real if abs(v.imag) < 1e-12 and abs(v) < 1e12 else None

    def at(c, a):
        """c at X = a, if that is a finite real number with every ln( and √(
        in c defined there: ln(~1)² is real, but not here"""
        c = sympy.cancel(c)                         # (X+X²)/(X+1) is X
        for u in sympy.preorder_traversal(c):
            if isinstance(u, sympy.log) or isinstance(u, sympy.Pow) and u.exp.is_Rational and u.exp.q == 2:
                v = finite(u.args[0].subs(X, a))
                if v is None or v < 0 or v == 0 and isinstance(u, sympy.log):
                    return None
        return c.subs(X, a) if finite(c.subs(X, a)) is not None else None

    def same(o, e):
        v = fvalue(o)
        for _ in range(2):
            p = {w: sympy.Rational(rnd.randint(200, 1400), 1000) for w in e.free_symbols | v.free_symbols}
            try:
                a, b = complex(sympy.N(v.subs(p), 30)), complex(sympy.N(e.subs(p), 30))
            except (TypeError, ValueError, ZeroDivisionError):
                continue                                # a pole: another point
            if abs(b) < 1e6 and abs(a - b) > 1e-9 * (1 + abs(b)):
                return False
        return True

    def form(cmd, o, order):
        v = fvalue(o)
        if cmd == 'DERIVAT':
            return X not in v.free_symbols
        if cmd in ('TANGENTLINE', 'NORMALLINE'):
            return v.is_polynomial(X) and sympy.degree(v, X) <= 1
        if cmd == 'TAYLOR':
            return v.is_polynomial(X) and (v.is_number or sympy.degree(v, X) <= order)
        return True

    for i in range(n):
        cmd = rnd.choice(['DERIV', 'DERIV', 'DERIVAT', 'TANGENTLINE', 'NORMALLINE',
                          'TAYLOR', 'TAYLOR', 'IMPDIF', 'CENTRALDIFF'])
        order, want = 1, None                 # want: sympy's answer; None: must be an error
        if cmd == 'IMPDIF':
            t, e, tr = fn(True)
            rhs = rnd.choice([[], [], [0x31], [0x58, 0x59]])
            if rhs:
                t, e = t + [EQ_] + rhs, e - (1 if rhs == [0x31] else X * Y)
            order = rnd.choice([1, 1, 2])
            t += [COMMA, 0x58, COMMA, 0x59] + ([COMMA, 0x32] if order == 2 else [])
            if sympy.diff(e, Y) != 0:
                want = sympy.idiff(e, Y, X, order)
        else:
            t, e, tr = fn()
            pt, a = rnd.choice(POINTS)
            if cmd == 'DERIV':
                order = rnd.choice([1, 1, 1, 2, 3])
                how = rnd.random()
                bare = order == 1 and how < 0.5         # no variable: X, else the first one
                t += [] if bare else [COMMA, 0x58] + ([] if order == 1 else [COMMA, 0x30 + order])
                v = X if not bare or X in e.free_symbols else min(e.free_symbols, key=str, default=X)
                want = sympy.diff(e, v, order)
            elif cmd == 'DERIVAT':
                if rnd.random() < 0.3:
                    order = 2
                t += [COMMA, 0x58, COMMA] + pt + ([COMMA, 0x32] if order == 2 else [])
                want = at(sympy.diff(e, X, order), a)
            elif cmd in ('TANGENTLINE', 'NORMALLINE'):
                t += [COMMA, 0x58, COMMA] + pt
                f0, d = at(e, a), at(sympy.diff(e, X), a)
                if f0 is not None and d is not None:
                    if cmd == 'TANGENTLINE':
                        want = f0 + d * (X - a)
                    elif finite(d) != 0:
                        want = f0 - (X - a) / d
            elif cmd == 'TAYLOR':
                order = rnd.choice([1, 2, 3, 4])
                if rnd.random() < 0.6:
                    pt, a = [0x30], 0
                    t += [COMMA, 0x58, COMMA, 0x30 + order]
                else:
                    t += [COMMA, 0x58, COMMA, 0x30 + order, COMMA] + pt
                ds = [at(sympy.diff(e, X, k), a) for k in range(order + 1)]
                if None not in ds:
                    want = sum(d * (X - a) ** k / sympy.factorial(k) for k, d in enumerate(ds))
            else:
                h = rnd.choice([[0x48], [0x48], [0x31], [0x31, DIV, 0x32]])
                hv = sym(0x48) if h == [0x48] else sympy.Rational(1, 2) if len(h) == 3 else 1
                t += [COMMA, 0x58, COMMA] + h
                want = (e.subs(X, X + hv) - e.subs(X, X - hv)) / (2 * hv)
                tr = False
        c = cmd.encode() + bytes([LP] + t + [RP])
        if len(c) > MAXLEN:
            continue
        radian = None
        for pre in ('', 'M') + (('G',) if i % 5 == 0 else ()):
            o = ask_fn(eng, report, c, pre)
            if pre == 'G':
                if (o != 'MODE' and not (o == radian and o in LIMITS)) if tr else (o != radian):
                    report('DEG', c, o if isinstance(o, bytes) else str(o).encode(), b'MODE' if tr else radian)
                continue
            if not pre:
                radian = o
            if o is None or isinstance(o, str):
                if o not in LIMITS and (want is not None or o not in ('DOMAIN', 'DIVIDE BY 0')):
                    report(cmd, c, (o or 'PASS').encode(), None)
                got[str(o)] = got.get(str(o), 0) + 1
                continue
            try:
                named = any(b in (BB, EXT) or LN <= b <= TAN for b in o)   # else engine.c's width()
                ok = (want is not None and same(o, want) and form(cmd, o, order)
                      and (fcols if named else width)(o, pre == 'M') <= WIDTH)
            except (Syntax, TypeError, ValueError, SyntaxError, sympy.SympifyError):
                ok = False
            if not ok:
                report(cmd + ' ' + pre, c, o, None)
            else:
                got[cmd] = got.get(cmd, 0) + 1

    # The TI-Nspire CX II CAS reference's examples and the manual's, the
    # errors, and the OS's own numerical commands, which stay the OS's.
    for entry, pre, want in (
            ('DERIV(sin(X))', '', 'cos(X)'),
            ('DERIV(cos(X))', '', '-sin(X)'),
            ('DERIV(tan(X))', '', 'tan(X)²+1'),
            ('DERIV(ln(X))', '', '1/X'),
            ('DERIV(log(X))', '', '1/(Xln(10))'),
            ('DERIV(e^(2X))', '', '2e^(2X)'),
            ('DERIV(logBASE(X,2))', '', '1/(Xln(2))'),
            ('DERIV(X³,X,2)', '', '6X'),
            ('DERIV(Xsin(X))', '', 'Xcos(X)+sin(X)'),
            ('DERIV(sin(X),X,4)', '', 'sin(X)'),
            ('DERIV(sin(X))', 'G', 'MODE'),
            ('DERIV(e^(X))', 'G', 'e^(X)'),
            ('DERIVAT(X³,X,2)', '', '12'),
            ('DERIVAT(sin(X),X,1)', '', 'cos(1)'),
            ('DERIVAT(e^(X),X,1)', '', 'e'),
            ('DERIVAT(√(X),X,4)', '', '1/4'),
            ('DERIVAT(1/X,X,0)', '', 'DOMAIN'),
            ('DERIVAT(X²,X,X)', '', 'ARGUMENT'),
            ('TANGENTLINE(X²,X,1)', '', '2X-1'),
            ('TANGENTLINE(e^(X),X,0)', '', 'X+1'),
            ('TANGENTLINE(ln(X),X,~1)', '', 'DOMAIN'),
            ('NORMALLINE(X²,X,1)', '', '3/2-X/2'),
            ('NORMALLINE(X²,X,0)', '', 'DIVIDE BY 0'),
            ('TAYLOR(e^(X),X,3)', '', 'X³/6+X²/2+X+1'),
            ('TAYLOR(sin(X),X,5)', '', 'X^5/120-X³/6+X'),
            ('TAYLOR(ln(X),X,3,1)', '', 'X³/3-3X²/2+3X-11/6'),
            ('TAYLOR(sin(X),X,3)', 'G', 'MODE'),
            ('IMPDIF(X²+Y²=1,X,Y)', '', '-X/Y'),
            ('IMPDIF(X²+Y²=1,X,Y,2)', '', '(-X²-Y²)/Y³'),
            ('IMPDIF(X³+Y³=6XY,X,Y)', '', '(2Y-X²)/(Y²-2X)'),
            ('IMPDIF(X²=1,X,Y)', '', 'DIVIDE BY 0'),
            ('CENTRALDIFF(X³,X,H)', '', 'H²+3X²'),
            ('CENTRALDIFF(1/X,X,H)', '', '1/(H²-X²)'),
            ('INTEGRAL(X,X)', '', 'X²/2'),
            ('INTEGRAL(1/X,X)', '', 'ln(abs(X))'),
            ('INTEGRAL(Xsin(X),X)', '', 'sin(X)-Xcos(X)'),
            ('INTEGRAL(ln(X),X)', '', 'Xln(X)-X'),
            ('INTEGRAL(sin(X)²,X)', '', 'X/2-sin(2X)/4'),
            ('INTEGRAL(X²e^(2X),X)', 'M', 'X²e^(2X)/2-Xe^(2X)/2+e^(2X)/4'),
            ('INTEGRAL(1/(X²-1),X)', 'M', 'ln(abs(X-1))/2-ln(abs(X+1))/2'),
            ('INTEGRAL(1/(X(X+1)²),X)', 'M', 'ln(abs(X))-ln(abs(X+1))+1/(X+1)'),
            ('INTEGRAL(X,X,0,1)', '', '1/2'),
            ('INTEGRAL(1/X,X,1,2)', '', 'ln(2)'),
            ('INTEGRAL(sin(X),X,0,1)', '', '1-cos(1)'),
            ('INTEGRAL(1/X,X,~1,1)', '', 'DOMAIN'),
            ('INTEGRAL(ln(X),X,0,1)', '', 'SYMCE LIMIT'),       # improper
            ('INTEGRAL(1/(X²+1),X)', '', 'SYMCE LIMIT'),        # arctan
            ('INTEGRAL(sin(X),X)', 'G', 'MODE'),
            ('LIMIT(sin(X)/X,X,0)', '', '1'),
            ('LIMIT((1-cos(X))/X²,X,0)', '', '1/2'),
            ('LIMIT((X²-1)/(X-1),X,1)', '', '2'),
            ('LIMIT(1/X,X,0)', '', 'UNDEFINED'),
            ('LIMIT(1/X,X,0,1)', '', '+INFINITY'),
            ('LIMIT(1/X,X,0,~1)', '', '-INFINITY'),
            ('LIMIT(1/X²,X,0)', '', '+INFINITY'),
            ('LIMIT((2X²+1)/(3X²-X),X,1ᴇ99)', '', '2/3'),
            ('LIMIT(X³/(X+1),X,~1ᴇ99)', '', '+INFINITY'),
            ('LIMIT(sin(X),X,1ᴇ99)', '', 'SYMCE LIMIT'),
            ('LIMIT(X,X)', '', 'ARGUMENT'),
            ('SUM(X,X,1,3)', '', '6'),
            ('SUM(X²,X,1,N)', '', 'N³/3+N²/2+N/6'),
            ('SUM(1/X,X,1,4)', '', '25/12'),
            ('SUM(X,X,3,1)', '', '-2'),                         # Karr's convention
            ('SUM(X,X,1,1000)', '', '500500'),
            ('SUM(e^(X),X,1,N)', '', '(e^(N+1)-e)/(e-1)'),
            ('SUM(1/X,X,0,3)', '', 'DOMAIN'),
            ('SUM(1/X,X,1,N)', '', 'SYMCE LIMIT'),
            ('PRODUCT(X,X,1,5)', '', '120'),
            ('PRODUCT(X,X,4,1)', '', '1/6'),
            ('PRODUCT(X,X,1,N)', '', 'SYMCE LIMIT'),
            ('FMIN(X²-2X,X)', '', 'X=1'),
            ('FMAX(X³-3X,X,~2,2)', '', 'X=-1 or X=2'),
            ('FMIN(X^4-4X²+1,X)', '', 'X=-√(2) or X=√(2)'),
            ('FMAX(1/(X²+1),X)', '', 'X=0'),
            ('FMIN(X³,X)', '', 'AT -INFINITY'),
            ('FMIN(1/(X²+1),X)', '', 'AT INFINITY'),
            ('FMIN(5,X)', '', 'ALWAYS TRUE'),
            ('FMIN(X,X,1,0)', '', 'ARGUMENT'),
            ('ARCLEN(X,X,0,1)', '', '√(2)'),
            ('ARCLEN(X²,X,0,1)', 'M', 'ln(2+√(5))/4+√(5)/2'),
            ('ARCLEN(2X√(X)/3,X,0,3)', '', '14/3'),
            ('ARCLEN(X²/4-ln(X)/2,X,1,2)', '', 'ln(2)/2+3/4'),
            ('ARCLEN(X³,X,0,1)', '', 'SYMCE LIMIT'),
            ('SERIES(1/sin(X),X,3)', 'M', '7X³/360+X/6+1/X'),
            ('SERIES(e^(X)/X,X,2)', 'M', 'X²/6+X/2+1+1/X'),
            ('SERIES(1/X,X,3,1)', '', '4-6X+4X²-X³'),
            ('SERIES(sin(X),X,3)', 'G', 'MODE'),
            ('DOMINANTTERM(1/sin(X),X)', '', '1/X'),
            ('DOMINANTTERM((1-cos(X))/X,X,0)', '', 'X/2'),
            ('DOMINANTTERM((2X³+X)/(X-1),X,1ᴇ99)', '', '2X²'),
            # √ of more than a monomial is a kernel, √(X²) and abs( |X|
            ('√(X²)', '', 'abs(X)'),
            ('√(8X²)', '', '2√(2)abs(X)'),
            ('√(4X+4)', '', '2√(X+1)'),
            ('√(X²+2X+1)', '', 'abs(X+1)'),
            ('1/√(X+1)', '', '√(X+1)/(X+1)'),
            ('√((X+1)/2)', '', '√(2)√(X+1)/2'),
            ('abs(~X)', '', 'abs(X)'),
            ('√(√(2)-X)', '', None),                  # a root under a root: the OS's
            ('EXPAND((√(X+1)+1)²)', '', 'X+2√(X+1)+2'),
            ('DERIV(√(X+1),X)', '', '√(X+1)/(2X+2)'),
            ('DERIV(abs(X),X)', '', 'abs(X)/X'),
            ('TAYLOR(√(X+1),X,2)', '', '1+X/2-X²/8'),
            ('LIMIT((√(X+1)-2)/(X-3),X,3)', '', '1/4'),
            ('INTEGRAL(√(X+1),X)', '', '2X√(X+1)/3+2√(X+1)/3'),
            ('INTEGRAL(1/√(X),X)', '', '2√(X)'),
            ('INTEGRAL(1/√(X+1)+X,X)', '', 'X²/2+2√(X+1)'),   # no constant from X := X+1
            ('INTEGRAL(√(3-2X),X,0,1)', '', '√(3)-1/3'),
            ('INTEGRAL(1/√(X),X,0,1)', '', '2'),
            ('INTEGRAL(√(X+1),X,~2,0)', '', 'SYMCE LIMIT'),    # √ of < 0
            ('INTEGRAL(√(X+1)/X,X)', '', 'SYMCE LIMIT'),
            ('SOLVE(√(X+1)=X,X)', '', 'X=(1+√(5))/2'),       # (1-√(5))/2 is extraneous
            ('SOLVE(√(X)=~3,X)', '', 'NO SOLUTION'),
            ('SOLVE(abs(X)=2,X)', '', 'X=-2 or X=2'),
            ('SOLVE(abs(X-1)=2X,X)', '', 'X=1/3'),
            ('SOLVE(√(X²)=X,X)', '', 'SYMCE LIMIT'),           # every X >= 0
            ('SOLVE(√(X)+√(X+1)=3,X)', '', 'SYMCE LIMIT'),     # two roots
            ('DISTANCE((X,0),(1,0))', '', 'abs(X-1)'),
            # a variable in an exponent: a kernel, on the home screen only
            ('X^X', '', 'X^X'),
            ('X^X+X^X', 'M', '2X^X'),
            ('(X^X)(X^X)', '', '(X^X)²'),
            ('X^X/X^X', '', '1'),
            ('(X^X+1)²', '', '(X^X)²+2X^X+1'),
            ('3*2^X', '', '3*2^X'),
            ('X^4*2^X', '', 'X^4*2^X'),
            ('(X+1)^(2X)', 'M', '(X+1)^(2X)'),
            ('X^~X', '', 'X^(-X)'),
            ('(~2)^X', '', '(-2)^X'),
            ('X^Y/Y^X', '', 'X^Y/Y^X'),
            ('1^X', '', '1'),
            ('0^X', '', None),                        # 0 or undefined: the OS's
            ('X^X^X', '', None),                      # a kernel in a kernel
            ('X^0.5', '', None),
            ('DERIV(X^X,X)', '', 'SYMCE LIMIT'),
            ('\x25X³,X,2)', '', None),                # nDeriv(
            ('\x24X,X,0,1)', '', None),               # fnInt(
            ('\x27X²,X,~1,1)', '', None),             # fMin(
            ('\x28~X²,X,~1,1)', '', None)):           # fMax(
        o = ask_fn(eng, report, ti(entry), pre)
        if (readable(o) if isinstance(o, bytes) else o) != want:
            report('EXACT', ti(entry), o if isinstance(o, bytes) else str(o).encode(), str(want).encode())
    return got


# ---- 9. calculus, the second half: INTEGRAL .. DOMINANTTERM ----
def calculus2(rnd, n, eng, report):
    """Each answer against sympy (mpmath for a definite integral): INTEGRAL's
    derivative is the entry, or its value the quadrature's; LIMIT, SUM,
    PRODUCT, SERIES and DOMINANTTERM sympy's own; FMIN and FMAX the points
    where f is least (greatest) among f′'s real roots and the ends, or the
    open end past them all; ARCLEN the quadrature of √(1+f′²). Only a limit
    may refuse; any other error must be the one sympy's answer calls for
    (DOMAIN at a pole, ±INFINITY, UNDEFINED where the sides differ, AT
    ±INFINITY), and where sympy has no answer (a pole's sum, ln(X) from 0) an
    answer is wrong. In degrees trig of X is ERROR: MODE."""
    import mpmath
    got = {}
    X, oo = sym(0x58), sympy.oo
    TRIG = (SIN, COS, TAN)

    def co(k):                  # a coefficient as typed: 1 none, -1 the negation
        return ('~' if k < 0 else '') + (str(abs(k)) if abs(k) != 1 else '')

    def nm(k):
        return ('~' if k < 0 else '') + str(abs(k))

    def pm(k):                  # X+k as typed
        return 'X' + ('+%d' % k if k > 0 else '-%d' % -k if k else '')

    def k_():
        return rnd.choice([1, 2, 3, -1, -2])

    def poly(d):
        """a random polynomial of degree d, as typed, and as sympy"""
        cs = [rnd.choice([0, 1, 2, 3, -1, -2]) for _ in range(d)] + [rnd.choice([1, 2, 3, -1, -2])]
        return ptxt(cs), sum(c * X ** p for p, c in enumerate(cs))

    def ptxt(cs):
        s = ''
        for p in range(len(cs) - 1, -1, -1):
            c = cs[p]
            if c:
                t = (str(abs(c)) if abs(c) != 1 or not p else '') + ('X' + ['', '', '²', '³', '^4'][p] if p else '')
                s += ('-' if s else '~') + t if c < 0 else ('+' if s else '') + t
        return s or '0'

    def aspoly(e):
        return ptxt([int(c) for c in reversed(sympy.Poly(e, X).all_coeffs())])

    def value(o):               # a number's value
        v = fvalue(o)
        if v.free_symbols:
            raise ValueError
        return complex(sympy.N(v, 30))

    def near(a, b):
        return abs(a - b) <= 1e-9 * (1 + abs(b))

    def alike(v, e):            # the same function: at random points, poles skipped
        seen = 0
        for _ in range(6):
            p = {w: sympy.Rational(rnd.randint(200, 1400), 1000) for w in e.free_symbols | v.free_symbols}
            try:
                a, b = complex(sympy.N(v.subs(p), 30)), complex(sympy.N(e.subs(p), 30))
            except (TypeError, ValueError, ZeroDivisionError):
                continue
            if abs(b) < 1e6 or not p:           # a constant, as e^(18)+78, has no pole to skip
                if not near(a, b):
                    return False
                seen += 1
            if seen == 3:
                break
        return seen > 0

    def lim(f, a, d=0):
        """sympy's limit as LIMIT answers it: a value, or its error"""
        if a in (oo, -oo):
            v = sympy.limit(f, X, a)
        elif d:
            v = sympy.limit(f, X, a, '+' if d > 0 else '-')
        else:
            l, r = sympy.limit(f, X, a, '-'), sympy.limit(f, X, a, '+')
            v = l if l == r else 'UNDEFINED'
        if v in (oo, -oo):
            return '+INFINITY' if v == oo else '-INFINITY'
        if isinstance(v, str) or v.has(sympy.zoo, sympy.nan, sympy.AccumBounds, sympy.Limit):
            return v if isinstance(v, str) else None
        return v

    def iterm():
        """(typed, sympy, poles, domain: X > x0 (strict) or X >= x0)"""
        c, k, r = k_(), k_(), rnd.random()
        a, b = rnd.sample([-2, -1, 0, 1, 2, 3], 2)
        if r < 0.2:
            p = rnd.randint(0, 3)
            return (nm(c) if not p else co(c) + 'X' + ['', '', '²', '³'][p]), c * X ** p, [], None
        if r < 0.3:
            return nm(c) + '/(' + pm(a) + ')', sympy.Integer(c) / (X + a), [-a], None
        if r < 0.37:
            return nm(c) + '/(' + pm(a) + ')²', sympy.Integer(c) / (X + a) ** 2, [-a], None
        if r < 0.45:
            return (nm(c) + '/((' + pm(a) + ')(' + pm(b) + '))', sympy.Integer(c) / ((X + a) * (X + b)),
                    [-a, -b], None)
        if r < 0.66:
            t, f = rnd.choice([('sin(', sympy.sin), ('cos(', sympy.cos), ('e^(', sympy.exp)])
            p = rnd.choice([0, 0, 1, 2])
            return (co(c) + ['', 'X', 'X²'][p] + t + co(k) + 'X)', c * X ** p * f(k * X), [], None)
        if r < 0.74:
            p = rnd.randint(0, 1)
            return co(c) + 'X' * p + 'ln(X)', c * X ** p * sympy.log(X), [], (0, True)
        if r < 0.8:
            return co(c) + 'ln(' + pm(a) + ')', c * sympy.log(X + a), [], (-a, True)
        if r < 0.86:
            p = rnd.randint(0, 1)
            return co(c) + 'X' * p + '√(X)', c * X ** p * sympy.sqrt(X), [], (0, False)
        if r < 0.94:
            t = rnd.choice([('sin(X)²', sympy.sin(X) ** 2), ('sin(X)cos(X)', sympy.sin(X) * sympy.cos(X)),
                            ('cos(' + co(k) + 'X)²', sympy.cos(k * X) ** 2)])
            return co(c) + t[0], c * t[1], [], None
        t = rnd.choice([('tan(X)', sympy.tan(X), [], None), ('1/(X²+1)', 1 / (X ** 2 + 1), [], None),
                        ('e^(X²)', sympy.exp(X ** 2), [], None)])
        return t                                    # refused: no answer is right but an error

    def kfn(m=True):
        """a function for SERIES, DOMINANTTERM, LIMIT at 0: (typed, sympy)"""
        k = k_()
        t, e = rnd.choice([('sin(' + co(k) + 'X)', sympy.sin(k * X)), ('cos(' + co(k) + 'X)', sympy.cos(k * X)),
                           ('e^(' + co(k) + 'X)', sympy.exp(k * X)), ('ln(X+1)', sympy.log(X + 1)),
                           ('1/(1-X)', 1 / (1 - X)), ('tan(X)', sympy.tan(X)), ('(e^(X)-1)', sympy.exp(X) - 1),
                           ('(1-cos(X))', 1 - sympy.cos(X)), ('(sin(X)-X)', sympy.sin(X) - X),
                           ('e^(X)sin(X)', sympy.exp(X) * sympy.sin(X))])
        r = rnd.random()
        if m and r < 0.4:
            p = rnd.randint(1, 3)
            return t + '/X' + ['', '', '²', '³'][p], e / X ** p
        if m and r < 0.55:
            return '1/(' + t + ')', 1 / e
        if m and r < 0.65:
            return 'X/(' + t + ')', X / e
        return t, e

    PTS = [('0', 0), ('1', 1), ('2', 2), ('3', 3), ('~1', -1), ('~2', -2), ('1/2', sympy.Rational(1, 2))]

    for i in range(n):
        cmd = rnd.choice(['INTEGRAL', 'INTEGRAL', 'INTEGRAL', 'LIMIT', 'LIMIT', 'SUM', 'SUM', 'PRODUCT',
                          'FMIN', 'FMAX', 'ARCLEN', 'SERIES', 'SERIES', 'DOMINANTTERM'])
        want, check = None, None        # want: a value; an error's message; None: any error
        try:
            if cmd == 'INTEGRAL':
                ts = [iterm() for _ in range(rnd.randint(1, 2))]
                t = '+'.join(x[0] for x in ts)
                f = sum(x[1] for x in ts)
                poles = sum((x[2] for x in ts), [])
                dom = max((x[3] for x in ts if x[3]), default=None)
                if rnd.random() < 0.5:
                    (lt, lo), (ht, hi) = rnd.sample(PTS, 2)
                    t += ',X,' + lt + ',' + ht
                    a, b = min(lo, hi), max(lo, hi)
                    den = sympy.fraction(sympy.cancel(f))[1]     # a pole of one term that the sum cancels is none
                    if any(a <= p <= b and den.subs(X, p) == 0 for p in poles):
                        want = 'DOMAIN'
                    elif not (dom and (a < dom[0] or a == dom[0] and dom[1])):
                        g = sympy.lambdify(X, f, 'mpmath')
                        want = complex(mpmath.quad(g, [lo, hi]))
                        check = lambda o: near(value(o), want)
                else:
                    t += ',X'
                    want = f
                    # ln|u|' is u'/u, as ln(u)'
                    check = lambda o: alike(sympy.diff(fvalue(o).replace(sympy.Abs, lambda u: u), X), f)
            elif cmd == 'LIMIT':
                d = rnd.choice([0, 0, 1, -1])
                if rnd.random() < 0.5:                  # rational, with a factor in common now and then
                    pt, a = rnd.choice(PTS[:6] + [('1ᴇ99', oo), ('~1ᴇ99', -oo)])
                    P, Q = poly(rnd.randint(0, 2))[1], poly(rnd.randint(1, 2))[1]
                    if a not in (oo, -oo) and rnd.random() < 0.5:
                        Q = sympy.expand(Q * (X - a))
                        if rnd.random() < 0.5:
                            P = sympy.expand(P * (X - a))
                    t, f = '(' + aspoly(P) + ')/(' + aspoly(Q) + ')', P / Q
                else:
                    (t, f), (pt, a) = kfn(), rnd.choice(PTS[:1] * 4 + PTS[1:3])
                if a in (oo, -oo):
                    d = 0
                t += ',X,' + pt + (',' + nm(d) if d else '')
                want = lim(f, a, d)
                if want is not None and not isinstance(want, str):
                    check = lambda o: near(value(o), complex(want))
            elif cmd in ('SUM', 'PRODUCT'):
                r = rnd.random()
                if r < 0.6:
                    t, f = poly(rnd.randint(0, 3 if cmd == 'SUM' else 1))
                elif r < 0.8:
                    t, f = rnd.choice([('1/X', 1 / X), ('1/(X(X+1))', 1 / (X * (X + 1))), ('X/(X+1)', X / (X + 1))])
                else:
                    k = k_()
                    t, f = poly(rnd.randint(0, 2))
                    t, f = t + '+e^(' + co(k) + 'X)', f + sympy.exp(k * X)
                if cmd == 'SUM' and rnd.random() < 0.5:
                    (lt, lo), (ht, hi) = rnd.choice([(('1', 1), ('N', sym(0x4E))), (('0', 0), ('N', sym(0x4E))),
                                                     (('A', sym(0x41)), ('B', sym(0x42))),
                                                     (('1', 1), ('2N', 2 * sym(0x4E)))])
                else:
                    l = rnd.randint(-3, 5)
                    lo, hi = l, l + rnd.randint(-3, 7)
                    lt, ht = nm(lo), nm(hi)
                t += ',X,' + lt + ',' + ht
                if isinstance(lo, int):             # Karr: backwards, the range between, negated or inverted
                    a, b = (lo, hi) if hi >= lo - 1 else (hi + 1, lo - 1)
                    vs = [f.subs(X, k) for k in range(a, b + 1)]
                    w = sympy.Add(*vs) if cmd == 'SUM' else sympy.Mul(*vs)
                    if any(v.has(sympy.zoo, sympy.nan) for v in vs):
                        w = 'DOMAIN'
                    elif (a, b) != (lo, hi):
                        w = -w if cmd == 'SUM' else 'DIVIDE BY 0' if w == 0 else 1 / w
                else:
                    w = sympy.summation(f, (X, lo, hi))
                if isinstance(w, str) or not w.has(sympy.zoo, sympy.nan, sympy.oo, sympy.Sum):
                    want = w
                    check = lambda o: alike(fvalue(o), want)
            elif cmd in ('FMIN', 'FMAX'):
                if rnd.random() < 0.8:
                    t, f = poly(rnd.randint(1, 4))
                else:
                    k = rnd.randint(1, 3)
                    t, f = rnd.choice([('1/(X²+%d)' % k, 1 / (X ** 2 + k)), ('X/(X²+%d)' % k, X / (X ** 2 + k))])
                ends = rnd.choice([(None, None), (None, None), 'both', 'lo'])
                lo = hi = None
                if ends == 'both':
                    lo, hi = sorted(rnd.sample(range(-3, 4), 2))
                    t += ',X,' + nm(lo) + ',' + nm(hi)
                elif ends == 'lo':
                    lo = rnd.randint(-3, 3)
                    t += ',X,' + nm(lo)
                else:
                    t += ',X'
                g = -f if cmd == 'FMAX' else f
                d = sympy.together(sympy.diff(g, X))
                if d == 0:
                    want = 'ALWAYS TRUE'
                else:
                    cs = [r for r in sympy.Poly(sympy.numer(d), X).real_roots()
                          if (lo is None or r > lo) and (hi is None or r < hi)]
                    cs += [e for e in (lo, hi) if e is not None]
                    vs = [complex(sympy.N(g.subs(X, c), 30)).real for c in cs]
                    m = min(vs) if vs else None
                    at, tt = set(), set()
                    for side, open_ in ((-1, lo is None), (1, hi is None)):
                        if open_:
                            L = sympy.limit(g, X, side * oo)
                            if L == -oo:
                                at.add(side)
                            elif L != oo and (m is None or float(L) < m - 1e-12):
                                tt.add(side)
                    at = at or tt
                    if at:
                        want = 'AT INFINITY' if len(at) == 2 else 'AT -INFINITY' if -1 in at else 'AT +INFINITY'
                    else:
                        want = sorted(complex(sympy.N(c, 30)).real for c, v in zip(cs, vs) if abs(v - m) < 1e-9)
                        check = lambda o: (all(x[:2] == bytes([0x58, EQ]) for x in o.split(bytes([OR]))) and
                                           len(o.split(bytes([OR]))) == len(want) and
                                           all(near(value(x[2:]), w) for x, w in zip(o.split(bytes([OR])), want)))
            elif cmd == 'ARCLEN':
                r = rnd.random()
                if r < 0.75:
                    t, f = poly(rnd.randint(1, 3))
                    dom = None
                else:
                    t, f, dom = rnd.choice([('X²/4-ln(X)/2', X ** 2 / 4 - sympy.log(X) / 2, (0, True)),
                                            ('2X√(X)/3', 2 * X * sympy.sqrt(X) / 3, (0, False)),
                                            ('X²/8-ln(X)', X ** 2 / 8 - sympy.log(X), (0, True))])
                (lt, lo), (ht, hi) = rnd.sample(PTS, 2)
                t += ',X,' + lt + ',' + ht
                if not (dom and (min(lo, hi) < dom[0] or min(lo, hi) == dom[0] and dom[1])):
                    g = sympy.lambdify(X, sympy.sqrt(1 + sympy.diff(f, X) ** 2), 'mpmath')
                    want = complex(mpmath.quad(g, [lo, hi]))
                    check = lambda o: near(value(o), want)
            else:                                   # SERIES, DOMINANTTERM
                pt, a = '0', 0
                if cmd == 'DOMINANTTERM' and rnd.random() < 0.3:
                    P, Q = poly(rnd.randint(0, 3))[1], poly(rnd.randint(0, 3))[1]
                    t, f = '(' + aspoly(P) + ')/(' + aspoly(Q) + ')', P / Q
                    pt, a = rnd.choice([('1ᴇ99', oo), ('~1ᴇ99', -oo)])
                elif rnd.random() < 0.2:
                    t, f = rnd.choice([('ln(X)', sympy.log(X)), ('1/X', 1 / X), ('X/(X+1)', X / (X + 1)),
                                       ('1/(X-1)²', 1 / (X - 1) ** 2)])
                    pt, a = '1', 1
                else:
                    t, f = kfn()
                if cmd == 'SERIES':
                    o_ = rnd.randint(1, 4)
                    t += ',X,%d' % o_ + (',' + pt if a or rnd.random() < 0.3 else '')
                    want = sympy.series(f, X, a, o_ + 1).removeO()
                elif a in (oo, -oo):
                    t += ',X,' + pt
                    nu, de = sympy.fraction(sympy.cancel(f))
                    want = sympy.LT(nu, X) / sympy.LT(de, X)
                else:
                    t += ',X' + (',' + pt if a or rnd.random() < 0.5 else '')
                    u = sympy.expand(sympy.series(f.subs(X, X + a), X, 0, 8).removeO())
                    if u != 0:
                        want = min(sympy.Add.make_args(u), key=lambda v: v.as_coeff_exponent(X)[1]).subs(X, X - a)
                if want is not None:
                    check = lambda o: alike(fvalue(o), want)
        except (NotImplementedError, TypeError, ValueError, ZeroDivisionError, sympy.PolynomialError):
            continue
        c = cmd.encode() + ti('(' + t + ')')
        if len(c) > MAXLEN:
            continue
        trig = any(b in TRIG for b in c)
        radian = None
        for pre in ('', 'M') + (('G',) if i % 4 == 0 else ()):
            o = ask_fn(eng, report, c, pre)
            if pre == 'G':
                if (o != 'MODE' and o not in LIMITS) if trig else (o != radian):
                    report('DEG', c, o if isinstance(o, bytes) else str(o).encode(), b'MODE' if trig else radian)
                continue
            if not pre:
                radian = o
            if not isinstance(o, bytes):
                if not (o in LIMITS or o == want if isinstance(want, str) else o in LIMITS or
                        want is None and o is not None):
                    report(cmd, c, (o or 'PASS').encode(), str(want).encode())
                got[str(o)] = got.get(str(o), 0) + 1
                continue
            try:
                named = any(b in (BB, EXT) or LN <= b <= TAN for b in o)
                ok = check is not None and check(o) and (fcols if named else width)(o, pre == 'M') <= WIDTH
            except (Syntax, TypeError, ValueError, SyntaxError, sympy.SympifyError):
                ok = False
            if not ok:
                report(cmd + ' ' + pre, c, o, str(want).encode())
            else:
                got[cmd] = got.get(cmd, 0) + 1
    return got


# ---- 10. geometry: the Geometry menu's DISTANCE .. DILATE ----

def geometry(rnd, n, eng, report):
    """Exact cases, then random points against Fractions: MIDPOINT, CENTROID,
    AREA (the shoelace), INTERSECT of two lines, ROTATE by quarter turns
    about a point, DILATE about a point."""
    from fractions import Fraction as F
    got = {}
    for entry, pre, want in (
            ('MIDPOINT((1,2),(3,4))', '', '(2,3)'),
            ('MIDPOINT((1,2),(4,7)', 'D', '(2.5,4.5)'),
            ('MIDPOINT((A,B),(C,D))', '', '(A/2+C/2,B/2+D/2)'),
            ('DISTANCE((0,0),(3,4))', '', '5'),
            ('DISTANCE((1,1),(2,3))', '', '√(5)'),
            ('DISTANCE((0,0),(A,B))', '', '√(A²+B²)'),
            ('DISTANCE((1,2),Y=2X+3)', '', '3√(5)/5'),
            ('DISTANCE(3X+4Y=10,(0,0))', '', '2'),
            ('DISTANCE((0,0),X=3)', '', '3'),
            ('SLOPE((1,2),(3,8))', '', '3'),
            ('SLOPE((1,2),(1,8))', '', 'DIVIDE BY 0'),
            ('SLOPE(2X+3Y=6)', '', '-2/3'),
            ('SLOPE(Y-1=2(X-3))', '', '2'),
            ('LINE((0,1),(2,5))', '', 'Y=2X+1'),
            ('LINE((1,2),(1,5))', '', 'X=1'),
            ('LINE((1,2),3)', '', 'Y=3X-1'),
            ('LINE((1,1),(1,1))', '', 'ARGUMENT'),
            ('PARALLEL((1,2),Y=3X+1)', '', 'Y=3X-1'),
            ('PERPENDICULAR((1,2),Y=3X+1)', '', 'Y=7/3-X/3'),
            ('PERPENDICULAR((0,0),Y=4)', '', 'X=0'),
            ('PERPBISECTOR((0,0),(4,2))', '', 'Y=5-2X'),
            ('INTERSECT(Y=X,Y=2-X)', '', '(1,1)'),
            ('INTERSECT(2X+3Y=7,X-Y=1)', '', '(2,1)'),
            ('INTERSECT(2X+1,~X+4)', '', '(1,3)'),
            ('INTERSECT(Y=X,Y=X+1)', '', 'NO SOLUTION'),
            ('INTERSECT(Y=X,2Y=2X)', '', 'ALWAYS TRUE'),
            ('INTERSECT(Y=X²,Y=1)', '', 'ARGUMENT'),
            ('REFLECT((1,2),Y=X)', '', '(2,1)'),
            ('REFLECT((1,0),X+Y=2)', '', '(2,1)'),
            ('PARTITION((0,0),(10,5),2,3)', '', '(4,2)'),
            ('PARTITION((0,0),(10,5),1,~1)', '', 'DIVIDE BY 0'),
            ('AREA((0,0),(4,0),(0,3))', '', '6'),
            ('AREA((0,0),(4,0),(4,3),(3,5),(1,5),(0,3))', '', '18'),
            ('AREA((0,0),(A,0),(0,B))', '', 'SYMCE LIMIT'),
            ('AREA((0,0),(1,1))', '', 'ARGUMENT'),
            ('PERIMETER((0,0),(1,0),(0,1))', '', '2+√(2)'),
            ('CENTROID((0,0),(1,0),(0,1))', '', '(1/3,1/3)'),
            ('CIRCUMCENTER((0,0),(4,0),(0,6))', '', '(2,3)'),
            ('CIRCUMCENTER((0,0),(1,1),(2,2))', '', 'NO SOLUTION'),
            ('ORTHOCENTER((0,0),(4,0),(1,3))', '', '(1,1)'),
            ('INCENTER((0,0),(1,0),(0,1))', '', '(1-√(2)/2,1-√(2)/2)'),
            ('HYPOT(3,4)', '', '5'),
            ('LEG(5,3)', '', '4'),
            ('LEG(3,5)', '', 'DOMAIN'),
            ('CIRCLE((1,~2),3)', '', '(X-1)²+(Y+2)²=9'),
            ('CIRCLE((0,0),(3,4))', '', 'X²+Y²=25'),
            ('CIRCLE((0,0),(2,0),(0,2))', '', '(X-1)²+(Y-1)²=2'),
            ('CIRCLE(X²+Y²-2X+4Y=4)', '', 'C=(1,-2),R=3'),
            ('CIRCLE(X²+Y²-X+Y=1)', 'M', 'C=(1/2,-1/2),R=√(6)/2'),
            ('CIRCLE(X²+2Y²=8)', '', 'ARGUMENT'),
            ('CIRCLE(X²+Y²=~1)', '', 'DOMAIN'),
            ('ROTATE((1,0),45)', '', '(√(2)/2,√(2)/2)'),
            ('ROTATE((1,2),60)', '', '(1/2-√(3),1+√(3)/2)'),
            ('ROTATE((1,2),450)', 'G', '(-2,1)'),
            ('ROTATE((1,0),20)', '', 'SYMCE LIMIT'),
            ('DILATE((2,3),1/2,(1,1))', '', '(3/2,2)')):
        o = ask_fn(eng, report, ti(entry), pre)
        if (readable(o) if isinstance(o, bytes) else o) != want:
            report('EXACT', ti(entry), o if isinstance(o, bytes) else str(o).encode(), str(want).encode())

    def num(v):                 # a Fraction as typed: ~ for its sign
        return ('~' if v < 0 else '') + str(abs(v))

    def pt(p):
        return '(%s,%s)' % (num(p[0]), num(p[1]))

    for _ in range(n):
        ps = [(F(rnd.randint(-9, 9)), F(rnd.randint(-9, 9))) for _ in range(rnd.randint(3, 5))]
        (x0, y0), (x1, y1) = ps[0], ps[1]
        cmd = rnd.choice(('MIDPOINT', 'CENTROID', 'AREA', 'INTERSECT', 'ROTATE', 'DILATE'))
        if cmd == 'MIDPOINT':
            e, want = 'MIDPOINT(%s,%s)' % (pt(ps[0]), pt(ps[1])), ((x0 + x1) / 2, (y0 + y1) / 2)
        elif cmd == 'CENTROID':
            e = 'CENTROID(%s)' % ','.join(map(pt, ps[:3]))
            want = (sum(p[0] for p in ps[:3]) / 3, sum(p[1] for p in ps[:3]) / 3)
        elif cmd == 'AREA':
            e = 'AREA(%s)' % ','.join(map(pt, ps))
            want = abs(sum(p[0] * q[1] - q[0] * p[1] for p, q in zip(ps, ps[1:] + ps[:1]))) / 2
        elif cmd == 'INTERSECT':        # Y=x0X+y0 and Y=x1X+y1
            if x0 == x1:
                continue
            e = 'INTERSECT(Y=%sX+%s,Y=%sX+%s)' % (num(x0), num(y0), num(x1), num(y1))
            x = (y1 - y0) / (x0 - x1)
            want = (x, x0 * x + y0)
        elif cmd == 'ROTATE':           # ps[0] about ps[1]
            q = rnd.choice((90, 180, 270, -90))
            e = 'ROTATE(%s,%s,%s)' % (pt(ps[0]), num(q), pt(ps[1]))
            dx, dy = x0 - x1, y0 - y1
            for _ in range(q % 360 // 90):
                dx, dy = -dy, dx
            want = (x1 + dx, y1 + dy)
        else:                           # ps[0] by k about ps[1]
            k = F(rnd.randint(-4, 4), rnd.randint(1, 3))
            e = 'DILATE(%s,%s,%s)' % (pt(ps[0]), num(k), pt(ps[1]))
            want = (x1 + k * (x0 - x1), y1 + k * (y0 - y1))
        want = str(want) if cmd == 'AREA' else '(%s,%s)' % want
        o = ask_fn(eng, report, ti(e))
        if not isinstance(o, bytes) or readable(o) != want:
            report(cmd, ti(e), o if isinstance(o, bytes) else str(o).encode(), want.encode())
        else:
            got[cmd] = got.get(cmd, 0) + 1
    return got


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
    rnd = random.Random(20260926)
    eng = subprocess.Popen([ENGINE], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    mini = subprocess.Popen([MINI], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)

    def ask(p, t, ans=b'', mp=0, dec=0, vals=()):
        """ans: the tokens Ans stands for, or a TI real as 0xFF and 9 bytes.
        mp: as in MathPrint; dec: as for MODE ANSWERS: DEC (the engine only).
        vals: (letter token, 9 bytes) stored in that letter, for SOLVE."""
        if ans and ans[0] != 0xFF:
            ans = bytes([len(ans)]) + ans
        p.stdin.write(('M' if mp else '') + ('D' if dec else '') + t.hex() + (',' + ans.hex() if ans else '') +
                      ''.join(';%02x%s' % (k, v.hex()) for k, v in vals) + '\n')
        p.stdin.flush()
        r = p.stdout.readline().strip()
        if r[:1] == 'E':                     # an error screen: its message, as text
            return bytes.fromhex(r[1:]).split(b'\0')[0].decode()
        return None if r == 'PASS' else bytes.fromhex(r)

    bad, stats, shown = 0, dict(same=0, answered=0, refused_ok=0, refused_fit=0, wide=0, kernel=0), []

    def report(kind, t, got, want):
        nonlocal bad
        bad += 1
        if bad <= 20:
            print('%-6s %-30s engine %-18s want %s' % (
                kind, show(t), got and show(got), want and show(want)))

    def judge(t, whole, ans=b'', decs=(), own=False, alt=None):
        """whole: the answer however wide, or None; decs: the same in decimals
        where they end, as MODE ANSWERS: DEC wants it, if that is checked too.
        own: t uses the engine's own Ans, so it must answer or error, never
        pass. alt: t spelt out, for lax_value() when t has Ans in it.
        Returns Classic's answer."""
        lax = []
        for dec, mp in [(1, 1), (1, 0)] * len(decs) + [(0, 1), (0, 0)]:
            w = decs[0] if dec else whole
            want = w if w is not None and width(w, mp) <= WIDTH else None
            got = ask(eng, t, ans, mp, dec)
            if own and got is None:
                report('PASSED' + ' M' * mp, t, got, want)
                continue
            if own and isinstance(got, str):    # its error screen is its refusal
                got = None
            if got is None:
                if want is None:
                    stats['refused_ok'] += 1
                else:
                    stats['refused_fit'] += 1
                    if len(shown) < 8:
                        shown.append('%s -> %s' % (show(t), show(want)))
            elif got != want:
                if want is None and not isinstance(got, str) and w is None:
                    if not lax:
                        lax.append(lax_value(alt if alt is not None else t) if alt is not None or not ans else None)
                    if lax[0] is not None and same_value(got, lax[0]):
                        stats['kernel'] += 1
                        continue
                report('WRONG' + ' M' * mp + ' D' * dec, t, got, want)
            else:
                stats['answered'] += 1
                stats['wide'] += len(got) > WIDTH
        return got

    # 1. minipoly's corpus: byte-identical wherever minipoly answers
    for i in range(n):
        t = structured(rnd) if i % 4 else bytes(rnd.choice(ALPHABET) for _ in range(rnd.randint(1, 12)))
        old = ask(mini, t)
        got = judge(t, expected(t))
        if old is not None:
            if got != old:
                report('OLD', t, got, old)
            else:
                stats['same'] += 1

    # 2. trees: the expected answer comes from the tree, not from any parser
    for i in range(n):
        node = tree(rnd, rnd.randint(1, 4))
        t = bytes(render(node, rnd))
        if rnd.random() < 0.1:                       # close parens left off at the end
            t = t.rstrip(bytes([RP]))
        if len(t) > MAXLEN:
            continue
        nv = len({b for b in t if isvar(b)})
        try:
            v = value(node)
            want = fmt(v, nv) if nv or has_root(v) else None
            dec = fmt(v, nv, True) if nv else None     # DEC leaves √(8) to the OS
        except Refuse:
            want = dec = None
        # the Pratt parser has to agree with the tree, or oracle 3 is wrong
        if expected(t) != want:
            report('PRATT', t, expected(t), want)
        judge(t, want, decs=(dec,) if i % 2 else ())

    # 3. mutated trees: one token swapped, so mostly invalid
    for i in range(n):
        t = bytearray(render(tree(rnd, rnd.randint(1, 3)), rnd))
        t[rnd.randrange(len(t))] = rnd.choice(ALPHABET + [LP, RP, NEG, DIV, SQR, CUBE, INV] + FRAC)
        judge(bytes(t[:MAXLEN]), expected(bytes(t[:MAXLEN])))

    # 4. Ans: a tree's answer, then a tree with some of its variables replaced
    # by Ans. The oracle sees (answer) spelled out where Ans was, with no
    # length limit, since the engine never sees that expansion as input.
    # A third of the time Ans is the OS's own result instead, a TI real: the
    # engine takes it when it is exactly a decimal of up to six digits.
    for i in range(n):
        if rnd.random() < 0.3:
            v = Fraction(rnd.choice([1, -1]) * rnd.randint(0, 999999), 10 ** rnd.randint(0, 5))
            a, spelt = b'\xff' + ti_real(v), spell(v)
            if rnd.random() < 0.2:                   # seven digits: not exact
                a = b'\xff' + ti_real(Fraction(1), '1234567')
                spelt = None
        else:
            # MathPrint's answer: it may be wider than Classic's 26 tokens
            a = ask(eng, bytes(render(tree(rnd, rnd.randint(1, 3)), rnd)), mp=1)
            if a is None:
                continue
            back = ask(eng, bytes([ANS]), a, 1)
            if back != a:
                report('ANS', bytes([ANS]), back, a)
            spelt = bytes([LP]) + a + bytes([RP])
        t = bytes(render(tree(rnd, rnd.randint(1, 3)), rnd))
        if len(t) > MAXLEN:
            continue
        spots = [k for k, b in enumerate(t) if isvar(b)]
        pick = set(rnd.sample(spots, min(len(spots), rnd.randint(1, 2))))
        u = bytes(ANS if k in pick else b for k, b in enumerate(t))
        want = None
        if spelt is not None or not pick:           # no Ans in it: t itself
            spelled = b''.join(spelt if k in pick else bytes([b]) for k, b in enumerate(t))
            nv = len({b for b in spelled if isvar(b)})
            try:
                v = pratt(spelled)
                want = (fmt(v, nv, DOT in spelled) if nv or (pick and a[0] != 0xFF)
                        or (SQRT in spelled and has_root(v)) else None)
            except (Syntax, Refuse):
                pass
        judge(u, want, a, own=bool(pick) and a[0] != 0xFF,
              alt=spelled if spelt is not None or not pick else None)
    # A command's answer as Ans: POLYDEGREE's 2 is worked with, a list refused.
    for t, a, want in ((bytes([ANS, ADD, 0x31]), b'2', b'3'),
                       (bytes([ANS, MUL, 0x33]), bytes([LB, NEG, 0x32, LC, 0x32, RB]), 'SYMCE LIMIT')):
        got = ask(eng, t, a)
        if got != want:
            report('ANS', t, got, want)

    # 5. two variables, every term one degree: products of linear forms aU+bV
    # (and U²+V² now and then), a factor or two shared, so homgcd() gets a gcd
    # to find; a non-homogeneous term now and then, which it must leave alone,
    # and a factor that is not homogeneous on one side, which it must see past.
    def form(u, v):
        if rnd.random() < 0.15:
            return [LP, u, SQR, ADD, v, SQR, RP]
        a, b = rnd.choice([1, 1, 2, 3, -1, -2]), rnd.choice([1, 1, 2, -1, -3])
        f = [LP] + ([NEG] if a < 0 else []) + (list(str(abs(a)).encode()) if abs(a) > 1 else [])
        return f + [u, ADD if b > 0 else SUB] + (list(str(abs(b)).encode()) if abs(b) > 1 else []) + [v, RP]
    homs = 0
    for i in range(n // 2):
        u, v = rnd.sample([0x58, 0x58, 0x59, 0x41, 0x42, 0x5B], 2) if rnd.random() < 0.3 else (0x58, 0x59)
        if u == v:
            continue
        common = [form(u, v) for _ in range(rnd.randint(0, 2))]
        top = sum([form(u, v) for _ in range(rnd.randint(0, 2))] + common, [])
        bot = sum([form(u, v) for _ in range(rnd.randint(1, 2))] + common, [])
        if rnd.random() < 0.1:
            bot += [ADD, 0x31]
        if rnd.random() < 0.4:                  # a factor not homogeneous
            extra = [LP] + form(u, v) + [ADD] + rnd.choice(
                [[0x31], form(u, v) + form(u, v), [rnd.choice([0x5A, 0x41, u])]]) + [RP]
            if rnd.random() < 0.5:
                top = top + extra
            else:
                bot = bot + extra
        t = bytes([LP] + (top or [0x31]) + [RP, DIV, LP] + bot + [RP])
        if len(t) <= MAXLEN and judge(t, expected(t)) is not None:
            homs += 1
    print('\nhomogeneous two-variable fractions answered: %d' % homs)

    sv = solve_vars(rnd, n // 16, ask, eng, report)
    print('SOLVE with stored letters answered: %d' % sv)
    cmds = commands(rnd, n // 2, ask, eng, report)
    print('commands answered: %s' % ', '.join('%s %d' % kv for kv in sorted(cmds.items())))
    fns = functions(rnd, n // 8, eng, report)
    print('functions answered: %s' % ', '.join('%s %d' % kv for kv in sorted(fns.items())))
    cal = calculus(rnd, n // 8, eng, report)
    print('calculus answered: %s' % ', '.join('%s %d' % kv for kv in sorted(cal.items())))
    cal = calculus2(rnd, n // 16, eng, report)
    print('calculus, second half, answered: %s' % ', '.join('%s %d' % kv for kv in sorted(cal.items())))
    geo = geometry(rnd, n // 8, eng, report)
    print('geometry answered: %s' % ', '.join('%s %d' % kv for kv in sorted(geo.items())))

    print('\nminipoly answers reproduced: %(same)d' % stats)
    print('answered and matched: %(answered)d   refused, out of scope: %(refused_ok)d'
          % stats)
    print('refused although the answer fits: %(refused_fit)d' % stats)
    print('a root kept whole, its value checked: %(kernel)d' % stats)
    print('MathPrint answers past 26 tokens: %(wide)d' % stats)
    for s in shown:
        print('   ', s)
    print('%d WRONG' % bad)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
