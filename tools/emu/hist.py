#!/usr/bin/env python3
"""A failed SymCE entry stays in history, like TI's own errors (1/0).

    python3 hist.py

The error is thrown at hook event 2, before the OS files the entry; .Lerr (hook.c) files
"Error" itself through 0x91FC2 type 0x0E. Per mode, after SOLVE with no real solution and 1:Quit:
  * the history record (0xD008EE) is type 0x15, length 5, "Error", what 1/0 leaves
  * 2nd ENTRY brings the entry back; in MathPrint so does UP, UP (the first UP is the Error line)
The entry's tokens are read from the edit buffer and compared with the same entry just typed.
"""
import re, sys
import screen
import e2e

ENTRY = "alpha down 1 xton * xton + 1 2nd math 1 0 , xton )"       # SOLVE(X*X+1=0,X): error 0x2B
REC = b"\x15\x05\x00Error"

def read(pro, keys):
    for settle in (1500, 1900, 2300):
        r = screen.read([("rec", 0xD008EE, 12), ("edit", 0xD02437, 12), ("lo", 0xD1A800, 768), ("hi", 0xD2A800, 256)],
                        keys=pro + keys, files=e2e.FILES, launch=e2e.LAUNCH, lead=False, settle=settle)
        e = [int.from_bytes(r["edit"][i:i+3], "little") for i in (0, 3, 6, 9)]
        if 0xD1A800 <= e[0] <= e[1] < 0xD1AB00 and 0xD2A800 <= e[2] <= e[3] < 0xD2A900:
            buf = r["lo"][e[0]-0xD1A800:e[1]-0xD1A800] + r["hi"][e[2]-0xD2A800:e[3]-0xD2A800]
            return r["rec"], re.sub(rb"\xef\x2a..", b"\xef\x2a", buf)      # a box's length moves with the baseline
    return r["rec"], None

CASES = (     # name, entry keys, keys that leave the error screen, modes
    ("NO SOLUTION", ENTRY, "1", ("C", "M")),
    ("steps( no roots", e2e.spell("STEPS") + " " + ENTRY + " )", "clear . 1", ("C", "M")),
    ("1/0 control", "1 / 0", "1", ("C", "M")),
)
bad = 0
for name, entry, leave, modes in CASES:
    for mode, pro in (("Classic", e2e.PROLOGUE), ("MathPrint", e2e.PROLOGUE.replace("C ", "", 1))):
        if mode[0] not in modes:
            continue
        recalls = ("2nd enter",) if mode == "Classic" else ("2nd enter", "up up enter")
        if mode == "MathPrint" and "^" in entry:
            recalls = ("up up enter",)          # 2nd ENTRY puts a ^ box where the read below cannot see it (the entry is there)
        _, typed = read(pro, entry)
        rec, _ = read(pro, entry + " w1000 enter . . " + leave + " . .")
        ok = rec[:1] + rec[2:9] == REC
        print(name, mode, "record", rec.hex(), "OK" if ok else "FAIL"); bad += not ok
        for rc in recalls:
            _, got = read(pro, entry + " w1000 enter . . " + leave + " . . " + rc + " . .")
            ok = got is not None and got == typed
            print(name, mode, "recall", rc, "OK" if ok else "FAIL " + str(got and got.hex())); bad += not ok
sys.exit(bad)
