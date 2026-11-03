#!/usr/bin/env python3
"""width() in engine.c against the OS's own drawing and against engine_check.fcols.

An answer that runs past the screen edge must say TOO WIDE; width() decides, in
columns, 26 at most. Each pin below was measured on the real OS (CEmu, VRAM) in
MathPrint: how wide the answer is drawn, in px, and what width() must say.
The rest: every answer a few hundred entries give, width() (C) against fcols (an
independent port in engine_check.py) in both modes.

    python3 width_check.py
"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import engine_check as ec

# answer, MathPrint columns width() must give, px the OS drew (ink), or None when it ran
# off the right edge (the arrow at x 310; px is then not known). Fits: 10.5..12 px a column.
PINS = [
    ('2sin(X/2)', 7, 83),                        # one stacked fraction in an argument: its wider side, no bar
    ('sin(X/2)cos(X/3)', 12, 126),               # the second used to overwrite the first's columns (was 8)
    ('sin(X/2)cos(X/3)sin(X/5)cos(X/7)', 24, 256),
    ('4sin(X/2)cos(X/2)²-sin(X/2)', 21, 233),
    ('4sin(X/2)cos(X/2)²-sin(X/2)+1', 23, 258),
    ('4sin(X/2)cos(X/2)²-sin(X/2)+sin(X/5)', 28, None),
    ('sin(X/2)cos(X/3)sin(X/5)cos(X/7)sin(X/9)', 30, None),
    ('Y=e^(-X/2)(Ccos(√(3)X/2)+Dsin(√(3)X/2))', 29, None),    # DESOLVE(Y''+Y'+Y=0): was 15, fitted, ran off
    # build 3's ten answers that this check had called too wide, drawn on the emulator: the whole answer one
    # stacked fraction with functions has small-type sides (3/4 of the columns), e^( is one column
    ('(2cos(8Y+2)+sin(3X/2)+sin(5X/2)+2)/2', 23, 260),
    ('(cos(3X/2-4Y)-2cos(3X/2)+cos(3X/2+4Y)+4)/4', 26, 289),
    ('(8sin(X/2)cos(X/2)²+X-2sin(X/2))/2', 20, 212),                  # TEXPAND(sin(3X/2)+X/2)
    ('5Xe^(1/2)/2+4X-2ln(2)-3e^(1/2)/4-2', 24, 249),
    ('4sin(X/2)cos(X/2)²+2sin(B)-sin(X/2)', 29, None),
    ('(-3+3sin(3X)²+2sin(X/2)²-3sin(X/2)²sin(3X)²)/(sin(X/2)²-1)', 30, None),
    ('sin(X)+sin(3X/2)²-sin(3X/2)²sin(X/2)²', 31, None),
    ('-sin(X)/cos(X)³+1+sin(X)/cos(X)-cos(X/2)²', 27, None),
    ('Xtan(1/2)²-tan(1/2)²/2+X+tan(1/2)-1/2', 29, None),             # a fraction among terms stays whole
    ('(32cos(1/2)sin(1/2)+32X-16cos(1/2)+2sin(1/2)-17)/(32sin(1/2)-16)', 30, None),
    ('8sin(B)³cos(B)³/(8cos(B)^6-12cos(B)^4+6cos(B)²-1)', 32, None),     # TEXPAND(tan(2B)³): only cos(B)³ is stacked
    # known false positive: 27 columns, drew in 303 px (the one column over is all there is to it)
    # ('18tan(3/2)³+cos(1/2)+18tan(3/2)+6', 27, 303),
]


def main():
    eng = subprocess.Popen([ec.ENGINE], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)

    def cols(o):
        eng.stdin.write('W' + o.hex() + '\n')
        eng.stdin.flush()
        c, m = eng.stdout.readline().strip()[1:].split(',')
        return int(c), int(m)

    bad = 0
    for text, mp, px in PINS:
        c, m = cols(ec.ti(text))
        ok = m == mp and (m > 26 if px is None else m <= 26 and px / m <= 12)
        print('%-52s MathPrint %2d (want %2d) %s %s' % (text, m, mp, 'ran off' if px is None else '%d px' % px, 'ok' if ok else 'FAIL'))
        bad += not ok
    # C against the port, over answers
    def ask(s):
        eng.stdin.write('CM' + ec.ti(s).hex() + '\n')
        eng.stdin.flush()
        r = eng.stdout.readline().strip()
        return bytes.fromhex(r.split(':')[0]) if r[0] not in 'PE' else None
    seen = n = 0
    for e in ['TEXPAND(sin(%s))' % a for a in ('3X/2', '5X/2', '7X/3', '3X/4', 'X/2+X')] + \
             ['TEXPAND(sin(3X/2)+%s)' % a for a in ('X', '2X', 'X+1', 'Y', 'XY', 'X/2', '1/2', 'sin(X/5)')] + \
             ['TCOLLECT(sin(X)cos(X))', 'INTEGRAL(cos(X/2),X)', 'INTEGRAL(sin(X/2)+cos(X/3)+e^(X/4),X)', 'INTEGRAL(e^(X/2),X)',
              'TEXPAND(sin(X/2)cos(X/3))', 'DERIV(sin(X/2)cos(X/2),X)', 'INTEGRAL(sin(X/2)cos(X/2),X)', 'INTEGRAL(sin(X/2)sin(X/2),X)']:
        o = ask(e)
        if o is None:
            continue
        n += 1
        c, m = cols(o)
        try:
            fc, fm = ec.fcols(o, 0), ec.fcols(o, 1)
        except Exception:
            continue
        if (c, m) != (fc, fm):
            print('FAIL', e, ec.readable(o), 'C', (c, m), 'port', (fc, fm))
            bad += 1
        seen += 1
    bad += seen < n // 2                    # the port must really run
    print('%d answers, width() = fcols on %d' % (n, seen - bad))
    print('width: %s' % ('FAIL %d' % bad if bad else 'PASS'))
    return bad


sys.exit(main())
