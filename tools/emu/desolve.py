#!/usr/bin/env python3
"""DESOLVE( on the real home screen: Classic (the drawn answer, read back from textShadow)
and MathPrint (VRAM: no error, an answer is drawn, and it is not what the entry alone leaves).

    python3 desolve.py

Y' is 2nd ANGLE 2 (token 0xAE); `and` is 2nd MATH LOGIC 1 (token 0x40). The tests load the
app from this directory (make emu copies it here). The answers are engine_check.py's.
"""
import sys
import e2e
import screen

KEY = {"X": "xton", "Y": "alpha 1", "'": "2nd apps 2 w400", "=": "2nd math 1 w400", " and ": "2nd math right 1 w400"}


def keys(eq):
    """The key presses typing the equation eq (Y X ' = digits + - * / ( ) ^ and ' and ')."""
    out, i = [], 0
    while i < len(eq):
        if eq.startswith(" and ", i):
            out.append(KEY[" and "]); i += 5
        else:
            c = eq[i]; i += 1
            out.append(KEY.get(c, "dp" if c == "." else c))
    return " ".join(out)


def entry(eq):
    # w8000: the 10-part app (626 KB) finishes relocating about 12 s after the prologue; keys typed in that
    # deaf window are lost (a key after the first "(" went missing). A real calculator loses them too.
    return "w8000 " + e2e.spell("DESOLVE") + " " + keys(eq) + " , xton , alpha 1 )"


E, SQ = b"\xdb", b"\x12"          # the e and the raised 2 glyphs
CASES = [
    ("Y'=2Y", b"Y=C" + E + b"^(2X)"),
    ("Y'=2Y and Y(0)=3", b"Y=3" + E + b"^(2X)"),
    ("Y'=X/Y and Y(0)=2", b"Y=\x10(X" + SQ + b"+4)"),
    ("Y''=2 and Y(0)=1 and Y'(0)=0", b"Y=X" + SQ + b"+1"),
    ("Y''-3Y'+2Y=0 and Y(0)=1 and Y'(0)=0", b"Y=2" + E + b"^(X)-" + E + b"^(2X)"),
    ("XY'=2Y and Y(1)=3", b"Y=3X" + SQ),                    # gaps-de: explicit Y, a power of X
    ("XY'-Y=X^2", b"Y=CX+X" + SQ),                          # an integrating factor
]
MP = (0, 1, 4, 5)                       # no / or ^ typed: no templates to leave
MPONLY = ["Y''+Y'+Y=0"]                 # a root of 3 in the cosine, e^(-X/2) and X/2 in both: 31 columns, past the edge in MathPrint too: the answer view opens (wide.py)


def main():
    ok = True
    for eq, want in CASES:
        rows = e2e.run(entry(eq) + " enter .")[1:]
        got = rows[-1][0] if rows else b"<missing>"
        good = got == want and rows[-1][1] and e2e.run.err == 0
        ok &= good
        print("%-40s -> %-18s %s" % (eq, e2e.show(got), "ok  Classic" if good else "FAIL want %r got %r" % (want, got)))
    for i in MP:
        eq = CASES[i][0]
        _, _, typed = e2e.mathprint(entry(eq) + " .", vram=True)
        _, _, shown = e2e.mathprint(entry(eq) + " enter .", vram=True)
        good = shown[-1] == 0 and shown[:-2] != typed[:-2]      # no error, and a screen drawn over the entry's
        ok &= good
        print("%-40s MathPrint %s" % (eq, "ok  answer drawn, no error" if good else "FAIL errNo %d" % shown[-1]))
    for eq in MPONLY:
        _, _, typed = e2e.mathprint(entry(eq) + " .", vram=True)
        _, _, shown = e2e.mathprint(entry(eq) + " enter .", vram=True)
        good = shown[-1] == 0 and shown[:-2] != typed[:-2]      # no error, the answer view over the entry
        ok &= good
        print("%-40s MathPrint %s" % (eq, "ok  answer view, no error" if good else "FAIL errNo %d" % shown[-1]))
    print("ALL PASS" if ok else "FAILED")
    sys.exit(0 if ok else 1)


main()
