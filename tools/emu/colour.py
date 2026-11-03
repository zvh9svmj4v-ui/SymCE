#!/usr/bin/env python3
"""Lowercase letters (a command's name) are drawn in TI's blue, 0x001F, on the home screen only.

Menus type solve( etc. as lowercase tokens; hand-typed capitals are variables. The font hook
(fhook.c) turns the colour on for a..z glyphs (not in a run of letters that began with a capital: Done, Ans, Xmin) when cxCurApp is the home screen and no menu is open,
and puts it back when the drawing routine returns. Counts the 0x001F pixels in VRAM, Classic and
MathPrint, Evo font on and off:

    python3 colour.py [out_dir]

Parentheses are rainbow on the Classic home screen (fhook.c .Lpar): by depth, red, green, magenta, orange, teal,
olive, round again; an unmatched ) stays black. MathPrint draws its parentheses as vectors without the font hook:
menu.c paren_pass recolours them from the cursor glyph's call (mprb below).
"""
import os, re, sys
import e2e, vram, mpparen

SET = "apps w300 " + "down w150 " * 16 + "enter . . "                       # APPS > SymCE: 3 = font
TI_FONT = SET + "3 . clear . "
BLUE = 0x001F
PAL = (0xD800, 0x0460, 0xB016, 0xE360, 0x04B2, 0x8B60)            # rainbow parentheses by depth (fhook.c .Lpal)
FAILS = []


BASE = {}


def shot(pre, keys, label, y0=0, y1=240, raw=False):
    """(blue pixels, other colours) that the keys added to the bare home screen: its "Done" line is TI's
    own lowercase text, blue too, and the status bar has its own colours"""
    mode = pre + ("C " if keys.startswith("C ") else "")
    if label != "base" and mode not in BASE:
        BASE[mode] = shot(pre, "C " if keys.startswith("C ") else "", "base", 0, 240)
    px, _ = vram.grab(vram.SYMCE + pre + keys + " w300", y0, y1, 1, 1, files=vram.FILES, launch=e2e.LAUNCH, lead=False)
    if len(sys.argv) > 1:
        vram.png({k: v for k, v in px.items() if k[0] < 320}, os.path.join(sys.argv[1], label.replace(" ", "_") + ".png"))
    blue = {k for k, v in px.items() if v == BLUE}
    other = {v for v in px.values() if v not in (BLUE, 0, 0xFFFF) and v not in PAL}
    if label != "base" and not raw:
        b0, o0 = BASE[mode]
        blue, other = blue - b0, other - o0
    return blue, other


def check(label, ok, why):
    print("%-4s %s: %s" % ("ok" if ok else "FAIL", label, why))
    if not ok:
        FAILS.append(label)


for font, pre in (("evo", ""), ("ti", TI_FONT)):
    for mode, c in (("classic", "C "), ("mathprint", "")):
        if os.environ.get("ONLY") and "%s %s" % (font, mode) not in os.environ["ONLY"]:
            continue                                       # ONLY="evo mathprint,ti classic": a subset while working
        # a menu-typed command: blue letters, nothing but black, white and blue on the screen
        blue, other = shot(pre, c + "alpha down 1", "%s %s solve" % (font, mode))
        check("%s %s solve(" % (font, mode), len(blue) > 30 and not other, "%d blue px, other colours %s" % (len(blue), sorted(other)))
        # TI's own mixed words are black: "Done" (the bare screen), Ans, and Ans after other text; the run began with a capital
        blue, _ = shot(pre, c, "%s %s done" % (font, mode), raw=True)
        check("%s %s Done" % (font, mode), not blue, "%d blue px" % len(blue))
        blue, _ = shot(pre, c + "5 sto alpha math enter", "%s %s sto" % (font, mode), raw=True)
        check("%s %s 5->A, Done" % (font, mode), not blue, "%d blue px" % len(blue))
        blue, _ = shot(pre, c + "2nd (-)", "%s %s ans" % (font, mode), raw=True)
        check("%s %s Ans" % (font, mode), not blue, "%d blue px" % len(blue))
        blue, _ = shot(pre, c + "2 + 2nd (-)", "%s %s 2+ans" % (font, mode), raw=True)
        check("%s %s 2+Ans" % (font, mode), not blue, "%d blue px" % len(blue))
        # the run state does not leak between lines or between answers: a variable's answer, then a command
        solve, _ = shot(pre, c + "alpha down 1", "%s %s solve0" % (font, mode))
        blue, _ = shot(pre, c + "xton enter . alpha down 1", "%s %s after X" % (font, mode))
        check("%s %s solve( after an answer" % (font, mode), len(blue) >= 0.9 * len(solve), "%d blue px (alone %d)" % (len(blue), len(solve)))
        # the cursor blinking on a letter puts that letter back alone: it keeps its colour (best of three moments)
        best = max(len(shot(pre, c + "alpha down 1 left left left w%d" % w, "%s %s blink%d" % (font, mode, w))[0]) for w in (700, 1100, 1700))
        check("%s %s blink inside solve(" % (font, mode), best >= len(solve) - 2, "%d blue px (steady %d)" % (best, len(solve)))
        blue = min(len(shot(pre, c + "2nd (-) left w%d" % w, "%s %s ansblink%d" % (font, mode, w), raw=True)[0]) for w in (700, 1100, 1700))
        check("%s %s blink inside Ans" % (font, mode), not blue, "%d blue px" % blue)
        # a history line that wraps; 2nd ENTRY recalls an old command line
        n = 5 if c else 4                       # Classic wraps onto a second row; MathPrint scrolls sideways and shows four
        blue, _ = shot(pre, c + "alpha down 1 " * 5, "%s %s wrap" % (font, mode))
        check("%s %s wrapped solve(" % (font, mode), len(blue) >= 0.95 * n * len(solve), "%d blue px (%d x %d)" % (len(blue), n, len(solve)))
        blue, _ = shot(pre, c + "alpha down 1 enter . . 1 . . 2nd enter", "%s %s recall" % (font, mode))
        check("%s %s 2nd ENTRY recall" % (font, mode), len(blue) >= 0.9 * len(solve), "%d blue px (one %d)" % (len(blue), len(solve)))
        # capitals are variables: no blue
        blue, other = shot(pre, c + "alpha math alpha apps alpha prgm xton", "%s %s caps" % (font, mode))
        check("%s %s capitals" % (font, mode), not blue, "%d blue px" % len(blue))
        # TI's own lowercase function name is blue too (accepted)
        blue, _ = shot(pre, c + "sin", "%s %s sin" % (font, mode))
        check("%s %s sin(" % (font, mode), len(blue) > 10, "%d blue px" % len(blue))
        # a TI menu over the home screen: round( stays black
        blue, _ = shot(pre, c + "math right", "%s %s math" % (font, mode))
        check("%s %s MATH menu" % (font, mode), not blue, "%d blue px" % len(blue))
        # SymCE's own popup, empty entry
        blue, _ = shot(pre, c + "alpha down", "%s %s popup" % (font, mode))
        check("%s %s SymCE menu" % (font, mode), not blue, "%d blue px" % len(blue))
        # the status bar never turns blue
        blue, _ = shot(pre, c + "alpha down 1", "%s %s bar" % (font, mode), 0, 20)
        check("%s %s status bar" % (font, mode), not blue, "%d blue px" % len(blue))
        # the OS replays the old text from its 1-bit shadow when a TI menu closes: black (measured, no hook call);
        # what is typed afterwards is blue again
        blue, _ = shot(pre, c + "alpha down 1 math clear", "%s %s menu closed" % (font, mode))
        check("%s %s old text after a TI menu" % (font, mode), not blue, "%d blue px (the replay is black)" % len(blue))
        blue, _ = shot(pre, c + "alpha down 1 math clear alpha down 1", "%s %s typed after" % (font, mode))
        check("%s %s typed after a TI menu" % (font, mode), len(blue) > 30, "%d blue px" % len(blue))
        # SymCE's settings screen
        blue, _ = shot(pre, c + SET + "clear", "%s %s settings" % (font, mode))
        check("%s %s SymCE settings" % (font, mode), not blue, "%d blue px" % len(blue))

# ---- rainbow parentheses -------------------------------------------------------------------------------------
def want(text, d=0):
    """per character: the rainbow colour its paren must have, None for any other glyph; also the depth left open"""
    out = []
    for ch in text:
        if ch == "(":
            out.append(PAL[d % 6]); d += 1
        elif ch == ")":
            if d:
                d -= 1; out.append(PAL[d % 6])
            else:
                out.append(0)                       # nothing open: black
        else:
            out.append(None)
    return out, d


def rows_ok(px, shadow, skip=None, only=None):
    """(parens checked, wrong cells) over every text row: the ink of each paren cell is its depth's colour,
    every other glyph's ink is not a rainbow colour. Rows are independent lines. skip: (row, col) of the cursor."""
    bad, n, d = [], 0, 0
    for r in range(10):
        txt = "".join(chr(c) if 32 <= c < 127 else "." for c in shadow[r * 26:r * 26 + 26])
        if only is not None and r not in only:
            continue
        wraps = r and prev[0] != " " and prev[25] != " "          # the row above is full: this row goes on with that entry
        ws, d = want(txt, d if wraps else 0)
        prev = txt
        for c, w in enumerate(ws):
            if skip == (r, c):
                continue
            ink = {v for (x, y), v in px.items() if 12 * c <= x < 12 * c + 12 and 20 * r + 39 <= y < 20 * r + 54 and v != 0xFFFF}
            if w is None:
                if ink & set(PAL):
                    bad.append((r, c, txt[c], "rainbow ink on a non-paren"))
            else:
                n += 1
                if ink != {w}:
                    bad.append((r, c, txt[c], "%s want %04X" % (sorted("%04X" % v for v in ink), w)))
    return n, bad


def rainbow(label, keys, texts, pre="", only=None, need=None):
    px, r = vram.grab(vram.SYMCE + pre + "C " + keys + " w300", 0, 240, 1, 1, files=vram.FILES, launch=e2e.LAUNCH, lead=False,
                      extra=[("shadow", 0xD006C0, 260), ("cur", 0xD00595, 2)])
    if len(sys.argv) > 1:
        vram.png({k: v for k, v in px.items() if k[0] < 320}, os.path.join(sys.argv[1], "rb_" + label.replace(" ", "_") + ".png"))
    sh = r["shadow"]
    shown = ["".join(chr(c) if 32 <= c < 127 else "." for c in sh[i * 26:i * 26 + 26]).rstrip() for i in range(10)]
    n, bad = rows_ok(px, sh, skip=(r["cur"][0], r["cur"][1]), only=only)
    has = lambda l, t: any(all(a == b or a == "." for a, b in zip(l[i:i + len(t)], t)) and i + len(t) <= len(l) for i in range(len(l)))     # "." is the cursor
    ok = all(any(has(l, t) for l in shown) for t in texts) and not bad and n >= (need or sum(t.count("(") + t.count(")") for t in texts) - 1)
    check("rainbow " + label, ok, "%d parens checked, shown %s, wrong %s" % (n, [s for s in shown if s], bad[:4]))


if not os.environ.get("ONLY") or "rainbow" in os.environ["ONLY"]:
    rainbow("nested", "( ( ( 1 ) ) )", ["(((1)))"])
    rainbow("sin", "sin ( xton + 1 ) ^ 2 )", ["sin((X+1)^2)"])
    rainbow("wrap past six", "2 ( 3 ( 4 ( 5 ( 6 ( 7 ( 8 ( 9 ) ) ) ) ) ) )", ["2(3(4(5(6(7(8(9)))))))"])
    rainbow("unmatched", "1 ) ( 2 )", ["1)(2)"])
    rainbow("insert", "( ( 1 ) ) left left left (", ["(((1))"])
    rainbow("delete", "( ( 1 ) ) left left left left del", ["(1))"])
    rainbow("history", "( ( 1 ) ) enter . . ( 2 ) enter . . ( ( ( 3 ) ) ) enter . . ( 4 )", ["((1))", "(2)", "(((3)))", "(4)"])
    rainbow("scrolled", "( ( 1 ) ) enter . . ( 2 ) enter . . ( ( ( 3 ) ) ) enter . . ( ( 4 ) ) enter . . ( 5 ) enter . . ( 6 ) enter . . w1500 ( ( 7 ) )",
            ["((7))"], need=4)
    rainbow("recall", "( ( 1 ) ) enter . . 2nd enter", ["((1))"])
    rainbow("wrapped entry", "( ( 1 2 3 4 5 6 7 8 9 1 2 3 4 5 6 7 8 9 1 2 3 4 ) ( 5 ) )", ["((1234567891234567891234)("], need=4)
    rainbow("ti font", "( ( 1 ) ) sin ( ( 2 ) )", ["((1))sin(((2))"], pre=TI_FONT)

# the cursor blinking over a paren: the put-back of that cell draws the paren in its own colour, never another's
for keys, col, nm in (("( ( 1 ) ) left left left left", 1, "(  on the 2nd paren"), ("( ( 1 ) ) left left", 3, ") on a close")):
    seen, wrong = set(), []
    for w in (400, 800, 1300, 1700):
        px, _ = vram.grab(vram.SYMCE + "C " + keys + " w%d" % w, 40, 80, 1, 1, files=vram.FILES, launch=e2e.LAUNCH, lead=False)
        ink = {v for (x, y), v in px.items() if 12 * col <= x < 12 * col + 12 and 59 <= y < 74 and v != 0xFFFF}
        seen |= ink & set(PAL)
        wrong += sorted("%04X" % v for v in ink - {0} - {want("((1))")[0][col]})
    check("blink on a paren (%s)" % nm, seen == {want("((1))")[0][col]} and not wrong, "colours seen %s, wrong %s" % (sorted("%04X" % v for v in seen), wrong))

# SymCE's popup over a rainbow line saves the screen's seven colours and puts them back
px, r = vram.grab(vram.SYMCE + "C ( ( ( 1 ) ) ) alpha down w300", 0, 240, 1, 1, files=vram.FILES, launch=e2e.LAUNCH, lead=False)
shown = sum(1 for (x, y), v in px.items() if y > 120 and v != 0xFFFF)
check("popup over a rainbow line opens", shown > 500, "%d non-white px below row 120" % shown)
rainbow("after the popup closes", "( ( ( 1 ) ) ) alpha down clear", ["((("])
rainbow("after a help screen closes", "( ( ( 1 ) ) ) alpha down + . . clear . . clear", ["((("])

# MathPrint draws its parentheses as black vectors, not through the font hook: menu.c paren_pass recolours them
# by depth from the cursor glyph's call (fhook.c .Lcpass). mpparen.py reads them back out of VRAM.
def mprb(label, keys, n, pre="", black_ok=False):
    px, _ = vram.grab(vram.SYMCE + pre + keys + " w1200", 0, 240, 1, 1, files=vram.FILES, launch=e2e.LAUNCH, lead=False)
    if len(sys.argv) > 1:
        vram.png({k: v for k, v in px.items() if k[0] < 320}, os.path.join(sys.argv[1], "mp_" + label.replace(" ", "_") + ".png"), 2)
    f = mpparen.find(px)
    bad = mpparen.wrong(f, black_ok)
    check("mathprint rainbow " + label, (len(f) == n or black_ok and len(f) == n - 1) and not bad, "%d of %d parens found, wrong %s" % (len(f), n, bad[:3]))


if not os.environ.get("ONLY") or "mathprint" in os.environ["ONLY"] or "rainbow" in os.environ["ONLY"]:
    for font, pre in (("evo", ""), ("ti", TI_FONT)):
        mprb(font + " nested", "( ( 1 + 2 ) * ( 3 ) )", 6, pre)
        mprb(font + " sin", "sin ( 2 ( xton + 1 ) )", 5, pre)
        mprb(font + " unmatched", "1 ) ( 2 )", 3, pre)
        mprb(font + " six deep", "2 ( 3 ( 4 ( 5 ( 6 ( 7 ( 8 ( 9 ) ) ) ) ) ) )", 14, pre)
        mprb(font + " history", "( ( 1 ) ) enter . . ( 2 ) enter . . ( ( ( 3 ) ) ) enter . . ( 4 )", 14, pre)
        mprb(font + " recall", "( ( 1 ) ) enter . . 2nd enter", 8, pre)
        mprb(font + " insert", "( ( 1 ) ) left left left (", 5, pre)
        mprb(font + " delete", "( ( 1 ) ) left left left left del", 3, pre)
        mprb(font + " answer", "( xton + 1 ) ( xton - 1 ) enter . .", 4, pre)
    # the cursor on a paren: the OS blinks it black by vector, so black is allowed, never another depth's colour
    for w in (400, 800, 1300):
        mprb("blink on ) %d" % w, "( ( 1 ) ) left left w%d" % w, 4, black_ok=True)
    # SymCE's popup over twenty rainbow parens opens, and the line is whole again after it closes
    px, _ = vram.grab(vram.SYMCE + "( ( ( 1 ) ) ) ( ( ( 2 ) ) ) ( ( ( 3 ) ) ) ( ( ( 4 ) ) ) ( ( ( 5 ) ) ) ( 6 ) ( 7 ) ( 8 ) alpha down w300", 0, 240, 1, 1, files=vram.FILES, launch=e2e.LAUNCH, lead=False)
    shown = sum(1 for (x, y), v in px.items() if y > 120 and v != 0xFFFF)
    check("mathprint popup over parens opens", shown > 500, "%d non-white px below row 120" % shown)
    mprb("after the popup closes", "( ( ( 1 ) ) ) alpha down clear", 6)

# SymCE's own NO SOLUTION error, then Quit: the OS redraws home from its 1-bit shadows in black, so hook.c .Lerr saves the
# coloured pixels (menu.c err_save) and fhook.c .Lcpass puts them back (err_put)
SOLVE = "alpha down 1 xton ^ 2 %s+ 1 2nd math 1 0 , xton ) enter . . w500 1 . w600"
for mode, c, mid, ink, need in (("classic", "C ", "", PAL, 25), ("mathprint", "", "right ", (BLUE,), 150)):
    px, _ = vram.grab(vram.SYMCE + c + SOLVE % mid, 0, 240, 1, 1, files=vram.FILES, launch=e2e.LAUNCH, lead=False)
    n = sum(1 for v in px.values() if v in ink)
    check("%s coloured history after an error and Quit" % mode, n >= need, "%d coloured px, need %d" % (n, need))

# a full home screen scrolls the entry to the bottom: the saved colours still land on the entry's own pixels, none on other lines
FILL = "( 1 ) + ( 2 ) enter . . " * 4 + "( xton + 1 ) ( xton - 1 ) enter . . " * 4
for mode, c, mid, ink in (("classic", "C ", "", PAL), ("mathprint", "", "right ", (BLUE,))):
    px, _ = vram.grab(vram.SYMCE + c + FILL + SOLVE % mid, 0, 240, 1, 1, files=vram.FILES, launch=e2e.LAUNCH, lead=False)
    ys = [y for (x, y), v in px.items() if v in ink and (mode == "classic" or y > 120)]
    check("%s full screen: colour only on the entry's line" % mode, ys and max(ys) - min(ys) < 24, "%d px, rows %s..%s" % (len(ys), min(ys, default=None), max(ys, default=None)))

sys.exit(1 if FAILS else 0)
