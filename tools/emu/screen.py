#!/usr/bin/env python3
"""Read calculator MEMORY and the home screen TEXT out of the autotester.

The autotester reports a CRC and nothing else, so the harness could only ever
answer "did these bytes change". That is why SymCE shipped four times without
anyone seeing what the calculator actually displayed.

CRC-32C is affine, so a range of four bytes or fewer is recoverable from its
CRC (crcread.invert). Chaining one hash per four bytes turns that into a plain
memory read of any length, in a single emulator run: every window is hashed in
the same sequence, so the cost is one boot, not one boot per window.

    python3 screen.py                       # boot, dump the home screen
    python3 screen.py "down enter . ."      # ... after prgmA + those keys
    python3 screen.py --files SYMCE.8xp,SYMCE1.8xv --launch SYMCE --asm

textShadow is the OS's home screen character buffer: 10 rows of 26 columns at
0xD006C0. It holds TI token bytes, which are ASCII for the printable range, so
the answer line reads back literally -- "4X" is 0x34 0x58.
"""
import argparse, json, os, re, subprocess, sys
import crcread

HERE = os.path.dirname(os.path.abspath(__file__))
STOCK = os.environ.get("AUTOTESTER_ROM", os.path.expanduser("~/CEdev/ti84pce.rom"))
TESTER = os.path.expanduser("~/CEdev/bin/cemu-autotester")


def installed(img, dest):
    """The image as the installer leaves it at `dest`: every relocation table
    entry (where, what; both relative to the code) patched."""
    b = bytearray(img)
    le = lambda i: int.from_bytes(b[i:i + 3], "little")
    code = 256 + le(256 + 0x12)
    for e in range(298, code, 6):
        where, what = le(e), le(e + 3)
        b[code + where:code + where + 3] = (dest + code + what).to_bytes(3, "little")
    return bytes(b)


def roomy(stock):
    """The ROM without its dumper's ROMData appvars, each alone in a sector
    (entry at byte 1): ~830 KB a user's calculator does not hold, which left
    no room for SymCE past 69 KB with its appvars archived."""
    # SYMCE_ROOMY_TO=0x2E91E7 (test only): also erase every app below that address (an app
    # boundary), for installs of 11-16 parts, which a stock CE only holds with TI's apps deleted.
    to = int(os.environ.get("SYMCE_ROOMY_TO", "0"), 0)
    out = os.path.join(HERE, "_base2%s.rom" % ("_%X" % to if to else ""))
    if os.path.exists(out) and os.path.getmtime(out) >= os.path.getmtime(stock):
        return out
    b = bytearray(open(stock, "rb").read())
    # The dumper's other archived vars (0x0C..0x13: ROMData, PCAS, TITOL, GDGrphcs, DEMONEW, an old
    # SYMCE) are junk too: an 8- or 9-part app needs its appvars AND the app in the blank run
    # (1 sector per appvar + the app, ~17 of 21), which 0x14..0x1F alone (13) cannot hold.
    b[0x0C * 0x10000:0x14 * 0x10000] = b"\xff" * (8 * 0x10000)
    for s in range(0x0C, 0x40):
        at, top = s * 0x10000, (s + 1) * 0x10000
        end = at + 4 + int.from_bytes(b[at + 2:at + 4], "little")
        if b[at + 11:at + 18] == b"ROMData" and b[end:top] == b"\xff" * (top - end):
            b[at:top] = b"\xff" * (top - at)
    if to:
        hook = bytes(b[0x224040:0x22444F])          # AsmHook2: the tests open it from APPS, so it stays,
        b[0x0C * 0x10000:to] = b"\xff" * (to - 0x0C * 0x10000)
        b[to - len(hook):to] = installed(hook, to - len(hook))      # relocated, lowest app
    open(out, "wb").write(b)
    return out


ROM = roomy(STOCK)

# Commands are lowercase tokens, so the home screen shows factor( where the tests were
# written for FACTOR(. A range named "screen" (the textShadow) comes back with command
# names in capitals, so the expectations stay; "screen_raw" is the same range untouched.
_c = open(os.path.join(HERE, "..", "..", "symce", "src", "engine.c")).read().split("static const char cmds[] =")[1].split(";")[0]
CMDNAMES = sorted(set(re.findall(r"[A-Z]+", _c.replace("\\0", " "))), key=len, reverse=True)


def upper_cmds(b):
    for n in CMDNAMES:
        b = b.replace(n.lower().encode() + b"(", n.encode() + b"(")
    return b

TEXTSHADOW = 0xD006C0
COLS, ROWS = 26, 10


HOLD_MS = int(os.environ.get("SYMCE_HOLD_MS", 200))      # how long every plain key is held


def read(ranges, keys="", files=("arTIfiCE.8xp",), launch="A", asm=False,
         boot=1500, settle=1500, per_key=200, lead=True, step=0, crcs=(), rom=ROM):
    """Read [(name, addr, length), ...] after launching `launch` and pressing `keys`.

    Returns {name: bytes}. Every 4-byte window goes in one sequence, so the
    whole read costs a single emulator run. `crcs` are more (name, addr,
    length) ranges hashed whole: {name: CRC} or None, for comparing a region
    too big to read (VRAM) between two runs. `rom` is the flash image to boot.
    """
    dstep = 1 if crcs else step
    seq = ["delay|%d" % boot]
    if launch and lead:
        seq += ["action|launch", "delay|4000"]
    for k in keys.split():
        if k == ".":
            seq += ["delay|2000"]
        elif k[0] == "w" and k[1:].isdigit():
            seq += ["delay|" + k[1:]]            # w150: wait 150 ms
        elif "|" in k:
            seq += [k, "delay|%d" % per_key]         # hold|right, release|right: a raw step
        elif k == "dp":
            seq += ["key|.", "delay|%d" % per_key]   # the decimal point
        elif k == "L":
            seq += ["action|launch", "delay|4000"]
        elif k == "C":
            # The CE home screen only renders through textShadow in CLASSIC
            # mode; MathPrint draws glyphs straight to VRAM, so a MathPrint
            # answer reads back as a blank buffer.
            seq += ["action|useClassic", "delay|1500"]
        else:
            # key|k is down for delay_after_step ms; at 1 (VRAM reads, so every vram.grab / mathprint run) that is a 1 ms
            # tap, and the OS needs 5 keypad polls (one per halt wake-up, ~10 ms apart) to accept a key. MathPrint's rainbow
            # paren pass (fhook.c .Lcpass, ~5 ms at each cursor blink) can swallow a wake-up, the tap then misses its 5th
            # poll and the OS never sees the key: desolve.py lost the ( of Y'(0) (hook A=1 called 40 times for 41 keys),
            # DESOLVE( got ERR:SYNTAX, errNo 0xAB. So hold it 120 ms, as a finger does (a 40 ms hold still lost one).
            # Every key is held, not only at delay_after_step 1: at 0 a key|k tap is the same dropped-poll lottery, and its
            # phase moves with the image size (e2e MathPrint nested lost the `2nd` of `2nd math 1 0` at a 800 KB pad: `math 1`
            # typed >Frac, token 03, for =, ERR:SYNTAX 0xAB; it passed at 700 KB and with per_key 310).
            seq += ["hold|" + k, "delay|%d" % HOLD_MS, "release|" + k, "delay|%d" % per_key]
    seq += ["delay|%d" % settle]

    hashes, plan, n = {}, [], 0
    # Whole-range hashes first, the moment the keys settle. A VRAM-sized hash
    # trips CEmu's sched_active assertion at delay_after_step 0, so they run at 1.
    for name, addr, length in crcs:
        seq.append("hash|" + name)
        hashes[name] = {"description": name, "start": hex(addr), "size": str(length),
                        "expected_CRCs": ["00000000"]}
    for name, addr, length in ranges:
        for off in range(0, length, 4):
            chunk = min(4, length - off)
            n += 1
            key = str(n)
            seq.append("hash|" + key)
            hashes[key] = {"description": key, "start": hex(addr + off),
                           "size": str(chunk), "expected_CRCs": ["00000000"]}
            plan.append((key, name, chunk))

    # delay_after_step 0: the hash phase must be a SNAPSHOT. At the default 80ms
    # per step a 68-window read runs the calculator for ~5s while it is being
    # read, which both smears the result and gives CEmu time to hit its
    # scheduler assertion. VRAM reads pass step=1: at 0, CEmu trips that
    # assertion after ~230 windows of 0xD40000.
    cfg = {"transfer_files": list(files),
           "target": {"name": launch or "A", "isASM": asm},
           "delay_after_step": dstep, "sequence": seq, "hashes": hashes}
    path = os.path.join(HERE, "_screen.%d.json" % os.getpid())     # per process: two tests at once must not share a config
    json.dump(cfg, open(path, "w"))
    try:
        r = subprocess.run([TESTER, path], capture_output=True, text=True,
                           env=dict(os.environ, AUTOTESTER_ROM=rom), cwd=HERE)
    finally:
        os.remove(path)
    # An unknown key name is SKIPPED, not rejected -- the run continues and
    # returns a plausible CRC for a sequence that never happened.
    if "unknown key" in r.stderr:
        sys.exit("BAD KEY:\n" + r.stderr)

    got = dict(re.findall(r'Hash #\S+ \("([^"]*)"\).*?got ([0-9A-F]{1,8})\)', r.stdout))
    # CEmu itself can abort mid-sequence (it has an internal scheduler assertion
    # that arTIfiCE's exit trips). Report how far it got and keep the bytes it
    # did produce, rather than throwing away a run that was mostly good.
    if len(got) < len(plan):
        sys.stderr.write("WARNING: emulator stopped after %d/%d windows%s\n" % (
            len(got), len(plan),
            "; " + r.stderr.strip().splitlines()[-1] if r.stderr.strip() else ""))
    out = {name: bytearray() for name, _, _ in ranges}
    for key, name, chunk in plan:
        out[name] += (crcread.invert(int(got[key], 16), chunk)
                      if key in got else b"\xff" * chunk)
    out = {k: bytes(v) for k, v in out.items()}
    if "screen" in out: out["screen"] = upper_cmds(out["screen"])
    out.update((name, int(got[name], 16) if name in got else None) for name, _, _ in crcs)
    return out


# textShadow holds DISPLAY GLYPH indices, not the tokens the hook wrote: the
# hook emits OS_TOK_SQUARE 0x0D / OS_TOK_CUBE 0x0F and these come back. Both
# measured on the real screen -- the cube is not the byte after the square.
GLYPHS = {0x12: "²", 0xD5: "³"}


def render(buf):
    """TI text buffer -> printable lines. Unknown non-ASCII shows as '.'."""
    lines = []
    for row in range(ROWS):
        chars = buf[row * COLS:(row + 1) * COLS]
        lines.append("".join(chr(c) if 32 <= c < 127 else GLYPHS.get(c, ".")
                             for c in chars))
    return lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("keys", nargs="?", default="")
    ap.add_argument("--files", default="arTIfiCE.8xp")
    ap.add_argument("--launch", default="A")
    ap.add_argument("--no-launch", action="store_true",
                    help="boot to the home screen without running anything")
    ap.add_argument("--asm", action="store_true")
    a = ap.parse_args()

    ranges = [("screen", TEXTSHADOW, COLS * ROWS),
              ("hookptr", 0xD025E1, 3),
              ("hookflag", 0xD000B4, 1),
              ("cx", 0xD007E0, 1)]
    r = read(ranges, keys=a.keys, files=tuple(a.files.split(",")),
             launch="" if a.no_launch else a.launch, asm=a.asm)
    print("hookptr=%s flag=%s cx=%s" %
          (r["hookptr"][::-1].hex(), r["hookflag"].hex(), r["cx"].hex()))
    print("+" + "-" * COLS + "+")
    for line in render(r["screen"]):
        print("|" + line + "|")
    print("+" + "-" * COLS + "+")


if __name__ == "__main__":
    main()
