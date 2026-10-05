"""Stat plots on SymCE's grapher against TI's own graph (GRAPH key setting 4 off), same keys, on the real ROM.

    python3 statplot.py [case ...]          # default: all; `python3 graph.py stat` runs the same

Lists L1..L6 are typed in, Plot1..3 are set up through the STAT PLOT editor, then GRAPH (or ZoomStat). Per case:
the window, deltas, ZPrevious and X / Y / Ans (trace.summ) must be equal, and every mark TI draws -- found from
the lists and TI's own window, at TI's pixel (27 + gx, 210 - gy) -- must show the same 7x7 pattern at SymCE's
pixel for that grid point (SymCE's plot is 319x219 on the same 265x165 grid). xyLine: the line goes through the
points. Cases marked FALL must leave TI's own graph (same screen as TI's)."""
import os, sys
from decimal import Decimal as D, ROUND_HALF_UP
import tigo
from tigo import Snap
from e2e import PROLOGUE, FILES, LAUNCH
import graph as G
import trace as T

PAL = G.PAL
LISTS = {1: "1 2 3 4 5", 2: "2 4 3 5 1", 3: "4 5 1 2 3", 4: "3 1 4 1 5", 5: "2 3 3 4 4 4 5 5 6 6 6 6 7 7 8 1 5", 6: "1 1 2 1 3"}
OFF = "2nd y= 4 enter w300 "
ZS = "zoom 9 w7000 "
DEF = {1: 1, 2: 2, 3: 3}                                  # Plot n's default colour


def lst(n):
    return "2nd ( " + " , ".join(LISTS[n].split()) + " 2nd ) sto 2nd %d enter w300 " % n


ALL = "".join(lst(n) for n in sorted(LISTS))


def nk(v):
    """a number as keys"""
    return " ".join("dp" if c == "." else "(-)" if c == "-" else c for c in v)


BOXL = {1: "1 2 3 4 5 6 7 8 9 30", 2: "-5 1 2 3 4 5 6 7 8 20 40", 3: "1 2 3 4 5 6 7 12.5", 4: "1 2 3 4", 5: "1 1 1 5", 6: "5"}
ALLB = "".join("2nd ( " + " , ".join(nk(v) for v in BOXL[n].split()) + " 2nd ) sto 2nd %d enter w300 " % n for n in sorted(BOXL))


def sel(k):
    return ("right " * k if k else "") + "enter w600 "


def plot(n, t=0, lists=(), mark=0, color=None, axis=0):
    """Plot n on: type t (0 Scatter 1 xyLine 2 Hist 3 ModBox 4 Box 5 NormProb), lists = L digits, mark 0 box 1 cross 2 dot,
    color = palette index. Each plot once per boot: the editor's cursor starts at what is stored."""
    k = "2nd y= " + "down " * (n - 1) + "enter w600 enter w600 down " + (sel(t) if t else "enter w600 ") + "down "
    for d in lists:
        k += "2nd %s enter w600 " % d                       # ENTER moves to the next row
    if t in (0, 1, 3):
        k += (sel(mark) if mark else "") + "down "
    elif t == 5:                                            # Data Axis (X, Y), then Mark
        k += (sel(axis) if axis else "") + "down " + (sel(mark) if mark else "") + "down "
    if color is not None:
        k += sel(color - DEF[n])
    return k + "2nd mode . "


WIN = lambda *v: "window w300 " + "".join(" ".join("dp" if c == "." else "(-)" if c == "-" else c for c in str(x)) + " enter " for x in v) + "2nd mode . "
THREE = ALL + plot(1, 0, (1, 2)) + plot(2, 0, (1, 3), 1) + plot(3, 0, (1, 4), 2)
# name: (keys before GRAPH, keys after GRAPH, [(type, xlist, ylist, mark, color)], fall)
CASES = {
    "zs3": (THREE + ZS, "", [(0, 1, 2, 0, 1), (0, 1, 3, 1, 2), (0, 1, 4, 2, 3)], False),
    "over": (ALL + plot(1, 0, (1, 2)) + plot(2, 0, (1, 2), 1) + plot(3, 0, (1, 2), 2) + ZS, "", [(0, 1, 2, 0, 1), (0, 1, 2, 1, 2), (0, 1, 2, 2, 3)], False),
    "col": (ALL + plot(1, 0, (1, 2), 0, 5) + plot(2, 0, (1, 3), 1, 6) + plot(3, 0, (1, 4), 2, 10) + ZS, "", [(0, 1, 2, 0, 5), (0, 1, 3, 1, 6), (0, 1, 4, 2, 10)], False),
    "clip": (THREE + WIN(2, 4.5, 1, 2, 4, 1), "", [(0, 1, 2, 0, 1), (0, 1, 3, 1, 2), (0, 1, 4, 2, 3)], False),
    "edge": (THREE + WIN(2.01, 4.51, 1, 2.01, 4.01, 1), "", [(0, 1, 2, 0, 1), (0, 1, 3, 1, 2), (0, 1, 4, 2, 3)], False),
    "xy": (ALL + plot(1, 1, (1, 2)) + plot(2, 1, (1, 3), 1) + plot(3, 1, (1, 4), 2) + ZS, "", [(1, 1, 2, 0, 1), (1, 1, 3, 1, 2), (1, 1, 4, 2, 3)], False),
    "xyclip": (ALL + plot(1, 1, (1, 2)) + plot(2, 1, (1, 3), 1) + plot(3, 1, (1, 4), 2) + WIN(2, 4.5, 1, 2, 4, 1), "", [(1, 1, 2, 0, 1), (1, 1, 3, 1, 2), (1, 1, 4, 2, 3)], False),
    "fn": (ALL + plot(1, 0, (1, 3)) + "y= xton 2nd mode . " + ZS, "", [(0, 1, 3, 0, 1)], False),
    "zsonly": (ALL + plot(2, 0, (1, 3), 1) + ZS, "", [(0, 1, 3, 1, 2)], False),
    "nolist": (OFF + "2nd y= enter enter 2nd mode . ", "", [], True),            # Plot1 on, L1 undefined: ERR
    "normz": (ALLB + plot(1, 5, (2,)) + ZS, "", [("N", 2, 0, 1, 0)], False),
    "normy": (ALLB + plot(2, 5, (1,), 1, None, 1) + ZS, "", [("N", 1, 1, 2, 1)], False),
    "normc": (ALLB + plot(3, 5, (2,), 2) + WIN(0, 6, 1, -1, 1, 1), "", [("N", 2, 0, 3, 2)], False),
    "norm1": (ALLB + plot(1, 5, (6,)) + ZS, "", [], True),                  # one value: ZoomStat's degenerate window is TI's error
    "normt": (ALLB + plot(1, 5, (4,), 0, None, 1) + WIN(-2, 2, 1, 0, 6, 1), "", [("N", 4, 1, 1, 0)], False),
    "boxz": (ALLB + plot(1, 4, (1,)) + ZS, "", [("B", 1, None, 1, 0)], False),
    "mboxz": (ALLB + plot(1, 3, (2,)) + ZS, "", [("B", 2, None, 1, 1)], False),
    "mboxc": (ALLB + plot(2, 3, (2,), 1, 7) + WIN(0, 30, 1, -10, 10, 1), "", [("B", 2, None, 2, 1, 7)], False),       # clipped, cross marks, a colour
    "mboxd": (ALLB + plot(3, 3, (2,), 2) + WIN(-4, 50, 1, -10, 10, 1), "", [("B", 2, None, 3, 1)], False),        # dot marks
    "boxclip": (ALLB + plot(1, 4, (1,)) + WIN(4, 25, 1, -10, 10, 1), "", [("B", 1, None, 1, 0)], False),
    "boxedge": (ALLB + plot(1, 4, (1,)) + WIN(3.1, 29.5, 1, -10, 10, 1), "", [("B", 1, None, 1, 0)], False),     # Q1 on gx = -1
    "mfence": (ALLB + plot(1, 3, (3,)) + WIN(0, 15, 1, -10, 10, 1), "", [("B", 3, None, 1, 1)], False),         # a value on the fence is not an outlier
    "boxf": (ALLB + plot(1, 4, (4, 5)) + WIN(0, 6, 1, -10, 10, 1), "", [("B", 4, 5, 1, 0)], False),            # Freq
    "box1": (ALLB + plot(1, 4, (6,)) + WIN(0, 15, 1, -10, 10, 1), "", [("B", 6, None, 1, 0)], False),         # one value
    "hista": (ALL + plot(1, 2, (5,)) + WIN(0, 12, 2, -2, 8, 1), "", [("H", 5, None, 1)], False),
    "histc": (ALL + plot(1, 2, (5,)) + WIN(1.5, 6.5, 1, 0, 3, 1), "", [("H", 5, None, 1)], False),
    "histf": (ALL + plot(2, 2, (1, 6)) + WIN(0, 12, 1, 0, 10, 1), "", [("H", 1, 6, 2)], False),
    "histz": (ALL + plot(1, 2, (5,)) + ZS, "", [("H", 5, None, 1)], False),
    "histn": (ALL + plot(1, 2, (5,)) + WIN(-3, 5, 1, -2, 8, 1), "", [("H", 5, None, 1)], False),     # bins start below the data
}
RAM = T.RAM + [(0xD0227E, 54), (0xD01E33, 0x30)]


def run(keys, after, side):
    K = PROLOGUE + (G.TI_OFF if side == "ti" else "") + keys + "graph . . " + after + "@a"
    return tigo.go(K, [Snap("a", rect=(0, 0, 320, 240), ram=RAM)], files=FILES, launch=LAUNCH)["a"]


def real(b):
    if not any(b):
        return D(0)
    d = "".join("%02x" % x for x in b[2:9])
    return (-1 if b[0] & 0x80 else 1) * D(d[0] + "." + d[1:]) * D(10) ** (b[1] - 0x80)


def grid(v, mn, dl):
    return int(((D(v) - mn) / dl).quantize(D(1), rounding=ROUND_HALF_UP))


def gpx(g):
    return (g * 318 + 132) // 264


def gpy(g):
    return ((164 - g) * 218 + 82) // 164


def marks(r, spec):
    """[(plot spec, gx, gy)] for the points inside the grid, from TI's window."""
    mn, my = real(T.rdb(r, 0xD01E33, 9)), real(T.rdb(r, 0xD01E4E, 9))
    dx, dy = real(T.rdb(r, 0xD01FAE, 9)), real(T.rdb(r, 0xD01FB7, 9))
    out = []
    for p in spec:
        xs, ys = LISTS[p[1]].split(), LISTS[p[2]].split()
        for a, b in zip(xs, ys):
            gx, gy = grid(a, mn, dx), grid(b, my, dy)
            out.append((p, gx, gy))
    return out


def window(px, cx, cy, rect, r=3):
    return {(i, j): px.get((cx + i, cy + j)) for i in range(-r, r + 1) for j in range(-r, r + 1)
            if rect[0] <= cx + i < rect[2] and rect[1] <= cy + j < rect[3]}


def hist(r, spec, a, b):
    """Histogram plots: the bins as the model says (bins of Xscl from Xmin, [left, right), Freq weights); on TI's pixels and on
    SymCE's the edges, tops and the baseline are in the plot colour and the middle of a tall bin is not."""
    mn, mx, sc = real(T.rdb(r, 0xD01E33, 9)), real(T.rdb(r, 0xD01E3C, 9)), real(T.rdb(r, 0xD01E45, 9))
    my, dx, dy = real(T.rdb(r, 0xD01E4E, 9)), real(T.rdb(r, 0xD01FAE, 9)), real(T.rdb(r, 0xD01FB7, 9))
    for _, xl, fl, pc in spec:
        c = PAL[DEF[pc]]
        xs = [D(v) for v in LISTS[xl].split()]
        fs = [D(v) for v in LISTS[fl].split()] if fl else [D(1)] * len(xs)
        nb = int(((mx - mn) / sc).to_integral_value(rounding="ROUND_FLOOR")) + 1
        cnt = [D(0)] * nb
        for x, f in zip(xs, fs):
            k = int(((x - mn) / sc).to_integral_value(rounding="ROUND_FLOOR"))
            if 0 <= k < nb:
                cnt[k] += f
        g = lambda v, m, d: int(((v - m) / d).quantize(D(1), rounding=ROUND_HALF_UP))
        y0 = g(D(0), my, dy)
        spots = []                                           # (gx, gy, wanted)
        for gx in range(10, 255, 20):
            spots.append((gx, y0, True))                      # the baseline
        for k, n in enumerate(cnt):
            if not n:
                continue
            ga, gb, gt = g(mn + k * sc, mn, dx), g(mn + (k + 1) * sc, mn, dx), g(n, my, dy)
            top = min(gt, 164)
            for gx in (ga, gb):
                if 0 <= gx <= 264:
                    spots.append((gx, (max(y0, 0) + top) // 2, True))
            if gt <= 164 and gt >= 0:
                spots.append(((ga + gb) // 2, gt, True))
            if top - max(y0, 0) > 12 and 0 <= (ga + gb) // 2 <= 264 and gb - ga > 12:
                spots.append(((ga + gb) // 2, (max(y0, 0) + top) // 2, False))      # inside the bin: white
        for gx, gy, want in spots:
            if not (0 <= gx <= 264 and 0 <= gy <= 164):
                continue
            pa = [a["px"].get((gpx(gx) + i, gpy(gy) + j)) for i in (-1, 0, 1) for j in (-1, 0, 1)]
            pb = [b["px"].get((27 + gx + i, 210 - gy + j)) for i in (0, 1) for j in (0, 1)]
            for who, p in (("SymCE", pa), ("TI", pb)):
                if (c in p) != want:
                    return "hist spot grid (%d, %d) %s: %s in the plot colour, %s" % (gx, gy, "should be" if want else "should not be", who, "not" if want else "is")
    return None


def quart(xs, fs):
    """Q1, median, Q3, the whisker ends and the outliers, as the 1-Var Stats rule says (Decimal model)."""
    v = sorted(sum(([x] * int(f) for x, f in zip(xs, fs)), []))
    med = lambda a: a[len(a) // 2] if len(a) % 2 else (a[len(a) // 2 - 1] + a[len(a) // 2]) / 2
    h = len(v) // 2
    q1, q3 = (med(v[:h]), med(v[len(v) - h:])) if h else (v[0], v[0])
    return v, q1, med(v), q3


def box(r, spec, a, b):
    """Box and ModBox: the model's quartiles, whiskers and outliers; the edges, whiskers, caps and outlier marks are in the
    plot colour on both screens, the inside of the box is not, and every outlier mark equals TI's."""
    mn, dx = real(T.rdb(r, 0xD01E33, 9)), real(T.rdb(r, 0xD01FAE, 9))
    for _, xl, fl, pc, mod, *co in spec:
        c = PAL[co[0] if co else DEF[pc]]
        xs = [D(t) for t in BOXL[xl].split()]
        fs = [D(t) for t in BOXL[fl].split()] if fl else [D(1)] * len(xs)
        v, q1, md, q3 = quart(xs, fs)
        iqr = q3 - q1
        keep = [t for t, f in zip(xs, fs) if f and (not mod or q1 - D("1.5") * iqr <= t <= q3 + D("1.5") * iqr)]
        out = [t for t, f in zip(xs, fs) if f and mod and not (q1 - D("1.5") * iqr <= t <= q3 + D("1.5") * iqr)]
        g = lambda t: int(((t - mn) / dx).quantize(D(1), rounding=ROUND_HALF_UP))
        gq1, gm, gq3, gl, gh = g(q1), g(md), g(q3), g(min(keep)), g(max(keep))
        clip = lambda lo, hi: (max(lo, 0), min(hi, 264))
        spots = []
        for gx in (gq1, gm, gq3):
            for gy in (140, 125):
                spots.append((gx, gy, True))
        lo, hi = clip(gq1, gq3)
        if lo <= hi:
            for gy in (152, 114):
                spots.append(((lo + hi) // 2, gy, True))
        for gx in (gl, gh):
            for gy in (135, 131):
                spots.append((gx, gy, True))
        lo, hi = clip(gl, gq1)
        if lo <= hi and gq1 - gl > 12:
            spots.append(((lo + hi) // 2, 133, True))
        lo, hi = clip(gq3, gh)
        if lo <= hi and gh - gq3 > 12:
            spots.append(((lo + hi) // 2, 133, True))
        if gm - gq1 > 12 and 0 <= (gq1 + gm) // 2 <= 264:
            spots.append(((gq1 + gm) // 2, 125, False))                 # inside the box: white
        for gx, gy, want in spots:
            if not (0 <= gx <= 264 and 0 <= gy <= 164):
                continue
            pa = [a["px"].get((gpx(gx) + i, gpy(gy) + j)) for i in (-1, 0, 1) for j in (-1, 0, 1)]
            pb = [b["px"].get((27 + gx + i, 210 - gy + j)) for i in (0, 1) for j in (0, 1)]
            for who, p in (("SymCE", pa), ("TI", pb)):
                if (c in p) != want:
                    return "box spot grid (%d, %d) %s: %s in the plot colour, %s" % (gx, gy, "should be" if want else "should not be", who, "not" if want else "is")
        # a pixel the model says is not drawn: nothing left of / right of the line ends, rows far from the box
        for gx in (0, 100, 200, 264):
            for gy in (30, 60, 100, 160):
                if not any(abs(gx - e) < 8 for e in (gq1, gm, gq3, gl, gh)):
                    if any(a["px"].get((gpx(gx) + i, gpy(gy) + j)) == c for i in (-1, 0, 1) for j in (-1, 0, 1)):
                        return "box: stray colour at grid (%d, %d)" % (gx, gy)
        for t in out:
            gx = g(t)
            if 0 <= gx <= 264:
                wa = window(a["px"], gpx(gx), gpy(133), (0, 0, 319, 219))
                wb = window(b["px"], 27 + gx, 210 - 133, (28, 47, 291, 210))
                d = [k for k in wa if k in wb and wa[k] != wb[k]]
                if d:
                    return "outlier mark %s at grid x %d: %d pixels differ" % (t, gx, len(d))
    return None


def normmarks(r, spec):
    """NormProb: TI's points are the sorted values against invNorm((i - 0.5) / n) on the chosen axis."""
    from statistics import NormalDist
    mn, my = real(T.rdb(r, 0xD01E33, 9)), real(T.rdb(r, 0xD01E4E, 9))
    dx, dy = real(T.rdb(r, 0xD01FAE, 9)), real(T.rdb(r, 0xD01FB7, 9))
    out = []
    for _, xl, ay, pc, mk in spec:
        v = sorted(D(t) for t in BOXL[xl].split())
        for i, t in enumerate(v):
            z = D(NormalDist().inv_cdf((i + 0.5) / len(v))) if len(v) > 1 else D(0)
            gx, gy = (grid(z, mn, dx), grid(t, my, dy)) if ay else (grid(t, mn, dx), grid(z, my, dy))
            out.append(((mk,), gx, gy))
    return out


def check(name, spec, fall):
    a, b = run(CASES[name][0], CASES[name][1], "sym"), run(CASES[name][0], CASES[name][1], "ti")
    sa, sb = T.summ(a), T.summ(b)
    bad = [k for k in sa if k != "cx" and sa[k] != sb[k]]
    if bad:
        return "%s differ" % ",".join(bad)
    if fall:
        return None if a["px"] == b["px"] else "screen differs from TI's"
    if spec and spec[0][0] == "N":
        if a["px"] == b["px"]:
            return "SymCE's screen is TI's (it fell back)"
        ra, rb = (0, 0, 319, 219), (28, 47, 291, 210)
        for p, gx, gy in normmarks(b, spec):
            if not (0 <= gx <= 264 and 0 <= gy <= 164):
                continue
            wa, wb = window(a["px"], gpx(gx), gpy(gy), ra), window(b["px"], 27 + gx, 210 - gy, rb)
            c = PAL[DEF[spec[0][3]]]
            d = [k for k in wa if k in wb and wa[k] != wb[k] and c in (wa[k], wb[k])]      # (the axes may sit a pixel apart)
            if d:
                return "norm mark at grid (%d, %d): %d pixels differ (first %s: SymCE %04X TI %04X)" % (gx, gy, len(d), d[0], wa[d[0]], wb[d[0]])
        return None
    if spec and spec[0][0] == "B":
        return "SymCE's screen is TI's (it fell back)" if a["px"] == b["px"] else box(b, spec, a, b)
    if spec and spec[0][0] == "H":
        return hist(b, spec, a, b)
    ra, rb = (0, 0, 319, 219), (28, 47, 291, 210)      # TI's grid minus its edge row and column (the frame shows there)
    for p, gx, gy in marks(b, spec):
        if not (0 <= gx <= 264 and 0 <= gy <= 164) or p[0] == 1:
            continue
        wa = window(a["px"], gpx(gx), gpy(gy), ra)
        wb = window(b["px"], 27 + gx, 210 - gy, rb)
        ks = [k for k in wa if k in wb]
        d = [k for k in ks if wa[k] != wb[k]]
        if d:
            return "mark %s at grid (%d, %d): %d of %d pixels differ (first %s: SymCE %04X TI %04X)" % (p, gx, gy, len(d), len(ks), d[0], wa[d[0]], wb[d[0]])
    for p, gx, gy in marks(b, spec):                         # xyLine: the colour is on both sides of the joins
        if p[0] != 1 or not (0 <= gx <= 264 and 0 <= gy <= 164):
            continue
        c = PAL[p[4]]
        if a["px"].get((gpx(gx), gpy(gy))) != c or b["px"].get((27 + gx, 210 - gy)) != c:
            return "xyLine point (%d, %d) not in colour %04X" % (gx, gy, c)
    return None


# ---- TRACE over plots: SymCE's label, X= Y= bar, cursor and X, Y variables against TI's, step by step ----
# name: (keys before GRAPH, [(keys, (plot, point) the cursor is on, or None for a function / no mark)])
TRC = {
    "t3": (THREE + ZS, [("trace", (1, 0)), ("right", (1, 1)), ("right right", (1, 3)), ("down", (2, 3)),
                        ("up", (1, 3)), ("left left", (1, 1))]),
    "tclip": (THREE + WIN(2, 4.5, 1, 1.5, 4.5, 1), [("trace", (1, 0)), ("right", (1, 1)), ("right", (1, 2)), ("right", (1, 3)), ("down", (2, 3))]),
    "txy": (ALL + plot(1, 1, (1, 2)) + plot(2, 1, (1, 3), 1) + plot(3, 1, (1, 4), 2) + ZS,
            [("trace", (1, 0)), ("right right", (1, 2)), ("down", (2, 2)), ("down", (3, 2)), ("left", (3, 1))]),
    "tfn": (ALL + plot(1, 0, (1, 3)) + "y= xton 2nd mode . " + ZS,
            [("trace", (1, 0)), ("right right", (1, 2)), ("up", (0, None)), ("up", (1, 2)), ("down", (0, None)), ("down", (1, 2))]),
    "tend": (THREE + ZS, [("trace", (1, 0)), ("right right right right", (1, 4)), ("right", (1, 4)), ("left", (1, 3))]),
    "tp2": (ALL + plot(2, 0, (1, 3), 1) + ZS, [("trace", (2, 0)), ("right", (2, 1)), ("up", (2, 1)), ("down", (2, 1))]),
    "tenter": (THREE + ZS, [("trace", (1, 0)), ("right right right", (1, 3)), ("enter", (1, 0))]),
    "tdigit": (THREE + ZS, [("trace", (1, 0)), ("5", None)]),
}
TMASK = ["###   ###", "#..# #..#", "#...#...#", " #..#..# ", "  #####  ", " #..#..# ", "#...#...#", "#..# #..#", "###   ###"]
TMASK2 = ["###   ###", "#### ####", "#########", " ###.### ", "  #...#  ", " ###.### ", "#########", "#### ####", "###   ###"]


def label_set(px, xa, xb, ya, yb, c):
    """The pixels of colour c in the label box, as (x - min x, y - min y)."""
    ps = [(x, y) for (x, y), v in px.items() if xa <= x < xb and ya <= y < yb and v == c]
    if not ps:
        return set()
    mx, my = min(x for x, _ in ps), min(y for _, y in ps)
    return {(x - mx, y - my) for x, y in ps}


def tracecase(name, show=False):
    pre, steps = TRC[name]
    res = {}
    for side in ("sym", "ti"):
        K = PROLOGUE + (G.TI_OFF if side == "ti" else "") + pre + "graph . . "
        sn = []
        for i, (k, _) in enumerate(steps):
            K += k + " w700 @s%d " % i
            sn.append(Snap("s%d" % i, rect=(0, 0, 320, 240), ram=RAM))
        res[side] = tigo.go(K, sn, files=FILES, launch=LAUNCH)
    ok = True
    for i, (k, ex) in enumerate(steps):
        a, b = res["sym"]["s%d" % i], res["ti"]["s%d" % i]
        if name == "tdigit" and i == 1:       # HOME with the digit typed: the entry line (SymCE's HOME has lost TI's history above it, as on any graph-to-HOME exit)
            n = sum(1 for p in range(320 * 222, 320 * 240) if a["px"].get((p % 320, p // 320)) != b["px"].get((p % 320, p // 320)))
            print("  %-8s %-20s %s" % (name, k, "ok" if n < 30 else "FAIL %d px differ" % n))
            ok = ok and n < 30
            if os.environ.get("STATPNG"):
                T.png(a["px"], os.environ["STATPNG"] + "/%s_sym_%d.png" % (name, i))
                T.png(b["px"], os.environ["STATPNG"] + "/%s_ti_%d.png" % (name, i))
            continue
        sa, sb = T.summ(a), T.summ(b)
        bad = [f for f in sa if f != "cx" and sa[f] != sb[f]]
        tl = T.text_lines(b["px"])
        sl = T.text_lines(a["px"], tl)
        err = []
        if bad:
            err.append("%s differ" % ",".join(bad))
        if [l[4] for l in tl] != [l[4] for l in sl] or not tl:
            err.append("bar text differs")
        if ex:
            plot_n, idx = ex
            c = PAL[DEF[plot_n] if plot_n else 1]
            la, lb = label_set(a["px"], 0, 150, 0, 14, c), label_set(b["px"], 20, 170, 30, 45, c)
            if plot_n and (not lb or la != lb):    # (the function label is trace.py's)
                err.append("label differs (SymCE %d px, TI %d px)" % (len(la), len(lb)))
        if ex and ex[1] is not None:
            xs, ys = LISTS[PLOTLISTS[(name, ex[0])][0]].split(), LISTS[PLOTLISTS[(name, ex[0])][1]].split()
            mn, my = real(T.rdb(b, 0xD01E33, 9)), real(T.rdb(b, 0xD01E4E, 9))
            dx, dy = real(T.rdb(b, 0xD01FAE, 9)), real(T.rdb(b, 0xD01FB7, 9))
            gx, gy = grid(xs[ex[1]], mn, dx), grid(ys[ex[1]], my, dy)
            if 0 <= gx <= 264 and 0 <= gy <= 164:
                wa = window(a["px"], gpx(gx), gpy(gy), (0, 0, 319, 219), 4)
                wb = window(b["px"], 27 + gx, 210 - gy, (28, 47, 291, 210), 4)
                d = [q for q in wa if q in wb and TMASK[q[1] + 4][q[0] + 4] == "#" == TMASK2[q[1] + 4][q[0] + 4] and (wa[q], wb[q]) != (0, 0)]   # the ring both frames draw
                if d:
                    err.append("cursor window: %d px differ (first %s: SymCE %04X TI %04X)" % (len(d), d[0], wa[d[0]], wb[d[0]]))
        print("  %-8s %-20s %s" % (name, k, "ok" if not err else "FAIL " + "; ".join(err)))
        if show and os.environ.get("STATPNG"):
            T.png(a["px"], os.environ["STATPNG"] + "/%s_sym_%d.png" % (name, i))
            T.png(b["px"], os.environ["STATPNG"] + "/%s_ti_%d.png" % (name, i))
        ok = ok and not err
    return ok


# the lists each plot of a TRC case uses (X list, Y list)
PLOTLISTS = {}
for _n in ("t3", "tclip", "txy"):
    PLOTLISTS.update({(_n, 1): (1, 2), (_n, 2): (1, 3), (_n, 3): (1, 4)})
PLOTLISTS.update({("tfn", 1): (1, 3), ("tp2", 2): (1, 3)})
PLOTLISTS.update({("tend", 1): (1, 2), ("tenter", 1): (1, 2), ("tdigit", 1): (1, 2)})


def main(names=None):
    names = names or list(CASES) + list(TRC)
    ok = True
    for n in names:
        if n in TRC:
            ok = tracecase(n, show=True) and ok
            continue
        e = check(n, CASES[n][2], CASES[n][3])
        print("%-8s %s" % (n, "ok" if e is None else "FAIL " + e))
        ok = ok and e is None
    return ok


if __name__ == "__main__":
    sys.exit(0 if main(sys.argv[1:]) else 1)
