"""Measure / compare the remaining DRAW and graph fallbacks against TI's own graph.
    python3 fallbacks.py shot <ti|sym> "<keys with @a @b snapshots>"   # exploration: PNGs to $FB_OUT"""
import os, sys
import tigo
from tigo import Snap, png
from e2e import PROLOGUE, FILES, LAUNCH
import graph as G

OUT = os.environ.get("FB_OUT", "_png")
LAY = 0xD09466
Y2 = "y= xton ^ 2 2nd mode . "


def run(side, keys, snaps=("a",), rect=(0, 0, 320, 240), ram=()):
    K = PROLOGUE + (G.TI_OFF if side == "ti" else "") + keys
    sn = [Snap(n, ram=[(LAY, 165 * 133)]) if n[0] == "L" else Snap(n, rect=(0, 214, 320, 240) if n[0] == "R" else (60, 40, 280, 200) if n[0] == "P" else rect, ram=list(ram)) for n in snaps]
    return tigo.go(K, sn, files=FILES, launch=LAUNCH)


if __name__ == "__main__":
    side, keys = sys.argv[1], sys.argv[2]
    names = sorted(set(w[1:] for w in keys.split() if w[0] == "@"))
    r = run(side, keys, names)
    os.makedirs(OUT, exist_ok=True)
    for n in names:
        if n[0] == "L":
            lay = r[n]["ram"][LAY]
            print(n, "layer px on:", sum(bin(b).count("1") > 0 and ((b >> 4) != 0) + ((b & 15) != 0) for b in lay))
            open(os.path.join(OUT, "%s_%s.bin" % (sys.argv[3], n)), "wb").write(lay)
            continue
        import pickle
        pickle.dump(r[n]["px"], open(os.path.join(OUT, "%s_%s.pkl" % (sys.argv[3], n)), "wb"))
        png(r[n]["px"], os.path.join(OUT, "%s_%s.png" % (sys.argv[3] if len(sys.argv) > 3 else "x", n)), scale=2,
            rect=(0, 214, 320, 240) if n[0] == "R" else (60, 40, 280, 200) if n[0] == "P" else None)


def pixels(lay):
    """Layer bytes -> {(gx, gy): nibble} of the drawn pixels (gy up, 0..164)."""
    o = {}
    for r in range(165):
        for j in range(133):
            b = lay[r * 133 + j]
            for h, n in ((0, b >> 4), (1, b & 15)):
                gx = 2 * j + h
                if n and gx <= 264:
                    o[(gx, 164 - r)] = n
    return o
