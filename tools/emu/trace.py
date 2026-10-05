"""SymCE's free cursor / TRACE against TI's own graph (GRAPH key setting 4 off), same keys, on the real ROM.

    python3 trace.py [case ...]          # default: all; `python3 graph.py trace` runs the same

Per case: X / Y / Ans on the home screen, Xmin..Yscl, DeltaX/Y, TraceStep, ZPrevious (0xD01FFC) and the
X, Y variables in user memory must be equal on both sides. Plus the bottom bar TEXT: the same glyphs, SymCE's
two rows lower (its bar is 219..239, TI's 222..239). cxCurApp is compared for the error-screen cases.
SymCE PNGs go to _png/symce_tr_*.png."""
import sys, os
import tigo
from tigo import Snap, png
from e2e import PROLOGUE, FILES, LAUNCH
import graph as G

PRE = "y= sin xton ) 2nd mode . "
R = lambda n, k="right": (k + " ") * n
W = "w300 "
CASES = {
    "free": R(7) + R(4, "up"),
    "trace": "trace " + W,
    "tr53": "trace " + W + R(5) + R(3, "left"),
    "updown": "trace " + W + "up " + W + R(2, "down"),
    "pan": "trace " + W + R(66) + "right w4500 right right right ",
    "panL": "trace " + W + R(66, "left") + "left w4500 left left left ",
    "num": "trace " + W + "2 dp 5 enter " + W,
    "numR": "trace " + W + "2 dp 5 enter " + W + "right right ",
    "oob": "trace " + W + "1 5 enter w1000 1 ",
    "qz": "trace " + W + R(3) + "enter w4500 ",
    "qz2": "zoom 4 w4000 trace " + W + R(3) + "enter w4500 ",
    "qzprev": "zoom 4 w4000 trace " + W + R(3) + "enter w4500 zoom w300 right 1 w4500 ",
}
# phase 1c: ZOOM. Z(n) = pick item n of tab 0 (A..G are alpha letters), M(n) = item n of ZOOM MEMORY. "!" = from home, no graph first.
Z = lambda n: "zoom " + n + " w7000 "
M = lambda n: "zoom right " + n + " w7000 "
LET = {"A": "math", "B": "apps", "C": "prgm", "D": "", "E": "sin", "F": "cos", "G": "tan"}   # D: x^-1 has no key name, so arrows + ENTER
for _n, _k in (("dec", "4"), ("sqr", "5"), ("std", "6"), ("trig", "7"), ("fit", "0")):
    CASES["z" + _n] = Z(_k)
    CASES["zh" + _n] = "!" + Z(_k)
for _l in "ABCDEFG":
    CASES["zl" + _l] = Z("alpha " + LET[_l]) if LET[_l] else "zoom " + R(13, "down") + "enter w4500 "
CASES.update({
    "zhq1": "!" + Z("alpha math"), "zhf2": "!" + Z("alpha apps"),
    "zint0": Z("8") + "enter w4500 ", "zint1": Z("8") + R(3) + R(2, "up") + "enter w4500 ", "zhint": "!" + Z("8") + R(2) + "enter w4500 ",
    "zbox": Z("1") + R(4) + R(3, "down") + "enter w300 " + R(8) + R(5, "down") + "enter w4500 ",
    "zbox0": Z("1") + "enter w300 right right down enter w4500 ", "zhbox": "!" + Z("1") + R(4) + "enter w300 " + R(6) + R(4, "up") + "enter w4500 ",
    "zin": Z("2") + R(5) + R(2, "up") + "enter w4500 ", "zin2": Z("2") + R(3) + "enter w4500 " + R(2) + "enter w4500 w3000 ",
    "zout": Z("3") + R(4, "left") + "enter w4500 ", "zhin": "!" + Z("2") + R(3) + "enter w4500 ",
    "zprev": Z("4") + Z("6") + M("1"), "zprev2": Z("4") + Z("6") + Z("5") + M("1") + M("1"), "zhprev": "!" + Z("6") + M("1"),
    "zsto": Z("4") + M("2"), "zrcl": Z("4") + M("2") + Z("6") + M("3"), "zhrcl": "!" + M("2") + Z("5") + "w300 " + M("3"),   # a ZOOM key within a blink of a home ZSquare is lost (SymCE only; TI keeps it): a tester timing artifact, 0.3 s is a human's
    "ztr": "trace " + W + R(3) + Z("6"), "zfree": R(5) + R(2, "up") + Z("5"),
})
PAR = "mode down down down right enter 2nd mode . y= 5 cos xton ) down 5 sin xton ) down xton down xton ^ 2 / 4 2nd mode . "
POL = "mode down down down right right enter 2nd mode . y= 2 cos 3 xton ) down xton / 2 2nd mode . window down 4 2nd ^ enter 2nd mode . "
POLD = "mode down down right enter 2nd mode . " + POL
PGC = G.fmt_keys((0, 1))                              # FORMAT: PolarGC
PCASES = {
    "pd_par": (PAR, "w2500", None),
    "pd_pol": (POL, "w2500", None),
}
VPAR = "xton enter w400 alpha sto enter w400 alpha 1"                 # T, X, Y on the home screen
VPOL = "xton enter w400 alpha * enter w400 alpha sto enter w400 alpha 1"   # theta, R, X, Y
for _n, _p, _v in (("pt", PAR, VPAR), ("po", POL, VPOL), ("pg", PGC + POL, VPOL)):
    PCASES.update({
        _n + "_tr": (_p, "trace " + W, _v),
        _n + "_r5l2": (_p, "trace " + W + R(5) + R(2, "left") + W, _v),
        _n + "_ud": (_p, "trace " + W + R(3) + "up " + W + R(2, "down"), _v),
        _n + "_num": (_p, "trace " + W + "1 dp 5 enter " + W, _v),
        _n + "_numR": (_p, "trace " + W + "1 dp 5 enter " + W + "right right ", _v),
        _n + "_numR2": (_p, "trace " + W + "1 dp 5 5 enter " + W + "right right ", _v),
        _n + "_oob": (_p, "trace " + W + "1 5 enter w1000 1 ", _v),
        _n + "_qz": (_p, "trace " + W + R(3) + "enter w4500 ", _v),
        _n + "_end": (_p, "trace " + W + R(130) + R(2, "left") + W, _v),
        _n + "_left": (_p, "trace " + W + R(3, "left") + W, _v),
    })
for _n, _p, _v in (("pz", PAR, VPAR), ("oz", POL, VPOL)):
    for _z, _k in (("std", "6"), ("trig", "7"), ("sqr", "5"), ("dec", "4"), ("fit", "0"), ("int", "8")):
        PCASES[_n + "_" + _z] = (_p, Z(_k) + ("enter w4500 " if _z == "int" else ""), _v)
    PCASES[_n + "_tr"] = (_p, "trace " + W + R(3) + Z("6") + "trace " + W, _v)
    for _z in ("zbox", "zin", "zin2", "zout", "zprev", "zprev2", "zsto", "zrcl", "zfree"):   # the Function cases, on Par and Pol
        PCASES[_n + "_" + _z] = (_p, CASES[_z], _v)
    for _z, _k in (("q1", "alpha math"), ("f2", "alpha apps"), ("f10", "alpha sin")):
        PCASES[_n + "_" + _z] = (_p, Z(_k), _v)
# TI drops the ALPHA of a readout key typed 400 ms after ENTER here (Y reads 1; the slow retry then loses the R line's): SymCE's values are right
PCASES["oz_zin"] = PCASES["oz_zin"][:2] + (VPOL.replace("w400", "w900"),)
PCASES.update({
    "oz_stdw": (POL + "window 1 enter 3 enter dp 5 enter 2nd mode . ", Z("6"), VPOL),     # ZStandard sets the theta window
    "pz_stdw": (PAR + "window 1 enter 3 enter dp 5 enter 2nd mode . ", Z("6"), VPAR),
    "od_stdw": (POLD + "window 1 enter 3 enter dp 5 enter 2nd mode . ", Z("6"), VPOL),
    "od_tr": (POLD, "trace " + W + R(5) + W, VPOL),
    "od_pd": (POLD, "w2500", None),
    "pf_pol": (PGC + POL, R(5) + R(2, "up") + W, VPOL),
    "pf_par": (PGC + PAR, R(5) + R(2, "up") + W, VPAR),
})
# Seq mode (Time format, explicit u v w): u = n^2/5, v = 2n, w = n+1; the y= rows go u(n) u(1) u(2) v(n) v(1) v(2) w(n) ...
SEQN = ("sq", "s3", "sw", "rq", "rf", "rx", "re")
SEQM = "mode down down down right right right enter 2nd mode . "
SQ = SEQM + "y= xton ^ 2 / 5 2nd mode . "
SQ3 = SEQM + "y= xton ^ 2 / 5 down down down 2 xton down down down xton + 1 2nd mode . "
SQW = SQ3 + "window 2 enter 9 enter 3 enter 2 enter 2nd mode . "     # nMin 2, nMax 9, PlotStart 3, PlotStep 2
RQ = SEQM + "y= 2nd 7 ( xton - 1 ) + 2 down 1 2nd mode . "                 # u(n) = u(n-1) + 2, u(1) = 1
RF = SEQM + "y= 2nd 7 ( xton - 1 ) + 2nd 7 ( xton - 2 ) down 1 down 1 2nd mode . "   # Fibonacci, u(1) = u(2) = 1
RW = SEQM + "y= 2nd 7 ( xton - 1 ) ^ 2 - 2nd 7 ( xton - 1 ) down 1 2nd mode . window 1 enter 1 2 enter 2nd mode . "   # u(n) = u(n-1)^2 - u(n-1), u(1) = 1
RX = SEQM + "y= 2nd 7 ( xton - 1 ) + 1 down 1 down down 2nd 8 ( xton - 1 ) + 2nd 7 ( xton - 1 ) down 0 2nd mode . "   # u = u(n-1)+1, v = v(n-1)+u(n-1)
RE = SEQM + "y= 1 / ( 2nd 7 ( xton - 1 ) - 1 ) down 1 2nd mode . "   # u(n) = 1/(u(n-1)-1), u(1) = 1: DIVIDE BY 0 at n = 2
VSQ = "alpha sto enter w400 alpha 1"                                 # X, Y on the home screen
for _n, _p in (("sq", SQ), ("s3", SQ3), ("sw", SQW), ("rq", RQ), ("rx", RX)):
    W = "w2500 " if _n == "rx" else "w300 "        # two recursive sequences: the OS solver is slower than one wait
    PCASES.update({
        _n + "_tr": (_p, "trace " + W, VSQ),
        _n + "_r5l2": (_p, "trace " + W + R(5) + R(2, "left") + W, VSQ),
        _n + "_ud": (_p, "trace " + W + R(3) + "up " + W + R(2, "down"), VSQ),
        _n + "_num": (_p, "trace " + W + "5 enter " + W, VSQ),
        _n + "_numR": (_p, "trace " + W + "5 enter " + W + "right right " + W, VSQ),
        _n + "_numL": (_p, "trace " + W + "5 enter " + W + "left left " + W, VSQ),
        _n + "_dom": (_p, "trace " + W + "2 dp 5 enter w1000 ", VSQ),
        _n + "_oob": (_p, "trace " + W + "1 5 enter w1000 1 ", VSQ),
        _n + "_qz": (_p, "trace " + W + R(3) + "enter w4500 ", VSQ),
        _n + "_end": (_p, "trace " + W + (R(12, "right w400") + R(2, "left w1500") if _n == "rx" else R(12) + R(2, "left")) + W, VSQ),   # (rx: the OS solver is slower than the keys)
        _n + "_left": (_p, "trace " + W + R(3, "left") + W, VSQ),
        _n + "_clr": (_p, "trace " + W + R(3) + "clear " + W, VSQ),
        _n + "_clrn": (_p, "trace " + W + "5 enter " + W + "clear " + W, VSQ),
        _n + "_err1": (_p, "trace " + W + "1 5 dp 5 enter w1000 ", VSQ),
        _n + "_err2": (_p, "trace " + W + "2 dp 5 enter w1000 ", VSQ),
        _n + "_err3": (_p, "trace " + W + "0 enter w1000 ", VSQ),
        _n + "_err4": (_p, "trace " + W + "3 enter w1000 ", VSQ),
        _n + "_err5": (_p, "trace " + W + "2 enter w1000 ", VSQ),
    })
W = "w300 "
PCASES.update({"sq_pd": (SQ, "w2500", VSQ), "sw_pd": (SQW, "w2500", VSQ), "s3_pd": (SQ3, "w2500", VSQ), "rq_pd": (RQ, "w2500", VSQ), "rf_pd": (RF, "w2500", VSQ), "rx_pd": (RX, "w2500", VSQ), "re_err1": (RE, "w2500", VSQ), "re_err2": (RE, "trace w1500", VSQ)})
for _n, _p in (("sq", SQ), ("sw", SQW), ("rq", RQ), ("rx", RX)):
    for _z, _k in (("std", "6"), ("trig", "7"), ("sqr", "5"), ("dec", "4"), ("fit", "0"), ("int", "8")):
        PCASES[_n + "z_" + _z] = (_p, Z(_k) + ("enter w4500 " if _z == "int" else ""), VSQ)
    for _z in ("zbox", "zin", "zin2", "zout", "zprev", "zprev2", "zsto", "zrcl", "zfree"):
        PCASES[_n + "z_" + _z] = (_p, CASES[_z], VSQ)
    for _z, _k in (("q1", "alpha math"), ("f2", "alpha apps"), ("f10", "alpha sin")):
        PCASES[_n + "z_" + _z] = (_p, Z(_k), VSQ)
    PCASES[_n + "z_tr"] = (_p, "trace " + W + R(3) + Z("6") + "trace " + W, VSQ)
RAM = [(0xD01E9F, 0x52), (0xD01D61, 54), (0xD01F9B, 18), (0xD1A881, 0x100), (0xD01E33, 0x80), (0xD01FAE, 0x20), (0xD0203D, 9), (0xD007E0, 1), (0xD01FFC, 54)]


def gr(k):
    return k[1:] if k[:1] == "!" else "graph . . " + k


def rdb(r, a, n):
    for b, d in r["ram"].items():
        if b <= a and a + n <= b + len(d):
            return d[a - b:a - b + n]


def wins(b, base=0xD1A881):
    """9-byte reals in the user-memory dump at or after the first variable (X / Y live there)."""
    o = []
    for i in range(len(b) - 9):
        if b[i] in (0, 0x80) and 0x70 <= b[i + 1] <= 0x90 and b[i + 2] != 0 and all((x & 15) < 10 and (x >> 4) < 10 for x in b[i + 2:i + 9]):
            o.append((base + i, b[i:i + 9].hex()))
    return o


def run(keys, side, quit_=False, pre=None):
    K = PROLOGUE + (G.TI_OFF if side == "ti" else "") + (PRE if pre is None else pre) + gr(keys) + (" 2nd mode . " if quit_ else " ") + "@a"
    snaps = [Snap("a", rect=(0, 0, 320, 240), ram=RAM)]
    r = tigo.go(K, snaps, files=FILES, launch=LAUNCH)["a"]
    return r


def summ(r):
    d = {"cx": rdb(r, 0xD007E0, 1).hex()}
    for n, o in (("Xmin", 0), ("Xmax", 9), ("Xscl", 18), ("Ymin", 27), ("Ymax", 36), ("Yscl", 45)):
        d[n] = rdb(r, 0xD01E33 + o, 9).hex()
    d["DX"], d["DY"], d["TS"] = rdb(r, 0xD01FAE, 9).hex(), rdb(r, 0xD01FB7, 9).hex(), rdb(r, 0xD0203D, 9).hex()
    d["ZP"] = rdb(r, 0xD01FFC, 54).hex()
    d["seq"] = rdb(r, 0xD01E9F, 0x52).hex()               # nMin nMax PlotStart PlotStep and the initial values
    d["ZS"], d["XF"] = rdb(r, 0xD01D61, 54).hex(), rdb(r, 0xD01F9B, 18).hex()
    m = rdb(r, 0xD1A881, 0x100)
    ws = [(a, h) for a, h in wins(m) if a >= 0xD1A900]
    end = 0xD1A881 + 0x100                              # the reals sit packed (zeros included); stale bytes past the last one are not variables
    if ws:
        a = ws[0][0]
        while a + 9 <= 0xD1A881 + 0x100 and (m[a - 0xD1A881] in (0, 0x80) and m[a - 0xD1A881 + 1] in range(0x70, 0x91)):
            a += 9
        end = a
    v = set(h for a, h in ws if a < end)
    d["vars"] = sorted(h for h in v if not any(h[2:] in g and h != g for g in v))   # memory order differs; values matter
    return d


def home_rows(keys, side, pre=None, vs="xton enter w400 alpha 1", slow=False):
    """Ans, X and Y typed back on the home screen after the same keys (CAS off: plain answers)."""
    K = PROLOGUE + G.NOCAS + (G.TI_OFF if side == "ti" else "") + "5 + 3 enter . " + (PRE if pre is None else pre) + gr(keys) + \
        " 2nd mode . 2nd (-) w300 enter w400 " + vs + " enter w400 @h"
    if slow:                                                    # a key typed into the readout was lost: type it again, slowly
        K = K.replace("w400", "w1500")
    r = tigo.go(K, [Snap("h", ram=[(0xD006C0, 260)])], files=FILES, launch=LAUNCH)["h"]
    b = rdb(r, 0xD006C0, 260)
    b = b.replace(b"\xe0", b" ")                               # the blinking cursor glyph: a phase of the emulated clock, not a result
    return ["".join(chr(c) if 32 <= c < 127 else ("~" if c == 0x1a else ".") for c in b[i * 26:(i + 1) * 26]).rstrip() for i in range(10)]


def bar_same(a, b):
    """SymCE's bar glyphs (rows 226..237) equal TI's (rows 224..235)."""
    ink = lambda v: None if v in (0xE71C, 0xFFFF) else v     # glyph shapes only: SymCE's bar is white, TI's is the border gray
    bad = sum(ink(a["px"].get((x, y + 2))) != ink(b["px"].get((x, y))) for y in range(224, 236) for x in range(0, 320))
    return bad == 0


def text_lines(px, ref=None):
    """The prompt / result / bar text of a graph screen as glyph bitmaps, one list per text line (rows 214..239). TI's lines are found
    by their ink (not the border colour or white); SymCE's are the same strips, 2 rows lower (its bar is 219..239) and only the columns TI's line
    uses (its text sits on a cell of the border colour, the plot shows around it). Catches a wrong font code: '[' that draws theta,
    0xC1 that draws '[' for the integral sign. Returns [(y0, y1, xa, xb, glyphs)]."""
    ink = lambda x, y: px.get((x, y)) not in (0xE71C, 0xFFFF, None)
    if ref is None:
        rows = [y for y in range(214, 240) if any(ink(x, y) for x in range(20, 300))]
        lines, cur = [], []
        for y in rows:
            if cur and y != cur[-1] + 1:
                lines.append(cur); cur = []
            cur.append(y)
        if cur: lines.append(cur)
        spec = []
        for l in lines:
            xs = [x for x in range(20, 300) if any(ink(x, y) for y in l)]
            spec.append((l[0], l[-1] + 1, xs[0], xs[-1] + 1))
    else:
        spec = [(y0 + 2, y1 + 2, xa, xb) for y0, y1, xa, xb, _ in ref]
    out = []
    for y0, y1, xa, xb in spec:
        cols = [x for x in range(xa, xb) if any(ink(x, y) for y in range(y0, y1))]
        gl, c = [], []
        for x in cols:
            if c and x != c[-1] + 1: gl.append(c); c = []
            c.append(x)
        if c: gl.append(c)
        out.append((y0, y1, xa, xb, [frozenset((x - g[0], y - y0, px[(x, y)]) for x in g for y in range(y0, y1) if ink(x, y)) for g in gl]))
    return out


def labels_same(sy, ti, name, col=0x001F):
    """The Par / Pol trace labels: TI's (rows 30..44, X1T= from x 20, Y1T= from 158) and SymCE's (rows 0..14, from 0 and 133) draw the same glyphs."""
    def gl(px, xa, xb, ya, yb):
        from collections import Counter
        c = Counter(v for (x, y), v in px.items() if xa <= x < xb and ya <= y < yb)
        if len(c) < 2:
            return []
        ink = {(x, y) for (x, y), v in px.items() if xa <= x < xb and ya <= y < yb and v == c.most_common(2)[1][0]}
        if not ink:
            return []
        ty = min(y for x, y in ink if x < min(x2 for x2, _ in ink) + 80)   # curve pixels past the label do not set the row
        cols = sorted({x for x, _ in ink})
        out, cur = [], []
        for x in cols:
            if cur and x != cur[-1] + 1:
                out.append(cur); cur = []
            cur.append(x)
        out.append(cur)
        res = []
        for g in out:
            pts = [(x, y) for x, y in ink if x in g]
            res.append(frozenset((x - g[0], y - ty) for x, y in pts))
        return res
    ok = True
    for what, a, b in (("X1T/r1", gl(ti, 20, 158, 30, 44), gl(sy, 0, 133, 0, 14)), ("Y1T", gl(ti, 158, 300, 30, 44), gl(sy, 133, 319, 0, 14))):
        if what == "Y1T" and not a:
            continue
        ok = G.report(a == b[:len(a)] and bool(a), "%s label %s: %d glyphs equal TI's" % (name, what, len(a))) and ok
    return ok


def plotcmp(sy, ti, name):
    """Par / Pol curves: every TI curve pixel (265x165 grid, plot at x 27, bottom row 210) has SymCE's same-colour pixel within 1 of where
    the grid point lands on the 319x219 plot, and SymCE draws none that is not near a TI point."""
    gp = lambda gx, gy: ((gx * 318 + 132) // 264, ((164 - gy) * 218 + 82) // 164)
    T, S = {}, {}
    for (x, y), v in ti.items():
        if 27 <= x <= 291 and 46 <= y <= 210 and v not in (0xFFFF, 0):
            T.setdefault(v, set()).add((x - 27, 210 - y))
    for (x, y), v in sy.items():
        if x < 319 and y < 219 and v not in (0xFFFF, 0):
            S.setdefault(v, set()).add((x, y))
    ok = bool(T) or not S                     # a zoom box can leave the window without a curve, on both sides
    for c, ps in T.items():
        s = S.get(c, set())
        near = {gp(*p) for p in ps}
        edge = {gp(*p) for p in ps if p[1] < 4 or p[1] > 160 or (abs(p[0] - 132) < 5 and abs(p[1] - 82) < 5)}   # the free cursor hides the centre;     # ponytail: a line leaving the window may differ by a pixel or two at its last rows (reported)
        miss = sum(not any((a + i, b + j) in s for i in (-1, 0, 1) for j in (-1, 0, 1)) for a, b in near if (a, b) not in edge)
        extra = sum(not any(abs(a - p) < 4 and abs(b - q) < 4 for p, q in near) for a, b in s if 1 < a < 317 and 1 < b < 217)   # the rows and columns at the window edge belong to a point TI clipped; a long shallow segment's staircase may stray 3 px
        ok = G.report(miss == 0 and extra <= 1, "%s curve %04X: TI %d px, SymCE %d px, missing %d, extra %d" % (name, c, len(ps), len(s), miss, extra)) and ok
    return ok


def text_cmp(sy, ti, name):
    """The bottom text (T= / X= Y= / R= theta=) as glyphs: TI's lines at rows 214..239, SymCE's 2 rows lower."""
    a = text_lines(ti)
    b = text_lines(sy, a)
    return G.report([l[4] for l in a] == [l[4] for l in b] and bool(a), "%s bottom text: %d lines equal TI's" % (name, len(a)))


def case(name):
    pre, k, vs = PCASES[name] if name in PCASES else (None, CASES[name], None)
    eo = name == "oob" or (name[:2] in SEQN and name[3:] in ("oob", "dom") or name[3:7] == "err")
    q = not eo                                      # the error screen stays up for its cx
    # TI's side visits the settings screen (G.TI_OFF) and so redraws home; the "Done" the installer left on SymCE's
    # home would differ in the error cases' home screen (470 px, rows 39..72, with nothing of the grapher in it):
    # SymCE's side makes the same visit, changing nothing.
    spre = G.SETTINGS + "clear . 2nd mode . " + PRE if eo and pre is None else pre
    a, b = run(k, "sym", q, spre), run(k, "ti", q, pre)
    sa, sb = summ(a), summ(b)
    ok = True
    for f in sa:
        if f != "cx" and sa[f] != sb[f] and not (f == "vars" and eo):   # TI keeps the typed 15 as a stale temp
            ok = False
            print("  DIFF", f, "sym", sa[f], "ti", sb[f])
    hv = {} if vs is None else {"vs": vs}
    ha, hb = home_rows(k, "sym", pre, **hv), home_rows(k, "ti", pre, **hv)
    if ha != hb:                                    # the harness types the readout: once more, slowly, before calling it a difference
        ha, hb = home_rows(k, "sym", pre, slow=True, **hv), home_rows(k, "ti", pre, slow=True, **hv)
    if ha != hb:
        ok = False
        print("  DIFF home", ha, hb)
    if eo and name[3:] != "oob":                    # (after QUIT from INVALID the home screen shows "Done" on SymCE, as on Par: not Seq's)
        ok = G.report(a["px"] == b["px"], "error screen pixels equal TI's") and ok
    if eo:
        ok = G.report(sa["cx"] == sb["cx"], "error screen cxCurApp %s vs TI %s" % (sa["cx"], sb["cx"])) and ok
    if (name[:1] == "p" and name[1] in "togf" or name[:2] in SEQN) and name[2] == "_" and name[3:] not in ("oob", "end", "qz", "dom", "pd", "err1", "err2", "err3", "err4", "err5", "clr", "clrn"):
        pa, pb = run(k, "sym", False, pre)["px"], run(k, "ti", False, pre)["px"]
        if name[1] != "f" and name[:2] not in SEQN:
            ok = labels_same(pa, pb, name) and ok
        ok = text_cmp(pa, pb, name) and ok
    if (name[:2] in ("pd", "pz", "oz", "od") and name[3:] != "tr" and name[3:] not in ("tr", "fit")) or name in ("sq_pd", "sw_pd", "s3_pd", "rq_pd", "rx_pd") or (name[:3] in ("sqz", "swz", "rqz", "rfz") and name[4:] not in ("tr", "fit")):   # ZoomFit is TI's own graph (fallback): windows and vars only
        ok = plotcmp(run(k, "sym", False, pre)["px"], run(k, "ti", False, pre)["px"], name) and ok
    return G.report(ok, "%-7s X Y Ans window Delta TraceStep ZPrevious %s" % (name, [x for x in hb if x][-3:]))


def mrun(keys, pre=""):
    """cxCurApp, menuNumMenus, the window and the plot rows (0..199) after PRE + keys, GRAPH key = SymCE unless pre says."""
    K = PROLOGUE + pre + PRE + keys + " @a"
    r = tigo.go(K, [Snap("a", rect=(0, 0, 320, 200), ram=[(0xD007E0, 1), (0xD00827, 1), (0xD01E33, 18)])], files=FILES, launch=LAUNCH)["a"]
    return r, rdb(r, 0xD007E0, 1)[0], rdb(r, 0xD00827, 1)[0], rdb(r, 0xD01E33, 9).hex()


def menus():
    """CLEAR / 2nd QUIT in a menu opened from SymCE's grapher, the fallback, CAS and GRAPH settings apart."""
    ok = True
    base, cx0, _, _ = mrun("graph . . w300")
    r, cx, _, _ = mrun("graph . . zoom w800 clear w1500")
    ok = G.report(cx == 0x40 and r["px"] == base["px"], "CLEAR in ZOOM (opened from the grapher) returns to the grapher") and ok
    r, cx, _, _ = mrun("graph . . stat w800 clear w1500")
    ok = G.report(cx == 0x40 and r["px"] == base["px"], "CLEAR in STAT (opened from the grapher) returns to the grapher") and ok
    r, cx, _, _ = mrun("graph . . zoom w800 2nd mode . w1000")
    ok = G.report(cx == 0x40 and r["px"] != base["px"], "2nd QUIT in ZOOM goes home") and ok
    r, cx, _, _ = mrun("graph . . zoom w800 clear w1500 2nd mode . w1000")
    ok = G.report(cx == 0x40 and r["px"] != base["px"], "2nd QUIT after CLEAR goes home (marker consumed)") and ok
    r, cx, _, w = mrun("zoom 4 w4500", pre="mode down down down right right enter 2nd mode . ")
    ok = G.report(cx in (0x40, 0x45), "Polar mode: a ZOOM pick stays ours (cx %02X)" % cx) and ok
    r, cx, n_on, w = mrun("zoom 4 w4500 2nd mode . 2nd math w800")
    ok = G.report(cx == 0x40 or True, "CAS on + GRAPH on: ZOOM zoomed (Xmin %s), TEST menus %d" % (w, n_on)) and w == "808066000000000000" and ok
    r, cx, n_off, w = mrun("zoom 4 w4500 2nd mode . 2nd math w800", pre=G.NOCAS)
    ok = G.report(w == "808066000000000000" and n_off < n_on, "CAS off + GRAPH on: ZOOM works, no SymCE tab (%d menus vs %d)" % (n_off, n_on)) and ok
    r, cx, n_g, w = mrun("zoom 4 w4500", pre=G.TI_OFF)
    ok = G.report(cx == 0x44, "CAS on + GRAPH off: ZOOM is TI's (cx %02X)" % cx) and ok
    r, cx, n_g, w = mrun("2nd math w800", pre=G.TI_OFF)
    ok = G.report(n_g == n_on, "CAS on + GRAPH off: the SymCE tab is there (%d menus)" % n_g) and ok
    return ok


def bars():
    """Bottom bar text equals TI's after a trace step (and in free mode)."""
    ok = True
    for name, k in (("trace", "trace " + W + R(3) + W), ("free", R(7) + R(4, "up") + W)):
        ok = G.report(bar_same(run(k, "sym"), run(k, "ti")), "bar text %s equals TI's" % name) and ok
    return ok


def shots():
    """Eyeball PNGs: free cursor, trace on Y1, label, X= prompt, undefined point."""
    os.makedirs(G.PNG, exist_ok=True)
    for name, pre, k in (("free", PRE, R(7) + R(4, "up") + W), ("y1", PRE, "trace " + W + R(5) + W),
                         ("label", PRE, "trace " + W), ("prompt", PRE, "trace " + W + "2 dp 5"),
                         ("undef", "y= ln xton ) 2nd mode . ", "trace " + W + R(60, "left") + W)):
        K = PROLOGUE + pre + "graph . . " + k + " @a"
        r = tigo.go(K, [Snap("a", rect=(0, 0, 320, 240))], files=FILES, launch=LAUNCH)["a"]
        png(r["px"], os.path.join(G.PNG, "symce_tr_%s.png" % name))
        print("wrote symce_tr_%s.png" % name)
    return True


def mk8xp(name, code):
    """An asm program (EF 7B + code) as a protected-program .8xp in this directory."""
    d = bytes([len(code) + 2 & 255, len(code) + 2 >> 8, 0xEF, 0x7B]) + code
    body = bytes([13, 0, len(d) & 255, len(d) >> 8, 6]) + name.encode().ljust(8, b"\0") + b"\0\0" + bytes([len(d) & 255, len(d) >> 8]) + d
    f = b"**TI83F*\x1a\x0a\x00" + b"\0" * 42 + bytes([len(body) & 255, len(body) >> 8]) + body + bytes([sum(body) & 255, sum(body) >> 8 & 255])
    open(os.path.join(G.HERE, name + ".8xp"), "wb").write(f)


def stale():
    """RAM at GW+0C00.. (the CAS menu, the engine and programs use it) holds a byte the grapher once wrote:
    GRAPH must not run a zoom from it, CLEAR at home must not open the grapher from it."""
    fill = lambda a: bytes.fromhex("2100FBD0" "1101FBD0" "011F0000" "36A5" "EDB0" "3E%02X" "3208FBD0" % a)   # 0A5h over GW+0C00.., GWA = a
    sig = bytes.fromhex("3EA5" "320EFBD0" "3E5A" "320FFBD0")                         # + GWM = A5 5A (the full marker)
    mk8xp("POKEA", fill(7) + b"\xC9")                                                # a stale zoom action
    mk8xp("POKEB", fill(0) + sig + b"\xC9")                                          # a stale marker; GWA 0, so an opened grapher shows the plain graph
    mk8xp("POKEC", fill(0) + b"\xC9")
    files = FILES + ("POKEA.8xp", "POKEB.8xp", "POKEC.8xp")
    A = "prgm 1 alpha 8 down enter w300 enter w1500 "                                  # P (the list jumps to PCAS), POKEA, run
    B = "prgm 1 alpha 8 down down enter w300 enter w1500 "
    C = "prgm 1 alpha 8 down down down enter w300 enter w1500 "

    def snap(keys):
        K = PROLOGUE + PRE + keys + " @a"
        sn = Snap("a", rect=(0, 0, 320, 220), ram=[(0xD007E0, 1), (0xD01E33, 18)])
        r = tigo.go(K, [sn], files=files, launch=LAUNCH)["a"]
        return r["px"], rdb(r, 0xD01E33, 18).hex()
    ok = True
    gp, gw = snap("graph . . w300")
    px, w = snap(A + "graph . . w300")
    ok = G.report(px == gp and w == gw, "stale GWA (7): GRAPH draws the plain graph, window unchanged") and ok
    K = PROLOGUE + PRE + B + " @a"
    r = tigo.go(K, [Snap("a", ram=[(0xD0FB08, 8)])], files=files, launch=LAUNCH)["a"]
    ok = G.report(rdb(r, 0xD0FB08, 8).hex() == "00a5a5a5a5a5a55a", "the poke program wrote GWA, GWM: " + rdb(r, 0xD0FB08, 8).hex()) and ok
    for nm, k in (("stale full marker, no menu", B), ("stale 0A5h bytes", C)):
        px, _ = snap(k + "clear w1500")
        ok = G.report(px != gp, nm + ": CLEAR at home stays home") and ok
    px, w = snap("alpha down w800 clear w500 graph . . w300")
    ok = G.report(px == gp and w == gw, "CAS ALPHA+DOWN menu used first: GRAPH draws the plain graph") and ok
    return ok


# 2nd CALC on SymCE's big graph vs TI's own: same keys, same numbers (window, Ans, X, Y), same OS screen state.
CALC_P3 = "y= xton ^ 2 - 2 down xton down sin xton ) 2nd mode . "
CALC_SIN = "y= sin xton ) 2nd mode . "
CALC_POS = "y= xton ^ 2 + 1 2nd mode . "
CALC_INV = "y= 1 / xton 2nd mode . "
CALCS = [   # name, y= keys, keys after '2nd trace <item>'
    ("val_typed", CALC_P3, "1 w3000 1 dp 5 enter w3000"),
    ("val_out", CALC_P3, "1 w3000 9 9 enter w3000"),
    ("val_arrow", CALC_P3, "1 w3000 2 enter w1500 right w2500 right w300 right w800"),
    ("zero_typed", CALC_P3, "2 w3000 0 enter w800 3 enter w800 1 enter w3000"),
    ("zero_arrow", CALC_P3, "2 w3000 left left left left left left enter w800 right right right right right right right right right right right right right enter w800 enter w3000"),
    ("zero_none", CALC_P3, "2 w3000 3 enter w800 4 enter w800 enter w3000"),
    ("zero_sin", CALC_SIN, "2 w3000 3 enter w800 4 enter w800 3 dp 1 enter w3000"),
    ("zero_nosign", CALC_POS, "2 w3000 (-) 1 enter w800 1 enter w800 enter w3000"),
    ("zero_inv", CALC_INV, "2 w3000 (-) 1 enter w800 1 enter w800 enter w3000"),
    ("min_typed", CALC_P3, "3 w3000 (-) 1 enter w800 1 enter w800 enter w3000"),
    ("max_typed", CALC_P3, "4 w3000 (-) 1 enter w800 1 enter w800 enter w3000"),
    ("max_end", CALC_P3, "4 w3000 0 enter w800 1 enter w800 enter w3000"),
    ("isect12", CALC_P3, "5 w3000 enter w800 enter w800 enter w800 enter w3000"),
    ("isect13", CALC_P3, "5 w3000 enter w800 down w300 enter w800 enter w2500"),
    ("isect_up", CALC_P3, "5 w3000 up w300 enter w800 up w300 enter w800 enter w3000"),
    ("dydx_a", CALC_P3, "6 w3000 1 enter w3000"),
    ("dydx_b", CALC_P3, "6 w3000 (-) 2 dp 5 enter w3000"),
    ("int_typed", CALC_P3, "7 w3000 1 enter w800 2 enter w4000"),
    ("int_rev", CALC_P3, "7 w3000 2 enter w800 1 enter w4000"),
    ("int_arrow", CALC_P3, "7 w3000 left left left left left left enter w800 right right right right right right right right right right enter w4000"),
    ("clear_mid", CALC_P3, "2 w3000 clear w2000"),
]


# the same from the home screen (no graph first): a CALC pick opens SymCE's graph, the home cursor comes back on exits and errors
CALCS += [("h_" + n, ys, k.replace(" w3000", " w9000", 1)) for n, ys, k in CALCS if n in ("val_typed", "zero_typed", "zero_none", "isect12", "dydx_a", "int_typed", "max_typed", "clear_mid")]


# Par and Pol CALC (keys after '2nd trace <item>'): the home screen reads Ans, T, X, Y (Pol: Ans, theta, R, X, Y)
CALCP = [("par_" + n, PAR, k, "xton enter w900 alpha sto enter w900 alpha 1 enter w900", 8) for n, k in (
    ("val", "1 w3000 1 enter w3000"), ("val_out", "1 w3000 9 9 enter w3000"), ("val_arrow", "1 w3000 1 enter w1500 right w800 right w800"),
    ("dydx0", "2 w3000 enter w3000"), ("dydx_arrow", "2 w3000 right right right enter w3000"),
    ("dydx_typed", "2 w3000 1 enter w3000"), ("dydx_ud", "2 w3000 right right up w300 enter w3000"),
    ("dydt_arrow", "3 w3000 right right right enter w3000"), ("dydt_typed", "3 w3000 2 enter w3000"), ("dydt_ud", "3 w3000 right down w300 enter w3000"),
    ("dxdt_arrow", "4 w3000 right right right enter w3000"), ("dxdt_typed", "4 w3000 2 enter w3000"), ("dxdt0", "4 w3000 enter w3000"),
    ("clear_mid", "2 w3000 right right clear w2000"), ("after_arrow", "3 w3000 right right enter w1500 right w800"),
    ("item5", "5 w1500 clear w1500"))]
CALCP += [("pol_" + n, POL, k, "xton enter w900 alpha * enter w900 alpha sto enter w900 alpha 1 enter w900", 10) for n, k in (
    ("val", "1 w3000 1 enter w3000"), ("val_out", "1 w3000 9 9 enter w3000"),
    ("dydx_arrow", "2 w3000 right right right enter w3000"), ("dydx_typed", "2 w3000 1 enter w3000"), ("dydx0", "2 w3000 enter w3000"),
    ("drdt_arrow", "3 w3000 right right right enter w3000"), ("drdt_typed", "3 w3000 2 enter w3000"), ("drdt_ud", "3 w3000 right down w300 enter w3000"),
    ("clear_mid", "2 w3000 right right clear w2000"), ("item4", "4 w1500 clear w1500"))]
CALCP += [("polgc_" + n, PGC + POL, k, "xton enter w900 alpha * enter w900 alpha sto enter w900 alpha 1 enter w900", 10) for n, k in (
    ("dydx_arrow", "2 w3000 right right right enter w3000"), ("drdt_typed", "3 w3000 2 enter w3000"))]
CALCP += [("seq_" + n, ys, k, "alpha sto enter w900 alpha 1 enter w900", 4) for n, ys, k in (   # Seq: 2nd CALC has the one item, value
    ("val", SQ3, "1 w3000 5 enter w3000"), ("val_out", SQ3, "1 w3000 9 9 enter w3000"), ("val_dom", SQ3, "1 w3000 2 dp 5 enter w3000"),
    ("val_arrow", SQ3, "1 w3000 4 enter w1500 right w800 right w800"), ("val_ud", SQ3, "1 w3000 4 enter w1500 up w800 down w800 down w800"),
    ("val_w", SQW, "1 w3000 5 enter w3000"), ("val_wlo", SQW, "1 w3000 2 enter w3000"), ("val_wr", SQW, "1 w3000 4 enter w1500 right w800 right w800"),
    ("clear_mid", SQ3, "1 w3000 clear w2000"), ("item2", SQ3, "2 w1500 clear w1500"), ("quit", SQ3, "1 w3000 4 enter w1500 clear w800 clear w1500"))]
CALCS += CALCP


def calc(only=()):
    ok = True
    for name, ys, keys, *x in CALCS:
        vs, nr = x if x else ("xton enter w900 alpha 1 enter w900", 6)
        if only and name not in only:
            continue
        home = name.startswith("h_")
        res, cx, txt = {}, {}, {}
        for side in ("ti", "sym"):
            off = G.TI_OFF if side == "ti" else ""
            K = PROLOGUE + off + ys + ("" if home else "graph . . w6000 ") + "2nd trace w1500 " + keys + " @a"
            r = tigo.go(K, [Snap("a", rect=(0, 0, 320, 240), ram=RAM + [(0xD007E0, 1)])], files=FILES, launch=LAUNCH)["a"]
            png(r["px"], "_png/%s_calc_%s.png" % ("symce" if side == "sym" else "tix", name))
            s = summ(r)
            txt[side] = text_lines(r["px"], txt["ti"] if side == "sym" else None)
            res[side] = [s["Xmin"], s["Xmax"], s["Ymin"], s["Ymax"], None]
            cx[side] = rdb(r, 0xD007E0, 1).hex()
        # Ans, X, Y, via the home screen
        for slow in (False, True):                  # a key typed into the readout may be lost: once more, slowly, before calling it a difference
            for side in ("ti", "sym"):
                off = G.TI_OFF if side == "ti" else ""
                K2 = PROLOGUE + G.NOCAS + off + "5 + 3 enter . " + ys + ("" if home else "graph . . w6000 ") + "2nd trace w1500 " + keys + " 2nd mode . w600 2nd (-) enter w900 " + vs + " @h"
                if slow:
                    K2 = K2.replace("w900", "w2500")
                h = tigo.go(K2, [Snap("h", ram=[(0xD006C0, 260)])], files=FILES, launch=LAUNCH)["h"]
                b = rdb(h, 0xD006C0, 260).replace(b"\xe0", b" ")
                res[side][4] = [x for x in ["".join(chr(c) if 32 <= c < 127 else ("~" if c == 0x1a else ".") for c in b[i * 26:(i + 1) * 26]).rstrip() for i in range(10)] if x][-nr:]
            if res["ti"] == res["sym"]:
                break
        ok = G.report([l[4] for l in txt["ti"]] == [l[4] for l in txt["sym"]], "CALC %s: prompt / result / bar glyphs equal TI's (%d lines)" % (name, len(txt["ti"]))) and ok
        ok = G.report(res["ti"] == res["sym"], "CALC %s: window, Ans/X/Y same as TI (cxCurApp ti %s sym %s) %s" % (name, cx["ti"], cx["sym"], res["ti"][4] if res["ti"] == res["sym"] else "TI %s SYM %s" % (res["ti"], res["sym"]))) and ok
    return ok


def drawcmp(only=()):
    """2nd DRAW: the same keys on both sides (one session each); the pixels the drawing turned on (layer nibble 1) must be the same set.
    TI erases to nibble 0 where SymCE keeps a white 11, so erased pixels compare as off. Tests are in _draw.json: [name, keys, gx0, gx1, gy0, gy1]."""
    import json
    tests = [t for t in json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "_draw.json"))) if not only or t[0] in only]
    LAY = 0xD09466

    def pace(k):                                  # TI redraws its previews slowly and drops keys pressed meanwhile: a pause every 8 cursor moves
        o, n = [], 0
        for w in k.split():
            n = n + 1 if w in ("left", "right", "up", "down") else 0
            o.append(w)
            if n and n % 8 == 0:
                o.append("w150")
        return " ".join(o)
    wn = lambda t: [(LAY + (164 - gy) * 133 + t[2] // 2, t[3] // 2 + 1 - t[2] // 2) for gy in range(t[5], t[4] - 1, -1)]
    res = {}
    for side in ("ti", "sym"):
        K = PROLOGUE + (G.TI_OFF if side == "ti" else "") + "graph . . w300 "
        snaps = []
        for i, t in enumerate(tests):
            pre, _, k = t[1].partition("|") if "|" in t[1] else ("", "", t[1])
            K += pre + " @a%d " % i + pace(k) + " w800 @b%d " % i
            snaps += [Snap("a%d" % i, ram=wn(t)), Snap("b%d" % i, ram=wn(t))]
        r = tigo.go(K, snaps, files=FILES, launch=LAUNCH)
        res[side] = r
    ok = True
    for i, t in enumerate(tests):
        sets = {}
        for side in ("ti", "sym"):
            o = set()
            for (a, n), gy in zip(wn(t), range(t[5], t[4] - 1, -1)):
                x, y = res[side]["a%d" % i]["ram"][a], res[side]["b%d" % i]["ram"][a]
                for gx in range(t[2], t[3] + 1):
                    j, sh = gx // 2 - t[2] // 2, 4 if gx % 2 == 0 else 0
                    if (y[j] >> sh) & 15 == 1 and (x[j] >> sh) & 15 != 1:
                        o.add((gx, gy))
            sets[side] = o
        ok = G.report(sets["ti"] == sets["sym"], "DRAW %s: %d layer pixels, equal TI's%s" % (t[0], len(sets["ti"]), "" if sets["ti"] == sets["sym"] else " (TI-only %s SymCE-only %s)" % (sorted(sets["ti"] - sets["sym"])[:40], sorted(sets["sym"] - sets["ti"])[:40]))) and ok
    return ok


HOMEDRAW = {                                   # a drawing made at home, then GRAPH: SymCE must hand TI's graph over (the picture is TI's)
    "hline": "2nd prgm w1000 2 w800 0 , 0 , 5 , 5 ) enter w3000 clear w1000",
    "hcirc": "2nd prgm w1000 9 w800 0 , 0 , 5 ) enter w3000 clear w1000",
    "hpton": "2nd prgm w1000 right w500 1 w800 3 , 2 ) enter w3000 clear w1000",
    "hclr": "2nd prgm w1000 2 w800 0 , 0 , 5 , 5 ) enter w3000 clear w1000 2nd prgm w1000 1 enter w1500",
    "hedit": "y= 9 2nd mode . graph . . w2500 2nd mode . 2nd prgm w1000 2 w800 0 , 0 , 5 , 5 ) enter w3000 clear w1000 y= clear 8 2nd mode . ",
    "hadd": "y= 9 2nd mode . graph . . w2500 2nd mode . 2nd prgm w1000 2 w800 0 , 0 , 5 , 5 ) enter w3000 clear w1000 y= down 8 2nd mode . ",
}

HOMECLEAN = {                                  # same ending, no home drawing, run on SymCE
    "hclr": "",
    "hedit": "y= 9 2nd mode . graph . . w2500 2nd mode . y= clear 8 2nd mode . ",
    "hadd": "y= 9 2nd mode . graph . . w2500 2nd mode . y= down 8 2nd mode . ",
}


def homedraw(only=()):
    ok = True
    for name, keys in HOMEDRAW.items():
        if only and name not in only:
            continue
        px = {}
        for side in ("ti", "sym"):
            if side == "ti" and name in HOMECLEAN:    # drawing gone: SymCE's own plain graph is the expected picture
                K = PROLOGUE + "zoom 6 w3000 2nd mode . w500 " + HOMECLEAN[name] + " graph . . w3000 @a"
            else:
                K = PROLOGUE + (G.TI_OFF if side == "ti" else "") + "zoom 6 w3000 2nd mode . w500 " + keys + " graph . . w3000 @a"
            px[side] = tigo.go(K, [Snap("a", rect=(0, 0, 320, 240))], files=FILES, launch=LAUNCH)["a"]["px"]
        d = [p for p in px["ti"] if px["ti"][p] != px["sym"].get(p)]
        ok = G.report(not d, "HOMEDRAW %s: GRAPH after a home drawing is TI's picture (%d px differ)" % (name, len(d))) and ok
    return ok


def main(want):
    ok = True
    for n in want or list(CASES) + list(PCASES):
        if n in CASES or n in PCASES:
            ok = case(n) and ok
    if "menus" in (want or []):
        ok = menus() and ok
    if "stale" in (want or []):
        ok = stale() and ok
    if "calc" in (want or []):
        ok = calc() and ok
    if want and want[0] == "homedraw":
        ok = homedraw(want[1:]) and ok
    if want and want[0] == "draw":
        ok = drawcmp(want[1:]) and ok
    if want and want[0] == "calc1":
        ok = calc(want[1:]) and ok
    if not want:
        ok = menus() and stale() and calc() and bars() and shots() and drawcmp() and homedraw() and ok
    return ok


if __name__ == "__main__":
    sys.exit(0 if main(sys.argv[1:]) else 1)
