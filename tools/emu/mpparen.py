"""Find MathPrint's vector parentheses in a VRAM grab (vram.grab's {(x, y): rgb565}) and say what colour each is.

The OS draws ( and ) as the ROM large glyph's 14 rows (6 px wide box); fhook.c's paren_pass (menu.c) recolours
them by depth. Shapes are (first column, run length) per row, in a 6 px box; a match needs every run inked and
the pixels either side of each run white. expected() is the colour by depth per band (parens with the same top
row), left to right, a ) with nothing open black.
"""
PAL = (0xD800, 0x0460, 0xB016, 0xE360, 0x04B2, 0x8B60)             # fhook.c .Lpal
_L = (3, 2, 3, 2, 3, 3, 3, 3, 3, 3, 2, 3, 2, 3)
OPEN = tuple(zip((3, 2, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 2, 3), _L))
CLOSE = tuple(zip((0, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 0), _L))


def find(px, y0=30, y1=227):
    """[(y, x, '(' or ')', {colours})] over the boxes whose top row is at y, sorted"""
    ink = lambda x, y: px.get((x, y), 0xFFFF) != 0xFFFF
    out = []
    for name, shape in (("(", OPEN), (")", CLOSE)):
        c0, l0 = shape[0]
        for y in range(y0, y1):
            for bx in range(0, 315):
                if not all(ink(bx + c0 + i, y) for i in range(l0)):
                    continue
                ok = True
                for r, (c, l) in enumerate(shape):
                    if not all(ink(bx + c + i, y + r) for i in range(l)) or ink(bx + c - 1, y + r) or ink(bx + c + l, y + r):
                        ok = False
                        break
                if ok:
                    cols = {px[(bx + c + i, y + r)] for r, (c, l) in enumerate(shape) for i in range(l)}
                    out.append((y, bx, name, cols))
    return sorted(out)


def expected(found):
    """the colour each found paren should have (0 = black), by depth within its band"""
    exp, d, band = [], 0, None
    for y, x, k, _ in found:
        if y != band:
            band, d = y, 0
        if k == "(":
            exp.append(PAL[d % 6]); d += 1
        elif d:
            d -= 1; exp.append(PAL[d % 6])
        else:
            exp.append(0)
    return exp


def wrong(found, black_ok=False):
    """[(y, x, kind, colours, want)] for parens that are not one colour = their depth's"""
    return [(y, x, k, sorted("%04X" % c for c in cols), w) for (y, x, k, cols), w in zip(found, expected(found))
            if cols != {w} and not (black_ok and cols == {0})]
