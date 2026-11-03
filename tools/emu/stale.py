"""A stale Text( marker (GWX, GWY) in the engine's work pool must not do anything on the home screen.
An asm program (run from AA with Asm( ) writes 0A5h 5Ah, or the old single 0A5h, to both markers; then CLEAR,
ENTER and a MATH pick must leave the same home screen as a no-op program does. python3 stale.py"""
import os, sys
import e2e, screen

def wrapped_installer(poke_code):
    """bin/SYMCE.8xp with its first instruction (call 020848h) replaced by a jump to a stub at its end: when SymCE
    is armed (the raw key hook pointer is set) the stub runs poke_code and returns (APPS > SymCE > 5 runs prgmSYMCE),
    else it does what the first instruction did and the installer goes on."""
    f = open(os.path.join(screen.HERE, "SYMCE.8xp"), "rb").read()
    elen = int.from_bytes(f[53:55], "little")
    ent = bytearray(f[55:55 + elen])
    hdr = 2 + 2 + 1 + 8 + 2                                      # 0d00, vlen, type, name, version + flags
    body = ent[hdr + 2:]
    assert body[2:4] == b"\xef\x7b" and body[4:8] == bytes.fromhex("cd480802")
    code = bytearray(body[4:])
    at = 0xD1A881 + len(code)                                    # userMem: where the stub lands
    code[0:4] = b"\xc3" + at.to_bytes(3, "little")
    stub = (bytes.fromhex("2ae125d0" "11000000" "b7ed52") + bytes([0x28, len(poke_code) + 1]) + poke_code + b"\xc9"
            + bytes.fromhex("cd480802") + b"\xc3" + (0xD1A885).to_bytes(3, "little"))
    code += stub
    nb = len(code) + 2
    body = nb.to_bytes(2, "little") + b"\xef\x7b" + code
    ent[hdr + 2:] = body
    ent[2:4] = ent[hdr:hdr + 2] = len(body).to_bytes(2, "little")
    path = "SYMCE.8xp"
    entry = bytes(ent)
    out = f[:53] + len(entry).to_bytes(2, "little") + entry + (sum(entry) & 0xFFFF).to_bytes(2, "little")
    open(os.path.join(screen.HERE, "_stale_SYMCE.8xp"), "wb").write(out)
    return "_stale_SYMCE.8xp"

def poke(a, b):    # GWX +0/+1 (0xD0FB12), GWP (0xD0FB14), GWY +0/+1 (0xD0FB16)
    ld = lambda v, ad: bytes([0x3E, v, 0x32]) + ad.to_bytes(3, "little")
    return (ld(a, 0xD0FB12) + ld(b, 0xD0FB13) + ld(0x34, 0xD0FB14) + ld(a, 0xD0FB16) + ld(b, 0xD0FB17))

KEYS = "alpha down 2 xton ^ 2 - 1 ) enter . . apps . up w300 up w300 enter w1500 5 w3000 clear . enter . . math 1 enter . . "

def rows(code):
    files = (wrapped_installer(code),) + e2e.FILES[1:]
    for settle in (1500, 1700, 1900, 2100, 2300):
        r = screen.read([("errNo", 0xD008DF, 1), ("screen", screen.TEXTSHADOW, screen.COLS * screen.ROWS)],
                        keys=e2e.PROLOGUE + KEYS, files=files, launch=e2e.LAUNCH, lead=False, settle=settle, per_key=300)
        if b"\xff" * 4 not in r["screen"]:
            break
    b = r["screen"]
    return r["errNo"], [b[i * screen.COLS:(i + 1) * screen.COLS].strip(b" ") for i in range(screen.ROWS) if b[i * screen.COLS:(i + 1) * screen.COLS].strip(b" ")]

def main():
    want = rows(b"\xc9")
    print("baseline", want)
    ok = any(b"X-1" in x for x in want[1])
    for name, code in (("A5 5A", poke(0xA5, 0x5A)), ("A5 only", poke(0xA5, 0))):
        got = rows(code)
        good = got == want
        ok &= good
        print("%-4s stale %s" % ("ok" if good else "FAIL", name), "" if good else got)
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
