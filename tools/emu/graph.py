#!/usr/bin/env python3
"""SymCE's own grapher (settings screen, 4: Graph), on the real ROM.

    python3 graph.py [shots] [pixels] [repeat] [time]      # default: all three

  shots   colour PNGs of the whole LCD to _png/fg_*.png (sin; three; zoom+pan;
          an unsupported token)
  pixels  about ten columns of sin(X) and X^2/5 sit within +-1 row of
          round((Ymax - f(x)) / (Ymax - Ymin) * 219)
  repeat  a held arrow keeps panning (GetCSC loop; a tap moves one step)
  time    key 4 to fully drawn, for the six functions timed in the OS graph
          (docs/TI84CE-KNOWLEDGE.md 11b): a CRC of rows 0..219 every few ms until
          it stops changing

The Y= entries go in with the SymCE app armed (as the OS graph would read them),
window at its default -10..10, Radian. Run `make -C symce emu`, or copy the build
into this directory, first.
"""
import json, math, os, re, struct, subprocess, sys, time, zlib
import screen, vram
from e2e import PROLOGUE, FILES, LAUNCH
from lifecycle import SETTINGS

HERE = screen.HERE
PNG = os.path.join(HERE, "_png")
BLUE, BLACK = 0x001F, 0x0000
# name, Y= keys: one string per equation
FUNCS = {
    "sin": ["sin xton )"],
    "three": ["sin xton )", "cos xton )", "xton ^2 / 5"],
    "unsup": ["2nd dp"],                                  # i: token 2C
}
OS_MS = {"(none)": 0, "X": 1864, "sin(X)": 2804, "X^2": 1624, "sin(X)/X+cos(3X)": 5740,
         "sqrt(X)": 1544, "sin,cos,X^2/5": 7480}
TIMED = {"(none)": [], "X": ["xton"], "sin(X)": ["sin xton )"], "X^2": ["xton ^2"],
         "sin(X)/X+cos(3X)": ["sin xton ) / xton + cos 3 xton )"],
         "sqrt(X)": ["2nd ^2 xton )"], "sin,cos,X^2/5": FUNCS["three"]}


def keys_for(funcs, after=""):
    """Type the equations, go home, open the settings screen, press 4, wait."""
    return (PROLOGUE + "y= " + " down ".join(funcs) + " 2nd mode . " + SETTINGS
            + "4 . . " + after)


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


def grab(keys):
    px, _ = vram.grab(keys, 0, vram.H, files=FILES, launch=LAUNCH, lead=False, settle=1500)
    return px


def shots():
    os.makedirs(PNG, exist_ok=True)
    out = {}
    for name, keys in (("fg_sin", keys_for(FUNCS["sin"])),
                       ("fg_three", keys_for(FUNCS["three"])),
                       ("fg_zoom", keys_for(FUNCS["sin"], "+ . . right . right . ")),
                       ("fg_unsup", keys_for(FUNCS["unsup"]))):
        out[name] = grab(keys)
        cpng(out[name], os.path.join(PNG, name + ".png"))
        print("wrote", name + ".png", "non-white px:", sum(v != 0xFFFF for v in out[name].values()))
    return out


def near(px, col, row, colour):
    return any(px.get((col, r)) == colour for r in range(row - 1, row + 2))


def pixels(shot):
    ok = True
    for label, px, f, colour, cols in (
            ("sin(X)", shot["fg_sin"], math.sin, BLUE, range(0, 320, 32)),
            ("X^2/5", shot["fg_three"], lambda x: x * x / 5, BLACK, range(100, 231, 13))):
        good = bad = 0
        for c in cols:
            x = -10 + c * 20 / 319
            if label == "X^2/5" and abs(x) < 2.5:
                continue                                  # on the black axis
            row = round((10 - f(x)) / 20 * 219)
            if near(px, c, row, colour):
                good += 1
            else:
                bad += 1
                print("  FAIL %s col %d x %.2f expect row %d" % (label, c, x, row))
        print("%s %s: %d of %d columns within +-1 row" % ("ok" if not bad else "FAIL", label, good, good + bad))
        ok = ok and not bad
    return ok


def axis_col(px):
    """The x of the black vertical axis in rows 100..119 (None: off screen)."""
    for x in range(320):
        if sum(px.get((x, y)) == BLACK for y in range(100, 120)) >= 18:
            return x


def repeat():
    """A held arrow keeps panning: right, tapped, moves the axis 40 px; held, more."""
    cols = []
    for after in ("right . ", "hold|right w700 release|right . "):
        px = grab(keys_for(["xton"], after))
        cols.append(axis_col(px))
    print("axis column: tap %s, hold %s (start 160)" % tuple(cols))
    ok = cols[0] is not None and (cols[1] is None or cols[1] < cols[0] - 40)
    print("%s key repeat" % ("ok" if ok else "FAIL"))
    # a 150 ms hold of a fast draw (Y1=X) is a tap: it pans once, not twice
    px = grab(keys_for(["xton"], "hold|right w150 release|right . "))
    c = axis_col(px)
    print("axis column: 150 ms hold %s (start 160, one step 120)" % c)
    ok2 = c is not None and 100 < c < 140
    print("%s short hold pans once" % ("ok" if ok2 else "FAIL"))
    return ok and ok2


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
    """ms from the 4 key to the last change of rows 0..219 (to the sample period)."""
    pre = PROLOGUE + "y= " + " down ".join(funcs) + " 2nd mode . " + SETTINGS
    addr, size = vram.VRAM, 220 * 640
    period, n = 20, 50
    while True:
        seq = ["delay|1500"] + keyseq(pre) + ["key|4"]
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


if __name__ == "__main__":
    want = sys.argv[1:] or ["shots", "pixels", "repeat", "time"]
    ok = True
    shot = None
    if "shots" in want or "pixels" in want:
        shot = shots()
    if "pixels" in want:
        ok = pixels(shot)
    if "repeat" in want:
        ok = repeat() and ok
    if "time" in want:
        timing()
    print("ALL PASS" if ok else "FAILED")
    sys.exit(0 if ok else 1)
