#!/usr/bin/env python3
"""Matrices and vectors on the real home screen (engine.c, matrix()): MDET( MINV( MRREF(
MTRANS( DOTP( CROSSP( NORM(, typed with the ALPHA letters and [ ] (2nd x, 2nd -), and the
Matrix submenu of the Algebra menu.

    python3 matrix.py

Classic reads the entry and the answer back through textShadow ([ is glyph C1, a minus 1A).
MathPrint draws straight to VRAM, so there the answer is checked by the OS not raising an
error and by a CRC of the screen rows under the entry, taken once from a screen looked at
(the OS draws a matrix as a grid; a wide one is TOO WIDE, as the engine's mpwidth says).
"""
import sys
import screen
import e2e

L, R = "2nd * ", "2nd - "           # [ and ]
NEG = b"\x1a"
LB = b"\xc1"
SQ = b"\x12"


def q(name, body):
    return e2e.spell(name) + " " + body + " ) enter . "


M12 = L + L + "1 , 2 " + R + L + "3 , 4 " + R + R
A1 = L + L + "alpha math , 1 " + R + L + "2 , alpha math " + R + R
M3 = L + L + "1 , 2 , 3 " + R + L + "4 , 5 , 6 " + R + R
V = lambda *e: L + " , ".join(e) + " " + R

# (name, keys, entry echo, answer)
CASES = [
    ("MDET", M12, b"MDET(" + LB + LB + b"1,2]" + LB + b"3,4]])", NEG + b"2"),
    ("MINV", M12, b"MINV(" + LB + LB + b"1,2]" + LB + b"3,4]])", LB + LB + NEG + b"2,1]" + LB + b"3/2," + NEG + b"1/2]]"),
    ("MRREF", M3, b"MRREF(" + LB + LB + b"1,2,3]" + LB + b"4,5,6]])", LB + LB + b"1,0," + NEG + b"1]" + LB + b"0,1,2]]"),
    ("MTRANS", M3, b"MTRANS(" + LB + LB + b"1,2,3]" + LB + b"4,5,6]])", LB + LB + b"1,4]" + LB + b"2,5]" + LB + b"3,6]]"),
    ("MDET", A1, b"MDET(" + LB + LB + b"A,1]" + LB + b"2,A]])", b"A" + SQ + b"-2"),
    ("DOTP", V("1", "2", "3") + " , " + V("4", "5", "6"),
     b"DOTP(" + LB + b"1,2,3]," + LB + b"4,5,6])", b"32"),
    ("CROSSP", V("1", "0", "0") + " , " + V("0", "1", "0"),
     b"CROSSP(" + LB + b"1,0,0]," + LB + b"0,1,0])", LB + b"0,0,1]"),
    ("NORM", V("3", "4"), b"NORM(" + LB + b"3,4])", b"5"),
]
ERRS = [
    ("MINV", L + L + "1 , 2 " + R + L + "2 , 4 " + R + R, 0x2B),           # singular
    ("MDET", L + L + "1 , 2 , 3 " + R + R, 0x2B),                            # not square
    ("DOTP", V("1", "2") + " , " + V("1", "2", "3"), 0x2B),                 # lengths
]

fails = 0


def check(ok, what):
    global fails
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    fails += not ok


def classic():
    for i in range(0, len(CASES), 4):
        batch = CASES[i:i + 4]
        rows = [t for t, _ in e2e.run("".join(q(n, k) for n, k, _, _ in batch))[1:]]
        for j, (n, k, echo, want) in enumerate(batch):
            got = rows[2 * j:2 * j + 2]
            check(got == [echo, want], "Classic %s: %r -> %r" % (n, got[0] if got else None, got[1:] and got[1]))
    for n, k, no in ERRS:
        e2e.run(q(n, k))
        check(e2e.run.err & 0x7F == no, "Classic %s: error screen (errNo %02X, 80 = Goto offered)" % (n, e2e.run.err))


def menu():
    # ALPHA+DOWN, A (the MATH key) opens Matrix >; 1..7 type the commands
    for key, cmd in zip("1 2 3 4 5 6 7".split(), "MDET( MINV( MRREF( MTRANS( DOTP( CROSSP( NORM(".split()):
        rows = [t for t, _ in e2e.run("alpha down math " + key)]
        check(rows and rows[-1] == cmd.encode(), "popup Matrix %s types %s" % (key, cmd))
    rows = [t for t, _ in e2e.run("2nd math left left left . " + "down " * 10 + "enter . 1")]
    check(rows and rows[-1] == b"MDET(", "2nd MATH Algebra tab item 11 opens Matrix >, 1 types MDET(")


def mathprint():
    import vram
    for n, k, want in ((n, k, w) for n, k, _, w in CASES if n in ("MINV", "MRREF", "CROSSP")):
        bands = [("v%d" % y, 0xD40000 + 640 * y, 640 * 16) for y in range(48, 144, 16)]
        for settle in (1500, 1900, 2300):
            r = screen.read([("err", 0xD008DF, 1), ("cx", 0xD007E0, 1)],
                            keys=e2e.PROLOGUE.replace("C ", "", 1) + q(n, k), files=e2e.FILES,
                            launch=e2e.LAUNCH, lead=False, settle=settle, step=1, crcs=bands)
            if None not in [r[b] for b, _, _ in bands]:
                break
        check(r["err"][0] == 0 and r["cx"][0] == 0x40, "MathPrint %s: answered, no error screen" % n)
        blank = [r[b] for b, _, _ in bands][-1]
        check(any(r[b] != blank for b, _, _ in bands[:4]), "MathPrint %s: something is drawn under the entry" % n)
    # too wide: a 4x4 inverse with a many-digit determinant
    big = L + L + (" " + R + L).join("3 , 1 , 0 , 0|0 , 3 , 1 , 0|0 , 0 , 3 , 1|1 , 0 , 0 , 3".split("|")) + " " + R + R
    e2e_run = screen.read([("err", 0xD008DF, 1)], keys=e2e.PROLOGUE.replace("C ", "", 1) + q("MINV", big),
                          files=e2e.FILES, launch=e2e.LAUNCH, lead=False, settle=1900)
    check(e2e_run["err"][0] == 0x2B, "MathPrint 4x4 inverse too wide: error screen")


classic()
menu()
mathprint()
print("ALL PASS" if not fails else "%d FAILED" % fails)
sys.exit(1 if fails else 0)
