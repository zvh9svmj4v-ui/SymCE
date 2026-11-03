"""SymCE's own grapher (the GRAPH key, raw key hook), on the real ROM.

    python3 graph.py [shots] [pixels] [format] [piece] [osparse] [side] [hand]
                     [fall] [on] [entry] [exitbar] [trace] [stat] [time]     # default: all

  shots   colour PNGs of the whole LCD to _png/fg_*.png
  pixels  columns of sin(X) and X^2/5 sit within +-1 row of the 319x240 mapping;
          the border colour; the + and 0 keys do nothing
  format  axes colour and ticks, GridDot / GridLine, thin vs normal line, palette
          colours 8 and 15, Detect Asymptotes on / off (tan(X), against TI's)
  piece   a piecewise function (relations, and, or) plots
  osparse an equation the compiler rejects (Y2=Y1+1) plots through the OS parser
  side    X, Y and Ans after a draw equal TI's (same keys, SymCE's GRAPH key off)
  hand    each key that leaves the graph reaches the screen TI's graph sends it to
  fall    a split screen, a stat plot, a line style of "above",
          a background image: TI's own graph opens
  on      ON interrupts a slow draw
  entry   GRAPH pressed straight from the Y= editor and from WINDOW opens the
          grapher; CLEAR and 2nd MODE leave it for the home screen
  exitbar every exit of the grapher leaves TI's status bar (rows 0..23) whole
  trace   free cursor, TRACE, number entry, pan, QuickZoom against TI (trace.py)
  stat    Scatter / xyLine plots, marks, colours, clipping, ZoomStat against TI (statplot.py); the rest fall to TI
  time    GRAPH to fully drawn, for the six functions timed in the OS graph
          (docs/TI84CE-KNOWLEDGE.md 11b): a CRC of rows 0..239 every few ms until
          it stops changing

The Y= entries go in with the SymCE app armed (as the OS graph would read them),
window at its default -10..10, Radian. Run `make -C symce emu`, or copy the build
into this directory, first.
"""
import json, math, os, re, struct, subprocess, sys, time, zlib
import screen, vram
from e2e import PROLOGUE, FILES, LAUNCH
from lifecycle import state, TYPE, SETTINGS

HERE = screen.HERE
PNG = os.path.join(HERE, "_png")
BLUE, BLACK, RED = 0x001F, 0x0000, 0xF800
PAL = {1: 0x001F, 2: 0xF800, 3: 0x0000, 4: 0xF81F, 5: 0x04E0, 6: 0xFC64, 7: 0xB100, 8: 0x0010,
       9: 0x049F, 10: 0xFFE0, 11: 0xFFFF, 12: 0xE71C, 13: 0xC618, 14: 0x8C51, 15: 0x52AA}
PW, PH = 319, 240
GRIDC, AXESC, STYLE = 0xD024F1, 0xD024F2, 0xD024BF
# name, Y= keys: one string per equation
FUNCS = {
    "sin": ["sin xton )"],
    "three": ["sin xton )", "cos xton )", "xton ^2 / 5"],
    "unsup": ["2nd dp"],                                  # i: token 2C, a complex: the OS parser says no
}
OS_MS = {"(none)": 0, "X": 1864, "sin(X)": 2804, "X^2": 1624, "sin(X)/X+cos(3X)": 5740,
         "sqrt(X)": 1544, "sin,cos,X^2/5": 7480}
TIMED = {"(none)": [], "X": ["xton"], "sin(X)": ["sin xton )"], "X^2": ["xton ^2"],
         "sin(X)/X+cos(3X)": ["sin xton ) / xton + cos 3 xton )"],
         "sqrt(X)": ["2nd ^2 xton )"], "sin,cos,X^2/5": FUNCS["three"]}
TI_OFF = SETTINGS + "4 . clear . 2nd mode . "                        # GRAPH goes to TI's own graph
SNAP_OFF = SETTINGS + "6 . clear . 2nd mode . "                    # TRACE does not snap to zeros etc: the TI-parity tests step the grid
NOCAS = SETTINGS + "1 . clear . "                         # the CAS off: the home screen shows plain answers


def keys_for(funcs, after="", pre=""):
    """Type the equations, go home, press GRAPH, wait."""
    return PROLOGUE + pre + "y= " + " down ".join(funcs) + " 2nd mode . graph . . " + after


def fmt_keys(*sets):
    """FORMAT screen settings: (row, rights) in ascending row order; back home."""
    k, row = "2nd zoom w800 ", 0
    for r, n in sets:
        k += "down " * (r - row) + "right " * n + "enter w300 "
        row = r
    return k + "2nd mode . "


def y1_style(color=None, line=None):
    """The Y= popup for Y1 (cursor at the end of its expression): Color, Line (1 normal,
    2..5 shade/path/animate, 6 dot-thick, 7 dotted, 0 thin), OK."""
    k = "left " * 8 + "enter "
    k += "right " * ((color or 1) - 1) + "down "
    if line is not None:
        k += "right " * ((line - 1) % 8)
    return k + "down enter "


def cpng(px, path):
    raw = b""
    for y in range(vram.H):
        row = bytearray([0])
        for x in range(vram.W):
            v = px.get((x, y), 0xFFFF)
            r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
            row += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
        raw += bytes(row)

    def ch(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", vram.W, vram.H, 8, 2, 0, 0, 0))
                           + ch(b"IDAT", zlib.compress(raw)) + ch(b"IEND", b""))


def grab(keys, extra=()):
    px, r = vram.grab(keys, 0, vram.H, files=FILES, launch=LAUNCH, lead=False, settle=1500, extra=extra)
    return (px, r) if extra else px


RAMS = [("grid", GRIDC, 1), ("axes", AXESC, 1), ("style", STYLE, 1)]


def gshot(keys, name=None):
    """(pixels, {grid, axes, style}) for a keys string; optionally saved as _png/<name>.png."""
    px, r = grab(keys, RAMS)
    if name:
        os.makedirs(PNG, exist_ok=True)
        cpng(px, os.path.join(PNG, name + ".png"))
    return px, {k: r[k][0] for k in ("grid", "axes", "style")}


def tishot(keys):
    """TI's own graph (SymCE's GRAPH key off): the whole LCD, read with the stock driver's timing."""
    import _ti
    return _ti.go(keys + " @a", [_ti.Snap("a", rect=(0, 0, 320, 240))], files=FILES, launch=LAUNCH)["a"]["px"]


def count(px, colour, rect=(0, 0, PW, PH)):
    return sum(1 for (x, y), v in px.items() if v == colour and rect[0] <= x < rect[2] and rect[1] <= y < rect[3])


def report(ok, text):
    print("%s %s" % ("ok" if ok else "FAIL", text))
    return ok


def shots():
    os.makedirs(PNG, exist_ok=True)
    out = {}
    for name, keys in (("fg_sin", keys_for(FUNCS["sin"])),
                       ("fg_three", keys_for(FUNCS["three"])),
                       ("fg_keys", keys_for(FUNCS["sin"], "+ . 0 . ")),
                       ("fg_unsup", keys_for(FUNCS["unsup"]))):
        out[name] = grab(keys)
        cpng(out[name], os.path.join(PNG, name + ".png"))
        print("wrote", name + ".png", "non-white px:", sum(v != 0xFFFF for v in out[name].values()))
    return out


def near(px, col, row, colour):
    return any(px.get((col, r)) == colour for r in range(row - 1, row + 2))


def xat(c):
    return -10 + c * 20 / 318


def rowof(f):
    return round((10 - f) / 20 * 239)


def curve_ok(px, f, colour, label, cols=range(100, 231, 13), skip=2.5):
    cols = [c for c in cols if abs(xat(c)) >= skip]
    good = sum(near(px, c, rowof(f(xat(c))), colour) for c in cols)
    return report(good == len(cols), "%s: %d of %d columns plotted" % (label, good, len(cols)))


def x2_ok(px, colour, label):
    return curve_ok(px, lambda x: x * x / 5, colour, label)


def entry():
    """GRAPH from Y= (nothing quit first) and from WINDOW; then home again."""
    ok = True
    for label, keys in (("GRAPH from Y= editor", "y= xton ^2 / 5 graph . . "),
                        ("GRAPH from WINDOW", "y= xton ^2 / 5 2nd mode . window graph . . ")):
        ok = x2_ok(grab(PROLOGUE + keys), BLUE, label) and ok
    for label, out in (("CLEAR", "clear . "), ("2nd MODE", "2nd mode . ")):
        for wait in ("", "w130 ", "w270 "):                 # CEmu's sched_active abort: shift the timing
            ptr, flag, _, rows = state(wait + "y= sin xton ) 2nd mode . graph . . " + out + TYPE)
            good = rows[-1:] == ["4X"]
            if good:
                break
        print("%s %s from grapher: home, CAS answers %s" % ("ok" if good else "FAIL", label, rows[-2:]))
        ok = ok and good
    return ok


def pixels(shot):
    ok = True
    for label, px, f, colour, cols in (
            ("sin(X)", shot["fg_sin"], math.sin, BLUE, range(0, 319, 32)),
            ("X^2/5", shot["fg_three"], lambda x: x * x / 5, BLACK, range(100, 231, 13))):
        ok = curve_ok(px, f, colour, label, cols) and ok
    px = shot["fg_sin"]
    ok = report(px[(PW, 5)] == 0xFFFF and px[(PW, PH - 1)] == 0xFFFF, "column 319 white like the plot") and ok
    ok = report(all(px[(159, y)] == BLACK for y in (220, 230, 239)) and sum(px[(x, 239)] != 0xFFFF for x in range(PW)) >= 9,
                "rows 220..239 are the plot: the y axis (and its foot tick, 9 px measured) run to the bottom edge") and ok
    ok = report(px[(250, 30)] == 0xFFFF and px[(10, 200)] == 0xFFFF, "plot background white") and ok
    ok = report(shot["fg_keys"] == shot["fg_sin"], "+ and 0 change nothing") and ok
    return foot() and ok


def foot():
    """After TRACE then CLEAR, and after TRACE, a typed number and CLEAR twice, the rows under the bar and the X= box hold the plot again:
    the whole screen equals the plain graph."""
    ok = True
    plain = grab(keys_for(["xton ^2 / 5"]))
    for label, k in (("TRACE, CLEAR", "trace w700 clear w700 "), ("TRACE, 5, CLEAR, CLEAR", "trace w700 5 w700 clear w700 clear w700 ")):
        after = grab(keys_for(["xton ^2 / 5"], k))
        bad = sum(after[xy] != plain[xy] for xy in plain)
        ok = report(bad == 0, "%s: the foot of the plot is back (%d px differ from the plain graph)" % (label, bad)) and ok
    return ok


def axis_ok(px, ram):
    ac = PAL.get(ram["axes"], 0)
    col, row = 159, 120                                   # value 0 of the -10..10 window
    ok = report(px.get((col, 30)) == ac and px.get((60, row)) == ac, "axes drawn in the FORMAT axes colour %04X" % ac)
    # ticks at every Xscl (1): x = 2 is column round(12 * 318 / 20) = 191, +-4 rows
    tk = [px.get((191, row + d)) == ac for d in (-4, 4)] + [px.get((col + d, rowof(2))) == ac for d in (-4, 4)]
    ok = report(all(tk) and px.get((191, row + 6)) != ac, "tick marks +-4 px at Xscl / Yscl") and ok
    return ok


def fmt():
    ok = True
    # 1. one equation off screen (Y1=20): only the furniture
    px, ram = gshot(keys_for(["2 0"], pre=""), "fg_axes")
    ok = axis_ok(px, ram) and ok
    # axes colour changed (FORMAT Axes colour, one step)
    px2, ram2 = gshot(keys_for(["2 0"], pre=fmt_keys((4, 1))), "fg_axes2")
    ok = report(ram2["axes"] != ram["axes"], "FORMAT changed the axes colour %d -> %d" % (ram["axes"], ram2["axes"])) and ok
    ok = axis_ok(px2, ram2) and ok
    # 2. GridDot and GridLine
    for label, n in (("GridDot", 1), ("GridLine", 2)):
        px, ram = gshot(keys_for(["2 0"], pre=fmt_keys((2, n))), "fg_" + label.lower())
        gc = PAL.get(ram["grid"], 0)
        col, row = 207, rowof(3.0)                            # x = 3: column round(13 * 15.9), y = 3
        blk = all(px.get((col + i, row + j)) == gc for i in (-1, 0, 1) for j in (-1, 0, 1))
        between = px.get((col + 2, row + 6)) == 0xFFFF and px.get((col + 7, row)) == 0xFFFF
        if label == "GridDot":
            ok = report(blk and between, "%s: 3x3 dots in the grid colour %04X at the intersections" % (label, gc)) and ok
        else:
            ln = px.get((col, 20)) == gc and px.get((30, row)) == gc and px.get((col + 3, 20)) == 0xFFFF
            ok = report(ln and px.get((col + 1, 20)) == 0xFFFF and px.get((30, row + 1)) == 0xFFFF, "%s: one-pixel full lines in the grid colour %04X" % (label, gc)) and ok
    # 3. thin vs normal vs dotted vs dot-thick for Y1=X
    n = {}
    for label, line in (("normal", 1), ("thin", 0), ("dotted", 7), ("dot-thick", 6)):
        px, ram = gshot(keys_for(["xton"], pre=""), None) if line == 1 else \
            gshot(PROLOGUE + "y= xton " + y1_style(line=line) + "2nd mode . graph . . ", "fg_" + label)
        n[label] = count(px, BLUE)
        if label != "normal":
            ok = report(ram["style"] == line, "Y1 line style in RAM is %d (%s)" % (ram["style"], label)) and ok
    print("  blue pixels:", n)
    ok = report(280 <= n["thin"] <= 340, "thin line: about one pixel per column") and ok
    ok = report(n["normal"] >= 1.4 * n["thin"], "normal line is thicker than thin") and ok
    ok = report(140 <= n["dotted"] <= 180, "dotted thin: a pixel at every second sample") and ok
    ok = report(n["dot-thick"] >= 8 * 280 // 3 and n["dot-thick"] <= 9 * 320, "dot-thick: a 3x3 dot per sample") and ok
    # 4. palette colours above 7
    for c in (8, 15):
        px, ram = gshot(PROLOGUE + "y= xton " + y1_style(color=c) + "2nd mode . graph . . ")
        ok = report(count(px, PAL[c]) > 250, "palette colour %d (%04X) is plotted: %d px" % (c, PAL[c], count(px, PAL[c]))) and ok
    return ok


def runmax(px, colour, c0, c1, rect_y=(0, PH)):
    """The longest vertical run of `colour` in any of the columns c0..c1."""
    best = 0
    for c in range(c0, c1):
        run = 0
        for y in range(*rect_y):
            if px.get((c, y)) == colour:
                run += 1
                best = max(best, run)
            else:
                run = 0
    return best


def asym():
    """tan(X) with Detect Asymptotes on (default) and off: a line across the pole only when off.
    TI's graph is the reference; the same classification."""
    ok = True
    res = {}
    for det, keys in (("on", ""), ("off", fmt_keys((9, 1)))):
        sym = grab(PROLOGUE + keys + "y= tan xton ) 2nd mode . graph . . ")
        ti = tishot(PROLOGUE + TI_OFF + keys + "y= tan xton ) 2nd mode . graph . . . ")
        a = runmax(sym, BLUE, 177, 192) / PH
        b = runmax(ti, BLUE, 172, 190, (46, 211)) / 165
        res[det] = (a, b)
        print("  detect asymptotes %s: longest vertical run SymCE %.2f of the plot, TI %.2f" % (det, a, b))
    ok = report(res["on"][0] < 0.5 and res["on"][1] < 0.5, "Detect Asymptotes on: no line across the poles (SymCE and TI)") and ok
    ok = report(res["off"][0] > 0.6 and res["off"][1] > 0.6, "Detect Asymptotes off: the lines are drawn (SymCE and TI)") and ok
    return ok


def piece():
    ok = True
    px = grab(keys_for(["( xton 2nd math 5 0 ) ( (-) xton ) + ( xton 2nd math 4 0 ) ( xton ^2 / 5 )"]), )
    cnt = count(px, BLUE)
    fx = lambda c: -xat(c) if xat(c) < 0 else xat(c) ** 2 / 5
    cols = [c for c in range(10, 319, 19) if abs(xat(c)) > 1 and 0 <= rowof(fx(c)) < PH]
    good = sum(near(px, c, rowof(fx(c)), BLUE) for c in cols)
    ok = report(good == len(cols), "piecewise (X<0)(-X)+(X>=0)X^2/5: %d of %d columns" % (good, len(cols))) and ok
    px = grab(keys_for(["2 ( xton 2nd math 3 1 ) - 3 ( xton 2nd math 6 1 )"]))
    ok = report(near(px, 250, rowof(2), BLUE) and near(px, 50, rowof(-3), BLUE), "step function with > and <=") and ok
    px = grab(keys_for(["5 ( ( xton 2nd math 3 2 ) 2nd math right 1 ( xton 2nd math 5 5 ) )"]))
    ok = report(near(px, 215, rowof(5), BLUE) and not near(px, 100, rowof(5), BLUE) and not near(px, 280, rowof(5), BLUE),
                "and: y = 5 only between x = 2 and 5") and ok
    return ok


def osparse():
    ok = True
    px = grab(keys_for(["xton ^2 / 5", "vars right 1 1 + 1"]))
    ok = curve_ok(px, lambda x: x * x / 5, BLUE, "Y1 = X^2/5") and ok
    ok = curve_ok(px, lambda x: x * x / 5 + 1, RED, "Y2 = Y1+1 through the OS parser") and ok
    return ok


def home_rows(before, draw, after):
    """The last home screen rows after: `before` (home), `draw` (opens the graph), CLEAR, `after`."""
    _, _, _, rows = state(before + draw + "clear . " + after)
    return rows[-6:]


def side():
    """X, Y and Ans after a draw, SymCE's against TI's (CAS off so the home screen shows plain answers)."""
    ok = True
    for label, eqs in (("compiled", ["xton ^2 / 5"]), ("OS parser", ["xton ^2 / 5", "vars right 1 1 + 1"])):
        draw = "y= " + " down ".join(eqs) + " 2nd mode . graph . . "
        shown = "2nd (-) enter . xton enter . alpha 1 enter . "
        a = home_rows(NOCAS + "5 + 3 enter . ", draw, shown)
        b = home_rows(NOCAS + "5 + 3 enter . " + TI_OFF, draw, shown)
        print("  SymCE:", a)
        print("  TI   :", b)
        ok = report(a == b, "%s: Ans, X and Y after drawing equal TI's" % label) and ok
    return ok


def leak():
    """Text the grapher draws (the X= box, the bar under a CALC result, the trace label) must not come back on
    home: leave the grapher, open and close a menu on home, and the whole screen and the OS's two home pixel
    shadows (0xD031F6, 0xD07396; VPutMap records its glyphs there and home replays them) are as if the
    grapher never ran. Classic and MathPrint."""
    ok = True
    bands = [("v%03d" % y, 0xD40000 + 640 * y, 640 * 16) for y in range(0, 240, 16)]

    def rd(keys, classic):
        pro = PROLOGUE if classic else PROLOGUE.replace("C ", "", 1)
        for settle in (1500, 1900, 2300):
            r = screen.read([("s1", 0xD031F6, 8400), ("s2", 0xD07396, 8400)], keys=pro + keys, files=FILES,
                            launch=LAUNCH, lead=False, settle=settle, crcs=bands, step=1)
            if b"\xff" * 4 not in r["s1"] and None not in [r[n] for n, _, _ in bands]:
                break
        return r

    base = "2 + 3 enter . y= xton ^2 / 5 2nd mode . "
    tail = "7 8 left 2nd math clear"
    for classic in (False, True):
        ref = rd(base + tail, classic)
        for name, keys in (("TRACE", "graph . . trace right clear 2nd mode . "),
                           ("CALC value", "graph . . 2nd trace 1 2 enter . clear 2nd mode . ")):
            r = rd(base + keys + tail, classic)
            bad = [n for n, _, _ in bands if r[n] != ref[n]] + [n for n in ("s1", "s2") if r[n] != ref[n]]
            ok = report(not bad, "%s, %s, home menu after: screen and shadows as without the graph%s"
                        % ("Classic" if classic else "MathPrint", name, " (differs: %s)" % bad if bad else "")) and ok
    return ok


# (name, keys before, 2nd?) for each key that leaves the graph
HANDS = [("Y=", "y="), ("WINDOW", "window"), ("ZOOM", "zoom"), ("MODE", "mode"),
         ("MATH", "math"), ("PRGM", "prgm"), ("VARS", "vars"), ("STAT", "stat"), ("APPS", "apps"),
         ("2nd CALC", "2nd trace"), ("2nd FORMAT", "2nd zoom"), ("2nd TABLE", "2nd graph"),
         ("2nd TBLSET", "2nd window"), ("2nd STAT PLOT", "2nd y="), ("2nd DRAW", "2nd prgm"),
         ("2nd MEM", "2nd +"), ("2nd LIST", "2nd stat"), ("2nd TEST", "2nd math"),
         ("2nd ANGLE", "2nd apps"), ("2nd DISTR", "2nd vars"), ("2nd MATRIX", "2nd -1")]


def hand():
    """Each key on SymCE's graph reaches the context (cxCurApp) it does on TI's."""
    ok = True
    for name, k in HANDS:
        draw = "y= xton ^2 / 5 2nd mode . graph . . "
        state(draw + k + " . ")
        a = state.cx
        state(TI_OFF + draw + k + " . ")
        b = state.cx
        # menu keys open over home (0x40) where TI opens them over the graph (0x44)
        # 2nd TABLE: SymCE draws its own table (graph_table.c) and leaves cxCurApp alone, TI opens cxTable 4A
        good = a == b or (a == 0x40 and b == 0x44) or (name == "2nd TABLE" and a == 0x45 and b == 0x4A)
        ok = report(good, "%s from the graph: SymCE cxCurApp %02X, TI %02X" % (name, a, b)) and ok
    return ok


def fall():
    """Each thing the grapher does not draw hands the GRAPH key to the OS (cxCurApp 44)."""
    ok = True
    y = "y= xton ^2 / 5 2nd mode . "
    cases = [("Horizontal split", "mode w300 " + "down " * 7 + "right enter w300 2nd mode . " + y),
             ("stat plot 1 on", "2nd y= enter enter 2nd mode . " + y),
             ("line style Above", y + "y= " + y1_style(line=2) + "2nd mode . "),
             ("background image", fmt_keys((8, 1)) + y)]
    for name, pre in cases:
        state(pre + "graph . . ")
        a = state.cx
        state(TI_OFF + pre + "graph . . ")
        ok = report(a == state.cx and a != 0x40, "%s: TI's graph (cxCurApp %02X, TI %02X)" % (name, a, state.cx)) and ok
    state("mode w300 " + "down " * 3 + "right enter w300 2nd mode . " + y + "graph . . ")
    ok = report(state.cx in (0x40, 0x45), "Parametric mode graph is ours (cxCurApp %02X)" % state.cx) and ok
    state(y + "graph . . ")
    ok = report(state.cx in (0x40, 0x45), "plain Function graph stays ours (cxCurApp %02X; 45 on the first, X and Y made under MODE)" % state.cx) and ok
    # An empty window (Xmin >= Xmax, Ymin >= Ymax, ZoomFit of a constant) is TI's ERR:WINDOW RANGE screen,
    # pixel for pixel, from every way in: GRAPH, TRACE, ZOOM In, 2nd CALC, 2nd TABLE, and then Quit.
    sq = "y= xton ^ 2 2nd mode . "
    for name, pre, tail in (("Ymin=Ymax", sq + "window down down down 5 enter 5 enter 2nd mode . ", ["graph . . ", "trace . . ", "zoom 2 w800 ", "2nd trace w800 ", "2nd graph . . ", "graph . . 1 . . "]),
                            ("Xmin>Xmax", sq + "window 5 enter 3 enter 2nd mode . ", ["graph . . ", "trace . . ", "zoom 2 w800 ", "2nd trace w800 ", "2nd graph . . ", "graph . . 1 . . "]),
                            ("ZoomFit of Y1=5", "y= 5 2nd mode . graph . . ", ["zoom 0 w7000 ", "zoom 0 w7000 1 . . "])):
        for t in tail:
            a, b = grab(pre + t), grab(TI_OFF + pre + t)
            ok = report(a == b, "%s, %s: TI's ERR:WINDOW RANGE screen (%s)" % (name, t.strip(" ."), "same pixels" if a == b else "DIFFERENT")) and ok
    return ok


EXITS = [("CLEAR", "clear . "), ("2nd QUIT", "2nd mode . "), ("Y=", "y= . "), ("WINDOW", "window . "), ("MODE", "mode . "),
         ("ZOOM menu, QUIT", "zoom w800 2nd mode . "), ("2nd CALC menu, QUIT", "2nd trace w800 2nd mode . "),
         ("TRACE, QUIT", "trace w300 2nd mode . "), ("8 s on the plot, CLEAR", "w8000 clear . "),
         ("8 s on the plot, Y=", "w8000 y= . "), ("cursor to the top, QUIT", "up " * 22 + "w300 2nd mode . ")]


def exitbar():
    """Every way out of the big graph leaves rows 0..23 of the home/editor screen (TI's status bar, which the grapher covers) the
    way TI's own graph leaves them: pixel for pixel, taken after the same 2nd QUIT on TI's graph."""
    import tigo
    from e2e import PROLOGUE as P
    pre = "y= sin xton ) 2nd mode . "
    shot = lambda k: tigo.go(k + " w600 @a", [tigo.Snap("a", rect=(0, 0, 320, 24))], files=FILES, launch=LAUNCH)["a"]["px"]
    base = shot(P + TI_OFF + pre + "graph . . 2nd mode . ")
    ok = True
    for name, keys in EXITS:
        px = shot(P + pre + "graph . . " + keys)
        bad = sum(px.get(k) != v for k, v in base.items())
        ok = report(bad == 0, "exit by %s: status bar rows 0..23 equal TI's (%d px differ)" % (name, bad)) and ok
    return ok


def on():
    """ON stops a slow draw: Y1 = fnInt(X,X,0,X) takes minutes, then CLEAR is read and the home screen works."""
    _, _, _, rows = state("y= math 9 xton , xton , 0 , xton ) 2nd mode . graph w3000 on w1500 clear . " + TYPE)
    return report(rows[-1:] == ["4X"], "ON interrupts a slow draw; CLEAR then leaves (rows %s)" % rows[-2:])


def run_seq(seq, hashes):
    cfg = {"transfer_files": list(FILES), "target": {"name": LAUNCH, "isASM": False},
           "delay_after_step": 1, "sequence": seq,
           "hashes": {n: {"description": n, "start": hex(a), "size": str(s), "expected_CRCs": ["00000000"]}
                      for n, (a, s) in hashes.items()}}
    p = os.path.join(HERE, "_gt.json")
    json.dump(cfg, open(p, "w"))
    r = subprocess.run([screen.TESTER, p], capture_output=True, text=True,
                       env=dict(os.environ, AUTOTESTER_ROM=screen.ROM), cwd=HERE)
    if "unknown key" in r.stderr:
        sys.exit("BAD KEY " + r.stderr)
    return dict(re.findall(r'Hash #\S+ \("([^"]*)"\).*?got ([0-9A-F]{1,8})\)', r.stdout))


def keyseq(keys):
    seq = []
    for k in keys.split():
        if k == ".":
            seq.append("delay|2000")
        elif k == "C":
            seq += ["action|useClassic", "delay|1500"]
        elif k == "L":
            seq += ["action|launch", "delay|4000"]
        else:
            seq += ["key|" + k, "delay|200"]
    return seq


def draw_ms(funcs):
    """ms from the GRAPH key to the last change of rows 0..239 (to the sample period)."""
    pre = PROLOGUE + "y= " + " down ".join(funcs) + " 2nd mode . "
    addr, size = vram.VRAM, 240 * 640
    period, n = 20, 50
    while True:
        seq = ["delay|1500"] + keyseq(pre) + ["key|graph"]
        hs = {}
        for i in range(n):
            seq.append("delay|%d" % period)
            hs["h%d" % i] = (addr, size)
            seq.append("hash|h%d" % i)
        seq.append("delay|3000")
        hs["fin"] = (addr, size)
        seq.append("hash|fin")
        got = run_seq(seq, hs)
        if len(got) < n + 1:
            print("  aborted at %d of %d samples, retrying" % (len(got), n + 1))
            period += 3                                   # the abort repeats for the same timing
            continue
        crcs = [got["h%d" % i] for i in range(n)]
        last = max((i for i, c in enumerate(crcs) if c != got["fin"]), default=-1)
        if last < n - 1:
            return (last + 2) * period
        period *= 4                                       # still drawing at the end


def timing():
    print("%-20s %8s %8s %6s" % ("Y=", "SymCE ms", "OS ms", "x"))
    for name, funcs in TIMED.items():
        ms = draw_ms(funcs)
        print("%-20s %8d %8d %6.1f" % (name, ms, OS_MS[name], OS_MS[name] / ms))


DISTINCT_MIN = 60  # fixed build ~100, the all-"3 sqrt(" bug 22
MENUS = [("2nd CALC", "2nd trace"), ("2nd FORMAT", "2nd zoom"), ("2nd DRAW", "2nd prgm"), ("ZOOM", "zoom")]


def menus():
    """Evo font on (the installer's default): a menu opened from the graph draws the same rows as at
    home. With the localize hook armed and answering NZ to events the OS expects Z for, 2nd CALC drew
    '3 sqrt(' for every item. Rows below the status bar are compared (its mode text differs)."""
    ok = True
    for name, k in MENUS:
        a = grab(keys_for(["xton ^2"], k + " . "))
        b = grab(PROLOGUE + k + " . ")
        same = all(a[xy] == b[xy] for xy in a if xy[1] >= 26)
        ok = report(same, "%s from the graph draws the home rows (Evo font on)" % name) and ok
        # both screens can be wrong the same way (every CALC row was '3 sqrt('): count distinct text rows
        ndist = len({tuple(a.get((x, y)) for x in range(26, 200)) for y in range(40, 200)})
        print("  %s distinct pixel rows: %d" % (name, ndist))
        if k == "2nd trace":
            ok = report(ndist >= DISTINCT_MIN, "2nd CALC rows differ from each other (not all one glyph)") and ok
        if not same:
            cpng(a, os.path.join(PNG, "menus_graph.png"))
            cpng(b, os.path.join(PNG, "menus_home.png"))
    # Evo font off (APPS > SymCE > 3: the localize hook is released), CAS on, GRAPH key SYMCE
    a = grab(keys_for(["xton ^2"], "2nd trace . ", pre=SETTINGS + "3 . clear . 2nd mode . "))
    ndist = len({tuple(a.get((x, y)) for x in range(26, 200)) for y in range(40, 200)})
    ok = report(ndist >= DISTINCT_MIN, "2nd CALC from the graph, Evo font off: rows differ (%d)" % ndist) and ok
    return ok


if __name__ == "__main__":
    allt = ["shots", "pixels", "format", "piece", "osparse", "side", "hand", "fall", "on", "entry", "menus", "exitbar", "leak", "trace", "stat", "time"]
    want = sys.argv[1:] or allt
    ok = True
    shot = None
    if "shots" in want or "pixels" in want:
        shot = shots()
    for name, fn in (("pixels", lambda: pixels(shot)), ("format", lambda: fmt() and asym()), ("piece", piece),
                     ("osparse", osparse), ("side", side), ("hand", hand), ("fall", fall), ("on", on),
                     ("entry", entry), ("menus", menus), ("exitbar", exitbar), ("leak", leak), ("trace", lambda: __import__("trace").main([])),
                     ("stat", lambda: __import__("statplot").main([]))):
        if name in want:
            ok = fn() and ok
    if "time" in want:
        timing()
    print("ALL PASS" if ok else "FAILED")
    sys.exit(0 if ok else 1)
