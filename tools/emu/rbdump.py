#!/usr/bin/env python3
"""Per-cell ink colours of the Classic home screen's text rows (dev helper): python3 rbdump.py "C ( ( 1 ) )" """
import sys, e2e, vram
PAL = [0xD800, 0x0460, 0xB016, 0xE360, 0x04B2, 0x8B60]
def cells(px, y0, y1):
    out = []
    for c in range(26):
        cols = {v for (x, y), v in px.items() if 12 * c <= x < 12 * c + 12 and y0 <= y < y1 and v != 0xFFFF}
        out.append(cols)
    return out
if __name__ == "__main__":
    px, _ = vram.grab(vram.SYMCE + sys.argv[1] + " w300", 0, 240, 1, 1, files=vram.FILES, launch=e2e.LAUNCH, lead=False)
    for r in range(10):
        cs = cells(px, 24 * r, 24 * r + 24)
        if any(cs): print(r, " ".join(("%04X" % min(s) if len(s) == 1 else ("-" if not s else "?%s" % sorted(s))) for s in cs))
