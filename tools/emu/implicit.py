"""Implicit equations and inequality shading on SymCE's grapher (docs/TI84CE-KNOWLEDGE.md, "Implicit").

    python3 implicit.py [case ...]      one emulator run each; default: all
    cases: circle hyper parab disk mixed trace time
    (run each under the machine's emulator lock; PNGs go to _png/imp_*.png, or $IMPL_PNG)

A Yn with a Y that is a relation (or relations joined by and / or) is drawn by SymCE's marching squares
grid: = a thin curve, inequalities a tinted region with a solid (<= >=) or dashed (< >) boundary. TI's own
graph says ERR:INVALID for any Y in an equation, so nothing TI draws is replaced. Pixels are checked
against Python's sign test at each pixel (tools/graph_check.py implicit_pixels). Run `make -C symce emu`
(or copy the build into this directory) first.
"""
import os, sys
import graph as G
from graph import grab, cpng, keys_for, report, curve_ok, PROLOGUE, PW, PH, BLUE, RED, BLACK
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import graph_check as GC

OUT = os.environ.get("IMPL_PNG", G.PNG)
WHITE = 0xFFFF
Y = "alpha 1"
REL = {"=": "1", "<": "5", ">": "3", "<=": "6", ">=": "4"}


def rel(r):
    return "2nd math " + REL[r] + " "


def circ(op, r2):
    return "xton ^2 + %s ^2 %s%s" % (Y, rel(op), r2)


# name: (Y= keys per equation, [(equation index, pen, f(x, y) >= 0 inside, kind)])
CASES = {
    "circle": ([circ("=", "2 5")], [(BLUE, lambda x, y: x * x + y * y - 25, "=")]),
    "hyper": (["xton ^2 / 9 - %s ^2 / 4 %s 1" % (Y, rel("="))], [(BLUE, lambda x, y: x * x / 9 - y * y / 4 - 1, "=")]),
    "parab": (["%s %s xton ^2" % (Y, rel(">"))], [(BLUE, lambda x, y: y - x * x, "<")]),
    "disk": ([circ("<=", "9")], [(BLUE, lambda x, y: 9 - x * x - y * y, "<=")]),
}
MIXED = ["sin xton )", circ("<=", "2 5"), "%s %s xton ^2 / 3 - 8" % (Y, rel("<"))]


def shot(name, funcs, pre=""):
    px = grab(keys_for(funcs, pre=pre))
    os.makedirs(OUT, exist_ok=True)
    cpng(px, os.path.join(OUT, "imp_" + name + ".png"))
    return px


def axes(c, r):
    return abs(c - 159) <= 4 or abs(r - 120) <= 4


def one(name):
    funcs, checks = CASES[name]
    px = shot(name, funcs)
    ok = True
    for pen, f, kind in checks:
        bad, first = GC.implicit_pixels(lambda c, r: px.get((c, r)), f, kind, pen, axes)
        ok = report(not bad, "%s: pixels follow the sign of f (%d wrong) %s" % (name, bad, first)) and ok
    return ok


def mixed():
    px = shot("mixed", MIXED)
    ok = curve_ok(px, __import__("math").sin, BLUE, "mixed: Y1=sin(X) over/under the shading", range(10, 319, 19), skip=0)
    rgb = lambda v: ((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
    r, g, b = rgb(px[(130, 80)])                                # inside Y2's disk: a red tint
    ok = report(r > 240 and g < 230 and abs(g - b) < 12, "mixed: Y2 tinted red inside the disk (%s)" % ((r, g, b),)) and ok
    r, g, b = rgb(px[(20, 200)])                                # under Y3's parabola: a gray tint
    ok = report(abs(r - g) < 12 and abs(g - b) < 12 and r < 230, "mixed: Y3 tinted gray (%s)" % ((r, g, b),)) and ok
    ok = report(px[(130, 20)] == WHITE, "mixed: white above Y3's parabola and outside the disk") and ok
    ok = report(px[(159, 60)] == RED, "mixed: Y2's solid boundary at the top of the disk") and ok
    return ok


def trace():
    """TRACE skips an implicit Yn: with Y1 = X/2 and Y2 = the circle, UP stays on Y1."""
    funcs = ["xton / 2", circ("=", "2 5")]
    a = grab(keys_for(funcs, "trace w700 "))
    b = grab(keys_for(funcs, "trace w700 up w700 "))
    cpng(b, os.path.join(OUT, "imp_trace_up.png"))
    lab = lambda px: [px.get((x, y)) for y in range(0, 16) for x in range(0, 110)]
    ok = report(lab(a) == lab(b) and a.get((150, 5)) is not None, "TRACE then UP: the label stays on Y1 (the implicit Y2 is skipped)")
    # only an implicit equation: TRACE must not hang or crash; the picture is the plain graph
    c = grab(keys_for([circ("=", "2 5")], "trace w700 right w300 "))
    ok = report(sum(v != WHITE for v in c.values()) > 600, "TRACE with only an implicit equation: the graph stays up") and ok
    return ok


def timing():
    for name, funcs in (("circle", CASES["circle"][0]), ("disk", CASES["disk"][0]), ("parab", CASES["parab"][0]),
                        ("mixed (sin, disk, parabola)", MIXED)):
        print("%-30s %6d ms" % (name, G.draw_ms(funcs)))
    return True


if __name__ == "__main__":
    want = sys.argv[1:] or list(CASES) + ["mixed", "trace", "time"]
    ok = True
    for n in want:
        ok = (one(n) if n in CASES else {"mixed": mixed, "trace": trace, "time": timing}[n]()) and ok
    print("ALL PASS" if ok else "FAILED")
    sys.exit(0 if ok else 1)
