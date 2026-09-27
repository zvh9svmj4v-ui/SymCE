#!/usr/bin/env python3
"""The shipped engine, run on the real ROM, against its host build.

    python3 engine_device.py [N] [SEED]

engine_check.py proves engine.c on the host. The calculator runs a different
compile of it -- ez80-clang, 24-bit int, libcrt's 32-bit helpers -- so this
links THAT compile (symce/obj/engine/engine.s, byte for byte what the app
carries) into engtest/, runs it over N entries on the real OS, and requires
the exact bytes the host build gives for every one, refusals included.

Needs `make` in symce first (for engine.s) and the ROM, like the other tests.
"""
import glob, os, random, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import screen
from engine_check import structured, tree, render, ti_real, ALPHABET, MAXLEN
from fractions import Fraction
from ez80sim import engine, Err

TOK = {'+': [0x70], '-': [0x71], '*': [0x82], '/': [0x83], '^': [0xF0],
       '(': [0x10], ')': [0x11], '~': [0xB0], '²': [0x0D], '³': [0x0F],
       'θ': [0x5B], '.': [0x3A], '⁄': [0xEF, 0x2E], '@': [0x72],
       '⁻': [0x0C], '¹': [],                # x⁻¹ is one token
       '√': [0xBC],                         # √( is one token: √8) is √(8)
       '=': [0x6A], ',': [0x2B], 'e': [0xBB, 0x31], 'ᴇ': [0x3B]}
NAMES = {'logBASE(': [0xEF, 0x34], 'sin(': [0xC2], 'cos(': [0xC4], 'tan(': [0xC6],
         'ln(': [0xBE], 'log(': [0xC0], 'e^(': [0xBF]}     # lower case: no clash with A-Z

# Where a 24-bit int or a wrong 32-bit helper would bite: coefficients at the
# 999999 limit, products past 2^23 and 2^31, gcds of big denominators, the
# term, depth, power and width limits, and the n/d bar.
EDGE = ["999999X+1", "999999X^7-999999X^6", "(999X+1)(999X-1)", "(707X+707)^2",
        "46341X*46341X", "8388607X", "8388608X-8388607X", "16777216X",
        "2147483647X", "123456789X", "(X+1)^6", "((X+1)/(X+2))^3", "(X+1)^7",
        "(X+1)^19", "(X+1)^31",
        "(2X+3)^5", "(X+Y+1)^4", "(X-Y)^3", "X/7+X/11+X/13", "(X/3+1/7)^3",
        "999999/999998X", "999999X/999999", "X/999999+X/999998",
        "(X+1)^7(Y+1)^2", "((((((X))))))", "(((((((X)))))))", "~~~~X",
        "X^0", "X^31", "X²³", "X³²", "1/2X", "2X⁄4", "(X+1)⁄(2)", "X⁄0", "X/0",
        "0X", "0/X", "ABCDEF", "ABCDEFG", "θ+θ", "A+B+C+D+E+F-A-B-C-D-E-F",
        "(A+B)(C+D)(E+F)", "(3X+4Y)^2-(3X-4Y)^2", "X" + "+X" * 31,
        "X" + "+X" * 32, "", "2+2", "X.5",
        "(X²-1)/(X-1)", "(X^7-1)/(X-1)", "(X+1)^7/(X+1)^6", "X^7/X^7", "1/X",
        "X/(X+1)", "(2X+3)^5/(2X+3)^4", "(X³-Y³)/(X-Y)", "(999999X²+X)/X",
        "(X+1)(X+2)(X+3)/(X²+3X+2)", "X/0X",
        "(X²-Y²+X-Y)/(X²-Y²)", "(X²-Y²)/(Xθ+Yθ+X+Y)", "(X³-Y³+X²-Y²)/(X²-2XY+Y²)",
        # fractions: Euclid, content and the 24-bit int on the way
        "1/X+1/X", "(X²+5X+6)/(X²-4)", "1/X+1/Y", "X^~7", "X^~8", "(X+Y)/(X-Y)",
        "(999X+1)/(999X²+1000X+1)", "1/(999999X+1)+1/(999998X+1)",
        "(707X²-707)/(707X+707)", "1/(X+1)^3", "(X/(X+1))^~3", "3/(2X)",
        "1/(1-X)", "(X^7-1)/(X^6-1)", "(XY+X)/(Y²-1)", "(X²-Y²)/(X+Y)", "(X+Y)/(X²-Y²)", "1/(X+Y)+1/(X-Y)",
        "(X²Y+XY²)/(X²Y²+XY)", "(X+Y)/(X²+Y²)", "(XY+X+Y+1)/(X+1)", "12/(X+4)+24-X/B",
        "(X²-Y²)/(X²+2XY+Y²)", "(600000X³-600000X²Y-600000XY²+600000Y³)/(600000X+600000Y)",
        "1/X^4+1/X^4", "(X+Y)/(XY+1)+1", "(X+Y²)/(X²Y+XY)", "1/(X-X)", "0^~1X",
        "(2X+1)/(4X²-1)+1/(2X-1)", "(X+1)⁄(X²-1)",
        "(X³-Y³)/(X²-Y²)", "(6X²+XY-Y²)/(4X²-Y²)", "1/(X²-Y²)+1/(X²-2XY+Y²)",
        "(X²+X-Y²+Y)/(X²+2XY+Y²+X+Y)", "(A²-θ²)/(A²-2Aθ+θ²)", "(X^7-Y^7)/(X^6-Y^6)",
        "X⁻¹", "(X+1)⁻¹", "2X⁻¹", "X²⁻¹", "X^2⁻¹", "X⁻¹⁻¹", "(X-X)⁻¹", "0⁻¹X",
        # roots: sqfree, the gcd in rmul, conjugates, near the limit
        "√8)", "√999999)", "√999998X)", "√999999)√999998)", "(1+√999999))^3",
        "√999999X)√999999X)", "√(X³)", "√8X³)", "√(3/4)", "√(Y^4/X)", "√X)√X)",
        "1/(1+√2))", "X/(X+√2))", "1/(√2)+√3))", "1/(X+√X))", "(√2)+√3))²",
        "√12X)+√27X)", "(X+√2))(X-√2))", "√2)+.5", "√X)√Y)", "√(X²)", "√(X+1)",
        "√(~4)", "√(XY)", "(√2)+√3)+√5))^4", "1/(√6)+√10)+√15))",
        # commands: int64 in the root and Kronecker searches, errors included
        "FACTOR(X²-4)", "FACTOR(X^4-1)", "FACTOR(X²+X-1)", "FACTOR(12)", "FACTOR(~12/7)",
        "FACTOR(X^6-1)", "FACTOR(X^4+X²+1)", "FACTOR(6X²-X-2)", "FACTOR(X³-8)",
        "FACTOR(X²-Y²)", "FACTOR(4X³Y-8X²Y²)", "FACTOR(AX+AY+BX+BY)", "FACTOR((X²-1)/(X+2))",
        "FACTOR(X^4-5X²+6)", "FACTOR(999999X²-999999)", "FACTOR(X^7-X)", "FACTOR(0)",
        "FACTOR(X²+1)", "FACTOR(999999X^7+1)", "FACTOR(720X^6-1)", "FACTOR(X²-2X+1-Y²)",
        "SOLVE(X²=4,X)", "SOLVE(X²=2,X)", "SOLVE(X²+X=1,X)", "SOLVE(X³-X=0,X)",
        "SOLVE(6X²-X-2=0,X)", "SOLVE(X²+1=0,X)", "SOLVE(X=X,X)", "SOLVE(X+2Y=3,X)",
        "SOLVE(X+2Y=3,Y)", "SOLVE(AX+B=0,X)", "SOLVE(X³=2,X)", "SOLVE(X√2)=1,X)",
        "SOLVE(X²+2X=1,X)", "SOLVE(999999X²=2,X)", "SOLVE(X+2=4,X)", "SOLVE(Y+4=8,Y)",
        "SOLVE(X²-4)", "SOLVE(X²=4)", "SOLVE(X²-4,X)", "SOLVE(X=4,2)", "SOLVE(X=4,",
        "DERIV(X³)", "DERIV(1/X)", "DERIV(√X))", "DERIV(X²Y,Y)", "DERIV(1/(X+1))",
        "DERIV(5)", "DERIV(X^7-X)", "EXPAND((X+1)²)", "EXPAND((X+Y)^3)",
        "EXPAND(X(X-1)(X+1))", "FACTOR(", "SOLVE(,X)", "DERIV(X,2)",
        # the Algebra menu's: lists, i, int64 floats in NROOTS
        "POLYROOTS(X²-2)", "POLYROOTS(2X²-3X+1)", "POLYROOTS(X²+1)", "POLYROOTS(AX+B)",
        "POLYROOTS(0)", "POLYROOTS(1/X)", "CPOLYROOTS(X^4+4)", "CPOLYROOTS(X²+X+1)",
        "CZEROS(X²+2X+5)", "CSOLVE(X²+2X+5=0,X)", "CSOLVE(X²=~4,X)", "CFACTOR(X²+1)",
        "CFACTOR(X²+X+1)", "CFACTOR(X²+Y²)", "NROOTS(X²-2)", "NROOTS(X³-3X+1)",
        "NROOTS(X²+X+1)", "NROOTS(X²-2000X+999999)", "NROOTS((X²+1)²)", "NROOTS(X^5-X-1)",
        "NROOTS(999999X^7+X-1)", "POLYREMAINDER(X³+2X+1,X²+1)", "POLYQUOTIENT(X²,2X+1)",
        "POLYQUOTIENT(X²,0)", "POLYQUOTIENT(X²,AX+1)", "POLYGCD(4X+4,6X+6)", "POLYGCD(12,18)",
        "POLYGCD(X²-Y²,X+Y)", "POLYGCD(X)", "POLYGCD(999999X²-999999,999998X+999998)",
        "POLYCOEFFS(AX²+BX+C)", "POLYCOEFFS(X/Y+1)", "POLYCOEFFS(0)", "POLYDEGREE(XY²,Y)",
        "POLYDEGREE(1/X)", "POLYDEGREE(0)", "COMDENOM(1/X+1/Y)", "COMDENOM(1/(X+1)+1/(X-1))",
        "LEFT(X²+1=3X)", "RIGHT(X²+1=3X)", "LEFT(X²)",
        # Convert Expression and Trigonometry: kernels, letter-cos, the widths
        "TEXPAND(sin(2X))", "TEXPAND(cos(A+B))", "TEXPAND(sin(3θ))", "TEXPAND(tan(2X))",
        "TEXPAND(cos(3X))", "TEXPAND(sin(7X))", "TEXPAND(sin(X/2+1))", "TCOLLECT(sin(X)cos(X))",
        "TCOLLECT(cos(X)²)", "TCOLLECT(sin(A)cos(B))", "TCOLLECT(cos(X)^4)", "TCOLLECT(sin(X)³)",
        "TOSIN(cos(X)²)", "TOCOS(sin(X)^4)", "TOSIN(tan(X))", "TOSIN(cos(X)³)", "TOEXP(sin(X))",
        "TOEXP(tan(X))", "TOEXP(1/sin(X))", "TOLN(log(X))", "TOLN(logBASE(X,3))", "TOLN(log(~2))",
        "TOLN(log(XY))", "TOLOGBASE(ln(X),5)", "TOLOGBASE(logBASE(10,3)-logBASE(5,5),5)",
        "TOLOGBASE(ln(X))", "TOLOGBASE(ln(X),1)", "TOLOGBASE(ln(X),e)", "sin(X)", "2X+sin(X)",
        # the Calculus menu's: kernels' derivatives, substitution, the errors
        "DERIV(sin(X))", "DERIV(tan(X))", "DERIV(log(X))", "DERIV(logBASE(X,2))", "DERIV(e^(2X))",
        "DERIV(sin(X)/X)", "DERIV(X³,X,2)", "DERIV(tan(X),X,7)", "DERIVAT(e^(X),X,1)",
        "DERIVAT(√X),X,4)", "DERIVAT(1/X,X,0)", "TANGENTLINE(X²,X,1)", "NORMALLINE(X²,X,1)",
        "NORMALLINE(X²,X,0)", "TAYLOR(sin(X),X,5)", "TAYLOR(ln(X),X,3,1)", "TAYLOR(√X),X,2,1)",
        "IMPDIF(X³+Y³=6XY,X,Y)", "IMPDIF(X²+Y²=1,X,Y,2)", "CENTRALDIFF(1/X,X,H)", "INTEGRAL(X,X)",
        # its second half: partial fractions, the sums' 32-bit binomials, 1ᴇ99 for ∞
        "INTEGRAL(X²,X,0,3)", "INTEGRAL(1/(X(X+1)²),X)", "INTEGRAL(X/(X²-4),X,0,1)",
        "INTEGRAL(Xsin(2X),X)", "INTEGRAL(e^(X)+1/X,X)", "INTEGRAL(1/X,X,~1,1)", "INTEGRAL(1/(X²+1),X)",
        "LIMIT((sin(X)-X)/X³,X,0)", "LIMIT((X²-1)/(X-1),X,1)", "LIMIT(1/X,X,0,~1)", "LIMIT(1/X,X,0)",
        "LIMIT((2X²+1)/(X²-3),X,1ᴇ99)", "SUM(X²,X,1,N)", "SUM(X,X,1,1000)", "SUM(e^(X),X,1,9)",
        "SUM(1/(X(X+1)),X,1,99)", "SUM(X,X,3,1)", "PRODUCT((X+1)/X,X,1,99)", "PRODUCT(X,X,1,13)",
        "FMIN(X²-2X,X)", "FMAX(X³-3X,X,~3,3)", "FMIN(X³,X)", "ARCLEN(X²,X,0,1)", "ARCLEN(2X√X)/3,X,0,3)",
        "SERIES(1/sin(X),X,3)", "SERIES(tan(X),X,5)", "DOMINANTTERM((2X³+X)/(X-1),X,1ᴇ99)",
        # the Geometry menu's: points parsed and backtracked, lines, Cramer, roots,
        # degrees mod 360 in a 24-bit int, products at the 999999 limit
        "MIDPOINT((1,2),(4,7))", "MIDPOINT((A,B),(C,D))", "DISTANCE((1,2),Y=2X+3)",
        "DISTANCE((999999,0),(0,999999))", "SLOPE(2X+3Y=6)", "LINE((1,2),(1,5))", "LINE((1,2),3)",
        "PERPENDICULAR((1,2),Y=3X+1)", "INTERSECT(2X+3Y=7,X-Y=1)", "INTERSECT(Y=X,2Y=2X)",
        "INTERSECT(Y=X²,Y=1)", "PARTITION((0,0),(10,5),2,3)", "AREA((0,0),(4,0),(4,3),(3,5),(1,5),(0,3))",
        "PERIMETER((0,0),(1,0),(0,1))", "CIRCUMCENTER((0,0),(4,0),(0,6))", "ORTHOCENTER((0,0),(4,0),(1,3))",
        "INCENTER((0,0),(1,0),(0,1))", "HYPOT(999,999)", "LEG(3,5)", "CIRCLE((0,0),(2,0),(0,2))",
        "CIRCLE(X²+Y²-X+Y=1)", "REFLECT((1,0),X+Y=2)", "ROTATE((1,2),60)", "ROTATE((3,4),~450,(1,1))",
        "ROTATE((1,0),20)", "DILATE((2,3),1/2,(1,1))",
        # √ as a kernel: its bits in a 32-bit rad, |X|, the squared SOLVE and its
        # sign checks, INTEGRAL's u = aX+b, a root under a root refused
        "√(X²)", "√(8X²)", "√(999999X+999999)", "√(X²+2X+1)", "1/(√(X+1)+1)", "abs(~X)",
        "√(X+1)^3", "√(√(2)-X)", "DERIV(√(X+1),X)", "DERIV(abs(X),X)", "TAYLOR(√(X+1),X,2)",
        "LIMIT((√(X+1)-2)/(X-3),X,3)", "INTEGRAL(√(3-2X),X,0,1)", "INTEGRAL(1/√(X+1)+X,X)",
        "INTEGRAL(X√(X+1),X)", "SOLVE(√(X+1)=X,X)", "SOLVE(√(X)=~3,X)", "SOLVE(abs(X-1)=2X,X)",
        "SOLVE(abs(X²-4)=5,X)", "SOLVE(√(X²)=X,X)", "DISTANCE((X,0),(1,0))"]
DEGREE = ["TOEXP(sin(X))", "TEXPAND(sin(3θ))", "TOLN(log(X))",    # MODE DEGREE, once each
          "DERIV(sin(X))", "DERIV(e^(X))", "CENTRALDIFF(cos(X),X,1)", "INTEGRAL(sin(X),X)"]
PART = 40000                            # vector bytes per program: see main()
CMDS = [b'EXPAND\x10', b'FACTOR\x10', b'SOLVE\x10', b'DERIV\x10', b'POLYROOTS\x10', b'CPOLYROOTS\x10',
        b'CFACTOR\x10', b'POLYCOEFFS\x10', b'POLYDEGREE\x10', b'COMDENOM\x10', b'TCOLLECT\x10']

# (entry, what Ans is), @ standing for the Ans token: the answer is substituted
# in, so its width and its own limits meet the entry's. A Fraction is the OS's
# own result, a TI real: the BCD decode is new ez80 code.
EDGE_ANS = [("@*3", "4X"), ("@", "4X"), ("@²", "999999X"), ("@^7", "X+1"),
            ("@(@)", "X/7+1/3"), ("@/999999", "999999X"), ("@+@", ""),
            ("2+2", "4X"), ("@-@", "~X³"), ("3@", "X"), ("@^3", "(X+Y+1)^2"),
            ("@X", Fraction(4)), ("@X", Fraction(-1, 4)), ("@X", Fraction(0)),
            ("@X", Fraction(999999)), ("@X^7", Fraction(123, 100000)),
            ("@X", Fraction(1000000)), ("@X", Fraction(1, 1000000)),
            ("@²X", Fraction(-999, 1000)), ("X/@", Fraction(25, 10)),
            ("@+1", "X/(X+1)"), ("1/@", "X/(X+1)"), ("@²", "1/(2X)"),
            # SymCE's Ans is never the OS's: a number is answered, a list refused
            ("@+1", "POLYDEGREE(X³)"), ("@*3", "√8)"), ("@*3", "POLYROOTS(X²-4)"), ("@", "CZEROS(X²+1)"),
            # a function answer is only for a command: its letter-cos read back
            ("TCOLLECT(@)", "TEXPAND(sin(2X))"), ("@*2", "TEXPAND(sin(2X))"),
            ("TOLOGBASE(@,10)", "TOLN(log(X))"),
            # √ and abs( read back from Ans: the kernel is made again
            ("@²", "√(X+1)"), ("@*@", "DISTANCE((X,0),(1,0))"), ("SOLVE(@=2,X)", "√(4X²)")]


def last(a):
    """What the hook keeps at lastAns for Ans = a: tokens, a real, or nothing."""
    if isinstance(a, Fraction):
        return b'\xff' + ti_real(a)
    return bytes([len(a)]) + a


def tok(s):
    out = []
    while s:
        n = next((k for k in NAMES if s.startswith(k)), None)
        if n:
            out, s = out + NAMES[n], s[len(n):]
            continue
        c, s = s[0], s[1:]
        out += TOK.get(c, [ord(c)] if c not in '0123456789' else [0x30 + int(c)])
    return bytes(out)


def cases(n, seed):
    rnd = random.Random(seed)
    out = [(tok(e), last(b'')) for e in EDGE]
    out += [(tok(e), last(a if isinstance(a, Fraction) else engine(tok(a)) if a else b''))
            for e, a in EDGE_ANS]
    while len(out) < n:
        r = rnd.random()
        if r < 0.45:
            t = bytes(render(tree(rnd, rnd.randint(1, 4)), rnd))
        elif r < 0.85:
            t = structured(rnd)
        else:
            t = bytes(rnd.choice(ALPHABET) for _ in range(rnd.randint(1, 12)))
        if rnd.random() < 0.15:                  # a command around it
            t = rnd.choice(CMDS) + t + b'\x11'
        if len(t) > MAXLEN:
            continue
        # A quarter use Ans: one of their variables replaced by it, standing
        # for an earlier answer.
        prev = [a for a in map(lambda v: engine(*v), out[-8:]) if a] \
            if rnd.random() < 0.25 else []
        spots = [k for k, b in enumerate(t) if 0x41 <= b <= 0x5B]
        if prev and spots:
            k = rnd.choice(spots)
            out.append((t[:k] + b'\x72' + t[k + 1:], last(rnd.choice(prev))))
        else:
            out.append((t, last(b'')))
    return out


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 960
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 20260926
    # The edge cases in every mode the hook passes: Classic, MathPrint (wider
    # answers fit), and each with MODE ANSWERS: DEC; the rest take turns.
    modes = (0, 0x20, 0x01, 0x21)
    edge = len(EDGE) + len(EDGE_ANS)
    vecs = [(t, p, m, engine(t, p, m)) for i, (t, p) in enumerate(cases(n, seed))
            for m in (modes if i < edge else (modes[i % 4],))]
    vecs += [(tok(t), last(b''), 0x04, engine(tok(t), last(b''), 0x04)) for t in DEGREE]
    # an error screen: FF, then the message's length and bytes
    enc = [p + bytes([m, len(t)]) + t
           + (bytes([0xFF, len(a)]) if isinstance(a, Err) else bytes([len(a)])) + a
           for t, p, m, a in vecs]
    # The engine and its vectors are one program, run from free RAM: 147 KB
    # ran, 157 KB never started (the engine is 107 KB). So the vectors go in
    # parts of under PART bytes, one program each.
    parts, size = [[]], 0
    for i, e in enumerate(enc):
        if size + len(e) >= PART:
            parts, size = parts + [[]], 0
        parts[-1].append(i)
        size += len(e)

    answered = sum(1 for *_, a in vecs if a and not isinstance(a, Err))
    thrown = sum(1 for *_, a in vecs if isinstance(a, Err))
    with_ans = sum(1 for _, p, _, a in vecs if p[0] and a)
    wide = sum(1 for *_, a in vecs if len(a) > 26)
    print("%d vectors (%d answered by the host build, %d of them through Ans, %d past 26 tokens, "
          "%d error screens), %d bytes in %d parts" % (len(vecs), answered, with_ans, wide, thrown,
                                                       sum(map(len, enc)), len(parts)))
    env = dict(os.environ, PATH=os.path.expanduser('~/CEdev/bin') + os.pathsep + os.environ['PATH'])
    deep = 0
    for part in parts:
        blob = b''.join(enc[i] for i in part) + b'\xfe'
        with open(os.path.join(HERE, 'engtest/src/vectors.h'), 'w') as f:
            f.write('/* Generated by tools/emu/engine_device.py -- do not edit. */\n')
            f.write('static const uint8_t vectors[] = {\n')
            for i in range(0, len(blob), 20):
                f.write('    ' + ','.join('0x%02X' % b for b in blob[i:i + 20]) + ',\n')
            f.write('};\n')
        b = subprocess.run(['make', '-C', os.path.join(HERE, 'engtest')], env=env,
                           capture_output=True, text=True)
        if b.returncode:
            sys.exit(b.stdout[-2000:] + b.stderr[-2000:])
        # Each '.' is 2s of emulated time; the report says how far it got if short.
        r = screen.read([("report", 0xD0EE40, 46)],
                        keys="apps down enter . clear . L" + " ." * 150,
                        files=tuple(sorted(glob.glob(os.path.join(HERE, "engtest/bin/ENGTEST.8xp*")))),
                        launch="ENGTEST", lead=False)["report"]
        magic, ran = r[:4], int.from_bytes(r[4:7], 'little')
        bad, first = int.from_bytes(r[7:10], 'little'), int.from_bytes(r[10:13], 'little')
        if magic != b'DONE':
            sys.exit("calculator did not finish: magic %r after %d vectors" % (magic, ran))
        if ran != len(part):
            sys.exit("calculator ran %d of %d vectors" % (ran, len(part)))
        if bad:
            t, _, _, want = vecs[part[first]]
            got = r[14:14 + min(r[13], 26)]
            sys.exit("%d MISMATCHES; first #%d entry %s host %s calculator %s"
                     % (bad, part[first], t.hex(), want.hex() or 'refuse', got.hex() or 'refuse'))
        if int.from_bytes(r[40:43], 'little') > deep:
            deep, at = int.from_bytes(r[40:43], 'little'), vecs[part[int.from_bytes(r[43:46], 'little')]][0]
    # ENGTEST paints from 3,900 bytes below its frame: all of it gone may be an overflow
    print("stack: at most %d bytes below the caller's frame%s, for %s"
          % (deep, ' (ALL, so maybe more)' if deep >= 3900 else '', at.hex()))
    print("ALL MATCH: the calculator's engine gives the host's bytes for every entry")
    return 0


if __name__ == '__main__':
    sys.exit(main())
