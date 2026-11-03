#!/usr/bin/env python3
"""An answer wider than the screen, on the real OS: the answer view, the home screen after it, scroll-back and recall.

    python3 wide.py

SOLVE(X*X+X=1,X) is 29 columns in Classic, EXPAND((X+1)^7) 40 in MathPrint. After ENTER the view is open (the black
title bar at the top of VRAM's band, engine.c's step log flagged 0x80); after CLEAR, per mode:
  * Classic: the entry, the answer clipped to one row with the OS's ellipsis 0xCE last (what UP's redraw makes of the
    record, so scroll-back does not change it), the row below clear, and the next entry typed there
  * MathPrint: the history record is the full answer (type 0x18), UP, UP and 2nd ENTRY change nothing
  * both: 2nd ENTRY recalls the entry, and ENTER answers it again (the view reopens)
"""
import sys
import cmdkeys, e2e, screen, vram

CLASSIC = e2e.PROLOGUE
MATHPRINT = e2e.PROLOGUE.replace("C ", "", 1)
SOLVE = cmdkeys.spell("SOLVE") + " xton * xton + xton 2nd math 1 1 , xton ) "
EXPAND = cmdkeys.spell("EXPAND") + " ( xton + 1 ) ^ 7 right ) "
BAR = vram.VRAM + 2 * (36 * 320 + 4)          # the view's title bar is black from row 30 to 47
fails = 0


def check(ok, what):
    global fails
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    fails += not ok


def read(pro, keys):
    for settle in (1900, 2300):
        r = screen.read([("txt", screen.TEXTSHADOW, screen.COLS * screen.ROWS), ("rec", 0xD008EE, 30), ("log", 0xD13800, 8),
                         ("bar", BAR, 4)], keys=pro + keys, step=1, files=e2e.FILES, launch=e2e.LAUNCH, lead=False, settle=settle)
        if "txt" in r:
            break
    t = r["txt"]
    return [t[i * screen.COLS:(i + 1) * screen.COLS] for i in range(screen.ROWS)], r


def rows(t, n):
    return [bytes(t[i]) for i in range(n)]


# Classic
t, r = read(CLASSIC, SOLVE + "enter . .")
check(r["log"][0] == 0 and r["log"][2] == 0x80 and r["bar"] == b"\0\0\0\0", "Classic: ENTER opens the answer view (log flag 0x80, black title bar)")
t, r = read(CLASSIC, SOLVE + "enter . . clear . .")
check(r["bar"] != b"\0\0\0\0", "Classic: CLEAR closes the view")
a = bytes(t[2])
check(bytes(t[1]).startswith(b"solve(") and a[:3] == b"X=(" and a[25] == 0xCE and not bytes(t[3]).strip(b" \0\xe0"),
      "Classic: the entry, the answer clipped to one row with 0xCE last, the row below clear")
n = r["rec"][2] + r["rec"][3] * 256
check(r["rec"][0] == 0 and n == 24 and r["rec"][4 + n - 1] == 0x32, "Classic: the history record is the whole answer, ending /2 (type 0, length %d)" % n)
t2, _ = read(CLASSIC, SOLVE + "enter . . clear . 1 + 1 . .")
check(bytes(t2[2]) == a and bytes(t2[3]).startswith(b"1+1"), "Classic: the next entry is typed on the row below the answer")
for keys in ("up up . .", "up down . .", "up up down down . ."):
    t2, _ = read(CLASSIC, SOLVE + "enter . . clear . " + keys)
    check(rows(t2, 3) == rows(t, 3), "Classic: scroll-back (%s) leaves the entry and the answer as they were" % keys.split(" .")[0])
t2, _ = read(CLASSIC, SOLVE + "enter . . clear . 2nd enter . .")
check(bytes(t2[3]).startswith(b"solve(X*X+X=1,X)") or bytes(t2[3]).startswith(b"solve(X^2"), "Classic: 2nd ENTRY recalls the entry")
t2, r2 = read(CLASSIC, SOLVE + "enter . . clear . 2nd enter . . enter . .")
check(r2["log"][0] == 0 and r2["log"][2] == 0x80 and r2["bar"] == b"\0\0\0\0", "Classic: ENTER on the recalled entry opens the view again")

# MathPrint: the OS draws the answer cut off at the edge, the view shows it whole
t, r = read(MATHPRINT, EXPAND + "enter . .")
check(r["log"][0] == 0 and r["log"][2] == 0x80 and r["bar"] == b"\0\0\0\0", "MathPrint: ENTER opens the answer view")
t, r = read(MATHPRINT, EXPAND + "enter . . clear . .")
check(r["bar"] != b"\0\0\0\0" and r["rec"][0] == 0x18, "MathPrint: CLEAR closes it, the history record is type 0x18 (%02X)" % r["rec"][0])
base, _r = vram.grab(MATHPRINT + EXPAND + "enter . . clear . .", 50, 100, 4, 4, files=e2e.FILES, launch=e2e.LAUNCH, lead=False, settle=1900)
for keys in ("up down . .", "2nd enter . ."):
    px, _r = vram.grab(MATHPRINT + EXPAND + "enter . . clear . " + keys, 50, 100, 4, 4, files=e2e.FILES, launch=e2e.LAUNCH, lead=False, settle=1900)
    same = sum(1 for k, v in px.items() if base.get(k) == v)
    check(same >= len(px) * 0.9, "MathPrint: %s leaves the screen (%d of %d samples)" % (keys.split(" .")[0], same, len(px)))

# keys in the view: every one but UP/DOWN closes it (consumed), nothing stray is drawn, and the home screen still works
# (the 1+1 answer is on the screen as it is after plain CLEAR; MathPrint draws no text, so it compares VRAM samples)
TAIL = " 2nd mode . . 1 + 1 enter . ."
refpx, _r = vram.grab(MATHPRINT + EXPAND + "enter . . clear ." + TAIL, 40, 240, 4, 4, files=e2e.FILES, launch=e2e.LAUNCH, lead=False, settle=1900)
for name, keys in (("2nd QUIT", "2nd mode ."), ("ON", "on ."), ("MODE", "mode ."), ("Y=", "y= ."), ("GRAPH", "graph ."), ("digit 5", "5 .")):
    t, r = read(CLASSIC, SOLVE + "enter . . " + keys + TAIL)
    txt = [bytes(row) for row in t]
    check(r["bar"] != b"\0\0\0\0" and any(x.rstrip(b" \0").endswith(b"2") and b"1+1" not in x for x in txt) and any(x.startswith(b"1+1") for x in txt),
          "Classic: %s closes the view, home screen works (1+1 = 2)" % name)
    px, r = vram.grab(MATHPRINT + EXPAND + "enter . . " + keys + TAIL, 40, 240, 4, 4, files=e2e.FILES, launch=e2e.LAUNCH, lead=False, settle=1900)
    same = sum(1 for k, v in px.items() if refpx.get(k) == v)
    check(same >= len(px) * 0.97, "MathPrint: %s closes the view, home screen as after CLEAR (%d of %d samples)" % (name, same, len(px)))
sys.exit(fails)
