"""SymCE's own TABLE (2nd GRAPH, raw key hook) against TI's, on the real ROM.

    python3 table.py [cells] [frac] [fall] [leave] [time]   (no argument: all but time)

Both tables run on the same ROM and the same Y= entries; TI's is the one with the GRAPH key
setting at TI (APPS > SymCE > 4). PNGs go to $TABLE_OUT (default _png/).
"""
import os, sys, time
import graph, tigo, vram, screen
from graph import PROLOGUE, TI_OFF, SETTINGS, cpng, run_seq, keyseq, FILES, LAUNCH

screen.TESTER = os.environ.get('EMU_WRAP', screen.TESTER)   # a wrapper that takes the machine-wide emulator lock per run
OUT = os.environ.get("TABLE_OUT", os.path.join(screen.HERE, "_png"))
FUNCS = ["1 / xton", "2nd ^2 xton )", "xton ^ 3 * 1 0 0 0 / 7", "2nd log xton )", "xton", "xton ^2"]
RAM = [(0xD02267, 9), (0xD02270, 9), (0xD00093, 1)]
FAIL = []


def eqs(funcs):
    return "y= " + " down ".join(funcs) + " 2nd mode . "


def snaps_for(prefix, steps, ti):
    steps = [(st[0], st[1] if ti or len(st) < 3 else st[2]) for st in steps]
    """run `prefix` then each (name, keys) step, snapshotting LCD + TblStart/DeltaTbl/tblFlags after each."""
    names = [n for n, _ in steps]
    sn = [tigo.Snap(n, ram=RAM, rect=(0, 0, 320, 240)) for n in names]
    keys = PROLOGUE + (TI_OFF if ti else "") + prefix
    for n, k in steps:
        keys += k + " w400 @" + n + " "
    return tigo.go(keys, sn, files=FILES, launch=LAUNCH)


def compare(label, prefix, steps, ram=True, ymax=240):
    """TI's table vs SymCE's on the same keys: every step must match pixel for pixel (and TblStart)."""
    a, b = snaps_for(prefix, steps, True), snaps_for(prefix, steps, False)
    os.makedirs(OUT, exist_ok=True)
    for n in [st[0] for st in steps]:
        cpng(a[n]["px"], os.path.join(OUT, "%s_%s_ti.png" % (label, n)))
        cpng(b[n]["px"], os.path.join(OUT, "%s_%s_symce.png" % (label, n)))
        bad = sum(1 for p in a[n]["px"] if p[1] < ymax and a[n]["px"][p] != b[n]["px"].get(p))
        rb = ram and a[n]["ram"][0xD02267][1:7] != b[n]["ram"][0xD02267][1:7]   # TI also sets a flag in the first byte; the last digits differ: TI rounds its repeated adds differently
        print("%s %-6s %-5s %d px differ%s" % (label, n, "FAIL" if bad or rb else "ok", bad, ", TblStart %s vs %s" % (a[n]["ram"][0xD02267].hex(), b[n]["ram"][0xD02267].hex()) if rb else ""))
        if bad or rb:
            FAIL.append(label + ":" + n)
        sys.stdout.flush()


def cells():
    setup = eqs(FUNCS) + "2nd window (-) 3 down 1 down 2nd mode . "
    compare("cells", setup, [("o0", "2nd graph w700"), ("u1", "up"), ("u2", "up"), ("r1", "right"),
        ("up1", "up"), ("up2", "up"), ("h2", "right"), ("h3", "right"), ("h4", "right"), ("h5", "right"),
        ("h6", "right"), ("h7", "right"), ("l1", "left"), ("dn", "down"), ("en", "enter"), ("pg", "2nd down"),
        ("l5", "left left left left left left"), ("dl", "del"), ("cl", "clear")])
    # leave and come back: TI keeps TblStart and the selection
    compare("reopen", setup + "2nd graph w700 down down right right down 2nd mode . ",
            [("a", "2nd graph w700"), ("b", "down down down down down down down down down down down down")])


def frac():
    setup = eqs(FUNCS) + "2nd window 1 / 2 down 1 / 3 down 2nd mode . "
    compare("frac", setup, [("o0", "2nd graph w700"), ("d1", "down down down down down down"),
        ("r1", "right right"), ("d2", "down down down down down down down down down down down down down down")], ymax=204)   # the bottom line shows the last digits of a repeating X, where TI rounds its repeated adds differently


def full():
    """the bottom line shows the cell's whole value (14 digits, negatives, E notation)."""
    setup = eqs(["sin xton ) * 1 0 0 0", "xton ^ 9 / 7", "(-) 2nd ^2 xton ) / 3", "1 / xton"]) \
        + "2nd window (-) 2 down 1 / 4 down 2nd mode . "
    compare("full", setup, [("o", "2nd graph w700"), ("a", "right"), ("b", "right down down"), ("c", "right down"),
        ("d", "right"), ("e", "left down down down down down down down down down down down down down down"), ("f", "right")])


def fall():
    """whatever TI's table does, SymCE must show the same: Ask, Par mode, ENTER on the header, typing."""
    fs = eqs(["xton ^2", "xton + 1"])
    compare("ask", fs + "2nd window down down right enter 2nd mode . ", [("o", "2nd graph w700"), ("d", "down")])
    compare("par", "mode down down down right enter 2nd mode . " + "y= xton down xton 2nd mode . ",
            [("o", "2nd graph w700")])
    # the key that opens TI's table is not replayed, so ours needs `+` twice where TI needs it once
    compare("plus", fs, [("o", "2nd graph w700"), ("e", "+ w500", "+ w500 + w500")])
    compare("typ", fs, [("o", "2nd graph w700"), ("t", "5 w500")])


def leave():
    fs = eqs(["xton ^2", "xton + 1"])
    # 2nd QUIT lands on the home screen exactly as it was (TI's own home differs from ours by the setup keys only)
    a = snaps_for(fs, [("h", "w700")], False)["h"]["px"]
    b = snaps_for(fs, [("t", "2nd graph w700"), ("q", "2nd mode w700")], False)["q"]["px"]
    bad = sum(1 for p in a if a[p] != b.get(p))
    print("quit: home after the table equals home before it: %d px differ" % bad)
    if bad:
        FAIL.append("quit")
    compare("yeq", fs, [("o", "2nd graph w700"), ("y", "y= w700")])
    compare("win", fs, [("o", "2nd graph w700"), ("w", "window w700")])
    a = snaps_for(fs, [("g", "graph w1500")], False)["g"]["px"]
    b = snaps_for(fs, [("t", "2nd graph w700"), ("g", "graph w1500")], False)["g"]["px"]
    bad = sum(1 for p in a if a[p] != b.get(p))
    print("graph from the table equals plain GRAPH: %d px differ" % bad)
    if bad:
        FAIL.append("graph")


def timing():
    for ti in (True, False):
        pre = eqs(FUNCS)
        print("%s first draw: %d ms" % ("TI" if ti else "SymCE", ms_after(pre, "2nd graph", ti)))
        print("%s scroll 1 row at the bottom: %d ms" % ("TI" if ti else "SymCE",
              ms_after(pre + "2nd graph . " + "down " * 10, "down", ti)))
        print("%s right (columns): %d ms" % ("TI" if ti else "SymCE", ms_after(pre + "2nd graph . ", "right", ti)))


def ms_after(pre, trig, ti, y0=24, y1=240, period=20, n=50):
    """ms from the last key of `trig` (keys are 'key|x' steps) to the last change of rows y0..y1."""
    addr, size = vram.VRAM + y0 * 640, (y1 - y0) * 640
    while True:
        seq = ["delay|1500"] + keyseq(PROLOGUE + (TI_OFF if ti else "") + pre)
        for k in trig.split():
            seq += ["key|" + k, "delay|50"]
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
            period += 3
            continue
        crcs = [got["h%d" % i] for i in range(n)]
        last = max((i for i, c in enumerate(crcs) if c != got["fin"]), default=-1)
        if last < n - 1:
            return (last + 2) * period
        period *= 4



if __name__ == "__main__":
    want = (os.environ.get("TABLE_RUN") or "").split() or sys.argv[1:] or ["cells", "frac", "full", "fall", "leave"]
    for w in want:
        globals()[w]()
    print("TABLE FAIL: " + " ".join(FAIL) if FAIL else "TABLE OK")
    sys.exit(1 if FAIL else 0)
