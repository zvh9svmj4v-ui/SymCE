#!/usr/bin/env python3
"""Settings item 7 (Paren colors): OFF leaves parentheses black, toggling back ON restores the rainbow. Classic and MathPrint, Evo font and TI font.

    python3 parentoggle.py [out_dir]       (out_dir gets rb_off.png, rb_on.png)
"""
import os, sys
import e2e, vram, mpparen

SET = "apps w300 " + "down w150 " * 16 + "enter . . "                       # APPS > SymCE
PAL = {0xD800, 0x0460, 0xB016, 0xE360, 0x04B2, 0x8B60}              # fhook.c .Lpal
KEYS = "( ( 1 + 2 ) * ( 3 + ( 4 - 5 ) ) ) + sin ( ( 6 ) )"
FAILS = []


TI_FONT = "3 . clear . "                                           # settings item 3, after SET


def run(label, pre, mode="C "):
    px, _ = vram.grab(vram.SYMCE + pre + mode + KEYS + " w1500", 0, 240, 1, 1, files=vram.FILES, launch=e2e.LAUNCH, lead=False)
    if len(sys.argv) > 1:
        vram.png({k: v for k, v in px.items() if k[0] < 320}, os.path.join(sys.argv[1], "rb_%s.png" % label), 3)
    return {v for (x, y), v in px.items() if 30 < y} & PAL, sum(1 for v in px.values() if v == 0), mpparen.find(px)


def check(name, ok, why):
    print("%-4s %s: %s" % ("ok" if ok else "FAIL", name, why))
    if not ok:
        FAILS.append(name)


for font, fpre in (("evo", ""), ("ti", SET + TI_FONT)):
    for mname, mode in (("classic", "C "), ("mathprint", "")):
        t = "%s %s " % (font, mname)
        on0, _, f = run(t + "on0", fpre, mode)
        check(t + "default ON: rainbow colours drawn", len(on0) == 3 and (mode or not mpparen.wrong(f)), "%d palette colours" % len(on0))
        off, black, f = run(t + "off", fpre + SET + "7 . clear . ", mode)
        check(t + "OFF: no rainbow colour, text still drawn", not off and black > 200 and (mode or len(f) >= 8 and all(c == {0} for *_, c in f)),
              "%d palette colours, %d black px, %d black parens" % (len(off), black, len(f)))
        on, _, f = run(t + "on", fpre + SET + "7 . 7 . clear . ", mode)
        check(t + "ON again: rainbow back", len(on) == 3 and (mode or not mpparen.wrong(f)), "%d palette colours" % len(on))
sys.exit(1 if FAILS else 0)
