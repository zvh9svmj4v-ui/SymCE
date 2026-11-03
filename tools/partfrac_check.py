"""PARTFRAC(N/D,X) against sympy: fixed vectors, then random rational functions;
the answer must equal N/D and be a polynomial plus terms r/f^k with f an
irreducible factor of D and deg r < deg f. Usage: partfrac_check.py [N]"""
import sys, random, re
import sympy
from sympy.parsing.sympy_parser import parse_expr, standard_transformations, implicit_multiplication_application
from ineqlib import Eng

FIXED = [
    ('1/(X²-1)', '1/(2(X-1))-1/(2(X+1))'), ('(X+3)/(X³-X)', '-3/X+2/(X-1)+1/(X+1)'),
    ('X/((X-1)(X²+1))', '1/(2(X-1))-(X-1)/(2(X²+1))'), ('(X²+1)/(X²-1)', '1+1/(X-1)-1/(X+1)'),
    ('X³/(X²-1)', 'X+1/(2(X-1))+1/(2(X+1))'), ('1/(X²(X+1))', '-1/X+1/X²+1/(X+1)'),
    ('1/(X-1)', '1/(X-1)'), ('1/(X²+1)', '1/(X²+1)'), ('5', '5'),
    ('1/(2X-2)-1/(3X+3)', '1/(2(X-1))-1/(3(X+1))'), ('X/((X-1)²(X²+1))', '1/(2(X-1)²)-1/(2(X²+1))'),
    ('1/(X³+1)', '1/(3(X+1))-(X-2)/(3(X²-X+1))'),
]
X = sympy.Symbol('X')
TR = standard_transformations + (implicit_multiplication_application,)


def parse(t):
    t = re.sub(r'(?<=[)X\d])²', '**2', t).replace('³', '**3').replace('^', '**')
    return parse_expr(t, local_dict={'X': X}, transformations=TR)


def text(e):
    s = str(sympy.expand(e)).replace(' ', '').replace('**2', '²').replace('**3', '³').replace('**', '^')
    return s


def main(n):
    eng = Eng('M')                       # MathPrint: nothing is too wide
    bad = 0
    for q, want in FIXED:
        o = eng.ask('PARTFRAC(' + q + ',X)')
        if o != want:
            bad += 1; print('FIXED', q, o, want)
    rnd = random.Random(11)
    seen = 0
    for _ in range(n):
        D = 1
        for _ in range(rnd.randint(1, 3)):
            r = rnd.random()
            D *= (X - rnd.randint(-3, 3)) if r < .55 else (X ** 2 + rnd.randint(1, 3)) if r < .8 else (2 * X + rnd.choice([1, 3]))
        Nn = sum(rnd.randint(-4, 4) * X ** k for k in range(rnd.randint(0, 4)))
        if Nn == 0 or sympy.gcd(Nn, D) != 1: continue
        D = sympy.expand(D)
        f = Nn / D
        o = eng.ask('PARTFRAC((' + text(Nn) + ')/(' + text(D) + '),X)')
        if o.startswith('E:') or o == 'PASS':
            if not o.startswith('E:SYMCE LIMIT') and not o.startswith('E:TOO'): print('ERR', f, o); bad += 1
            continue
        seen += 1
        ans = parse(o)
        if sympy.simplify(ans - f) != 0:
            bad += 1; print('VALUE', f, o); continue
        for t in sympy.Add.make_args(sympy.together(ans) if False else ans):
            num, den = sympy.fraction(sympy.cancel(t))
            if t.is_polynomial(X): continue            # the polynomial part
            c, fl = sympy.factor_list(den)
            if len(fl) != 1 or sympy.degree(num, X) >= sympy.degree(fl[0][0], X):
                bad += 1; print('SHAPE', f, o, t); break
    print('partfrac_check: %d answered, %s' % (seen, 'ok' if not bad else '%d BAD' % bad))
    return bad


if __name__ == '__main__':
    sys.exit(1 if main(int(sys.argv[1]) if len(sys.argv) > 1 else 200) else 0)
