#!/usr/bin/env python3
"""The installer with an app bigger than one TI variable, on the real ROM.

    python3 bigapp.py          (make emu builds _pad/, the app padded by make PAD)

The app rides in appvars SYMCE1.8xv .. that prgmSYMCE writes to flash piece
by piece. With the padded build (several appvars, over 128 KB):

  * fresh install: every byte in flash is the image, relocated for where it
    landed, and 2X+2X answers 4X
  * over an older, smaller SymCE: that one is deleted and this one written
  * a RAM clear, AsmHook2 reopened, the installer again: it still answers
  * room only once the archive is garbage collected: the installer collects
    it, which moves the appvars, and still writes the app whole
  * a missing appvar, or one from another build: named on screen, nothing
    written
  * no room even with the old SymCE deleted and the archive garbage
    collected: "No blank flash", and the old SymCE still installed

The ROM under test has its archive full of the ROM dumper's ROMData appvars,
so the flash states are made here, as ROM files: `_roomy.rom` is that ROM
with the ROMData sectors erased, as on a calculator with free archive; an
installed old SymCE is written into a copy the way the installer writes it.
"""
import os, re, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import screen, zpack
from crcread import crc32c
from e2e import armed_in_flash
from lifecycle import RAM_CLEAR, TYPE

HERE = screen.HERE
PAD = os.path.join(HERE, "_pad")         # the padded build
SHIPPED = os.environ.get("SYMCE_OLD", HERE)   # the "older" build (SYMCE_OLD=dir: a real previous release)
OLD = os.path.join(HERE, "old")          # an older SymCE small enough for full flash
LOWEST = 0x20EF61                        # CEaShell, the lowest app in this ROM
DELETED = 0x140001                       # ROMDataD, deleted: 64 KB a GC frees
# The test ROM's blank run (0x14..0x1F, ~13 sectors under the lowest app) holds the appvars OR the
# app, not both: a 9-part app (~520 KB) needs 9 sectors of appvars plus 8 for itself, as on any
# real calculator (~3 MB archive). The big fixtures blank the dumper's junk below it too.
SECTOR = 0x10000
ASMHOOK2 = (0x224040, 0x22444F)             # its bytes in the stock ROM, trailer included
HEAD = 74                                # a .8xv's header, up to its data
# Arm AsmHook2 and run the installer; a 128 KB write takes a few seconds.
INSTALL = "C . apps down enter . clear . L . . . . . . "
DONE = "clear . "


def app_boundaries():
    """Start addresses of the stock ROM's apps, lowest first (walk the trailers down from 0x3B0000)."""
    b, u, out = open(screen.STOCK, "rb").read(), 0x3B0000, []
    while True:
        n = int.from_bytes(b[u - 3:u], "little")
        if n == 0xFFFFFF:
            return out[::-1]
        u -= 3 + n
        out.append(u)


def parts(where):
    """The build's files, installer first."""
    return [os.path.join(where, f) for f in
            ["SYMCE.8xp"] + sorted(f for f in os.listdir(where) if re.fullmatch(r"SYMCE[0-9A-Z]\.8xv", f))]


def sectors(files):
    """Archive sectors the files take once sent: each entry (header ~18 B + data) next to the last one
    if it fits in the sector, else the next (measured: 3 parts of <= 21.7 KB per sector)."""
    n, used = 0, SECTOR
    for f in files:
        e = os.path.getsize(f) - HEAD - 2 + 18
        if used + e > SECTOR - 1:
            n, used = n + 1, 1
        used += e
    return n


def image(where):
    """(image, build id) out of a build's appvars: unpacked and checked as the installer does.
    old/ and SYMCE_OLD=dir may hold a release from before packing: build id, then the raw slice."""
    ps = parts(where)[1:]
    if open(ps[0], "rb").read()[HEAD + 4:HEAD + 6] == b"ZX":
        return zpack.unpack(ps)
    datas = [open(p, "rb").read()[HEAD:-2] for p in ps]
    assert len({d[:4] for d in datas}) == 1
    return b"".join(d[4:] for d in datas), datas[0][:4]


installed = screen.installed


def tifile(path, out, flag=None, id=None):
    """A copy of a TI file with its archived flag or appvar build id changed."""
    b = bytearray(open(path, "rb").read())
    if flag is not None:
        b[69] = flag
    if id is not None:
        b[HEAD:HEAD + 4] = id
    b[-2:] = (sum(b[55:-2]) & 0xFFFF).to_bytes(2, "little")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    open(out, "wb").write(b)
    return out


def rom(name, erase=(), old=None, undelete=(), delete=(), wipe=None, lowest=None):
    """A ROM file: sectors erased, archive entries undeleted, an old app
    installed right below the lowest one (`lowest`). wipe (lo, hi): every byte
    between is erased, TI's apps too (hi an app boundary)."""
    b = bytearray(open(screen.STOCK, "rb").read())
    lowest = lowest or LOWEST
    if wipe:
        hook = bytes(b[ASMHOOK2[0]:ASMHOOK2[1]])
        b[wipe[0]:wipe[1]] = b"\xff" * (wipe[1] - wipe[0])
        # the tests open AsmHook2 from APPS to run prgmSYMCE: put it back, lowest, relocated
        at = wipe[1] - len(hook)
        b[at:wipe[1]] = installed(hook, at)
    for s in erase:
        b[s * SECTOR:(s + 1) * SECTOR] = b"\xff" * SECTOR
    for entry in undelete:
        assert b[entry] == 0xF0
        b[entry] = 0xFC
    for sec in delete:                  # every entry of the sector marked deleted: flash only clears bits
        at, off = sec * SECTOR, sec * SECTOR + 1
        while off < at + SECTOR - 3 and b[off] != 0xFF:
            assert b[off] in (0xFC, 0xF0)
            b[off] = 0xF0
            off += 3 + int.from_bytes(b[off + 1:off + 3], "little")
    if old:
        dest = lowest - len(old)
        assert b[dest - 3:lowest] == b"\xff" * (len(old) + 3), "no blank flash for the old app"
        b[dest:lowest] = installed(old, dest)
    path = os.path.join(HERE, "_%s.rom" % name)
    open(path, "wb").write(b)
    return path


def state(files, keys, flash, app, deleted=DELETED, lowest=None):
    """Screen, hook, and the flash where an app of `app` bytes would go."""
    lowest = lowest or LOWEST
    dest = lowest - app
    r = screen.read([("screen", screen.TEXTSHADOW, screen.COLS * screen.ROWS),
                     ("hookptr", 0xD025E1, 3), ("flag", 0xD000B4, 1),
                     ("slot", dest - 3, 3), ("trailer", lowest - 3, 3),
                     ("romdatad", deleted, 1)],
                    keys=keys, files=files, launch="SYMCE", lead=False,
                    crcs=[("app", dest, app)], rom=flash)
    rows = [l.strip() for l in screen.render(r["screen"]) if l.strip()]
    return rows, int.from_bytes(r["hookptr"], "little"), r["flag"][0], r, dest


def junk(name, size=60000):
    """An unarchived (RAM) appvar of `size` bytes of data: free RAM it takes the installer cannot have."""
    path = os.path.join(HERE, "_junk", name + "_a.8xv")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "wb").write(zpack.tifile(name, bytes(size)))
    return tifile(path, os.path.join(HERE, "_junk", name + ".8xv"), flag=0)


def main():
    new, new_id = image(PAD)
    old, _ = image(SHIPPED)
    assert len(new) > 2 * 65000, "make PAD: the padded app should take several appvars"
    nparts = len(parts(PAD)) - 1
    # Parts (the archive sectors they fill) and the app (the slot must lie above the archive's last
    # sector in use + 64 KB) both live in the blank run: when it is longer than CEaShell's start the
    # fixture erases TI's apps below the first app boundary that makes room (a stock CE needs
    # them deleted too).
    nsec = sectors(parts(PAD))
    need = 0x0C * SECTOR + (nsec + -(-len(new) // SECTOR) + 2) * SECTOR
    L = max([LOWEST] + [a for a in app_boundaries() if a >= need][:1])
    big = L != LOWEST
    if big:
        B, L = L, L - (ASMHOOK2[1] - ASMHOOK2[0])      # AsmHook2 sits below the boundary
    if big:
        print("lowest app moved to %06X (%d parts, %d bytes)" % (L, nparts, len(new)))
    # fixture ROM: the blank run from sector `lo` up to the lowest app
    run = lambda name, lo, **kw: rom(name, wipe=(lo * SECTOR, B), lowest=L, **kw) if big else \
        rom(name, erase=range(lo, 0x20), **kw)
    roomy = run("roomy", 0x0C)
    ok = True

    def check(label, good, detail):
        nonlocal ok
        ok &= bool(good)
        print("%-26s %s  %s" % (label, "ok  " if good else "FAIL", detail))

    def answers(label, files, keys, flash, collected=False, deleted=DELETED):
        rows, ptr, flag, r, dest = state(files, keys, flash, len(new), deleted, L)
        good_bytes = r["app"] == crc32c(installed(new, dest))
        check(label, "4X" in rows and armed_in_flash(ptr, flag) and dest <= ptr < L
              and good_bytes and r["slot"] == b"\xff" * 3
              and int.from_bytes(r["trailer"], "little") == len(new) - 3
              and (not collected or r["romdatad"] != b"\xf0"),
              "%d bytes at %06X, image %s, hook %06X, rows %s%s"
              % (len(new), dest, "matches" if good_bytes else "DIFFERS", ptr, rows[-2:],
                 ", ROMDataD collected" if collected and r["romdatad"] != b"\xf0" else ""))

    def refused(label, files, flash, text, app, want):
        rows, ptr, flag, r, dest = state(files, INSTALL, flash, app, lowest=L)
        check(label, any(text in l for l in rows) and not ptr
              and r["app"] == crc32c(want),
              "rows %s, flash %s" % (rows[:2], "as it was" if r["app"] == crc32c(want) else "CHANGED"))

    answers("fresh install", parts(PAD), INSTALL + DONE + TYPE, roomy)
    with_old = run("roomy_old", 0x0C, old=old)
    answers("replaces older SymCE", parts(PAD), INSTALL + DONE + TYPE, with_old)
    answers("RAM clear, reinstall", parts(PAD),
            INSTALL + DONE + RAM_CLEAR + INSTALL[2:] + DONE + TYPE, with_old)
    # The sizes below come from the build, so the fixture keeps working as the app grows. The app slot
    # (just under the lowest app) must lie above the archive's last sector in use + 64 KB (installer
    # `room`); nsec sectors hold the files.
    slot = L - (len(new) + 3)
    # GC room: a fully deleted sector `gone` just below the erased run and the appvars sent above it,
    # the last of them in the slot's own sector (no room as is); the GC packs the appvars down into
    # `gone` (measured: bigapp "GC makes room"), and then they end below the slot. Packed parts are
    # small enough (~21 KB) for the OS to tuck some into the slack of the junk sectors below `gone`,
    # so where the last one lands is measured: move `gone` until it is the slot's sector.
    # The junk the stock ROM keeps in 0x0C..gone-1 stays.
    sslot = slot // SECTOR
    gone = sslot - nsec
    for _ in range(6):
        assert 0x0C <= gone < sslot, "no GC fixture for an app this size"
        gc = run("gc", gone + 1, delete=[gone])
        r = screen.read([("s%02X" % x, x * SECTOR, 1) for x in range(gone + 1, L // SECTOR)], keys="",
                        files=tuple(parts(PAD)), launch="SYMCE", lead=False, rom=gc)
        top = max([int(k[1:], 16) for k, v in r.items() if v != b"\xff"] or [gone])
        print("GC fixture: sector %02X deleted, appvars end in %02X, slot in %02X" % (gone, top, sslot))
        if top == sslot:
            break
        gone += sslot - top
    else:
        sys.exit("GC fixture: no sector layout found")
    answers("GC makes room", parts(PAD), INSTALL + DONE + TYPE,
            gc, collected=True,
            deleted=gone * SECTOR + 1)

    blank = b"\xff" * len(new)
    refused("missing appvar refused", [p for p in parts(PAD) if not p.endswith("2.8xv")],
            roomy, "SYMCE2 is missing", len(new), blank)
    last = os.path.basename(parts(PAD)[-1])             # SYMCE9, or SYMCEG with 16 parts
    refused("last appvar refused", parts(PAD)[:-1], roomy, last[:-4] + " is missing", len(new), blank)
    other = tifile(parts(PAD)[2], os.path.join(HERE, "_other", "SYMCE2.8xv"),
                   id=bytes(x ^ 0xFF for x in new_id))
    refused("other build refused", parts(PAD)[:2] + [other] + parts(PAD)[3:],
            roomy, "SYMCE2 is from", len(new), blank)

    # Wrong or mixed sets: an old uncompressed SYMCE1 beside new parts, a part whose sums do not
    # match its slice, and too little free RAM to unpack in (a big unarchived appvar eats it).
    refused("old-format part refused",
            [parts(PAD)[0], os.path.join(OLD, "SYMCE1.8xv")] + parts(PAD)[2:],
            roomy, "SYMCE1 is not a", len(new), blank)
    bad = bytearray(open(parts(PAD)[3], "rb").read())
    bad[HEAD + 12] ^= 0x55                                # s2 of SYMCE3's header
    bad[-2:] = (sum(bad[55:-2]) & 0xFFFF).to_bytes(2, "little")
    badp = os.path.join(HERE, "_bad", "SYMCE3.8xv")
    os.makedirs(os.path.dirname(badp), exist_ok=True)
    open(badp, "wb").write(bad)
    refused("damaged part refused", parts(PAD)[:3] + [badp] + parts(PAD)[4:],
            roomy, "SYMCE appvar is damaged", len(new), blank)
    refused("low RAM refused", [junk("JUNKA"), junk("JUNKB")] + parts(PAD), roomy, "Not enough RAM",
            len(new), blank)

    # The archive as full as this ROM's, nothing in it deleted, the old SymCE
    # right below the lowest app: the new one fits neither beside it nor in
    # its place, and garbage collecting frees no sector. The appvars (too much
    # for free RAM together) are sent archived, so the archive has nsec sectors
    # (and one spare) erased for them and is full again after.
    # The old SymCE is a pinned build (old/, a fixture) under the 61 KB free
    # here: the shipped one is past it, and erasing a sector for it lets the GC
    # pack the archive into one sector fewer, room enough for the new app.
    old, _ = image(OLD)
    # (past 12 parts the erased run reaches ROMDataD's sector 0x14 itself: nothing to undelete there)
    lo = 0x20 - nsec - 1
    tight = rom("tight", old=old, undelete=[0x140001] if lo > 0x14 else [], erase=range(lo, 0x20))
    rows, ptr, flag, r, dest = state(parts(PAD), INSTALL, tight, len(old))
    kept = r["app"] == crc32c(installed(old, dest))
    check("no room keeps old SymCE", any("No blank flash" in l for l in rows) and kept
          and int.from_bytes(r["trailer"], "little") == len(old) - 3,
          "rows %s, old app %s" % (rows[:2], "kept" if kept else "CHANGED"))
    # Sent archived into that archive, the shipped build's SYMCE1 fits and
    # SYMCE2 does not: the OS leaves SYMCE2 an empty appvar in RAM (seen on a
    # real calculator too), which the installer must not call another build.
    full = rom("full", old=old, undelete=[0x140001])
    rows, ptr, flag, r, dest = state(parts(HERE), INSTALL, full, len(old))
    kept = r["app"] == crc32c(installed(old, dest))
    check("empty appvar refused", any("is empty" in l for l in rows) and kept,
          "rows %s, old app %s" % (rows[:2], "kept" if kept else "CHANGED"))

    print("\nALL PASS" if ok else "\nFAILURES")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
