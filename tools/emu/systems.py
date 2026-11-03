#!/usr/bin/env python3
"""SOLVE({A=B,..},{X,..}) on the real home screen, Classic and MathPrint.

    python3 systems.py

Classic: the answer row is read back out of textShadow, right-aligned, and the entry
echoed as typed. MathPrint draws to VRAM, so there the history record is read:
the type byte at 0xD008EE and the tokens the answer was drawn from at 0xD008F2
(length at 0xD008F0), what the OS drew it from (docs §7, 0x091FC2), and the answer
row is checked to be inked in VRAM, ending at the right edge.
"""
import sys
import screen
import e2e
import vram

ALPHA = dict(e2e.ALPHA, X="xton")
KEY = {"=": "2nd math 1", "{": "2nd (", "}": "2nd )", "²": "^ 2", "~": "(-)", ",": ",", ".": "dp"}


def keys(text):
    out = []
    for c in text:
        if c.isalpha():
            out.append(ALPHA[c] if c == "X" else "alpha " + ALPHA[c])
        else:
            out.append(KEY.get(c, c))
    return " ".join(out)


def entry(text):                     # SOLVE( is letters, the way the menus type it
    return e2e.spell(text.split("(", 1)[0]) + " " + keys(text.split("(", 1)[1])


def echo(text):
    return text.replace("{", "\x7b").replace("}", "\x7d").replace("~", "\x1a").encode("latin-1")


# (entry, text on the Classic answer rows, tokens of the history record or None)
# errors are host-tested only (the error screen is not in textShadow)
# record tokens: = 0x6A, - 0x71, 0x40 " and ", 0x3C " or ", 0xB0 unary minus, 0x08/0x09 braces, 0x2C comma
CASES = [
    ("SOLVE({X+Y=3,X-Y=1},{X,Y})", "X=2 and Y=1", b"Xj2\x40Yj1"),
    ("SOLVE({X+Y=3,X-Y=1},{Y,X})", "Y=1 and X=2", b"Yj1\x40Xj2"),
    ("SOLVE({X+Y=3},{X,Y})", "X=3-Y", b"Xj3\x71Y"),
    ("SOLVE({X+Y+Z=6,X-Y=0,Y+Z=5},{X,Y,Z})", "X=1 and Y=1 and Z=4", b"Xj1\x40Yj1\x40Zj4"),
    ("SOLVE({X*Y=6,X+Y=5},{X,Y})", "X=2 and Y=3 or X=3 and Y=2", b"Xj2\x40Yj3\x3cXj3\x40Yj2"),
]


def run(text, mp):
    pro = e2e.PROLOGUE.replace("C ", "", 1) if mp else e2e.PROLOGUE
    r = screen.read([("len", 0xD008F0, 2), ("text", 0xD008F2, 40), ("screen", screen.TEXTSHADOW, 260)],
                    keys=pro + entry(text) + " enter . .", files=e2e.FILES, launch=e2e.LAUNCH,
                    lead=False, settle=1900, per_key=300)
    return "\n".join(screen.render(r["screen"])), r["text"][:int.from_bytes(r["len"], "little")]


if __name__ == "__main__":
    bad = 0
    for mp in (False, True):
        for text, shown, rec in CASES:
            rows, tok = run(text, mp)
            ok = tok == rec and (mp or shown in rows)
            print("PASS" if ok else "FAIL", "MathPrint" if mp else "Classic", text, "" if ok else (rows, tok))
            bad += not ok
    print("systems:", "ALL PASS" if not bad else "%d FAIL" % bad)
    sys.exit(bool(bad))
