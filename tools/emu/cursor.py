#!/usr/bin/env python3
"""The insert cursor is a 2 px vertical bar (fhook.c), Evo font on and off.

Types "12" on the home screen (Classic, then MathPrint) and reads VRAM at a
blink-on moment: the cursor cell must hold a bar 2 px wide and 14 rows tall and
no underline (the ROM's is 10 px wide, rows 12-13 of the glyph).

    python3 cursor.py [out_dir]     # also writes PNGs of each case
"""
import os, sys
import e2e, screen, vram

SET = "apps " + "down " * 16 + "enter . . "                       # APPS > SymCE: 1 CAS, 3 font
TI_FONT = SET + "3 . clear . "
X0 = 24                                                        # the third cell: "1" "2" then the cursor
FAILS = []


def case(label, pre, keys, y0, y1, block=False):
    px, _ = vram.grab(vram.SYMCE + pre + keys + " w300", y0, y1, files=vram.FILES, launch=e2e.LAUNCH, lead=False)
    cell = {k: v for k, v in px.items() if X0 <= k[0] < X0 + 12}
    ink = {(x - X0, y) for (x, y), v in cell.items() if vram.dark(v)}
    cols = {c for c, _ in ink}
    rows = sorted(y for y in {y for _, y in ink} if (2, y) in ink and (3, y) in ink)
    # a 2 px bar 14 rows tall (a run of rows), nothing right of it; the dotted rule may add 2 rows
    if block:                                                  # CAS off: TI's overwrite cursor, a 10 x 14 block (0xE0)
        ok = cols == set(range(2, 12)) and len(ink) == 140
    else:
        ok = cols == {2, 3} and any(rows[i:i + 14] == list(range(rows[i], rows[i] + 14)) for i in range(len(rows)))
    if len(sys.argv) > 1:
        vram.png({k: v for k, v in px.items() if k[0] < 70}, os.path.join(sys.argv[1], label + ".png"))
    print("%-4s %s: cursor cell columns %s, %d bar rows" % ("ok" if ok else "FAIL", label, sorted(cols), len(rows)))
    if not ok:
        FAILS.append(label)


for font, pre in (("evo", ""), ("ti", TI_FONT)):
    # rows below the status bar and the "Done" line's dotted rule (Evo: after the installer; TI font: one line up)
    case(font + " classic", pre, "C 1 2", 57, 76)
    case(font + " mathprint", pre, "1 2", 57 if not pre else 30, 76 if not pre else 53)
# the settings flows: the font hook follows CAS and Evo, whichever order they are switched in
# (with the CAS off the OS is in overwrite: its block cursor, not an insert glyph, whatever the font hook does)
for label, keys, block in (("cas off: TI's block", "1 . clear . ", True),
                           ("cas off, on", "1 . 1 . clear . ", False),
                           ("evo off, on", "3 . 3 . clear . ", False),
                           ("cas off, evo off, on", "1 . 3 . 3 . clear . ", True),
                           ("cas off, evo on, off", "1 . 3 . 3 . 3 . clear . ", True),
                           ("evo off, cas off, on", "3 . 1 . 1 . clear . ", False)):
    case(label, SET + keys, "C 1 2", 57, 76, block)
# 2:Goto after an error: the OS is in overwrite until the first key, but the bar must show at once
px, _ = vram.grab(vram.SYMCE + "C alpha down 1 xton - 4 ) enter . . 2 . . w300", 36, 56, files=vram.FILES, launch=e2e.LAUNCH, lead=False)
ink = {(x, y) for (x, y), v in px.items() if x < 200 and vram.dark(v)}
full = [x for x in range(200) if sum((x, y) in ink for y in range(36, 56)) >= 14]
# the stem of the l of solve(, 2 px, and the cursor; a block would be a run of ten columns
good = len(full) == 4 and full[3] - full[2] == 1 and full[2] > 100
print("%-4s after 2:Goto: the cursor is a 2 px bar at once (full-height columns %s)" % ("ok" if good else "FAIL", full))
if not good:
    FAILS.append("goto")
# CAS off and Evo off: nothing of ours is left in the font slot (hookflags3 bit 5)
r = screen.read([("f", 0xD000B5, 1)], keys=vram.SYMCE + SET + "1 . 3 . clear . ", step=1, files=vram.FILES, launch=e2e.LAUNCH, lead=False)
good = not r["f"][0] & 0x20
print("%-4s cas off, evo off: font hook disarmed (hookflags3 %02X)" % ("ok" if good else "FAIL", r["f"][0]))
if not good:
    FAILS.append("disarm")
sys.exit(1 if FAILS else 0)
