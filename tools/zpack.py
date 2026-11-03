#!/usr/bin/env python3
"""Pack the SymCE app image into the compressed install appvars SYMCE1.8xv .. (SYMCEA .. SYMCEZ).

    zpack.py obj/SYMCE.img bin/SYMCE.8xp OUTDIR [--salvador PATH]
    zpack.py --unpack DIR            # print the image a set of parts holds (tests use unpack())

Each part is one archived appvar:

    dd  build id            FNV-1a of the whole image (the installer carries the same)
    db  'Z','X'             format marker: a part without it is an old uncompressed one
    dw  ulen                bytes it unpacks to (1..65000), the installer's RAM buffer
    db  last                1 on the final part
    dl  s1; dl s2           Fletcher-style sums of the unpacked slice: s1 += byte (16 bit),
                            s2 += s1 (24 bit); the installer checks both before writing anything
    ...                     the slice as a ZX0 (v2) stream

ZX0 is Einar Saukas' format; the stream comes from salvador (Emmanuel Marty, tools/salvador, zlib
licence, output "within 0.02% of the zx0 packer"), the installer's decoder is the toolchain's
(CEdev libce zx0.o, "turbo", Einar Saukas and introspec).  A TI archive sector (64 KB) holds three
parts of at most CAP bytes, so slices are cut to the longest that packs under CAP: the cost of an
archived appvar is whole sectors, and one part per sector would waste the compression.
"""
import os, subprocess, sys, tempfile

MAX_SLICE = 65000       # the installer's RAM buffer (installer.asm MAX_SLICE)
HEADER = 15
CAP = 21700             # bytes per part, header included: three per 64 KB sector (docs section 8)
MAX_PARTS = 35          # SYMCE1..9, SYMCEA..Z
APPVAR = 0x15
HEAD = 74               # a .8xv's header, up to its data


def fnv(img):
    h = 2166136261
    for b in img:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def sums(data):
    s1 = s2 = 0
    for b in data:
        s1 = (s1 + b) & 0xFFFF
        s2 = (s2 + s1) & 0xFFFFFF
    return s1, s2


def tifile(name, data):
    """An archived appvar file, as TI Connect CE sends it (makefile's `tifile` macro, main.asm)."""
    n = len(data)
    entry = (13).to_bytes(2, "little") + (n + 2).to_bytes(2, "little") + bytes([APPVAR]) + \
        name.encode().ljust(8, b"\0") + bytes([0, 0x80]) + (n + 2).to_bytes(2, "little") + \
        n.to_bytes(2, "little") + data
    return b"**TI83F*" + bytes([26, 10, 0]) + bytes(42) + (n + 19).to_bytes(2, "little") + entry + \
        (sum(entry) & 0xFFFF).to_bytes(2, "little")


def part_name(i):
    assert 1 <= i <= MAX_PARTS
    return "SYMCE" + (str(i) if i < 10 else chr(ord("A") + i - 10))


def zx0_decode(src, limit=1 << 20):
    """ZX0 v2 decoder, a port of Einar Saukas' dzx0.c. Raises ValueError on a bad stream.
    Returns (data, bytes of src used)."""
    out, st = bytearray(), {"pos": 0, "mask": 0, "val": 0, "last": 0, "back": False}

    def byte():
        if st["pos"] >= len(src):
            raise ValueError("truncated")
        st["pos"] += 1
        st["last"] = src[st["pos"] - 1]
        return st["last"]

    def bit():
        if st["back"]:
            st["back"] = False
            return st["last"] & 1
        st["mask"] >>= 1
        if st["mask"] == 0:
            st["mask"] = 128
            st["val"] = byte()
        return 1 if st["val"] & st["mask"] else 0

    def gamma(inverted):
        v = 1
        while not bit():
            v = v << 1 | (bit() ^ inverted)
        return v

    def match(off, n):
        if off > len(out) or off < 1 or len(out) + n > limit:
            raise ValueError("bad offset")
        for _ in range(n):
            out.append(out[-off])

    off, state = 1, "lit"
    while True:
        if state == "lit":
            out += bytes(byte() for _ in range(gamma(0)))
            state = "new" if bit() else "last"
        elif state == "last":
            match(off, gamma(0))
            state = "lit" if not bit() else "new"
        else:
            off = gamma(1)
            if off == 256:
                return bytes(out), st["pos"]
            off = off * 128 - (byte() >> 1)
            st["back"] = True
            match(off, gamma(0) + 1)
            state = "new" if bit() else "lit"


def compress(data, salvador):
    with tempfile.TemporaryDirectory() as d:
        i, o = os.path.join(d, "i"), os.path.join(d, "o")
        open(i, "wb").write(data)
        subprocess.run([salvador, i, o], check=True, stdout=subprocess.DEVNULL)
        c = open(o, "rb").read()
    assert zx0_decode(c) == (data, len(c)), "salvador output does not round-trip"
    return c


def header(bid, ulen, last, data):
    s1, s2 = sums(data)
    return bid.to_bytes(4, "little") + b"ZX" + ulen.to_bytes(2, "little") + bytes([last]) + \
        s1.to_bytes(3, "little") + s2.to_bytes(3, "little")


def pack(img, salvador, cap=CAP):
    """[(name, appvar data)] for the image: slices as long as pack under `cap` bytes."""
    bid, parts, pos = fnv(img), [], 0
    while pos < len(img):
        rest = img[pos:]
        n = min(MAX_SLICE, len(rest))
        lo, hi, c = 0, None, compress(rest[:n], salvador)       # lo fits, hi does not
        while True:
            if HEADER + len(c) <= cap:
                lo, best = n, c
                if hi is None or hi - lo <= 128:
                    break
                n = (lo + hi) // 2
            else:
                hi = n
                n = (lo + n) // 2 if lo else int(n * (cap - HEADER) / len(c) * 0.99)
            c = compress(rest[:n], salvador)
        last = int(pos + lo == len(img))
        parts.append((part_name(len(parts) + 1), header(bid, lo, last, rest[:lo]) + best))
        pos += lo
        assert len(parts) <= MAX_PARTS, "image too big for SYMCE1..Z"
    return parts


def unpack(files):
    """Image bytes from .8xv file paths (in part order), checking what the installer checks."""
    img, ids = b"", set()
    for f in files:
        d = open(f, "rb").read()[HEAD:-2]
        assert d[4:6] == b"ZX", "%s is not a packed part" % f
        ulen, last = int.from_bytes(d[6:8], "little"), d[8]
        data, used = zx0_decode(d[HEADER:], MAX_SLICE)
        assert len(data) == ulen and used == len(d) - HEADER
        assert sums(data) == (int.from_bytes(d[9:12], "little"), int.from_bytes(d[12:15], "little"))
        ids.add(d[:4])
        img += data
    assert len(ids) == 1
    return img, ids.pop()


def main(a):
    if a[0] == "--size":                    # image bytes the parts in DIR hold (makefile BIG)
        import glob
        fs = sorted(glob.glob(os.path.join(a[1], "SYMCE[1-9A-Z].8xv")), key=lambda f: (len(f), f))
        print(len(unpack(fs)[0]))
        return
    if a[0] == "--unpack":
        import glob
        fs = sorted(glob.glob(os.path.join(a[1], "SYMCE[1-9A-Z].8xv")), key=lambda f: (len(f), f))
        img, bid = unpack(fs)
        print(len(fs), "parts,", len(img), "bytes, id %08X" % int.from_bytes(bid, "little"))
        return
    img_path, inst, out = a[:3]
    salvador = a[a.index("--salvador") + 1] if "--salvador" in a else "salvador"
    img = open(img_path, "rb").read()
    # the installer carries the build id the parts must have
    assert b"SymCE\0\0\0" + fnv(img).to_bytes(4, "little") in open(inst, "rb").read(), \
        "installer %s was not built from this image" % inst
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        if f.startswith("SYMCE") and f.endswith(".8xv"):
            os.remove(os.path.join(out, f))
    total = n = 0
    for name, data in pack(img, salvador):
        open(os.path.join(out, name + ".8xv"), "wb").write(tifile(name, data))
        total += len(data) + HEAD + 2
        n += 1
    print("zpack: %d bytes -> %d parts, %d bytes of appvars (%.1f %%)" %
          (len(img), n, total, 100.0 * total / len(img)))


if __name__ == "__main__":
    main(sys.argv[1:])
