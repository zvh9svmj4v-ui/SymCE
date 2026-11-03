#!/usr/bin/env python3
"""Does editTop move when the cursor enters a MathPrint box?

If it does, a hook can tell "inside a box" from "flat entry" WITHOUT any flag
guess: record editTop while the line is still virgin, compare at ENTER. The
baseline moves as history grows, which is why it has to be captured per entry
rather than hardcoded -- that is what killed every earlier attempt.
"""
import e2e, screen


def read(keys, probes):
    """The probes after the keys, MathPrint, through the SymCE app (screen.read: the old DEMO.8xp harness is gone)."""
    r = screen.read([(n, int(a, 16), sz) for n, a, sz in probes], keys=e2e.PROLOGUE.replace("C ", "", 1) + keys,
                    files=e2e.FILES, launch=e2e.LAUNCH, lead=False, settle=1200)
    return {n: int.from_bytes(r[n], "little") for n, _, _ in probes}

P=[("top","0xD02437",3),("cur","0xD0243A",3),("tail","0xD0243D",3),
   ("btm","0xD02440",3),("cmd","0xD0008C",1),("tf","0xD00085",1)]

CASES = [
 ("virgin (nothing typed)", "clear"),
 ("flat 2X+2X",             "clear 2 xton + 2 xton"),
 ("cube root, X inside",    "clear math 4 xton"),
 ("x-th root, X inside",    "clear math 5 xton"),
 ("exponent X^2",           "clear xton ^ 2"),
 ("fraction n/d",           "clear alpha y= 1"),
 ("after one entry, virgin","clear 1 + 1 enter . clear"),
 ("after one entry, flat",  "clear 1 + 1 enter . 2 xton + 2 xton"),
 ("after one entry, root",  "clear 1 + 1 enter . math 4 xton"),
]
if __name__=="__main__":
    print("%-26s %-8s %-8s %-8s %-8s cmd tf" % ("state","top","cur","tail","btm"))
    for label,keys in CASES:
        r=read(keys,P)
        print("%-26s %06X   %06X   %06X   %06X   %02X  %02X" %
              (label,r["top"],r["cur"],r["tail"],r["btm"],r["cmd"],r["tf"]))
