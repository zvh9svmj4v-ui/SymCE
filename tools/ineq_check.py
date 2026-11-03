"""SOLVE(A op B,X) with < > ≤ ≥ ≠, against sympy: fixed vectors, then random
rational functions, the answer tested at points around each root and at the
roots. Usage: ineq_check.py [N]  (needs symce/bin/host/engine_cli)."""
import sys, random, re
import sympy
from ineqlib import Eng

FIXED = [
    ('X²-4>0', 'X<-2 or X>2'), ('X²-4<0', '-2<X<2'), ('X²-2X-3≤0', '-1≤X≤3'),
    ('X²-2X-3≥0', 'X≤-1 or X≥3'), ('(X-1)²>0', 'X≠1'), ('(X-1)²≤0', 'X=1'),
    ('(X+1)/(X-2)≥0', 'X≤-1 or X>2'), ('1/X<1', 'X<0 or X>1'),
    ('X²>2', 'X<-√(2) or X>√(2)'), ('X≠3', 'X≠3'), ('X(X-1)(X+2)>0', '-2<X<0 or X>1'),
    ('X³-X<0', 'X<-1 or 0<X<1'), ('X²-X-1≤0', '(1-√(5))/2≤X≤(1+√(5))/2'),
    ('2X<1', 'X<1/2'), ('X/2≥3', 'X≥6'), ('X²+1>0', 'E:ALWAYS TRUE|'), ('X²+1<0', 'E:NO SOLUTION|'),
    ('abs(X-1)<3', '-2<X<4'), ('abs(X-1)≤3', '-2≤X≤4'), ('abs(X-1)>3', 'X<-2 or X>4'),
    ('abs(2X+1)≥5', 'X≤-3 or X≥2'), ('abs(X)<0', 'E:NO SOLUTION|'), ('abs(X)≤0', 'X=0'),
    ('abs(X)>0', 'X≠0'), ('abs(X-1)>~1', 'E:ALWAYS TRUE|'), ('abs(X-1)<~1', 'E:NO SOLUTION|'),
    ('2abs(X-1)+1≤7', '-2≤X≤4'), ('~abs(X-1)>~3', '-2<X<4'), ('abs(X²-1)<3', '-2<X<2'),
    ('abs(X)/X>0', 'X>0'), ('abs(X-1)≠3', 'X<-2 or -2<X<4 or X>4'), ('abs(X-1)<3/2', '-1/2<X<5/2'),
    ('abs(X-1)<X', 'E:SYMCE LIMIT|SOLVE'),
]
OPS = ['<', '>', '≤', '≥', '≠']
PY = {'<': lambda a: a < 0, '>': lambda a: a > 0, '≤': lambda a: a <= 0, '≥': lambda a: a >= 0, '≠': lambda a: a != 0}


def truth(ans):
    """the answer text as a function of a sympy number"""
    t = ans.replace('≤', '<=').replace('≥', '>=').replace('≠', '!=')
    t = re.sub(r'(?<![<>!=])=(?!=)', '==', t)
    t = t.replace('√(', 'sqrt(').replace('²', '**2')
    t = re.sub(r'(\d+)/(\d+)', r'Rational(\1,\2)', t)
    t = re.sub(r'\(([^()]*)\)/(\d+)', r'((\1)/\2)', t)
    return lambda x: bool(eval(t.replace('X', '(x)'), {'sqrt': sympy.sqrt, 'x': x, 'Rational': sympy.Rational, 'abs': abs}))


def main(n):
    eng = Eng()
    bad = 0
    for e, want in FIXED:
        o = eng.ask('SOLVE(' + e + ',X)')
        if o != want:
            bad += 1; print('FIXED', e, o, want)
    X = sympy.Symbol('x')
    rnd = random.Random(7)
    seen = 0
    for _ in range(n):
        def poly():
            f = 1
            for _ in range(rnd.randint(0, 3)):
                r = rnd.random()
                if r < .6: f *= (X - rnd.randint(-4, 4))
                elif r < .75: f *= (X ** 2 - rnd.choice([2, 3, 5]))
                elif r < .9: f *= (X ** 2 + rnd.randint(1, 4))
                else: f *= (2 * X + rnd.choice([1, 3, -1]))
            return sympy.expand(f * rnd.choice([1, 1, -1, 2]))
        N, D = poly(), poly()
        if rnd.random() < .5: D = sympy.Integer(1)
        if sympy.gcd(N, D) != 1 and D != 1: continue
        op = rnd.choice(OPS)
        f = N / D
        s = lambda p: str(sympy.expand(p)).replace(' ', '').replace('**', '^').replace('x', 'X')
        # entry: (N)op(D)... as N/D op 0 in plain text
        txt = '(' + s(N).replace('X^2', 'X²').replace('X^3', 'X³') + ')' + ('/(' + s(D).replace('X^2', 'X²').replace('X^3', 'X³') + ')' if D != 1 else '')
        if '^' in txt or '**' in txt: continue
        try:
            o = eng.ask('SOLVE(' + txt + op + '0,X)')
        except ValueError:
            continue
        if o.startswith('E:'):
            if o[2:].split('|')[0] in ('ALWAYS TRUE', 'NO SOLUTION'):
                pts = [sympy.Rational(k, 2) for k in range(-12, 13)]
                want = o.startswith('E:ALWAYS')
                for p in pts:
                    if D.subs(X, p) == 0: continue
                    if PY[op](f.subs(X, p)) != want:
                        bad += 1; print('WRONG', txt, op, o, p); break
            continue
        if o == 'PASS': continue
        seen += 1
        T = truth(o)
        roots = set()
        for p in (N, D):
            for r in sympy.Poly(p, X).real_roots() if sympy.degree(p, X) > 0 else []:
                roots.add(r)
        pts = list(roots) + [sympy.Rational(k, 7) for k in range(-60, 61, 3)]
        for r in roots:
            pts += [r + sympy.Rational(1, 100), r - sympy.Rational(1, 100)]
        for p in pts:
            if D.subs(X, p) == 0:
                if D == 1: continue
                if T(p): bad += 1; print('POLE IN SET', txt, op, o, p); break
                continue
            if PY[op](f.subs(X, p)) != T(p):
                bad += 1; print('WRONG', txt, op, o, p, sympy.N(p)); break
    print('ineq_check: %d answered, %s' % (seen, 'ok' if not bad else '%d BAD' % bad))
    return bad


if __name__ == '__main__':
    sys.exit(1 if main(int(sys.argv[1]) if len(sys.argv) > 1 else 300) else 0)
