"""Multi-snapshot driver: RAM and VRAM reads in one emulator run (TI side and SymCE side).
    import tigo; r = tigo.go("y= sin xton ) zoom 6 . .", [tigo.Snap("a", ram=[...], rect=(0,0,320,240))])
Script = key string (screen.py syntax: '.' 2 s, wN ms, 'hold|right', 'dp') with
'@name' tokens that take a snapshot in the same emulator run (RAM + VRAM rect)."""
import json, os, re, struct, subprocess, sys, zlib
import screen, crcread
HERE = screen.HERE
VRAM, W, H = 0xD40000, 320, 240
ROM = screen.ROM
FULL = (0, 0, 320, 240)

class Snap:
    def __init__(self, name, ram=(), rect=None, crc=()):
        self.name, self.ram, self.rect, self.crc = name, list(ram), rect, list(crc)

def go(script, snaps, rom=ROM, boot=1500, per_key=200, step=1, tries=6, files=("arTIfiCE.8xp",), launch=""):
    snaps = {s.name: s for s in snaps}
    for attempt in range(tries):
        seq = ["delay|%d" % (boot + 37 * attempt)]
        hashes, plan = {}, []
        n = 0
        for k in script.split():
            if k == ".": seq.append("delay|2000")
            elif k[0] == "@":
                s = snaps[k[1:]]
                regs = [(("r", a), a, l) for a, l in s.ram]
                if s.rect:
                    rl = s.rect if isinstance(s.rect[0], tuple) else [s.rect]
                    for (x0, y0, x1, y1) in rl:
                        for y in range(y0, y1):
                            for x in range(x0 & ~1, x1, 2):
                                regs.append((("p", x, y), VRAM + 2 * (y * W + x), 4))
                for i, (a, l) in enumerate(s.crc):
                    n += 1; nm = str(n)
                    hashes[nm] = {"description": nm, "start": hex(a), "size": str(l), "expected_CRCs": ["00000000"]}
                    seq.append("hash|" + nm)
                    plan.append((nm, s.name, ("c", i), 0, 0))
                for tag, a, l in regs:
                    for off in range(0, l, 4):
                        n += 1
                        nm = str(n)
                        hashes[nm] = {"description": nm, "start": hex(a + off), "size": str(min(4, l - off)),
                                      "expected_CRCs": ["00000000"]}
                        seq.append("hash|" + nm)
                        plan.append((nm, s.name, tag, off, min(4, l - off)))
            elif k[0] == "w" and k[1:].isdigit(): seq.append("delay|" + k[1:])
            elif "|" in k: seq += [k, "delay|%d" % per_key]
            elif k == "dp": seq += ["key|.", "delay|%d" % per_key]
            elif k == "C": seq += ["action|useClassic", "delay|1500"]
            elif k == "L": seq += ["action|launch", "delay|4000"]
            else: seq += ["key|" + k, "delay|%d" % per_key]
        cfg = {"transfer_files": list(files), "target": {"name": launch or "A", "isASM": False},
               "delay_after_step": step, "sequence": seq, "hashes": hashes}
        p = os.path.join(HERE, "_ti.json"); json.dump(cfg, open(p, "w"))
        r = subprocess.run([screen.TESTER, p], capture_output=True, text=True,
                           env=dict(os.environ, AUTOTESTER_ROM=rom), cwd=HERE)
        if "unknown key" in r.stderr: sys.exit("BAD KEY:\n" + r.stderr)
        got = dict(re.findall(r'Hash #\S+ \("([^"]*)"\).*?got ([0-9A-F]{1,8})\)', r.stdout))
        if len(got) < len(plan):
            sys.stderr.write("retry %d: %d/%d\n" % (attempt, len(got), len(plan)))
            continue
        out = {}
        for nm, sn, tag, off, l in plan:
            o = out.setdefault(sn, {"ram": {}, "px": {}})
            if tag[0] == "c":
                o.setdefault("crc", []).append(int(got[nm], 16)); continue
            b = crcread.invert(int(got[nm], 16), l)
            if tag[0] == "r":
                o["ram"][tag[1]] = o["ram"].get(tag[1], b"") + b
            else:
                o["px"][(tag[1], tag[2])], o["px"][(tag[1] + 1, tag[2])] = struct.unpack("<HH", b)
        return out
    sys.exit("emulator kept aborting")

def rgb(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))

def png(px, path, scale=1, rect=None):
    x0, y0, x1, y1 = rect or (0, 0, W, H)
    raw = b""
    for y in range(y0, y1):
        row = bytearray()
        for x in range(x0, x1):
            row += bytes(rgb(px.get((x, y), 0xFFFF))) * scale
        raw += (b"\0" + bytes(row)) * scale
    def ch(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", (x1 - x0) * scale, (y1 - y0) * scale, 8, 2, 0, 0, 0))
                           + ch(b"IDAT", zlib.compress(raw)) + ch(b"IEND", b""))

def diff(a, b):
    """{addr: (old,new)} between two ram snapshots (same ranges)."""
    d = {}
    for k in a:
        for i, (x, y) in enumerate(zip(a[k], b[k])):
            if x != y: d[k + i] = (x, y)
    return d
