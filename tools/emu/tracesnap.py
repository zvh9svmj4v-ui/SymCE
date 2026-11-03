"""TRACE snap (function mode): a step that passes a zero, Y-intercept, intersection, minimum or maximum stops on it,
real ROM, SymCE's own grapher (settings item 6 on, the default).

    python3 tracesnap.py [case ...]      # default: all

Per case: the keys, then one RIGHT/LEFT at a time with X, Y and the label (ptb) read from the grapher's work struct after each.
The checks: where it stops and what it calls it, nothing false for 1/X and tan(X), and that the step after a POI lands on the
next grid point (the one the step was aiming at; the other way, the one it left). PNG of a Zero to _png/symce_snap_zero.png."""
import os, sys
import tigo
from tigo import Snap, png
from e2e import PROLOGUE, FILES, LAUNCH
import graph as G

GW = 0xD0EF00
# offsets in struct gw (src/graph.c) of ptb, xv/yv, tps: __builtin_offsetof, 2026-10-08; tps is checked to be 0 or 1
OPTB, OXV, OTPS = 2695, 2138, 2764
RAM = [(GW + OXV, 18), (GW + OPTB, 28), (GW + OTPS, 1)]
TS = 2 * 20 / 264                                    # TraceStep at ZStandard
Z = lambda n: "zoom " + n + " w7000 "


def tireal(b):
    d = "".join("%02x" % x for x in b[2:9])
    v = int(d) * 10.0 ** (b[1] - 0x80 - 13)
    return -v if b[0] & 0x80 else v


def sample(r):
    xv = b"".join(r["ram"][a] for a in sorted(r["ram"]) if GW + OXV <= a < GW + OXV + 18)
    pt = b"".join(r["ram"][a] for a in sorted(r["ram"]) if GW + OPTB <= a < GW + OPTB + 28)
    tp = [r["ram"][a] for a in r["ram"] if a >= GW + OTPS][0][0]
    return tireal(xv[:9]), tireal(xv[9:18]), pt.split(b"\0")[0].decode("latin1"), tp


def run(setup, keys, n, key="right", shot=None):
    """setup: Y= keys, zoom keys; n presses of `key` after TRACE. Returns [(X, Y, label)] for the start and each press."""
    K = PROLOGUE + "y= " + setup[0] + " 2nd mode . graph . . " + setup[1] + "trace w500 "
    names = ["s%d" % i for i in range(n + 1)]
    K += "@s0 " + "".join("%s w%d @%s " % (key, 450, nm) for nm in names[1:])
    snaps = [Snap(nm, ram=RAM) for nm in names]
    if shot is not None:
        K = K.replace("@s%d " % shot, "@s%d @shot " % shot)
        snaps.append(Snap("shot", rect=(0, 0, 320, 240)))
    got = tigo.go(K, snaps, files=FILES, launch=LAUNCH)
    out = [sample(got[nm]) for nm in names]
    assert all(o[3] in (0, 1) for o in out), "struct gw offsets moved: recompute OPTB/OXV/OTPS"
    return out, got.get("shot")


def near(a, b, t=2e-3):
    return abs(a - b) < t


def check(name, ok, text):
    G.report(ok, "%-8s %s" % (name, text))
    return ok


def stops(res):
    return [(i, x, y, l) for i, (x, y, l, _) in enumerate(res) if l]


def parab():
    """Y1 = X^2-4, Y2 = X: Zero at 2, Intersection at (1+sqrt17)/2; the step after goes on from the grid, left goes back."""
    setup = ("xton ^2 - 4 down xton", Z("6"))
    res, _ = run(setup, None, 20)
    st = stops(res)
    zi = [s for s in st if s[3] == "Zero"]
    ii = [s for s in st if s[3] == "Intersection"]
    ok = check("zero", len(zi) == 1 and near(zi[0][1], 2.0) and abs(zi[0][2]) < 0.13, "Zero at X=2: %s" % zi)
    ok = check("isect", len(ii) == 1 and near(ii[0][1], (1 + 17 ** .5) / 2), "Intersection at X=%.4f: %s" % ((1 + 17 ** .5) / 2, ii)) and ok
    # grid: the point before the POI, then the next press is one TraceStep further than that
    for s in (zi + ii):
        i = s[0]
        a, b = res[i - 1][0], res[i + 1][0]
        ok = check("grid", near(b - a, TS, 1e-4) and not res[i + 1][2], "after %s: X %.4f -> %.4f -> %.4f (step %.4f)" % (s[3], a, s[1], b, TS)) and ok
    pic = None
    if zi:
        _, pic = run(setup, None, zi[0][0], shot=zi[0][0])
        png(pic["px"], os.path.join(G.PNG, "symce_snap_zero.png"))
    return ok


def off():
    """Settings item 6 off: the same keys never stop on anything (the grid only: 14 steps = 14 * TraceStep), and item 6 again turns it on."""
    K = PROLOGUE + G.SNAP_OFF + "y= xton ^2 - 4 2nd mode . graph . . " + Z("6") + "trace w500 " + "right w450 " * 14 + "@a"
    a = sample(tigo.go(K, [Snap("a", ram=RAM)], files=FILES, launch=LAUNCH)["a"])
    K = K.replace("trace w500", "trace w500 ").replace(G.SNAP_OFF, G.SNAP_OFF + G.SNAP_OFF)
    b = sample(tigo.go(K, [Snap("a", ram=RAM)], files=FILES, launch=LAUNCH)["a"])
    return check("off", not a[2] and near(a[0], 14 * TS) and b[2] == "Zero" and near(b[0], 2.0), "off: %s; off then on: %s" % (a, b))


def left_back():
    """Right to the Zero of X^2-4, then LEFT lands on the point it left, then RIGHT stops on the Zero again."""
    setup = ("xton ^2 - 4", Z("6"))
    K = PROLOGUE + "y= " + setup[0] + " 2nd mode . graph . . " + setup[1] + "trace w500 " + "right w450 " * 14 + "@a left w450 @b right w450 @c"
    got = tigo.go(K, [Snap(n, ram=RAM) for n in "abc"], files=FILES, launch=LAUNCH)
    a, b, c = (sample(got[n]) for n in "abc")
    return check("left", a[2] == "Zero" and not b[2] and near(b[0], 13 * TS) and c[2] == "Zero" and near(c[0], 2.0),
                 "Zero %s, LEFT %s, RIGHT %s" % (a, b, c))


def cubic():
    """Y1 = X^3-3X: right: Minimum at X=1 (Y=-2), Zero at sqrt3; left: Maximum at -1, Zero at -sqrt3."""
    ok = True
    for key, want in (("right", [("Minimum", 1.0), ("Zero", 3 ** .5)]), ("left", [("Maximum", -1.0), ("Zero", -(3 ** .5))])):
        res, _ = run(("xton ^ 3 - 3 xton", Z("6")), None, 14, key)
        st = [(l, x) for _, x, _, l in stops(res)]
        ok = check("cubic", len(st) == 2 and all(a == b[0] and near(x, b[1]) for (a, x), b in zip(st, want)), "%s: %s" % (key, st)) and ok
    return ok


def ink(px):
    """Non-white pixels on the label row and below (the bottom strip, PY = 214 on)."""
    return sum(1 for (x, y), v in px.items() if y >= 214 and v != 0xFFFF and v != (255, 255, 255))


def yint():
    """Y-intercept, on screen and in RAM. (a) Y1 = X^2-4 from the centre: X=0 is a grid point, LEFT, LEFT, RIGHT, RIGHT reaches it.
    (b) Window -9.97..10.03: X=0 falls between two grid points: LEFT then RIGHT (the first step has no sample behind it) stops on X=0 with Y=-4 (the bar redrawn, not left at
    the grid point's Y). Both: the label's pixels are on the bottom strip, and gone after the next press."""
    ok = True
    for name, win, keys, n in (("grid", Z("6"), "left w450 left w450 right w450 right w450", 1),
                               ("off-grid", "window (-) 9 dp 9 7 enter 1 0 dp 0 3 enter 2nd mode . graph . . ", "left w450 right w450", 0)):
        K = PROLOGUE + "y= xton ^2 - 4 2nd mode . graph . . " + win + "trace w500 " + keys + " @a @pa right w450 @b @pb"
        got = tigo.go(K, [Snap("a", ram=RAM), Snap("pa", rect=(0, 0, 320, 240)), Snap("b", ram=RAM), Snap("pb", rect=(0, 0, 320, 240))],
                      files=FILES, launch=LAUNCH)
        a, b = sample(got["a"]), sample(got["b"])
        ia, ib = ink(got["pa"]["px"]), ink(got["pb"]["px"])
        if name == "off-grid":
            png(got["pa"]["px"], os.path.join(G.PNG, "symce_snap_yint.png"))
        ok = check("yint", a[2] == "Y-intercept" and near(a[0], 0.0) and near(a[1], -4.0) and not b[2] and ia > 100 and got['pa']['px'] != got['pb']['px'] and 
                   (name == "off-grid" or near(b[0], TS)),
                   "%s: %s, ink %d; next: %s, ink %d" % (name, a, ia, b, ib)) and ok
    return ok


def recip():
    """1/(X-.05): the sign change at the pole is not a Zero (Y is huge there); 1/X: X=0 is undefined, nothing snaps."""
    ok = True
    for name, y in (("1/(X-.05)", "1 / ( xton - dp 0 5 )"), ("1/X", "1 / xton")):
        res, _ = run((y, Z("6")), None, 8)
        bad = [s for s in stops(res) if s[3] == "Zero"]
        ok = check("recip", not bad, "%s no Zero: %s" % (name, stops(res))) and ok
    return ok


def tan():
    """tan(X), ZTrig, Radian: 36 steps right from 0 cross the pole at pi/2: the only stop is the Zero at pi."""
    res, _ = run(("tan xton )", Z("7")), None, 36)
    st = stops(res)
    return check("tan", len(st) == 1 and st[0][3] == "Zero" and near(st[0][1], 3.14159, 4e-3), "stops %s" % st)


CASES = {"parab": parab, "off": off, "left": left_back, "cubic": cubic, "yint": yint, "recip": recip, "tan": tan}

if __name__ == "__main__":
    os.makedirs(G.PNG, exist_ok=True)
    want = sys.argv[1:] or list(CASES)
    ok = True
    for n in want:
        ok = CASES[n]() and ok
    print("PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)
