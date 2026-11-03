#!/usr/bin/env python3
"""How long the installer runs, in emulated seconds.

    python3 insttime.py DIR [ROM]      DIR holds SYMCE.8xp and SYMCE1.8xv ..

Arms AsmHook2, launches prgmSYMCE, waits T ms and reads the home screen: "SymCE app installed."
means done. Bisects T (each probe is a fresh emulator run), 0.5 s resolution. Compare a build
before and after a change to the installer.
"""
import os, sys
import screen, bigapp

where = os.path.abspath(sys.argv[1])
rom = sys.argv[2] if len(sys.argv) > 2 else screen.ROM
files = tuple(bigapp.parts(where))


def done(ms):
    r = screen.read([("screen", screen.TEXTSHADOW, screen.COLS * screen.ROWS)],
                    keys="C . apps down enter . clear . action|launch w%d" % ms, files=files, launch="SYMCE",
                    lead=False, rom=rom)
    return any("installed" in l for l in screen.render(r["screen"]))


lo, hi = 0, 1000
while not done(hi):
    lo, hi = hi, hi * 2
    if hi > 64000:
        sys.exit("not done after 32 s")
while hi - lo > 500:
    mid = (lo + hi) // 2 // 100 * 100
    lo, hi = (lo, mid) if done(mid) else (mid, hi)
print("installed after %.1f s of emulated time (%d files, %d bytes)" %
      (hi / 1000.0, len(files) - 1, sum(os.path.getsize(f) for f in files[1:])))
