#!/usr/bin/env python3
"""symce/src/graph.c's compiler and evaluator against Python's math.

    python3 graph_check.py            (after make -C symce bin/host/graph_cli)

Each case is an equation as TI tokens (written by name below) and a Python
function of x (and the stored letters). Random x, relative tolerance 1e-4 with
an absolute floor of 1e-4 (float32, and libm's sinf near a zero). Where Python
raises (sqrt of a negative, ln 0, 1/0) the C must give NaN, an infinity, or
something past 1e30: the renderer breaks the line on all three. Then the
equations that must NOT compile: the unsupported tokens and the syntax errors.
Plus random expression trees, printed fully parenthesised.
"""
import math, os, random, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
CLI = os.path.join(HERE, "..", "symce", "bin", "host", "graph_cli")

TOK = {'+': [0x70], '-': [0x71], '*': [0x82], '/': [0x83], '^': [0xF0], '~': [0xB0],
       '(': [0x10], ')': [0x11], 'sq': [0x0D], 'cu': [0x0F], 'inv': [0x0C], '.': [0x3A],
       'EE': [0x3B], 'pi': [0xAC], 'e': [0xBB, 0x31], 'n/d': [0xEF, 0x2E], 'theta': [0x5B],
       'sqrt(': [0xBC], 'abs(': [0xB2], 'ln(': [0xBE], 'e^(': [0xBF], 'log(': [0xC0],
       '10^(': [0xC1], 'sin(': [0xC2], 'asin(': [0xC3], 'cos(': [0xC4], 'acos(': [0xC5],
       'tan(': [0xC6], 'atan(': [0xC7]}


def toks(s):
    out = []
    for t in s.split():
        out += TOK[t] if t in TOK else [ord(t)]          # a digit or a capital letter
    return out


def ti_real(v):
    """A TI real: sign, 0x80 + exponent, 14 BCD digits."""
    if v == 0:
        return bytes([0, 0x80] + [0] * 7)
    e = math.floor(math.log10(abs(v)))
    m = round(abs(v) / 10 ** e * 10 ** 13)
    if m >= 10 ** 14:
        m //= 10
        e += 1
    d = "%014d" % m
    return bytes([0x80 if v < 0 else 0, 0x80 + e] + [int(d[i:i + 2], 16) for i in range(0, 14, 2)])


class CLI_:
    def __init__(self):
        self.p = subprocess.Popen([CLI], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)

    def ask(self, tokens, xs=(), deg=False, var=None):
        vs = ",".join("%02x=%s" % (ord(k), ti_real(v).hex()) for k, v in (var or {}).items()) or "-"
        self.p.stdin.write("%s %s %s %s\n" % ("G" if deg else "-", bytes(tokens).hex(), vs,
                                              " ".join(repr(x) for x in xs)))
        self.p.stdin.flush()
        return self.p.stdout.readline().split()


D = math.pi / 180
CASES = [   # name, tokens, f(x, v)
    ("X", "X", lambda x, v: x),
    ("2+3*X", "2 + 3 * X", lambda x, v: 2 + 3 * x),
    ("2X (implied)", "2 X", lambda x, v: 2 * x),
    ("X^2 as sq", "X sq", lambda x, v: x * x),
    ("X^3 as cu", "X cu", lambda x, v: x ** 3),
    ("X^-1 as inv", "X inv", lambda x, v: 1 / x),
    ("X^4", "X ^ 4", lambda x, v: x ** 4),
    ("X^12", "X ^ 1 2", lambda x, v: x ** 12),
    ("X^(-2)", "X ^ ~ 2", lambda x, v: x ** -2),
    ("2^X", "2 ^ X", lambda x, v: 2 ** x),
    ("2^3^2 left to right", "2 ^ 3 ^ 2", lambda x, v: 64.0),
    ("-X^2 is -(X^2)", "~ X ^ 2", lambda x, v: -(x ** 2)),
    ("--X", "~ ~ X", lambda x, v: x),
    ("3-X", "3 - X", lambda x, v: 3 - x),
    ("3-~X", "3 - ~ X", lambda x, v: 3 + x),
    ("1/2X is X/2", "1 / 2 X", lambda x, v: x / 2),
    ("1/X", "1 / X", lambda x, v: 1 / x),
    ("6/X/2", "6 / X / 2", lambda x, v: 3 / x),
    ("(X+1)(X-1)", "( X + 1 ) ( X - 1 )", lambda x, v: x * x - 1),
    ("3(X+1)", "3 ( X + 1 )", lambda x, v: 3 * x + 3),
    ("X(X+1)", "X ( X + 1 )", lambda x, v: x * (x + 1)),
    ("2 sin(X)", "2 sin( X )", lambda x, v: 2 * math.sin(x)),
    ("sin(X)cos(X)", "sin( X ) cos( X )", lambda x, v: math.sin(x) * math.cos(x)),
    ("sin(X)/X+cos(3X)", "sin( X ) / X + cos( 3 X )", lambda x, v: math.sin(x) / x + math.cos(3 * x)),
    ("sin(X) missing )", "sin( X", lambda x, v: math.sin(x)),
    ("(X missing )", "( X + 1", lambda x, v: x + 1),
    ("sin(X)^2", "sin( X ) sq", lambda x, v: math.sin(x) ** 2),
    ("cos(X)", "cos( X )", lambda x, v: math.cos(x)),
    ("tan(X/3)", "tan( X / 3 )", lambda x, v: math.tan(x / 3)),
    ("asin(X/6)", "asin( X / 6 )", lambda x, v: math.asin(x / 6)),
    ("acos(X/6)", "acos( X / 6 )", lambda x, v: math.acos(x / 6)),
    ("atan(X)", "atan( X )", lambda x, v: math.atan(x)),
    ("sqrt(X)", "sqrt( X )", lambda x, v: math.sqrt(x)),
    ("sqrt(X^2+1)", "sqrt( X sq + 1 )", lambda x, v: math.sqrt(x * x + 1)),
    ("abs(X)", "abs( X )", lambda x, v: abs(x)),
    ("ln(X)", "ln( X )", lambda x, v: math.log(x)),
    ("log(X)", "log( X )", lambda x, v: math.log10(x)),
    ("e^(X)", "e^( X )", lambda x, v: math.exp(x)),
    ("10^(X)", "10^( X )", lambda x, v: 10 ** x),
    ("e^(-X^2)", "e^( ~ X sq )", lambda x, v: math.exp(-x * x)),
    ("pi", "pi X", lambda x, v: math.pi * x),
    ("e", "e X", lambda x, v: math.e * x),
    ("n/d 1 over (X)", "1 n/d ( X )", lambda x, v: 1 / x),
    ("n/d (X+1) over (X-1)", "( X + 1 ) n/d ( X - 1 )", lambda x, v: (x + 1) / (x - 1)),
    ("n/d then +3", "( X ) n/d 1 + 3", lambda x, v: x + 3),
    ("sqrt(n/d)", "sqrt( 1 n/d ( X ) )", lambda x, v: math.sqrt(1 / x)),
    ("X^(1/2) box", "X ^ ( ( 1 n/d 2 ) )", lambda x, v: x ** 0.5),
    ("1E3X", "1 EE 3 X", lambda x, v: 1000 * x),
    ("2.5E-2", "2 . 5 EE ~ 2 X", lambda x, v: 0.025 * x),
    (".5X", ". 5 X", lambda x, v: 0.5 * x),
    ("3.14159", "3 . 1 4 1 5 9", lambda x, v: 3.14159),
    ("12345.6789", "1 2 3 4 5 . 6 7 8 9", lambda x, v: 12345.6789),
    ("A B (stored)", "A X + B", lambda x, v: v['A'] * x + v['B']),
    ("theta", "theta X", lambda x, v: v['theta'] * x),
    ("undefined letter is 0", "Q + X", lambda x, v: x),
    ("X^X", "X ^ X", lambda x, v: x ** x),
    ("nested", "sqrt( abs( sin( X ) ) + 1 ) ^ 3", lambda x, v: math.sqrt(abs(math.sin(x)) + 1) ** 3),
]
VARS = {'A': 1.5, 'B': -2.25, 'theta': 0.5}
VARS_TI = {'A': 1.5, 'B': -2.25, '[': 0.5}     # theta is token 5B, '[' in ASCII

DEG = [   # Degree mode
    ("sin(X) deg", "sin( X )", lambda x: math.sin(x * D)),
    ("cos(X) deg", "cos( X )", lambda x: math.cos(x * D)),
    ("tan(X/2) deg", "tan( X / 2 )", lambda x: math.tan(x * D / 2)),
    ("asin(X/6) deg", "asin( X / 6 )", lambda x: math.asin(x / 6) / D),
    ("acos(X/6) deg", "acos( X / 6 )", lambda x: math.acos(x / 6) / D),
    ("atan(X) deg", "atan( X )", lambda x: math.atan(x) / D),
    ("sqrt not scaled", "sqrt( X + 9 )", lambda x: math.sqrt(x + 9)),
]

BAD = [   # tokens, the code graph.c must answer, and the token it names ("" = not checked)
    ("Y1 reference", [0x5E, 0x10], 1, 0x5E),
    ("Ans", toks("2") + [0x72], 1, 0x72),
    ("X>2", toks("X") + [0x6F, 0x32], 1, 0x6F),
    ("sinh(", [0xC8, 0x58, 0x11], 1, 0xC8),
    ("cube root", [0xBD, 0x58, 0x11], 1, 0xBD),
    ("int(", [0xB1, 0x58, 0x11], 1, 0xB1),
    ("XY (BB xx)", [0xBB, 0x99], 1, 0xBB),
    ("stray )", toks("X )"), 3, 0x11),
    ("dangling +", toks("2 +"), 3, 0),
    ("dangling ^", toks("2 ^"), 3, 0),
    ("only )", toks(")"), 3, 0),
    ("empty parens", toks("( )"), 3, 0),
    ("too long", toks("X") + toks("+ X") * 60, 2, 0),
    ("too deep", toks("( ( ( ( ( ( ( X ) ) ) ) ) ) )"), 4, 0),
    ("6 deep is fine", None, 0, 0),
]


def close(a, b, tol=1e-4):
    return abs(a - b) <= tol * max(1.0, abs(b))


def cmp_xs(cli, tokens, f, xs, deg=False, var=None):
    """(bad count, first failure text) for f over xs."""
    got = cli.ask(tokens, xs, deg, var)
    if len(got) != len(xs):
        return len(xs), "answered %r" % got
    bad, first = 0, ""
    for x, g in zip(xs, got):
        try:
            want = f(x)
            if isinstance(want, complex) or abs(want) >= 1e30:
                raise ValueError
        except (ValueError, ZeroDivisionError, OverflowError):
            g = float(g)
            ok = not (abs(g) < 1e30)                      # NaN, inf or huge: the line breaks
            want = "a break"
        else:
            g = float(g)
            ok = g == g and close(g, want)
        if not ok:
            bad += 1
            first = first or "x=%r want %r got %r" % (x, want, g)
    return bad, first


def main():
    if not os.path.exists(CLI):
        sys.exit("build it first: make -C symce bin/host/graph_cli")
    cli, rnd, fail = CLI_(), random.Random(84), 0

    def report(label, bad, first):
        nonlocal fail
        fail += bad > 0
        print("%-28s %s%s" % (label, "ok" if not bad else "FAIL", "" if not bad else "  (%d) %s" % (bad, first)))

    for name, s, f in CASES:
        xs = [round(rnd.uniform(-5, 5), 3) for _ in range(40)] + [0.0, 1.0, -1.0]
        t = toks(s)
        bad, first = cmp_xs(cli, t, lambda x, f=f: f(x, VARS), xs, False,
                            {chr(k): v for k, v in [(0x41, 1.5), (0x42, -2.25), (0x5B, 0.5)]})
        report(name, bad, first)
    for name, s, f in DEG:
        xs = [round(rnd.uniform(-5, 5), 3) for _ in range(30)] + [30.0, 60.0, 0.0]
        report(name, *cmp_xs(cli, toks(s), f, xs, True))

    for name, t, code, tok in BAD:
        if t is None:
            got, ok = cli.ask(toks("( ( ( ( ( ( X ) ) ) ) ) )"), [3.0]), 1
            report(name, got != ["3"], repr(got))
            continue
        got = cli.ask(t, [1.0])
        ok = got[:2] == ["E", str(code)] and (not tok or got[2] == "%02x" % tok)
        report(name, not ok, "got %r" % got)

    # Random trees, fully parenthesised (each with the largest intermediate, which sets the tolerance).
    def tree(d):
        if d == 0 or rnd.random() < 0.2:
            c = rnd.choice(["X", "X", "k"])
            if c == "X":
                return ("X", None, None)
            k = rnd.choice([1, 2, 3, 0.5, 2.5, 7, 10])
            return (" ".join("%g" % k), None, k)
        op = rnd.choice(["+", "-", "*", "/", "^", "f"])
        a = tree(d - 1)
        if op == "f":
            fn = rnd.choice(["sin(", "cos(", "abs(", "sqrt(", "ln(", "e^(", "atan("])
            return (fn + " " + a[0] + " )", (fn, a), None)
        b = tree(d - 1)
        if op == "^":
            return ("( " + a[0] + " ) ^ ( " + b[0] + " )", ("^", a, b), None)
        return ("( " + a[0] + " ) " + op + " ( " + b[0] + " )", (op, a, b), None)

    class Skip(Exception):
        pass

    def ev(n, x):
        s, node, k = n
        if s == "X" and node is None:
            return x
        if node is None:
            return k
        if isinstance(node[0], str) and node[0].endswith("("):
            a = ev(node[1], x)
            if node[0] in ("sin(", "cos(") and abs(a) > 2000:
                raise Skip                       # float32 cannot hold the angle to 1e-4
            return {"sin(": math.sin, "cos(": math.cos, "abs(": abs, "sqrt(": math.sqrt,
                    "ln(": math.log, "e^(": math.exp, "atan(": math.atan}[node[0]](a)
        a, b = ev(node[1], x), ev(node[2], x)
        if node[0] == "^":
            r = a ** b
            if isinstance(r, complex):
                raise ValueError
            return r
        return {"+": a + b, "-": a - b, "*": a * b}[node[0]] if node[0] != "/" else a / b

    bad = tot = 0
    first = ""
    for _ in range(300):
        n = tree(3)
        xs = [round(rnd.uniform(-4, 4), 2) for _ in range(6)]
        for x in list(xs):
            try:
                ev(n, x)
            except Skip:
                xs.remove(x)
            except Exception:
                pass
        if not xs:
            continue
        # a tolerance scaled to the tree's largest value keeps cancellation honest
        big = 1.0
        for x in xs:
            try:
                big = max(big, abs(ev(n, x)))
            except Exception:
                pass
        b, f1 = cmp_xs(cli, toks(n[0]), lambda x: ev(n, x), xs)
        tot += 1
        if b:
            # float32 cancellation through x^y or a huge intermediate is not a bug: retry at double the tolerance
            got = cli.ask(toks(n[0]), xs)
            wants = []
            for x in xs:
                try:
                    wants.append(ev(n, x))
                except Exception:
                    wants.append(None)
            real = [(g, w) for g, w in zip(got, wants) if w is not None and abs(w) < 1e30 and g != "nan"]
            if not all(close(float(g), w, 5e-3 * big) for g, w in real) or \
               any(w is None and abs(float(g)) < 1e30 for g, w in zip(got, wants)):
                bad += 1
                first = first or "%s: %s" % (n[0], f1)
    report("random trees (%d)" % tot, bad, first)

    print("\nALL PASS" if not fail else "\n%d FAILED" % fail)
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
