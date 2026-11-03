#!/usr/bin/env python3
"""Many coloured parentheses on the MathPrint home screen must not break steps(, the 2nd MATH popup or HELP.

    python3 parenspill.py [OUTDIR]

save_px stores the pixels it has no 2-bit code for as literals above the codes; they used to run through
the steps( log (0xD13800) before steps_show read it, and past 1625 of them save_px failed and nothing
opened. Now steps_show moves the log below the literals first and save_px/restore_px keep a one-byte index
per extra-colour pixel (2270 fit; the rest would be black). 80 parens in the history (about 1800 literal pixels): steps(solve(2X+3=7,X)) shows the
same screen as with a plain history, and it, the popup and HELP come back exactly as they were.
"""
import os, sys
import cmdkeys, e2e, vram, mpparen

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.environ.get("TMPDIR", "/tmp"), "mp-paren")
os.makedirs(OUT, exist_ok=True)
PRO = vram.SYMCE + "w300 "                           # MathPrint, rainbow ON, Evo font
ROW = "( " * 10 + "1 " + ") " * 10 + "enter . . "
MANY = ROW * 4
PLAIN = "1 enter . . " * 4
SOLVE = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("SOLVE") + " 2 xton + 3 2nd math 1 7 , xton ) ) ")
POP = "2nd math . . "
TAB = "left left left . . "                           # the Alg tab of the OS menu
HELP = "+ . . "
fails = 0


def check(ok, what):
    global fails
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    fails += not ok


def g(keys, name):
    px, _ = vram.grab(PRO + keys + " w1500", 30, 240, 1, 1, files=vram.FILES, launch=e2e.LAUNCH, lead=False)
    vram.png({k: v for k, v in px.items() if k[0] < 320}, os.path.join(OUT, "spill_%s.png" % name), 2)
    return px


def diff(a, b):
    return [k for k in a if a[k] != b.get(k)]


def only_black(before, after, what):
    d = diff(before, after)
    bad = [k for k in d if k[1] < 218]                                 # the status/cursor rows may blink
    check(not bad, "%s: back exactly as it was, %d pixels differ above row 218 (black ones: %d)" % (what, len(bad), sum(1 for k in bad if after[k] == 0)))


home = g(MANY, "home")
f = mpparen.find(home)
col = [p for p in f if p[3] != {0}]
check(len(f) >= 40 and not mpparen.wrong(f), "%d parens on the home screen, %d coloured" % (len(f), len(col)))

base = g(PLAIN + SOLVE + "enter . .", "steps_plain")
st = g(MANY + SOLVE + "enter . .", "steps_many")
check(not diff(base, st), "steps( over 80 parens shows the same screen as over a plain history (%d pixels differ)" % len(diff(base, st)))
after = g(MANY + SOLVE + "enter . . clear . .", "steps_after")
f = mpparen.find(after)
PALS = set(mpparen.PAL) | {0, 0xFFFF, 0x001F}
foreign = {v for (x, y), v in after.items() if 48 <= y < 215} - PALS
rows = {}
for x in f:
    rows.setdefault(x[0], []).append(x)
want = [mpparen.PAL[i % 6] for i in range(10)] + [mpparen.PAL[(9 - i) % 6] for i in range(10)]
top = rows[min(rows)]
black = sum(1 for x in f if x[3] == {0})
black += sum(1 for x in f if 0 in x[3] and len(x[3]) > 1)
check(not foreign and len(top) == 20 and all(x[3] == {w} for x, w in zip(top, want)) and not black,
      "after steps( the home screen is back: top row exact, no foreign colour %s, %d of %d parens black or part black"
      % (sorted("%04X" % c for c in foreign)[:3], black, len(f)))

pop = g(MANY + POP + TAB, "popup")
check(len(diff(home, pop)) > 1000, "the 2nd MATH popup opens over 80 parens (%d pixels differ)" % len(diff(home, pop)))
only_black(home, g(MANY + POP + TAB + "clear . .", "popup_after"), "popup closed")

hp = g(MANY + POP + TAB + HELP, "help")
check(sum(1 for x in range(320) if hp.get((x, 35)) == 0) > 250, "HELP opens over 80 parens (black title bar)")
only_black(pop, g(MANY + POP + TAB + HELP + "clear . .", "help_after"), "HELP closed, the menu as it was")
sys.exit(1 if fails else 0)
