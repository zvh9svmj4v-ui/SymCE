#!/usr/bin/env python3
"""Read the LCD out of VRAM: the only way to see what MathPrint drew.

textShadow is blank in MathPrint, so a 2D answer (stacked fraction, radical,
raised power) can only be checked here. VRAM is 320x240 RGB565 at 0xD40000,
640 bytes per row; each 4-byte hash window is two pixels. Reads go through
screen.read with step=1 (at 0 CEmu trips its scheduler assertion).

    python3 vram.py "2 xton + 2 xton enter . ." --rows 30 90 --png out.png
    python3 vram.py KEYS --symce      # SymCE installed and armed first

A full-width row is 160 windows; the whole screen at full resolution (38400
windows) adds about 3 s to a run. --ystep/--xstep thin it out for ASCII output.
"""
import argparse, struct, sys, zlib
import screen
from e2e import FILES

VRAM, W, H = 0xD40000, 320, 240
SYMCE = "apps down enter . clear . L . . clear . "   # arm AsmHook2, run the installer


def grab(keys, y0=0, y1=H, ystep=1, xstep=1, extra=(), **kw):
    """({(x, y): rgb565}, the screen.read dict) for rows y0..y1 (every ystep),
    pixel pairs every xstep; `extra` ranges are read in the same run."""
    ranges, where = list(extra), [None] * len(extra)
    for y in range(y0, y1, ystep):
        for x in range(0, W, 2 * xstep):
            ranges.append(("%d_%d" % (x, y), VRAM + 2 * (y * W + x), 4))
            where.append((x, y))
    r = screen.read(ranges, keys=keys, step=1, **kw)
    px = {}
    for xy, (name, _, _) in zip(where, ranges):
        if xy:
            px[xy], px[(xy[0] + 1, xy[1])] = struct.unpack("<HH", r.pop(name))
    return px, r


def dark(v):
    r, g, b = v >> 11, (v >> 5) & 63, v & 31
    return r * 2 + g + b * 2 < 96          # of 190: black text on white


def ascii(px, x0=0, x1=W):
    ys = sorted({y for _, y in px})
    xs = sorted({x for x, _ in px if x0 <= x < x1})
    out = []
    for y in ys:
        out.append("%3d " % y + "".join("#" if dark(px[(x, y)]) else "." for x in xs))
    return "\n".join(out)


def png(px, path):
    """Grey PNG, stdlib only; unread pixels (FFFF from an aborted read) are white."""
    xs, ys = sorted({x for x, _ in px}), sorted({y for _, y in px})
    raw = b""
    for y in ys:
        row = bytearray([0])
        for x in xs:
            v = px[(x, y)]
            r, g, b = (v >> 11) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3
            row.append((r * 3 + g * 6 + b) // 10)
        raw += bytes(row)

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", len(xs), len(ys), 8, 0, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def spans(px, y0, y1):
    """(first, last) x of dark pixels in rows y0..y1, or None: where text landed."""
    xs = [x for (x, y), v in px.items() if y0 <= y < y1 and dark(v)]
    return (min(xs), max(xs)) if xs else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("keys", nargs="?", default="")
    ap.add_argument("--rows", nargs=2, type=int, default=(0, H))
    ap.add_argument("--ystep", type=int, default=1)
    ap.add_argument("--xstep", type=int, default=1)
    ap.add_argument("--files", default=",".join(FILES))
    ap.add_argument("--launch", default="SYMCE")
    ap.add_argument("--symce", action="store_true", help="arm AsmHook2 and run the installer first")
    ap.add_argument("--png")
    a = ap.parse_args()
    keys = (SYMCE if a.symce else "") + a.keys
    px, _ = grab(keys, a.rows[0], a.rows[1], a.ystep, a.xstep, files=tuple(a.files.split(",")),
              launch=a.launch, lead=not a.symce)
    print(ascii(px))
    if a.png:
        png(px, a.png)


if __name__ == "__main__":
    sys.exit(main())
