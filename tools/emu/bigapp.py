#!/usr/bin/env python3
"""The installer with an app bigger than one TI variable, on the real ROM.

    python3 bigapp.py          (make emu builds _pad/, the app padded by make PAD)

The app rides in appvars SYMCE1.8xv .. that prgmSYMCE writes to flash piece
by piece. With the padded build (three appvars, over 128 KB):

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
import screen
from crcread import crc32c
from e2e import armed_in_flash
from lifecycle import RAM_CLEAR, TYPE

HERE = screen.HERE
PAD = os.path.join(HERE, "_pad")         # the padded build
SHIPPED = HERE                           # the shipped build, the "older" one
OLD = os.path.join(HERE, "old")          # an older SymCE small enough for full flash
LOWEST = 0x20EF61                        # CEaShell, the lowest app in this ROM
DELETED = 0x140001                       # ROMDataD, deleted: 64 KB a GC frees
SECTOR = 0x10000
HEAD = 74                                # a .8xv's header, up to its data
# Arm AsmHook2 and run the installer; a 128 KB write takes a few seconds.
INSTALL = "C . apps down enter . clear . L . . . . . . "
DONE = "clear . "


def parts(where):
    """The build's files, installer first."""
    return [os.path.join(where, f) for f in
            ["SYMCE.8xp"] + sorted(f for f in os.listdir(where) if re.fullmatch(r"SYMCE\d\.8xv", f))]


def image(where):
    """(image, build id) out of a build's appvars."""
    img, ids = b"", set()
    for p in parts(where)[1:]:
        data = open(p, "rb").read()[HEAD:-2]
        ids.add(data[:4])
        img += data[4:]
    assert len(ids) == 1, ids
    return img, ids.pop()


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


def rom(name, erase=(), old=None, undelete=()):
    """A ROM file: sectors erased, archive entries undeleted, an old app
    installed right below the lowest one."""
    b = bytearray(open(screen.STOCK, "rb").read())
    for s in erase:
        b[s * SECTOR:(s + 1) * SECTOR] = b"\xff" * SECTOR
    for entry in undelete:
        assert b[entry] == 0xF0
        b[entry] = 0xFC
    if old:
        dest = LOWEST - len(old)
        assert b[dest - 3:LOWEST] == b"\xff" * (len(old) + 3), "no blank flash for the old app"
        b[dest:LOWEST] = installed(old, dest)
    path = os.path.join(HERE, "_%s.rom" % name)
    open(path, "wb").write(b)
    return path


def state(files, keys, flash, app):
    """Screen, hook, and the flash where an app of `app` bytes would go."""
    dest = LOWEST - app
    r = screen.read([("screen", screen.TEXTSHADOW, screen.COLS * screen.ROWS),
                     ("hookptr", 0xD025E1, 3), ("flag", 0xD000B4, 1),
                     ("slot", dest - 3, 3), ("trailer", LOWEST - 3, 3),
                     ("romdatad", DELETED, 1)],
                    keys=keys, files=files, launch="SYMCE", lead=False,
                    crcs=[("app", dest, app)], rom=flash)
    rows = [l.strip() for l in screen.render(r["screen"]) if l.strip()]
    return rows, int.from_bytes(r["hookptr"], "little"), r["flag"][0], r, dest


def main():
    new, new_id = image(PAD)
    old, _ = image(SHIPPED)
    assert len(new) > 2 * 65000, "make PAD: the padded app should take three appvars"
    roomy = rom("roomy", erase=range(0x14, 0x20))
    ok = True

    def check(label, good, detail):
        nonlocal ok
        ok &= bool(good)
        print("%-26s %s  %s" % (label, "ok  " if good else "FAIL", detail))

    def answers(label, files, keys, flash, collected=False):
        rows, ptr, flag, r, dest = state(files, keys, flash, len(new))
        good_bytes = r["app"] == crc32c(installed(new, dest))
        check(label, "4X" in rows and armed_in_flash(ptr, flag) and dest <= ptr < LOWEST
              and good_bytes and r["slot"] == b"\xff" * 3
              and int.from_bytes(r["trailer"], "little") == len(new) - 3
              and (not collected or r["romdatad"] != b"\xf0"),
              "%d bytes at %06X, image %s, hook %06X, rows %s%s"
              % (len(new), dest, "matches" if good_bytes else "DIFFERS", ptr, rows[-2:],
                 ", ROMDataD collected" if collected and r["romdatad"] != b"\xf0" else ""))

    def refused(label, files, flash, text, app, want):
        rows, ptr, flag, r, dest = state(files, INSTALL, flash, app)
        check(label, any(text in l for l in rows) and not ptr
              and r["app"] == crc32c(want),
              "rows %s, flash %s" % (rows[:2], "as it was" if r["app"] == crc32c(want) else "CHANGED"))

    answers("fresh install", parts(PAD), INSTALL + DONE + TYPE, roomy)
    with_old = rom("roomy_old", erase=range(0x14, 0x20), old=old)
    answers("replaces older SymCE", parts(PAD), INSTALL + DONE + TYPE, with_old)
    answers("RAM clear, reinstall", parts(PAD),
            INSTALL + DONE + RAM_CLEAR + INSTALL[2:] + DONE + TYPE, with_old)
    # Three sectors free: the appvars take two, and the app fits only once
    # the installer has garbage collected the archive, which moves them.
    answers("GC makes room", parts(PAD), INSTALL + DONE + TYPE,
            rom("gc", erase=range(0x1D, 0x20)), collected=True)

    blank = b"\xff" * len(new)
    refused("missing appvar refused", [p for p in parts(PAD) if not p.endswith("2.8xv")],
            roomy, "SYMCE2 is missing", len(new), blank)
    other = tifile(parts(PAD)[2], os.path.join(HERE, "_other", "SYMCE2.8xv"),
                   id=bytes(x ^ 0xFF for x in new_id))
    refused("other build refused", parts(PAD)[:2] + [other] + parts(PAD)[3:],
            roomy, "SYMCE2 is from", len(new), blank)

    # The archive as full as this ROM's, nothing in it deleted, the old SymCE
    # right below the lowest app: the new one fits neither beside it nor in
    # its place, and garbage collecting frees no sector. The three appvars
    # (160 KB, too much for free RAM together) are sent archived, so the
    # archive has three sectors erased for them and is full again after.
    # The old SymCE is a pinned build (old/, a fixture) under the 61 KB free
    # here: the shipped one is past it, and erasing a sector for it lets the GC
    # pack the archive into one sector fewer, room enough for the new app.
    old, _ = image(OLD)
    tight = rom("tight", old=old, undelete=[0x140001], erase=range(0x1D, 0x20))
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
