#!/usr/bin/env python3
"""SymCE's error screens: the description lines under TI's title (menu.c, menu_err, called
from lhook.c's event 0xD3), 2:Goto on SYNTAX and ARGUMENT (error 0xAB), and the localize
hook left as it was: with the Evo font off it is armed for the one screen only.

    python3 errs.py

Each case is read at the error screen: errNo, the lines at 0xD0EF00 (what the OS draws),
localizeHookPtr, hookflags3 bit 1 and tempArm. Classic and MathPrint, Evo on and off.
"""
import os
import sys
import screen
import e2e

SOLVE = "alpha down 1 xton + 2 alpha 1 2nd math 1 3 , alpha 1 )"
CASES = (
    ("SYNTAX", "alpha down 1 xton ^ 2 - 4 )", 0xAB, b"Check the arguments:\0SOLVE(eq,var)\0SOLVE({eq,..},{var,..})\0"),
    ("ARGUMENT", e2e.spell("POLYGCD") + " xton )", 0xAB, b"An argument is wrong:\0POLYGCD(p,q)\0POLYGCD(4X+4,6X+6)\0"),
    ("NO SOLUTION", "alpha down 1 xton ^ 2 + 1 2nd math 1 0 , xton )", 0x2B, b"No real solution.\0For complex ones use\0CSOLVE(.\0"),
    ("SYMCE LIMIT", "1 / 3 sto xton enter . " + SOLVE, 0x2B, b"SymCE can't do this one\0exactly.\0SOLVE(eq,var)\0SOLVE({eq,..},{var,..})\0"),
    ("MODE", "mode down down right enter clear . alpha down 7 3 sin xton ) )", 0x2B, b"This needs RADIAN mode.\0Press MODE and pick\0RADIAN.\0"),
    ("DOMAIN", e2e.spell("SUM") + " 1 / xton , xton , 0 , 3 )", 0x2B, b"Not defined for these\0values:\0SUM(expr,V,lo,hi)\0SUM(X^2,X,1,3)\0"),
    ("TOO WIDE", e2e.spell("EXPAND") + " ( xton + 1 ) ^ 7 )", 0x2B, b"The answer is too wide.\0Try MathPrint (MODE).\0"),
)
FONT_OFF = "apps " + "down " * 16 + "enter . . 3 . clear . "
fails = 0


def run(keys, pre, mp):
    pro = e2e.PROLOGUE.replace("C . ", "", 1) if mp else e2e.PROLOGUE
    for settle in (1500, 1900, 2300):
        r = screen.read([("err", 0xD008DF, 1), ("txt", 0xD0EF00, 96), ("lh", 0xD02611, 3), ("fl", 0xD000B5, 1),
                         ("ta", 0xD0EE06, 1), ("ttl", 0xD025A9, 13)],
                        keys=pro + pre + keys + " enter . .", files=e2e.FILES, launch=e2e.LAUNCH, lead=False,
                        settle=settle, per_key=300)
        if b"\xff" * 4 not in r["txt"]:
            return r
    return r


def case(name, keys, errno, lines, mp, evo):
    global fails
    r = run(keys, "" if evo else FONT_OFF, mp)
    lh = int.from_bytes(r["lh"], "little")
    on = bool(r["fl"][0] & 2)
    good = r["err"][0] == errno and r["ttl"].startswith(name.encode() + b"\0")
    good &= r["txt"].startswith(lines)
    good &= r["ta"][0] == 0 and (on and lh != 0 if evo else not on and lh == 0)
    label = "%s %s %s" % ("MathPrint" if mp else "Classic", "Evo on " if evo else "Evo off", name)
    print(("ok   " if good else "FAIL ") + label + ("" if good else "  errNo=%02X lines=%r hook=%06X on=%s tempArm=%d" % (
        r["err"][0], r["txt"][:60], lh, on, r["ta"][0])), flush=True)
    fails += not good


# 2:Goto lands on the offending token. (label, keys, byte offset from the entry's start or None; a command's
# letters are two bytes each, lowercase BB pairs)
# None: a 2D box in MathPrint, where the OS maps the offset into its own buffer; only that the
# cursor is back on the entry, in insert mode, is asserted, and the offset is printed.
SOLVE0 = "alpha down 1 "
GOTOS = (
    ("SYNTAX SOLVE(X-4)", SOLVE0 + "xton - 4 )", 14, 14),
    ("ARGUMENT POLYGCD(X)", e2e.spell("POLYGCD") + " xton )", 16, 16),
    ("ARGUMENT FACTOR(SOLVE(..))", "alpha down 2 " + SOLVE0 + "xton - 4 2nd math 1 0 , xton ) )", 13, 13),   # the inner command
    ("SYNTAX SOLVE(X^2-4)", SOLVE0 + "xton ^ 2 - 4 )", 16, None),
    ("SYNTAX SOLVE(n/d)", SOLVE0 + "alpha y= 1 xton down xton right right )", -1, None),   # MathPrint only
)


def gstate(keys, mp, more=""):
    pro = e2e.PROLOGUE.replace("C . ", "", 1) if mp else e2e.PROLOGUE
    for settle in (1900, 2300):
        r = screen.read([("err", 0xD008DF, 1), ("ed", 0xD02437, 12), ("tf", 0xD00085, 1), ("cf", 0xD0008C, 1)],
                        keys=pro + keys + " enter . . 2 . . " + more, files=e2e.FILES, launch=e2e.LAUNCH, lead=False,
                        settle=settle, per_key=300)
        if b"\xff" * 4 not in r["ed"]:
            break
    top, cur, tail, btm = (int.from_bytes(r["ed"][i:i + 3], "little") for i in (0, 3, 6, 9))
    return dict(err=r["err"][0], at=cur - top, after=btm - tail, tf=r["tf"][0], cf=r["cf"][0])


def gcase(name, keys, cl, mpo, mp):
    global fails
    want = mpo if mp else cl
    g = gstate(keys, mp)
    k = gstate(keys, mp, "7 . .")                # then a digit: insert keeps what follows the cursor
    good = g["err"] == 0xAB and g["cf"] & 0x20 == 0 and k["tf"] & 0x10 and k["after"] == g["after"]
    good &= want is None or g["at"] == want
    png = os.environ.get("GOTOPNG")
    if png:
        import vram
        pro = e2e.PROLOGUE.replace("C . ", "", 1) if mp else e2e.PROLOGUE
        px, _ = vram.grab(pro + keys + " enter . . 2 . .", 0, 120, files=e2e.FILES, launch=e2e.LAUNCH, lead=False,
                          settle=1500, per_key=300)
        vram.png(px, "%s/goto_%s_%s.png" % (png, "mp" if mp else "cl", "".join(c for c in name if c.isalnum())))
    print(("ok   " if good else "FAIL ") + "%s Goto %s: cursor at token %d (want %s), after Goto+7 tail %d->%d insert=%s" % (
        "MathPrint" if mp else "Classic", name, g["at"], "box" if want is None else want, g["after"], k["after"],
        bool(k["tf"] & 0x10)), flush=True)
    fails += not good


for mp in (False, True):
    for c in GOTOS:
        if mp or c[2] >= 0:
            gcase(*c, mp)
for mp in (False, True):
    for evo in (True, False):
        for c in CASES:
            if mp and c[0] in ("MODE", "TOO WIDE", "NO SOLUTION"):
                continue                    # the same text; MODE row 3 and NO SOLUTION's keys differ in MathPrint
            case(*c, mp, evo)
print("ALL PASS" if not fails else "%d FAIL" % fails)
sys.exit(bool(fails))
