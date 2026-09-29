#!/usr/bin/env python3
"""Acceptance: type on the REAL home screen, assert what the OS displays.

engine_check.py and hook_sim_test.py run on the host with no ROM, so they prove
the arithmetic and the gates but cannot tell you whether the hook is ever
reached. That gap is why four releases shipped without anyone seeing the
calculator's screen. This closes it: real OS, real keypresses, textShadow read
back through screen.read.

    python3 e2e.py

Two things about the launch path are measured, not assumed:

  * NOT via arTIfiCE. Leaving it (mode) is a context switch, which clears or
    resets whatever hook it does not like before you reach the home screen.
  * Via the AsmHook2 app (`apps down enter`), which is what this ROM has in
    flash. It arms the parser hook that makes prgmSYMCE runnable. prgmSYMCE is
    the installer: it writes the SymCE flash app and arms the hook in it. The
    app does move when an app above it is deleted, and the OS moves the hook
    pointer with it (lifecycle.py).

CLASSIC is forced because the home screen only renders through textShadow in
classic; MathPrint draws glyphs straight to VRAM and reads back blank. The gate
is editTop-based and does not care which mode it is in.

Three things are asserted per case: the echoed entry is exactly what was typed,
the answer text, and that the answer ends in the last column -- a real result
is right-aligned, a string is not, and the string version of this shipped.
Then 2nd ENTRY is pressed and must recall the entry alone: the version that
rewrote the entry to 2X+2X:"4X" leaked the rewrite into the recall.
"""
import glob
import os
import sys
import screen

# Get to a usable home screen with SymCE installed and armed. The installer's
# "press any key" prompt eats the last `clear`; the extra `.` covers the flash
# write.
PROLOGUE = "C . apps down enter . clear . L . . clear . "
# prgmSYMCE, and the appvars carrying the app it installs (make emu copies both).
FILES = ("SYMCE.8xp",) + tuple(sorted(os.path.basename(f) for f in
                                      glob.glob(os.path.join(screen.HERE, "SYMCE[1-9].8xv"))))
LAUNCH = "SYMCE"


def armed_in_flash(ptr, flag):
    """The only legal place for an armed hook on OS 5.8.x is inside a flash app
    (ROM 0x8CAE6), so anything at or above 0x3B0000 -- RAM included -- is the
    reboot-on-Y= build."""
    return 0 < ptr < 0x3B0000 and flag & 0x10

# What the raised-power tokens look like once the OS has drawn them. These are
# glyph indices, not the tokens the hook wrote; both were read off the screen.
SQ, CB = b"\x12", b"\xd5"

# (label, keys, expected answer bytes, why it matters). The label is also the
# echo the OS must show, byte for byte.
NEG = b"\x1a"                               # the OS's negation glyph
I = b"\xd7"                                 # i; a list is { , } as 7B 2C 7D, √ is 10 28
# ALPHA then a key types a letter (all 26 measured on this ROM); spell() types
# a command's name and its '(', as the menus do.
ALPHA = dict(zip("ABCDEFGHIJKLMNOPQRSTUVWXYZ",
                 "math apps prgm -1 sin cos tan ^ ^2 , ( ) / log 7 8 9 * ln 4 5 6 - sto 1 2".split()))


def spell(name):
    return " ".join("alpha " + ALPHA[c] for c in name) + " ("


CASES = [
    ("2X+2X", "2 xton + 2 xton", b"4X",     "like terms combine"),
    ("X+X",   "xton + xton",     b"2X",     "implicit coefficient 1"),
    ("X-X",   "xton - xton",     b"0",      "cancels to zero"),
    ("X*X",   "xton * xton",     b"X" + SQ, "product becomes a power"),
    ("X^2",   "xton ^ 2",        b"X" + SQ, "exponent; used to crash"),
    ("X^3",   "xton ^ 3",        b"X" + CB, "the other raised glyph"),
    ("X^4",   "xton ^ 4",        b"X^4",    "past 3: falls back to a flat caret"),
    ("X^8+X^8", "xton ^ 8 + xton ^ 8", b"2X^8", "past 7: a power has 4 bits, up to 15"),
    ("X^12Y", "xton ^ 1 2 alpha 1", b"X^12Y", "a two-digit power"),
    ("X^X*X^X", "xton ^ xton * xton ^ xton", b"(X^X)" + SQ, "a variable exponent stays symbolic"),
    ("3X-X",  "3 xton - xton",   b"2X",     "coefficients subtract"),
    ("2X-5X", "2 xton - 5 xton", NEG + b"3X", "negative: the OS's own minus glyph"),
    ("(X+1)^2", "( xton + 1 ) ^ 2", b"X" + SQ + b"+2X+1", "expands"),
    ("2X+3Y+X", "2 xton + 3 alpha 1 + xton", b"3X+3Y", "two variables"),
    ("X/2",   "xton / 2",        b"X/2",    "a fraction stays exact"),
    ("6X/3",  "6 xton / 3",      b"2X",     "a fraction that divides out"),
    ("3X/2-X", "3 xton / 2 - xton", b"X/2", "fractions combine"),
    ("1-X",   "1 - xton",        b"1-X",    "a leading constant keeps its place"),
    ("(X^2-1)/(X-1)", "( xton ^ 2 - 1 ) / ( xton - 1 )", b"X+1", "exact division by a polynomial"),
    ("1/X+1/X", "1 / xton + 1 / xton", b"2/X", "fractions add, with X = 0"),
    ("X/(X+1)", "xton / ( xton + 1 )", b"X/(X+1)", "lowest terms already"),
    ("(X^2+5X+6)/(X^2-4)", "( xton ^ 2 + 5 xton + 6 ) / ( xton ^ 2 - 4 )", b"(X+3)/(X-2)",
     "a common factor cancels"),
    ("(X+Y)/(X-Y)", "( xton + alpha 1 ) / ( xton - alpha 1 )", b"(X+Y)/(X-Y)",
     "two variables, nothing cancels"),
    ("(X^2-Y^2)/(X+Y)", "( xton ^ 2 - alpha 1 ^ 2 ) / ( xton + alpha 1 )", b"X-Y",
     "a two-variable factor cancels"),
    ("(X^2-Y^2)/(X^2-2XY+Y^2)", "( xton ^ 2 - alpha 1 ^ 2 ) / ( xton ^ 2 - 2 xton alpha 1 + alpha 1 ^ 2 )",
     b"(X+Y)/(X-Y)", "every term one degree: homgcd"),
    ("(X^2-Y^2+X-Y)/(X^2-Y^2)", "( xton ^ 2 - alpha 1 ^ 2 + xton - alpha 1 ) / ( xton ^ 2 - alpha 1 ^ 2 )",
     b"(X+Y+1)/(X+Y)", "one side of one degree: homgcd sees past the other"),
    (".5X+.25X", "dp 5 xton + dp 2 5 xton", b"0.75X", "decimals stay decimal"),
    ("X/3+.5", "xton / 3 + dp 5", b"X/3+0.5", "fractions where a decimal would not end"),
    ("2+2",   "2 + 2",           b"4",      "numeric: must fall through to the OS"),
    # Commands, picked from the menu. textShadow holds sqrt( as 10 28, and
    # " or " as its four characters.
    ("FACTOR(X^2-4)", "alpha down 2 xton ^ 2 - 4 )", b"(X-2)(X+2)", "FACTOR from the menu"),
    # = is TEST 1 (2nd math 1)
    ("SOLVE(X^2=2,X)", "alpha down 1 xton ^ 2 2nd math 1 2 , xton )", b"X=" + NEG + b"\x10(2) or X=\x10(2)",
     "roots smallest first, right-aligned by columns"),
    ("SOLVE(X+2=4,X)", "alpha down 1 xton + 2 2nd math 1 4 , xton )", b"X=2", "SOLVE(A=B,V)"),
    ("SOLVE(Y+4=8,Y)", "alpha down 1 alpha 1 + 4 2nd math 1 8 , alpha 1 )", b"Y=4", "for the variable after the comma"),
    ("DERIV(X^3)", "alpha down 4 xton ^ 3 )", b"3X" + SQ, "d/dX"),
    ("EXPAND((X+1)^2)", "alpha down 3 ( xton + 1 ) ^ 2 )", b"X" + SQ + b"+2X+1", "EXPAND"),
    ("FACTOR(X^4-5X^2+6)", "alpha down 2 xton ^ 4 - 5 xton ^ 2 + 6 )", b"(X" + SQ + b"-3)(X" + SQ + b"-2)",
     "split over sqrt is too wide for Classic: the rational factors"),
    # The Algebra menu's commands, spelled with ALPHA letters.
    ("POLYROOTS(X^2-2)", spell("POLYROOTS") + " xton ^ 2 - 2 )", b"{" + NEG + b"\x10(2),\x10(2)}",
     "exact real roots, a list"),
    ("CPOLYROOTS(X^2+1)", spell("CPOLYROOTS") + " xton ^ 2 + 1 )", b"{" + NEG + I + b"," + I + b"}",
     "complex roots: i"),
    ("NROOTS(X^2-2)", spell("NROOTS") + " xton ^ 2 - 2 )", b"{" + NEG + b"1.414213562,1.414213562}",
     "decimal roots, all 26 columns"),
    ("CSOLVE(X^2+2X+5=0,X)", spell("CSOLVE") + " xton ^ 2 + 2 xton + 5 2nd math 1 0 , xton )",
     b"X=" + NEG + b"1-2" + I + b" or X=" + NEG + b"1+2" + I, "SOLVE over C"),
    ("CZEROS(X^2+2X+5)", spell("CZEROS") + " xton ^ 2 + 2 xton + 5 )", b"{" + NEG + b"1-2" + I + b","
     + NEG + b"1+2" + I + b"}", "CPOLYROOTS by another name"),
    ("CFACTOR(X^2+1)", spell("CFACTOR") + " xton ^ 2 + 1 )", b"(X-" + I + b")(X+" + I + b")", "FACTOR over C"),
    ("POLYREMAINDER(X^3,X^2+1)", spell("POLYREMAINDER") + " xton ^ 3 , xton ^ 2 + 1 )", NEG + b"X",
     "remainder"),
    ("POLYQUOTIENT(X^2,2X+1)", spell("POLYQUOTIENT") + " xton ^ 2 , 2 xton + 1 )", b"X/2-1/4",
     "quotient, over the rationals"),
    ("POLYGCD(X^2-1,X^2+X)", spell("POLYGCD") + " xton ^ 2 - 1 , xton ^ 2 + xton )", b"X+1", "gcd"),
    ("POLYCOEFFS(X^2-3X+2)", spell("POLYCOEFFS") + " xton ^ 2 - 3 xton + 2 )", b"{1," + NEG + b"3,2}",
     "coefficients, highest first"),
    ("POLYDEGREE(X^3+X)", spell("POLYDEGREE") + " xton ^ 3 + xton )", b"3", "degree"),
    ("COMDENOM(1/X+1/Y)", spell("COMDENOM") + " 1 / xton + 1 / alpha 1 )", b"(X+Y)/(XY)",
     "one fraction"),
    ("LEFT(X^2=2X+1)", spell("LEFT") + " xton ^ 2 2nd math 1 2 xton + 1 )", b"X" + SQ, "left side"),
    ("RIGHT(X^2=2X+1)", spell("RIGHT") + " xton ^ 2 2nd math 1 2 xton + 1 )", b"2X+1", "right side"),
    # Trigonometry (8) and Convert Expression (7) from the menu. textShadow
    # holds each function name as its letters; right-aligned by the columns
    # the engine counted (cos( goes in as the letters c o s, then '(').
    ("TEXPAND(sin(2X))", "alpha down 8 1 sin 2 xton ) )", b"2sin(X)cos(X)", "tExpand"),
    ("TCOLLECT(sin(X)cos(X))", "alpha down 8 2 sin xton ) cos xton ) )", b"sin(2X)/2", "tCollect"),
    ("TOLN(log(X))", "alpha down 7 1 log xton ) )", b"ln(X)/ln(10)", "to ln"),
]

# Three per run: the screen is 10 rows and each case costs two (echoed entry,
# answer), plus the launch line, so a fourth would scroll the first off the top.
PER_RUN = 3


def show(b):
    return "".join(chr(c) if 32 <= c < 127 else screen.GLYPHS.get(c, ".")
                   for c in b)


def basic_program(name, tokens):
    """Write _NAME.8xp, an unarchived TI-BASIC program, here; return its file name."""
    body = len(tokens).to_bytes(2, "little") + tokens
    entry = (b"\x0d\x00" + len(body).to_bytes(2, "little") + b"\x05" + name.encode().ljust(8, b"\0")
             + b"\x00\x00" + len(body).to_bytes(2, "little") + body)
    path = "_%s.8xp" % name
    with open(os.path.join(screen.HERE, path), "wb") as f:
        f.write(b"**TI83F*\x1a\x0a\x00" + b"e2e".ljust(42, b"\0") + len(entry).to_bytes(2, "little")
                + entry + (sum(entry) & 0xFFFF).to_bytes(2, "little"))
    return path


def run(keys, files=FILES):
    """Non-blank screen rows after the keys, as (text, ends_in_last_column)."""
    # CEmu's scheduler assertion aborts some runs partway through the read, at
    # a fixed emulated moment: the same keys with a different settle read fine,
    # calculator included. Unread bytes come back as FF, so shift and retry.
    for settle in (1500, 1900, 2300):
        r = screen.read([("hookptr", 0xD025E1, 3), ("flag", 0xD000B4, 1), ("errNo", 0xD008DF, 1),
                         ("screen", screen.TEXTSHADOW, screen.COLS * screen.ROWS)],
                        keys=PROLOGUE + keys, files=files, launch=LAUNCH, lead=False,
                        settle=settle)
        if b"\xff" * 4 not in r["screen"]:
            break
    ptr = int.from_bytes(r["hookptr"], "little")
    if not armed_in_flash(ptr, r["flag"][0]):
        sys.exit("hook not armed in flash: hookptr=%06X flag=%02X" % (ptr, r["flag"][0]))
    run.err = r["errNo"][0]                # the ERR: screen is not in textShadow
    buf = r["screen"]
    rows = [buf[i * screen.COLS:(i + 1) * screen.COLS] for i in range(screen.ROWS)]
    # A row that is only E0 is the cursor, caught blinking on: where the blink
    # is at the read moves with the install's flash writes (interrupts off),
    # so with the app's size (measured: HEAD padded to 82 KB shows it too).
    return [(x.strip(b" "), x[-1:] != b" ") for x in rows if x.strip(b" ") not in (b"", b"\xe0")]


def main():
    ok = True
    for i in range(0, len(CASES), PER_RUN):
        batch = CASES[i:i + PER_RUN]
        rows = run(" ".join("%s enter ." % k for _, k, _, _ in batch))
        pairs = rows[1:]                       # rows[0] is the installer's "Done"
        for j, (label, keys, want, why) in enumerate(batch):
            echo, _     = pairs[2 * j]     if 2 * j     < len(pairs) else (b"<missing>", False)
            got, flush  = pairs[2 * j + 1] if 2 * j + 1 < len(pairs) else (b"<missing>", False)
            good = got == want and echo == label.encode() and flush
            if not good:
                # The OS drops a key tap that lands on a busy moment at a fixed
                # time after boot, which moves with the app's size (measured:
                # the lost key shifts one place per 200 ms of keys before it).
                # So a failed case gets one run of its own, at another time.
                alone = run("%s enter ." % keys)[1:]
                if len(alone) == 2:
                    (echo, _), (got, flush) = alone
                    good = got == want and echo == label.encode() and flush
            ok &= good
            print("%-7s (%-7s) -> %-7s %s" % (
                label, show(echo), show(got),
                "ok  " + why if good else "FAIL want echo %r answer %r right-aligned, got %r %r %s"
                % (label, want, echo, got, "right-aligned" if flush else "LEFT-ALIGNED")))

    # 2nd ENTRY must bring back the entry alone.
    rows = run("2 xton + 2 xton enter . 2nd enter .")
    recall = rows[-1][0] if len(rows) >= 4 else b"<missing>"
    good = recall == b"2X+2X" and rows[-2][0] == b"4X"
    ok &= good
    print("%-6s recall -> %-6s %s" % ("2X+2X", show(recall),
          "ok  2nd ENTRY holds only what was typed" if good else "FAIL"))

    # ENTER on an empty line re-runs the last entry; it used to get the OS's 0.
    rows = run("2 xton + 2 xton enter . enter .")
    got = [t for t, _ in rows[1:]]
    good = got == [b"2X+2X", b"4X", b"2X+2X", b"4X"]
    ok &= good
    print("%-6s re-run -> %-6s %s" % ("2X+2X", show(got[-1]) if got else "<missing>",
          "ok  ENTER on an empty line answers it again" if good else "FAIL %r" % got))

    # An operator first thing on a line makes the OS type Ans before it. Ans is
    # the answer SymCE showed, so 4X, *3 is 12X, not the OS's 0.
    got = [t for t, _ in run("2 xton + 2 xton enter . * 3 enter .")[1:]]
    good = got == [b"2X+2X", b"4X", b"Ans*3", b"12X"]
    ok &= good
    print("%-6s Ans    -> %-6s %s" % ("4X,*3", show(got[-1]) if got else "<missing>",
          "ok  Ans is the last answer shown" if good else "FAIL %r" % got))

    # ... and when the OS showed its own result, Ans is that: the hook keeps
    # OP1 at A=0. An exact decimal carries on; 1/3 would not.
    got = [t for t, _ in run("2 + 2 enter . * xton enter . 1 / 4 enter . * xton enter .")[1:]]
    good = got == [b"2+2", b"4", b"Ans*X", b"4X", b"1/4", b"0.25", b"Ans*X", b"X/4"]
    ok &= good
    print("%-6s Ans    -> %-6s %s" % ("4,*X", show(got[3]) if len(got) > 3 else "<missing>",
          "ok  Ans is the OS's own result too" if good else "FAIL %r" % got))

    # A SymCE answer that is a plain number is still SymCE's Ans, not the
    # OS's, which was never set: +1 after POLYDEGREE's 3 was 1, *3 after
    # 2√(2) was 3, both the OS's stale Ans.
    got = [t for t, _ in run(spell("POLYDEGREE") + " xton ^ 3 + xton ) enter . + 1 enter . "
                             "2nd ^2 8 ) enter . * 3 enter .")[1:]]
    good = got[:8] == [b"POLYDEGREE(X^3+X)", b"3", b"Ans+1", b"4",
                       b"\x10(8)", b"2\x10(2)", b"Ans*3", b"6\x10(2)"]
    ok &= good
    print("%-6s Ans    -> %-6s %s" % ("3,+1", show(got[3]) if len(got) > 3 else "<missing>",
          "ok  a number SymCE showed is Ans too" if good else "FAIL %r" % got))

    # SymCE answers at A=2, before the OS evaluates the entry, so X's value
    # never matters: /X with X = 0 used to be ERR:DIVIDE BY 0. A refused entry
    # is still the OS's: 1/0 must still be that error.
    got = [t for t, _ in run("( xton ^ 2 + 2 xton ) / xton enter . 1 / 0 enter .")[1:]]
    good = got == [b"(X^2+2X)/X", b"X+2", b"1/0"] and run.err == 0x82   # E_DivBy0
    ok &= good
    print("%-6s /X     -> %-6s %s" % ("X=0", show(got[1]) if len(got) > 1 else "<missing>",
          "ok  dividing by X while X is 0; 1/0 still errors" if good else "FAIL %r" % got))

    # SOLVE fills in the stored values of the letters it does not solve for
    # (engine look(), the hook's .Llook: ChkFindSym on the real variable), and
    # changes none of them: SOLVE(Y=X,Y) afterwards still gives the 5. DelVar X
    # takes X out of the VAT (measured), so the letter is a letter again. The
    # CATALOG (2nd 0) jumps with D, and DelVar is the fifth entry after dayOfWk(.
    solve = "alpha down 1 xton + 2 alpha 1 2nd math 1 3 , alpha 1 )"
    got = [t for t, _ in run("5 sto xton enter . %s enter . alpha down 1 alpha 1 2nd math 1 xton , alpha 1 ) enter ."
                             % solve)[1:]]
    good = got[-4:] == [b"SOLVE(X+2Y=3,Y)", b"Y=" + NEG + b"1", b"SOLVE(Y=X,Y)", b"Y=5"]
    ok &= good
    print("%-6s stored -> %-6s %s" % ("5->X", show(got[-3]) if len(got) > 3 else "<missing>",
          "ok  SOLVE(X+2Y=3,Y) is Y=-1 and X still holds 5" if good else "FAIL %r" % got))
    got = [t for t, _ in run("5 sto xton enter . 2nd 0 . -1 . down down down down down enter . xton enter . . %s enter ." % solve)[1:]]
    good = got[-2:] == [b"SOLVE(X+2Y=3,Y)", b"Y=(3-X)/2"]
    ok &= good
    print("%-6s DelVar -> %-6s %s" % ("X", show(got[-1]) if got else "<missing>",
          "ok  no value stored: X stays a letter" if good else "FAIL %r" % got))

    # A command SymCE cannot finish is its own ERR screen, 1:Quit only, the
    # message in appErr1; Quit goes home and the next entry answers as ever.
    for keys, msg in (("alpha down 1 xton ^ 2 + 1 2nd math 1 0 , xton )", b"NO SOLUTION\0\0"),
                      ("alpha down 1 xton ^ 2 - 4 )", b"SYNTAX\0SOLVE(A=B,X)\0\0"),
                      # 1/3 is stored as 0.33333333333333: over 6 digits
                      ("1 / 3 sto xton enter . " + solve, b"SYMCE LIMIT\0SOLVE\0"),
                      (spell("POLYGCD") + " xton )", b"ARGUMENT\0POLYGCD\0\0"),
                      # a list as Ans: SymCE cannot, and the OS's Ans is stale
                      (spell("POLYROOTS") + " xton ^ 2 - 4 ) enter . * 3", b"SYMCE LIMIT\0ANS\0\0"),
                      # MODE row 3 RADIAN DEGREE: trigFlags bit 2, and ▶exp needs radians
                      ("mode down down right enter clear . alpha down 7 3 sin xton ) )",
                       b"MODE\0USE RADIAN\0\0")):
        for settle in (1500, 1900, 2300):
            r = screen.read([("errNo", 0xD008DF, 1), ("msg", 0xD025A9, 26), ("cx", 0xD007E0, 1)],
                            keys=PROLOGUE + keys + " enter . .",
                            files=FILES, launch=LAUNCH, lead=False, settle=settle)
            if b"\xff" * 4 not in r["msg"]:
                break
        got = [t for t, _ in run(keys + " enter . . 1 . 2 xton + 2 xton enter .")[1:]]
        good = r["errNo"] == b"\x2b" and r["msg"].startswith(msg) and got[-2:] == [b"2X+2X", b"4X"]
        ok &= good
        shown = msg.rstrip(b"\0").replace(b"\0", b" / ").decode("latin-1")
        print("%-6s ERR    -> %-6s %s" % ("SymCE", shown, "ok  ERROR: %s, 1:Quit, home again" % shown
              if good else "FAIL errNo %s msg %r after %r" % (r["errNo"].hex(), r["msg"], got)))

    # Insert cursor: a key typed with the cursor on a character goes in before
    # it. Overwrite would turn XX into X2 and answer 2X.
    got = [t for t, _ in run("xton xton left 2 enter .")[1:]]
    good = got == [b"X2X", b"2X" + SQ]
    ok &= good
    print("%-6s insert -> %-6s %s" % ("XX<2", show(got[0]) if got else "<missing>",
          "ok  typing inserts, it does not overwrite" if good else "FAIL %r" % got))
    # ... and the cursor says so. CLEAR, DEL and the arrows end insert mode in
    # the OS, which then drew the block (overwrite) cursor until the next key;
    # the hook runs them through cxMain and sets it back (textFlags bit 4).
    for mode, pro in (("cl", PROLOGUE), ("mp", PROLOGUE.replace("C ", "", 1))):
        for keys in ("xton xton left", "2 xton clear", "clear", "xton xton 2nd left", "xton xton left del",
                     "xton xton left down", "xton xton down"):
            for settle in (1500, 1900, 2300):
                r = screen.read([("tf", 0xD00085, 1)], keys=pro + keys + " .",
                                files=FILES, launch=LAUNCH, lead=False, settle=settle)
                if r["tf"] != b"\xff":
                    break
            good = bool(r["tf"][0] & 0x10)
            ok &= good
            print("%-6s cursor -> %-6s %s" % (mode, "insert" if good else "block",
                  "ok  after %s" % keys if good else "FAIL after %s: textFlags %s" % (keys, r["tf"].hex())))

    # A program's output is the program's: Disp 2X+2X, then 2X+3X as the last
    # line, must both stay the OS's 0 although the ENTER that ran it was marked.
    # AA sorts first in the PRGM menu; in the autotester the first ENTER after a
    # menu paste is swallowed, stock OS included, hence two.
    prg = basic_program("AA", bytes.fromhex("DE3258703258" "3F" "3258703358"))
    got = [t for t, _ in run("prgm enter . enter . enter . .", FILES + (prg,))[1:]]
    good = got == [b"prgmAA", b"0", b"0"]
    ok &= good
    print("%-6s program -> %-6s %s" % ("Disp", show(b" ".join(got[1:])),
          "ok  a program's output stays numeric" if good else "FAIL %r" % got))

    # What a program reads with Input is the program's: X is 0, so A is 0.
    prg = basic_program("AB", bytes.fromhex("DC41" "3F" "DE41"))    # Input A:Disp A
    got = [t for t, _ in run("prgm enter . enter . enter . . 2 xton + 2 xton enter . .",
                             FILES + (prg,))[1:]]
    good = got[:3] == [b"prgmAB", b"?2X+2X", b"0"]
    ok &= good
    print("%-6s program -> %-6s %s" % ("Input", show(got[2]) if len(got) > 2 else "<missing>",
          "ok  what Input reads stays numeric" if good else "FAIL %r" % got))
    # MODE ANSWERS: DEC (the MODE screen's 11th row) sets 0xD0009A bit 0:
    # decimals where they end, a fraction where they do not.
    got = [t for t, _ in run("mode . " + "down . " * 10 + "right . enter . clear . clear . "
                             "xton / 4 enter . . xton / 3 enter . .")]
    good = got[-4:] == [b"X/4", b"0.25X", b"X/3", b"X/3"]
    ok &= good
    print("%-6s DEC    -> %-6s %s" % ("X/4", show(got[-3]) if len(got) > 2 else "<missing>",
          "ok  MODE ANSWERS: DEC gives decimals where they end" if good else "FAIL %r" % got))
    # MathPrint: the answer goes to the OS as a fraction result (type 0x18, '/'
    # as the n/d bar), which it draws in 2D and files with EF 2E. Arrowing up to
    # it and pressing ENTER pastes it as an n/d box, and +1 after that must add
    # to the whole fraction. Read off the history record and lastAns; the 2D
    # drawing itself is VRAM, measured by hand (docs §7, 0x091FC2).
    # A decimal point is token 0x3A both ways; the drawn '.' is VRAM, by hand.
    for keys, text, last, shown, why in (
            ("( xton + 3 ) / ( xton - 2 )", "10 32 58 70 31 11 ef2e 10 58 71 32 11",  # (2X+1)n/d(X-2)
             "0c 10 32 58 70 31 11 83 10 58 71 32 11", "(2X+1)/(X-2)",
             "stacked, filed as n/d; pasted back, +1 adds to all of it"),
            ("xton / 3 + dp 5", "58 ef2e 33 70 31 3a 35",                        # X n/d 3+1.5
             "07 58 83 33 70 31 3a 35", "X/3+1.5", "a decimal next to a fraction"),
            ("( xton + 1 ) ^ 6", "58f036 70 36 58f035 70 3135 58f034 70 3230 580f 70 3135 580d 70 36 58 70 32",
             "1d 58f036 70 36 58f035 70 3135 58f034 70 3230 580f 70 3135 580d 70 36 58 70 32",
             "(X+1)^6+1", "29 tokens: past Classic's 26, fits with powers raised"),
            ("xton / 2 + alpha 1 / 3 + xton alpha 1 / 5 + xton ^ 2 right / 7 + alpha 1 ^ 2 right / 1 1"
             " + xton ^ 2 right alpha 1 / 1 3",                                   # six bars, 30 tokens
             "580d59 ef2e 3133 70 580d ef2e 37 70 5859 ef2e 35 70 590d ef2e 3131 70 58 ef2e 32 70 59 ef2e 33",
             "20 580d5983313370580d8337705859833570590d83313170588332705983337031",
             "X²Y/13+...+Y/3+1", "30 tokens, each '/' two bytes once drawn")):
        for settle in (1500, 1900, 2300):
            r = screen.read([("type", 0xD008EE, 1), ("text", 0xD008F2, len(bytes.fromhex(text))),
                             ("last", 0xD0EE20, len(bytes.fromhex(last)))],
                            keys=PROLOGUE.replace("C ", "", 1)
                            + keys + " enter . . up enter . + 1 enter . .",
                            files=FILES, launch=LAUNCH, lead=False, settle=settle)
            if b"\xff" * 4 not in r["text"] + r["last"]:
                break
        good = (r["type"] == b"\x18" and r["text"] == bytes.fromhex(text)
                and r["last"] == bytes.fromhex(last))
        ok &= good
        print("%-6s 2D     -> %-6s %s" % ("MthPrt", shown if good else "", "ok  " + why if good
              else "FAIL type %s text %s last %s" % (r["type"].hex(), r["text"].hex(), r["last"].hex())))

    ok &= menu()
    ok &= fonts()
    print("\nALL PASS" if ok else "\nFAILURES ABOVE")
    return 0 if ok else 1


# ---- the Evo font: the table's glyphs, pixel for pixel, in VRAM -------------------
SYMCE_APP = "apps " + "down " * 16 + "enter . . "          # APPS > SymCE: the settings screen


def font_glyphs():
    """(mkfont's large table, small table, the ROM's own glyphs) as {code: rows}."""
    sys.path.insert(0, os.path.join(screen.HERE, "..", ".."))
    sys.path.insert(0, os.path.join(screen.HERE, ".."))
    import mkfont
    rom = open(screen.ROM, "rb").read()
    lg, sm = mkfont.load()
    rl = lambda c: mkfont.unpack_large(rom[0x3D6E + c * 28:0x3D6E + c * 28 + 28])
    rs = lambda c: mkfont.unpack_small(rom[0xA3CA9 + c * 25:0xA3CA9 + c * 25 + 25])
    return ({c: mkfont.unpack_large(v) for c, v in lg.items()}, {c: mkfont.unpack_small(v) for c, v in sm.items()},
            rl, rs)


def find(dark, rows, ys):
    """Is `rows` (strings of . and #) drawn, exactly, anywhere with its top in ys?"""
    h, w = len(rows), len(rows[0])
    ink = [(x, y) for y, r in enumerate(rows) for x, ch in enumerate(r) if ch == "#"]
    for y0 in ys:
        for x0 in range(0, 320 - w + 1):
            if all((x0 + x, y0 + y) in dark for x, y in ink) and all(
                    ((x0 + x, y0 + y) in dark) == (ch == "#") for y, r in enumerate(rows)
                    for x, ch in enumerate(r)):
                return (x0, y0)
    return None


def fonts():
    """Evo on after the installer: the home screen's large '2' and the status bar's small O
    are the table's, not the ROM's. APPS > SymCE > 3 puts the ROM's back; CAS off (1) leaves Evo alone."""
    import vram
    lg, sm, rl, rs = font_glyphs()
    typed = "2 xton + 2 xton enter . ."
    N, TWO = 0x4F, 0x32                     # "O": N is the ROM N shifted one pixel, not telling
    ok = True

    def look(name, keys, want_large, want_small):
        nonlocal ok
        for settle in (1500, 1900, 2300):
            px, r = vram.grab(PROLOGUE.replace("C ", "", 1) + keys, 0, 240, files=FILES, launch=LAUNCH,
                              lead=False, settle=settle, extra=[("txt", screen.TEXTSHADOW, 260)])
            dark = {xy for xy, v in px.items() if vram.dark(v)}
            two = [find(dark, g, range(20, 120)) for g in (lg[TWO], rl(TWO))]
            light = {xy for xy in px if xy not in dark}          # the status bar is white on dark
            nn = [find(light, g[:12], range(0, 1)) for g in (sm[N], rs(N))]
            if two[0] or two[1]:
                break
        got = (bool(two[0]), bool(two[1]), bool(nn[0]), bool(nn[1]))
        want = (want_large, not want_large, want_small, not want_small)
        # Right after the installer the status bar is still the one drawn before the hooks
        # were armed (it redraws on the next mode change): only the large font is asserted.
        good = got == want if want_small is not None else got[:2] == want[:2]
        ok &= good
        print("%-6s font   -> %-12s %s" % ("Evo", name, "ok  large 2 and small O " + ("Evo" if want_large else "ROM") + ("" if want_small is not None else " (small: not asserted)")
              if good else "FAIL evo/rom large %s small %s" % (got[:2], got[2:])))
        return r["txt"]

    def cell(name, keys):
        """The row above and the row below a replaced glyph's 12x14 cell, and the columns just
        outside it, are background. (Measured 2026-09-28: the width byte 0x0C left in 0xD005A4
        was drawn as a 2-px tick one row over EVERY glyph.)"""
        nonlocal ok
        for settle in (1500, 1900, 2300):
            px, _ = vram.grab(PROLOGUE.replace("C ", "", 1) + keys, 0, 240, files=FILES, launch=LAUNCH,
                              lead=False, settle=settle)
            dark = {xy for xy, v in px.items() if vram.dark(v)}
            at = find(dark, lg[TWO], range(20, 200))
            if at:
                break
        x0, y0 = at or (0, 0)
        bad = sorted((x, y) for y in (y0 - 1, y0 + 14) for x in range(x0 - 2, x0 + 14) if (x, y) in dark)
        ok &= bool(at) and not bad
        print("%-6s font   -> %-12s %s" % ("Evo", name, ("ok  no ink around the cell at %s" % (at,)) if at and not bad
                                        else "FAIL cell %s stray %s" % (at, bad)))

    def star_err():
        """'*' typed on the home screen is the redrawn 6x6 asterisk; the ERROR screen's help text
        (small font) has clean rows 118/138/158/178 above each line (Measured 2026-09-28: the
        same 0x0C tick, seen as 2-px marks at x = 11-12, 23-24, ...)."""
        nonlocal ok
        for settle in (1500, 1900, 2300):
            px, _ = vram.grab(PROLOGUE.replace("C ", "", 1) + "2 * 3 . .", 0, 240, files=FILES, launch=LAUNCH,
                              lead=False, settle=settle)
            dark = {xy for xy, v in px.items() if vram.dark(v)}
            at = find(dark, lg[0x2A], range(20, 200))
            if at:
                break
        ok &= bool(at)
        print("%-6s font   -> %-12s %s" % ("Evo", "star, 2*3", "ok  '*' glyph at %s" % (at,) if at else "FAIL no '*' glyph"))
        for settle in (1500, 1900, 2300):
            px, _ = vram.grab(PROLOGUE.replace("C ", "", 1) + "2 + enter . .", 0, 240, files=FILES, launch=LAUNCH,
                              lead=False, settle=settle)
            dark = {xy for xy, v in px.items() if vram.dark(v)}
            if any((x, y) in dark for x in range(320) for y in (120, 121, 122)):
                break
        bad = sorted((x, y) for y in (118, 138, 158, 178) for x in range(320) if (x, y) in dark)
        text = sum(1 for y in range(120, 132) for x in range(320) if (x, y) in dark)
        ok &= not bad and text > 50
        print("%-6s font   -> %-12s %s" % ("Evo", "error rows", "ok  rows 118/138/158/178 clean, help text drawn"
                                        if not bad and text > 50 else "FAIL stray %s, text px %d" % (bad[:8], text)))

    look("installed", typed, True, None)
    cell("cell, home", typed)
    cell("cell, menu", "math . .")
    star_err()
    look("on, app opened and closed", SYMCE_APP + "clear . " + typed, True, True)
    look("3: TI", SYMCE_APP + "3 . clear . " + typed, False, False)
    look("3 twice: Evo", SYMCE_APP + "3 . 3 . clear . " + typed, True, True)
    look("1: CAS off, Evo stays", SYMCE_APP + "1 . clear . " + typed, True, True)
    return ok


def mathprint(keys, vram=False):
    """MathPrint, after the keys: the entry's bytes before and after the cursor,
    and with vram, the screen as 16-row band CRCs. The entry is read out of
    the edit buffer's two halves, editTop..editCursor and editTail..editBtm;
    MathPrint keeps it at 0xD1A8xx / 0xD2A8xx (measured)."""
    lo, hi = 0xD1A800, 0xD2A800
    bands = [("v%03d" % y, 0xD40000 + 640 * y, 640 * 16) for y in range(0, 240, 16)] if vram else []
    for settle in (1500, 1900, 2300):
        r = screen.read([("edit", 0xD02437, 12), ("lo", lo, 256), ("hi", hi, 128),
                         ("cx", 0xD007E0, 1), ("errNo", 0xD008DF, 1)],
                        keys=PROLOGUE.replace("C ", "", 1) + keys, files=FILES, launch=LAUNCH,
                        lead=False, settle=settle, crcs=bands)
        if b"\xff" * 4 not in r["edit"] and None not in [r[n] for n, _, _ in bands]:
            break
    top, cur, tail, btm = (int.from_bytes(r["edit"][i:i + 3], "little") for i in (0, 3, 6, 9))
    state = [r[n] for n, _, _ in bands] + [r["cx"][0], r["errNo"][0]]
    if not (lo <= top <= cur < lo + 256 and hi <= tail <= btm < hi + 128):
        return None, None, state
    return r["lo"][top - lo:cur - lo], r["hi"][tail - hi:btm - hi], state


def menu():
    """ALPHA+DOWN pops up SymCE's Algebra and Calculus menus, the TI-Nspire's;
    a pick types its command at the cursor exactly as ALPHA letters would (or
    the OS's own key, for nDeriv( and the like), a > item opens its submenu,
    and closing it leaves the screen as it was. The Algebra and Calculus tabs
    in 2nd MATH type the same commands and open the same submenus."""
    ok = True

    def report(label, good, why, got):
        print("%-6s menu   -> %-14s %s" % (label, show(got) if isinstance(got, bytes) else "",
              "ok  " + why if good else "FAIL %r" % (got,)))
        return good

    # Classic, on an empty line: 2 is factor(, typed as F A C T O R (, and the
    # rest of the line goes after it. ENTER then works as ever, and so does the
    # next entry. (What FACTOR( answers is the engine's business.)
    got = [t for t, _ in run("alpha down 2 xton ^ 2 - 1 ) enter . 2 + 3 enter .")[1:]]
    ok &= report("empty", got[:1] == [b"FACTOR(X^2-1)"] and got[-2:] == [b"2+3", b"5"]
                 and run.err == 0, "picked on an empty line, ENTER after", got)

    # Mid-entry: the command goes in at the cursor, which ends up after the '('.
    got = run("2 xton left alpha down 3 1")[-1][0]
    ok &= report("mid", got == b"2EXPAND(1X", "inserted at the cursor, cursor after (", got)

    # CLEAR, ALPHA, 2nd+QUIT and ALPHA+DOWN again all close it with nothing
    # typed (ALPHA+DOWN again: ALPHA closes, DOWN reaches the OS, which ignores
    # it on a one-line entry); UP wraps to 0:Extract, RIGHT opens it, DOWN and
    # ENTER pick its right(; the history above stays.
    got = [t for t, _ in run("2 + 3 enter . 7 alpha down clear 8 alpha down alpha 9 "
                             "alpha down 2nd mode 4 alpha down alpha down 5 alpha down up right down enter")[1:]]
    ok &= report("close", got == [b"2+3", b"5", b"78945RIGHT("],
                 "CLEAR, ALPHA, QUIT, ALPHA+DOWN close; UP RIGHT DOWN ENTER", got)

    # Submenus: 5 is Polynomial Tools, whose 8 is Degree; LEFT from one, or
    # CLEAR, is back at Algebra (on the submenu's item: ENTER reopens it).
    got = [t for t, _ in run("alpha down 5 8 alpha down 5 left 2 alpha down 7 clear enter 3 "
                             "alpha down 5 left enter enter")[1:]]
    got = b"".join(got[-2:]).rstrip(b"\xe4")  # 31 characters: two rows; E4 is the cursor
    ok &= report("sub", got == b"POLYDEGREE(FACTOR(TOEXP(NROOTS(" and run.err == 0,
                 "Poly 8, LEFT, CLEAR back to Algebra", got)

    # MathPrint draws straight to VRAM, so the entry is read out of the edit
    # buffer and compared with the same line typed by hand -- boxes included.
    rest = " xton ^ 2 right - 1 )"
    menu_pick = mathprint("2 + 3 enter . alpha down 2" + rest)
    by_hand = mathprint("2 + 3 enter . alpha cos alpha math alpha prgm alpha 4 alpha 7 alpha * (" + rest)
    ok &= report("MthPrt", menu_pick[:2] == by_hand[:2] and menu_pick[0] is not None
                 and menu_pick[0].startswith(bytes.fromhex("46414354 4f5210")),
                 "the same buffer as FACTOR( typed by hand", menu_pick[0])
    before, after, _ = mathprint("7 8 left alpha down 1")
    ok &= report("MthPrt", (before, after) == (b"7SOLVE\x10", b"8"),          # 0x10 is (
                 "mid-entry: 7SOLVE( before the cursor, 8 after", before)
    # Closed with CLEAR, not a pixel differs from never having opened it: not
    # at once, and not after a submenu, back to Algebra, another and back,
    # nor after Geometry, the widest tab bar, and its Circle submenu.
    never = mathprint("2 + 3 enter . 7 8 left", vram=True)
    for keys in ("clear", "5 left 9 clear clear", "left apps clear clear"):
        closed = mathprint("2 + 3 enter . 7 8 left alpha down " + keys, vram=True)
        ok &= report("MthPrt", closed == never and closed[0] == b"7",
                     "%s: screen and entry exactly as before" % keys, closed[0])
    _, _, state = mathprint("alpha down 2" + rest + " enter . 2 + 3 enter .")
    ok &= report("MthPrt", state == [0x40, 0], "ENTER after a pick: home, no error", state)

    # 2nd MATH: Algebra, Calculus and Geometry tabs after TEST, LOGIC and
    # CONDITIONS. TEST's = (6A) and LOGIC's and (40) type as ever; LEFT from
    # TEST wraps to Geometry, then Calculus, then Algebra, whose 1 is solve(,
    # and so is RIGHT three times.
    keys = "7 2nd math enter 7 2nd math right enter 2nd math left left left 1"
    got = run(keys)[-1][0]
    ok &= report("tab", got == b"7=7 and SOLVE(" and run.err == 0, "TEST, LOGIC, then Algebra's solve(", got)
    before, after, _ = mathprint(keys)
    ok &= report("MthPrt", (before, after) == (bytes.fromhex("376a3740 534f4c564510"), b""),
                 "tab: 7=7 and SOLVE( as tokens", before)
    # A > item in the tab opens the popup at its submenu: 9 Complex, 3 cZeros.
    got = run("7 2nd math left left left 9 3")[-1][0]
    ok &= report("tab", got == b"7CZEROS(" and run.err == 0, "Complex, cZeros(", got)
    # In an exponent box, where ALPHA+DOWN is off: the screen is exactly the
    # one FACTOR( typed by hand draws (and not the one with 4 for 2).
    tail = " 3 right + 1"
    picked = mathprint("2 xton ^ 2nd math right right right 2" + tail, vram=True)
    by_hand = mathprint("2 xton ^ alpha cos alpha math alpha prgm alpha 4 alpha 7 alpha * (" + tail,
                        vram=True)
    ok &= report("MthPrt", picked == by_hand and picked[2][-2:] == [0x40, 0],
                 "tab in a box: the same screen as FACTOR( by hand", picked[0])
    # And a submenu's pick there: Trigonometry's tCollect(, T C O L L E C T (.
    picked = mathprint("2 xton ^ 2nd math left left left 8 2" + tail, vram=True)
    by_hand = mathprint("2 xton ^ alpha 4 alpha prgm alpha 7 alpha ) alpha ) alpha sin alpha prgm alpha 4 (" + tail,
                        vram=True)
    ok &= report("MthPrt", picked == by_hand and picked[2][-2:] == [0x40, 0],
                 "submenu in a box: as tCollect( by hand", picked[0])

    # The Calculus menu: RIGHT from Algebra in the popup, the fifth
    # tab in 2nd MATH. Derivative and Tangent Line are SymCE's, answered; the
    # numerical items type the OS's own nDeriv( fnInt( fMin( fMax(, which the
    # OS evaluates as ever.
    got = [show(t) for t, _ in run("alpha down right 1 sin xton ) ) enter . alpha down right 9 xton ^2 , "
                                   "xton , 1 ) enter . alpha down right -1 1 xton ^ 3 , xton , 2 ) enter .")[1:]]
    ok &= report("calc", got == ["DERIV(sin(X))", "cos(X)", "TANGENTLINE(X²,X,1)", "2X-1",
                                 "nDeriv(X^3,X,2)", "12"] and run.err == 0,
                 "Derivative, Tangent Line, nDeriv( by the OS", got)
    # The second half, answered exactly: 3 Integral, 4 Limit, 5 Sum, and B
    # Series (APPS), whose 2 is the generalized series. Keys pressed while
    # the engine works are lost (SUM of 100 terms one by one took 2-3.5 s).
    got = [show(t) for t, _ in run("alpha down right 3 xton ^2 , xton , 0 , 3 ) enter . "
                                   "alpha down right 4 sin xton ) / xton , xton , 0 ) enter .")[1:]]
    ok &= report("calc", got == ["INTEGRAL(X²,X,0,3)", "9", "LIMIT(sin(X)/X,X,0)", "1"] and run.err == 0,
                 "Integral, Limit", got)
    got = [show(t) for t, _ in run("alpha down right 5 xton , xton , 1 , 1 0 0 ) enter . "
                                   "alpha down right apps 2 1 / sin xton ) , xton , 3 ) enter .")[1:]]
    ok &= report("calc", got == ["SUM(X,X,1,100)", "5050", "SERIES(1/sin(X),X,3)", "7X³/360+X/6+1/X"]
                 and run.err == 0, "Sum, Series submenu's 2", got)
    # Past 10 items: UP wraps to D and scrolls, C is Implicit Derivative; A-D
    # are MATH APPS PRGM x⁻¹ (A Arc Length, D Numeric Calculations); LEFT from
    # a submenu is back on its item, ENTER reopens it; LEFT at the top is
    # Algebra (3 expand(); B, Series, 1 Taylor.
    got = run("alpha down right up up enter alpha down right math alpha down right -1 left enter 2 "
              "alpha down right left 3 alpha down right apps 1")
    got = b"".join(t for t, _ in got[-2:]).rstrip(b"\xe4")
    ok &= report("calc", got == b"IMPDIF(ARCLEN(CENTRALDIFF(EXPAND(TAYLOR(" and run.err == 0,
                 "scroll, A-D, submenu and back, tabs", got)
    # The tab: 9 Tangent Line; D, Numeric Calculations, opens the popup there,
    # whose 3 is the OS's fnInt(.
    got = [show(t) for t, _ in run("2nd math left left 9 xton ^2 , xton , 1 ) enter . "
                                   "2nd math left left up enter 3 xton , xton , 0 , 1 ) enter .")[1:]]
    ok &= report("calc", got == ["TANGENTLINE(X²,X,1)", "2X-1", "fnInt(X,X,0,1)", "0.5"] and run.err == 0,
                 "tab: Tangent Line, fnInt( by the OS", got)
    # MathPrint: the OS key goes in as the OS's own MATH 8 does, template and all.
    picked = mathprint("2 + alpha down right -1 1 xton", vram=True)
    by_hand = mathprint("2 + math 8 xton", vram=True)
    ok &= report("MthPrt", picked == by_hand and picked[2][-2:] == [0x40, 0],
                 "nDeriv( template, as MATH 8 by hand", picked[0])

    # The Geometry menu: LEFT from Algebra in the popup, LEFT from TEST in
    # 2nd MATH. 2 Midpoint; the tab's 1 Distance; B (APPS), Circle, 3 the
    # circle through 3 points.
    got = [show(t) for t, _ in run("alpha down left 2 ( 1 , 2 ) , ( 4 , 7 ) ) enter . "
                                   "2nd math left 1 ( 0 , 0 ) , ( 3 , 4 ) ) enter . "
                                   "alpha down left apps 3 ( 0 , 0 ) , ( 2 , 0 ) , ( 0 , 2 ) ) enter .")[1:]]
    ok &= report("geo", got == ["MIDPOINT((1,2),(4,7))", "(5/2,9/2)", "DISTANCE((0,0),(3,4))", "5",
                                "CIRCLE((0,0),(2,0),(0,2))", "(X-1)²+(Y-1)²=2"] and run.err == 0,
                 "Midpoint, Distance, Circle through 3 points", got)
    return ok

if __name__ == "__main__":
    sys.exit(main())
