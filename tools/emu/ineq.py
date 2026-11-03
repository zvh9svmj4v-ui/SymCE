#!/usr/bin/env python3
"""SOLVE inequalities and PARTFRAC on the real home screen.

    python3 ineq.py

Classic: the drawn answer is read back from textShadow (the test glyphs are the OS's own:
0x17 <=, 0x18 !=, 0x19 >=; 2nd MATH 1..6 types = != > >= < <=). MathPrint: the answer is
drawn to VRAM, so only that it differs from the bare entry and raised no error is checked.
"""
import sys
import e2e
from e2e import NEG, SQ, spell

LE, NE, GE = b"\x17", b"\x18", b"\x19"
T = lambda n: " 2nd math %d " % n          # 1 =  2 !=  3 >  4 >=  5 <  6 <=
S = "alpha down 1 "
X2M4 = "xton ^ 2 - 4"
X2M2X3 = "xton ^ 2 - 2 xton - 3"

CASES = [
    ("SOLVE(X^2-4>0,X)", S + X2M4 + T(3) + "0 , xton )", b"X<" + NEG + b"2 or X>2", "two rays, or"),
    ("SOLVE(X^2-4<0,X)", S + X2M4 + T(5) + "0 , xton )", NEG + b"2<X<2", "chained interval"),
    ("SOLVE(X^2-2X-3<=0,X)", S + X2M2X3 + T(6) + "0 , xton )", NEG + b"1" + LE + b"X" + LE + b"3",
     "closed interval keeps its ends"),
    ("SOLVE(X^2-2X-3>=0,X)", S + X2M2X3 + T(4) + "0 , xton )", b"X" + LE + NEG + b"1 or X" + GE + b"3",
     "closed rays"),
    ("SOLVE((X-1)^2>0,X)", S + "( xton - 1 ) ^ 2" + T(3) + "0 , xton )", b"X" + NE + b"1", "one hole"),
    ("SOLVE(1/X<1,X)", S + "1 / xton" + T(5) + "1 , xton )", b"X<0 or X>1", "pole excluded"),
    ("SOLVE((X+1)/(X-2)>=0,X)", S + "( xton + 1 ) / ( xton - 2 )" + T(4) + "0 , xton )",
     b"X" + LE + NEG + b"1 or X>2", "zero kept, pole dropped"),
    ("SOLVE(X^2>2,X)", S + "xton ^ 2" + T(3) + "2 , xton )", b"X<" + NEG + b"\x10(2) or X>\x10(2)", "radical ends"),
    ("SOLVE(abs(X-1)<3,X)", S + "math right 1 xton - 1 ) 2nd math 5 3 , xton )", NEG + b"2<X<4", "abs by squaring"),
    ("PARTFRAC(1/(X^2-1),X)", spell("PARTFRAC") + " 1 / ( xton ^ 2 - 1 ) , xton )",
     b"1/(2(X-1))-1/(2(X+1))", "two linear factors"),
    ("PARTFRAC((X+3)/(X^3-X),X)", spell("PARTFRAC") + " ( xton + 3 ) / ( xton ^ 3 - xton ) , xton )",
     NEG + b"3/X+2/(X-1)+1/(X+1)", "three"),
    ("PARTFRAC(X^3/(X^2-1),X)", spell("PARTFRAC") + " xton ^ 3 / ( xton ^ 2 - 1 ) , xton )",
     b"X+1/(2(X-1))+1/(2(X+1))", "polynomial part first"),
]


def main():
    ok = True
    for i in range(0, len(CASES), e2e.PER_RUN):
        batch = CASES[i:i + e2e.PER_RUN]
        pairs = e2e.run(" ".join("%s enter ." % k for _, k, _, _ in batch))[1:]
        for j, (label, keys, want, why) in enumerate(batch):
            got = pairs[2 * j + 1] if 2 * j + 1 < len(pairs) else (b"<missing>", False)
            if got[0] != want:
                alone = e2e.run("%s enter ." % keys)[1:]
                got = alone[1] if len(alone) == 2 else got
            good = got[0] == want and got[1]
            ok &= good
            print("%-26s -> %-26s %s" % (label, e2e.show(got[0]),
                  "ok   " + why if good else "FAIL want %r right-aligned=%s" % (want, got[1])), flush=True)
    # MathPrint: the answer is drawn to VRAM
    for label, keys in (("SOLVE((X-2)(X+2)>0,X)", S + "( xton - 2 ) ( xton + 2 )" + T(3) + "0 , xton )"),
                        ("PARTFRAC(1/((X-1)(X+1)),X)",
                         spell("PARTFRAC") + " 1 / ( xton - 1 ) ( xton + 1 ) right , xton )")):
        _, _, bare = e2e.mathprint(keys, vram=True)
        _, _, ans = e2e.mathprint(keys + " enter .", vram=True)
        good = ans[:-2] != bare[:-2] and ans[-1] == 0
        ok &= good
        print("MathPrint %-22s %s" % (label, "ok   answer drawn, no error" if good else "FAIL err=%r" % ans[-1:]),
              flush=True)
    print("ALL PASS" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
