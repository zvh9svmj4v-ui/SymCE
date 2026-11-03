#!/usr/bin/env python3
"""P1: SymCE commands in TI-BASIC programs through expr( (docs/TI84CE-KNOWLEDGE.md section 14).

The phook is armed by the installer.  Usage:  python3 basic.py [T1 T2 ...]   (no argument: all, pass/fail)
"""
import os, re, sys
import e2e, screen

ARM = ""
RUN1 = "prgm enter . enter . enter . ."   # the first program in the PRGM menu (the first ENTER after a paste is swallowed)

TOK = {}
for s, v in [("Disp ", "DE"), ("expr(", "BB2A"), ("prgm", "5F"), ("Y1", "5E10"), ("Str1", "AA00"), ("Str2", "AA01"), ("->", "04"),
             (":", "3E"), ("\n", "3F"), ("Menu(", "E6"), ("Lbl ", "D6"), ("Goto ", "D7"), ("Input ", "DC"),
             ("Pause ", "D8"), ("DispGraph", "DF"), ("While ", "D1"), ("For(", "D3"), ("End", "D4"), ("Return", "D5"), ("Output(", "E0"),
             ("String>Equ(", "BB56"), ("Equ>String(", "BB55"), ("Asm84CEPrgm", "EF7B"), ("ClrHome", "E1"),
             ('"', "2A"), ("(", "10"), (")", "11"), (",", "2B"), ("+", "70"), ("-", "71"), ("*", "82"), ("/", "83"),
             ("Disp", "DE"), ("Stop", "D9"), ("Repeat ", "D2"), ("If ", "CE"), ("Then", "CF"), ("Else", "D0"), ("^", "F0"), ("=", "6A")]:
    TOK[s] = bytes.fromhex(v)
for c in "0123456789":
    TOK[c] = bytes([0x30 + ord(c) - 48])
for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
    TOK[c] = bytes([ord(c)])
TOK["theta"] = b"\x5b"
KEYS = sorted(TOK, key=len, reverse=True)


def cn(name):
    return b"".join(bytes([0xBB, 0xB0 + ord(c) - 65 + (c >= "K")]) for c in name)


def tok(src, hand=False):
    """hand: the names stay capital letter tokens, as a program typed with ALPHA has them"""
    out, i = b"", 0
    while i < len(src):
        m = None if hand else re.match(r"(FACTOR|EXPAND|SOLVE|CFACTOR|POLYGCD)\(", src[i:])
        if m:
            out += cn(m.group(1)) + b"\x10"; i += m.end(); continue
        for k in KEYS:
            if src.startswith(k, i):
                out += TOK[k]; i += len(k); break
        else:
            sys.exit("no token for %r in %r" % (src[i:i + 8], src))
    return out


def prog(name, src, hand=False):
    return e2e.basic_program(name, tok(src, hand))



def _read(rd, keys, files, **kw):
    for settle in (1500, 1900, 2300, 2700, 3100):
        r = screen.read(rd + [("rom", 0, 4)], keys=e2e.PROLOGUE + ARM + keys, files=files, launch=e2e.LAUNCH,
                        lead=False, settle=settle, per_key=300, **kw)
        if r["rom"] == bytes.fromhex("f3ed7e5b"):
            return r
    sys.exit("!! emulator stopped early on every try")


EDIT = [("etop", 0xD02437, 3), ("ecur", 0xD0243A, 3), ("etail", 0xD0243D, 3), ("ebtm", 0xD02440, 3), ("eoff", 0xD008E3, 3),
        ("msg", 0xD025A9, 26)]


def run(keys, progs=(), extra=(), scr=True):
    """Run keys with the programs loaded; returns (read dict, text rows of the screen)."""
    files = e2e.FILES + tuple(progs)
    rd = ([("screen", screen.TEXTSHADOW, screen.COLS * screen.ROWS)] if scr else []) + [
        ("hookptr", 0xD025F9, 3), ("errNo", 0xD008DF, 1), ("cx", 0xD007E0, 1)] + list(extra)
    r = _read(rd, keys, files)
    sc = r.get("screen", b" " * (screen.COLS * screen.ROWS))
    rows = [sc[i * screen.COLS:(i + 1) * screen.COLS].decode("latin-1").rstrip() for i in range(screen.ROWS)]
    return r, [x for x in rows if x.strip()]


RUN1 = "prgm enter . enter . enter . ."
OK = True


def check(name, cond, info):
    global OK
    OK &= bool(cond)
    print("%-4s %-28s %s" % ("ok" if cond else "FAIL", name, info))


def T1():
    c = prog("C", 'Disp expr("FACTOR(X^2-1)")\nDisp 5')
    r, s = run(RUN1, (c,))
    check("T1 literal factor", any("(X-1)(X+1)" in x for x in s) and r["errNo"][0] == 0, s)


def T2():
    c = prog("C", '"EXPAND((X+1)^2)"->Str1\nDisp expr(Str1)\nDisp 5')
    r, s = run(RUN1, (c,))
    check("T2 expr(Str1)", any("X" in x and "2" in x and "+" in x for x in s) and r["errNo"][0] == 0, s)


def T3():
    c = prog("C", 'Disp expr("1+2")\nDisp expr("EXPAND(2X+2X)")\nDisp 5')
    r, s = run(RUN1, (c,))
    check("T3 plain+numeric", [x.strip() for x in s[2:5]] == ["3", "4X", "5"], s)


def T4():
    c = prog("C", 'Disp 1\nDisp expr("FACTOR(")\nDisp 5')       # SYMCE LIMIT: 2:Quit only
    r, s = run(RUN1, (c,), extra=EDIT)
    check("T4 limit error", r["errNo"][0] == 0x2B and r["msg"].startswith(b"SYMCE LIMIT"), "errNo %02X msg %r" % (r["errNo"][0], r["msg"][:20]))
    r, s = run(RUN1 + " 1 . . ", (c,), extra=EDIT)
    check("T4 quit goes home", r["cx"][0] == 0x40, "cx %02X %s" % (r["cx"][0], s))
    c = prog("C", 'Disp 1\nDisp 2\nDisp expr("POLYGCD(X)")\nDisp 5')   # ARGUMENT: 2:Goto too
    r, s = run(RUN1, (c,), extra=EDIT)
    check("T4 argument error", r["errNo"][0] == 0xAB and r["msg"].startswith(b"An argument") or r["msg"].startswith(b"ARGUMENT"), "errNo %02X msg %r" % (r["errNo"][0], r["msg"][:20]))
    r, s = run(RUN1 + " 2 . . ", (c,), extra=EDIT)
    off = int.from_bytes(r["eoff"], "little")
    check("T4 goto opens editor", r["cx"][0] == 0x46, "cx %02X errOffset %d" % (r["cx"][0], off))


def T5():
    c = prog("C", '"X+1"->Str1\nString>Equ(Str1,Y1)\nDispGraph')
    r, s = run(RUN1 + " . . .", (c,), scr=False)
    check("T5 Y1 graph", r["errNo"][0] == 0 and r["cx"][0] != 0, "errNo %02X cx %02X" % (r["errNo"][0], r["cx"][0]))


def T6():
    c = e2e.basic_program("C", bytes.fromhex("EF7B3E7732B0F7D0C9"))
    r, s = run(RUN1, (c,), extra=[("mark", 0xD0F7B0, 1)], scr=False)
    check("T6 EF7B program", r["mark"] == b"\x77", r["mark"].hex())


def T7():
    c = prog("C", 'Disp 1\nWhile 1\nDisp expr("1")\nEnd\nDisp 2')
    r, s = run(RUN1 + " . . . on . . . ", (c,))
    check("T7 ON break", r["errNo"][0] == 0x86, "errNo %02X cx %02X" % (r["errNo"][0], r["cx"][0]))


def T8():
    # home: "FACTOR(X)"->Str1 then Str1 recalls the text, it is not answered (the quote-aware gate)
    rows = e2e.run("alpha + alpha down 2 xton ) alpha + sto vars 7 1 enter . . vars 7 1 enter . .")
    t = rows[-1][0] if rows else b""
    check("T8 home string stays text", b"actor(x)" in t.lower() and b"X-1" not in t, t)


def T9():
    c = prog("C", 'Disp expr("EXPAND((X+1)^12)")\nDisp 5')
    r, s = run(RUN1, (c,), extra=EDIT)
    ok = r["errNo"][0] in (0, 0x2B) and s and s[-2:][0].strip() != "" 
    check("T9 long answer", ok, "errNo %02X msg %r screen %s" % (r["errNo"][0], r["msg"][:14], s[-4:]))


def T10():
    for depth in (1, 2, 3):
        ps = [prog("C", "prgmD" if depth > 1 else 'Disp expr("FACTOR(X^2-1)")')]
        if depth > 1:
            ps.append(prog("D", "prgmE" if depth > 2 else 'Disp expr("FACTOR(X^2-1)")'))
        if depth > 2:
            ps.append(prog("E", 'Disp expr("FACTOR(X^2-1)")'))
        r, s = run(RUN1, ps, extra=EDIT)
        good = any("(X-1)(X+1)" in x for x in s)
        mem = r["errNo"][0] == 0x8E or r["msg"].startswith(b"ERR") 
        check("T10 nested prgm x%d" % depth, good or r["errNo"][0] != 0, "answered %s errNo %02X cx %02X" % (good, r["errNo"][0], r["cx"][0]))


def H1():
    c = prog("C", 'Disp expr("FACTOR(X^2-1)")\nDisp 5', True)
    r, s = run(RUN1, (c,))
    check("H1 typed FACTOR", any("(X-1)(X+1)" in x for x in s) and r["errNo"][0] == 0, s)


def H2():
    c = prog("C", 'expr("SOLVE(X^2-4=0,X)")->Str1\nDisp Str1\nDisp 5', True)
    r, s = run(RUN1, (c,))
    check("H2 typed SOLVE->Str1", any("X=" in x for x in s) and r["errNo"][0] == 0, s)


def H3():
    c = prog("C", 'Disp 1\nDisp 2\nDisp expr("POLYGCD(X)")\nDisp 5', True)
    r, s = run(RUN1, (c,), extra=EDIT)
    check("H3 typed argument error", r["errNo"][0] == 0xAB and r["msg"].startswith(b"ARGUMENT"), "errNo %02X msg %r" % (r["errNo"][0], r["msg"][:20]))
    r, s = run(RUN1 + " 2 . . ", (c,), extra=EDIT)
    check("H3 goto opens editor", r["cx"][0] == 0x46, "cx %02X" % r["cx"][0])


def H4():
    c = prog("C", '"FACTOR(X)"->Str1\nDisp Str1\nDisp 5', True)
    r, s = run(RUN1, (c,))
    check("H4 typed string stays text", any("FACTOR(X)" in x for x in s) and r["errNo"][0] == 0, s)


def H5():
    c = prog("C", 'Disp expr("FACTO(2)")\nDisp 5', True)
    r, s = run(RUN1, (c,), extra=EDIT)
    print("     H5 shows", s, "errNo %02X" % r["errNo"][0])
    check("H5 near miss is the OS's", not any("SYMCE" in x or "(X" in x for x in s) and r["msg"][:5] != b"SYMCE", s)


def B1():
    c = prog("C", 'FACTOR(X^2-1)->Str1\nDisp Str1\nDisp 5', True)
    r, s = run(RUN1, (c,))
    check("B1 bare store", any("(X-1)(X+1)" in x for x in s) and r["errNo"][0] == 0, s)


def B2():
    c = prog("C", 'Disp EXPAND((X+1)^2)\nDisp 5', True)
    r, s = run(RUN1, (c,))
    check("B2 bare Disp", any("X" in x and "2" in x and "+" in x for x in s) and r["errNo"][0] == 0, s)


def B3():
    c1 = prog("C", 'Disp FACTOR(X^2-1\nDisp 5', True)
    r1, s1 = run(RUN1, (c1,))
    c2 = prog("C", 'FACTOR(X^2-1->Str1\nDisp Str1\nDisp 5', True)
    r2, s2 = run(RUN1, (c2,))
    ok = any("(X-1)(X+1)" in x for x in s1) and any("(X-1)(X+1)" in x for x in s2)
    check("B3 missing paren", ok and r1["errNo"][0] == 0 and r2["errNo"][0] == 0, "Disp %s, store %s" % (s1, s2))


def B4():
    c = prog("C", 'Disp EXPAND(FACTOR(X^2-1))\nDisp 5', True)
    r, s = run(RUN1, (c,))
    check("B4 nested bare", any("X" in x and "-1" in x for x in s) and r["errNo"][0] == 0, s)


def B5():
    c = prog("C", 'SOLVE(X^2=4,X)->Str1\nDisp Str1\nDisp 5', True)
    r, s = run(RUN1, (c,))
    check("B5 comma/SOLVE", any("X=" in x for x in s) and r["errNo"][0] == 0, s)


def B6():
    c = prog("C", 'Disp 1\nDisp 2\nDisp POLYGCD(X)\nDisp 5', True)
    r, s = run(RUN1, (c,), extra=EDIT)
    check("B6 error screen", r["errNo"][0] == 0xAB and (r["msg"].startswith(b"An argument") or r["msg"].startswith(b"ARGUMENT")), "errNo %02X msg %r" % (r["errNo"][0], r["msg"][:20]))
    r, s = run(RUN1 + " 2 . . ", (c,), extra=EDIT)
    off = int.from_bytes(r["eoff"], "little")
    # the call's first byte: Disp 1 : Disp 2 : Disp = 6 + 1 tokens, POLYGCD( starts at 7 (the first P2 build reported 14, a position in a temp string)
    check("B6 goto opens editor", r["cx"][0] == 0x46 and off == 7, "cx %02X errOffset %d" % (r["cx"][0], off))


def B7():
    c1 = prog("C", '2->A\n3->B\nDisp A(B+1)\nDisp 5', True)
    r1, s1 = run(RUN1, (c1,))
    c2 = prog("C", '2->X\nDisp 2X(X+1)\nDisp 5', True)
    r2, s2 = run(RUN1, (c2,))
    check("B7 numeric expressions", any("8" in x for x in s1) and any("12" in x for x in s2), "A(B+1) %s, 2X(X+1) %s" % (s1, s2))


def B8():
    c = prog("C", '"FACTOR(X)"->Str1\nDisp Str1\nDisp 5', True)
    r, s = run(RUN1, (c,))
    check("B8 string stays text", any("FACTOR(X)" in x for x in s) and r["errNo"][0] == 0, s)


def B9():
    c = prog("C", 'prgmD', True)
    d = prog("D", 'Disp FACTOR(X^2-1)\nDisp 5', True)
    r, s = run(RUN1, (c, d))
    check("B9 nested program", any("(X-1)(X+1)" in x for x in s) and r["errNo"][0] == 0, s)


def B10():
    c = prog("C", 'Disp 1\nWhile 1\nDisp FACTOR(X^2-1)\nEnd\nDisp 2', True)
    r, s = run(RUN1 + " . . . on . . . ", (c,))
    check("B10 ON break", r["cx"][0] == 0x52, "errNo %02X cx %02X" % (r["errNo"][0], r["cx"][0]))


def B11():
    c = prog("C", 'For(I,1,500)\nI+1->A\nEnd\nDisp A', True)
    r, s = run(RUN1, (c,))
    check("B11 loop timing", r["errNo"][0] == 0 and any("501" in x for x in s), "errNo %02X %s" % (r["errNo"][0], s))


# ---- P2 named copies: the user's program is never written; the copy is a hidden protected program the OS made ----
STATE = [("udata", 0xD2A800, 0x800), ("vatw", 0xD3FD00, 0x300), ("lvn", 0xD0EEC2, 1)]
HOME = " 1 + 1 enter . "              # a home entry: the hook's # event deletes the copies


def untouched(r, progs):
    """each (name, src): its data (size word + body) is in RAM once, its VAT entry still points at it, and no copy is left"""
    bad = []
    ud, vat = r["udata"], r["vatw"]
    for name, src in progs:
        b = tok(src, True)
        data = len(b).to_bytes(2, "little") + b
        i = ud.find(data)
        if ud.count(data) != 1:
            bad.append("%s body x%d" % (name, ud.count(data)))
            continue
        ptr = 0xD2A800 + i
        be = bytes([ptr >> 16, (ptr >> 8) & 255, ptr & 255])
        e = name.encode() + bytes([len(name)]) + be + b"\x00\x00"   # VAT entry, ascending: len, name, ptr hi..lo, 00 00, type
        if vat.count(e) != 1:
            bad.append("%s VAT ptr %06X x%d" % (name, ptr, vat.count(e)))
    if any(bytes([0x21, 0x30 + l, 2]) in vat for l in (1, 2, 3)):     # a copy's VAT entry: name "!<level>", length 2
        bad.append("a wrapped copy is still in the VAT")
    if r["lvn"][0] != 0:
        bad.append("levels in use %d" % r["lvn"][0])
    return bad or "ok"


def n_check(name, cond, r, progs, info):
    u = untouched(r, progs)
    check(name, cond and u == "ok", "%s; original %s" % (info, u))


def N1():
    P = [("C", 'FACTOR(X^2-1)->Str1\nDisp Str1\nDisp 5')]
    r, s = run(RUN1 + HOME, [prog(n, x, True) for n, x in P], extra=STATE)
    n_check("N1 normal end", any("(X-1)(X+1)" in x for x in s), r, P, s[-4:])


def N2():
    P = [("C", 'Disp 1\nWhile 1\nDisp FACTOR(X^2-1)\nEnd')]
    r, s = run(RUN1 + " . . . on . . . 1 . ." + HOME, [prog(n, x, True) for n, x in P], extra=STATE)
    n_check("N2 ON, 1:Quit", r["cx"][0] == 0x40, r, P, "cx %02X" % r["cx"][0])
    r, s = run(RUN1 + " . . . on . . . 2 . .", [prog(n, x, True) for n, x in P], extra=EDIT)
    # the copy is protected: the OS offers Quit only, 2 does nothing, and the editor never opens on the copy
    check("N2 ON, 2 offers no Goto", r["cx"][0] != 0x46 and r["errNo"][0] == 0x86, "cx %02X errNo %02X" % (r["cx"][0], r["errNo"][0]))


def N3():
    P = [("C", 'Disp FACTOR(X^2-1)\nDisp 1/0\nDisp 2')]
    r, s = run(RUN1 + " 1 . ." + HOME, [prog(n, x, True) for n, x in P], extra=STATE)
    n_check("N3 1/0, 1:Quit", r["cx"][0] == 0x40, r, P, "cx %02X" % r["cx"][0])
    r, s = run(RUN1 + " 2 . .", [prog(n, x, True) for n, x in P], extra=EDIT)
    check("N3 1/0, 2 offers no Goto", r["cx"][0] == 0x52 and r["errNo"][0] == 0x82, "cx %02X errNo %02X" % (r["cx"][0], r["errNo"][0]))


def N4():
    P = [("C", 'Disp 1\nDisp 2\nDisp POLYGCD(X)\nDisp 5')]
    r, s = run(RUN1 + " 2 . ." , [prog(n, x, True) for n, x in P], extra=EDIT + STATE)
    ok = r["cx"][0] == 0x46 and any("OLYGCD(X)" in x for x in s) and not any("expr" in x for x in s)
    info = "cx %02X off %d %s" % (r["cx"][0], int.from_bytes(r["eoff"], "little"), s)
    r, _ = run(RUN1 + " 2 . . 2nd mode ." + HOME, [prog(n, x, True) for n, x in P], extra=STATE)      # leave the editor: the original is intact
    n_check("N4 SymCE error, Goto shows the original", ok, r, P, info)


def N5():
    P = [("C", "0->A\n" + "A+1->A\n" * 60 + "FACTOR(X^2-1)->Str1\nDisp A\nDisp Str1")]
    r, s = run(RUN1 + HOME, [prog(n, x, True) for n, x in P], extra=STATE)
    n = len(tok(P[0][1], True))
    n_check("N5 %d byte program" % n, n > 300 and any("60" in x for x in s) and any("(X-1)(X+1)" in x for x in s), r, P, s[-4:])


def N6():
    P = [("A", 'Disp FACTOR(X^2-1)\nprgmB\nDisp EXPAND((X+1)^2)'), ("B", 'Disp FACTOR(X^2-4)')]
    r, s = run(RUN1 + HOME, [prog(n, x, True) for n, x in P], extra=STATE)
    n_check("N6 nested A/B", any("(X-1)(X+1)" in x for x in s) and any("(X-2)(X+2)" in x for x in s) and any("2X+1" in x for x in s),
            r, P, s)


def N7():
    P = [("C", 'Disp FACTOR(X^2-1)\nIf R=0\nThen\n1->R\nprgmC\nEnd\nDisp 9')]
    r, s = run(RUN1 + HOME, [prog(n, x, True) for n, x in P], extra=STATE)
    n_check("N7 calls itself", sum("(X-1)(X+1)" in x for x in s) == 2 and sum(x.strip() == "9" for x in s) == 2, r, P, s)


# ---- L cases: last value in place of Done when program ends in a bare command or store ----
def L1():
    P = [("C", 'Disp 6\nFACTOR(X^2-1)')]
    ps = [prog(n, x, True) for n, x in P]
    r, s = run(RUN1, ps)
    r2, _ = run(RUN1 + HOME, ps, extra=STATE)
    n_check("L1 bare last line", s[-1].strip() == "(X-1)(X+1)", r2, P, s[-3:])


def L2():
    P = [("C", 'Disp 6\nFACTOR(X^2-1)->Str1')]
    ps = [prog(n, x, True) for n, x in P]
    r, s = run(RUN1, ps)
    r2, _ = run(RUN1 + HOME, ps, extra=STATE)
    n_check("L2 bare store last line", s[-1].strip() == "(X-1)(X+1)", r2, P, s[-3:])


def L3():
    P = [("A", 'prgmB\nDisp 5'), ("B", 'FACTOR(X^2-1)')]
    ps = [prog(n, x, True) for n, x in P]
    r, s = run(RUN1, ps)
    r2, _ = run(RUN1 + HOME, ps, extra=STATE)
    n_check("L3 nested callee bare", s[-1].strip() == "Done", r2, P, s[-3:])


def L4():
    P = [("C", 'Disp 6\nFACTOR(X^2-1)')]
    ps = [prog(n, x, True) for n, x in P]
    r, s = run(RUN1 + " up . ", ps, extra=[("rec", 0xD008EE, 14)])
    r2, _ = run(RUN1 + " up . down . " + HOME, ps, extra=STATE)
    ans = tok("(X-1)(X+1)", True)
    ok = s[-1].strip() == "(X-1)(X+1)" and r["rec"][0] == 4 and r["rec"][4:14] == ans
    n_check("L4 bare then up", ok, r2, P, "screen %s rec %s" % (s[-1], r["rec"][:14].hex()))


def L5():
    P = [("C", 'Disp 5\nDisp 6')]
    ps = [prog(n, x, True) for n, x in P]
    r, s = run(RUN1, ps)
    r2, _ = run(RUN1 + HOME, ps, extra=STATE)
    n_check("L5 plain program Done", s[-1].strip() == "Done", r2, P, s[-3:])



if __name__ == "__main__":
    names = sys.argv[1:] or ["T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8", "T9", "T10", "H1", "H2", "H3", "H4", "H5",
                             "B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8", "B9", "B10", "B11",
                             "N1", "N2", "N3", "N4", "N5", "N6", "N7",
                             "L1", "L2", "L3", "L4", "L5"]
    for n in names:
        globals()[n]()
    sys.exit(0 if OK else 1)
