#!/usr/bin/env python3
"""The real stack use of the whole SymCE app on OS 5.8.4, for the deepest steps( / DESOLVE paths.

    python3 stack.py [name ...]

Needs the app built with the test flag, so the engine paints the OS stack and records the deepest byte:

    make -C symce XFLAGS=-DSTKPAINT && cp symce/bin/SYMCE* tools/emu/     (rm symce/obj/engine.inc first)

symce_engine (engine.c, -DSTKPAINT) paints 0xD19900 (stackBot is 0xD1987E, ti84pceg.inc; stackTop
0xD1A87E, 4 KB) up to 128 bytes below its caller's frame with A5, runs, and scans up for the first
changed byte. Painting 0xD1987E..0xD198FF itself resets the calculator (measured), hence 0xD19900;
a case whose lowest byte is 0xD19900 went deeper than that (left <= 130). The
record at 0xD09566: "STK", calls, entry SP (the wrapper's local), lowest byte touched, bytes used
below that entry, bytes LEFT above stackBot. Each case types its entry on the home screen from a
fresh boot, so "left" is the margin below the whole call chain, OS frames above included.
"""
import json, os, re, struct, subprocess, sys
import cmdkeys, e2e, screen, vram

CLASSIC = e2e.PROLOGUE
MATHPRINT = e2e.PROLOGUE.replace("C . ", "", 1)
REC = 0xD09566
LEFT_MIN = 512
TRIES = 4
STACKBOT, PAINTBOT = 0xD1987E, 0xD19900


def K(s, slow=False):
    """Key names that type s: UPPERCASE( is a command from the popup, X and Y letters, e^( abs( sin( from their keys."""
    out, i = [], 0
    while i < len(s):
        m = re.match(r"[A-Z]{3,}\(", s[i:])
        if m:
            out.append(cmdkeys.spell(m.group()[:-1])); i += m.end(); continue
        for lit, k in (("e^(", "2nd ln"), ("abs(", "math right 1"), ("sin(", "sin"), ("cos(", "cos"), ("tan(", "tan"), ("log(", "log"), ("ln(", "ln"), ("√(", "2nd ^2"), ("ᴇ", "2nd ,"), ("π", "2nd ^"), ("≤", "2nd math 6"), (">", "2nd math 3"),
                       (" and ", "w400 2nd math right 1 w400 w400"), ("''", "2nd apps 2 w400 2nd apps 2 w400"), ("'", "2nd apps 2 w400"), ("=", "2nd math 1 w400"),
                       ("[", "2nd *"), ("]", "2nd -")):
            if s.startswith(lit, i):
                out.append(k); i += len(lit); break
        else:
            c = s[i]; i += 1
            out.append({"X": "xton", "Y": "alpha w300 1 w300" if slow else "alpha 1", ".": "dp"}.get(c, c))
    return " ".join(out)


M4 = "[[1,2,3,4][5,6,7,8][9,10,11,12][13,14,15,17]]"
# (name, entry, MathPrint too: no / or ^, which would open templates)
CASES = [
    ("expand7", "STEPS(EXPAND((X+1)^7))", 0),
    ("comdenom3", "STEPS(COMDENOM(1/(X+1)+1/(X-1)+X/(X^2+1)))", 0),
    ("polyquot", "STEPS(POLYQUOTIENT(X^5+1,X^2+1,X))", 0),
    ("solve_abs", "STEPS(SOLVE(abs(2X-3)=5,X))", 0),
    ("solve_rat", "STEPS(SOLVE(1/(X-1)+1/(X+1)=1,X))", 0),
    ("solve_lin_le", "STEPS(SOLVE(3X-5≤2X+1,X))", 1),
    ("solve_quad_gt", "STEPS(SOLVE(X^2-5X+6>0,X))", 0),
    ("desolve2", "STEPS(DESOLVE(Y''+Y=0,X,Y))", 1),
    ("desolve2_forced", "STEPS(DESOLVE(Y''+Y=X,X,Y))", 1),
    ("desolve2_plain", "DESOLVE(Y''+Y=0,X,Y)", 1),
    ("desolve2_forced_plain", "DESOLVE(Y''+Y=X,X,Y)", 1),
    ("int_1_x2m1", "STEPS(INTEGRAL(1/(X^2-1),X))", 0),
    ("int_xex", "STEPS(INTEGRAL(X*e^(X),X))", 0),
    ("solve_exp2", "STEPS(SOLVE(2^X=1/3,X))", 0),
    ("solve_expq", "STEPS(SOLVE(e^(2X)-3e^(X)+2=0,X))", 0),
    ("solve_exp_mix", "STEPS(SOLVE(2^(X+1)=3^X,X))", 0),
    ("solve_trig", "STEPS(SOLVE(2sin(2X+π/3)=1,X))", 0),
    ("solve_trig_bnd", "STEPS(SOLVE(tan(X)=1,X,0,3π))", 0),
    ("solve_logs", "STEPS(SOLVE(log(X)+log(X-3)=1,X))", 0),
    ("int_usub", "STEPS(INTEGRAL(X*√(X^2+1),X))", 0),
    ("int_atan_cs", "STEPS(INTEGRAL(1/(X^2+2X+5),X))", 0),
    ("int_pi_lim", "STEPS(INTEGRAL(sin(X)^2,X,0,π))", 0),
    ("deriv_chain", "STEPS(DERIV(sin(cos(X))^3,X))", 0),
    ("solve_nested", "STEPS(SOLVE(EXPAND((X+1)^2)=0,X))", 0),
    ("taylor", "STEPS(TAYLOR(sin(X),X,7))", 1),
    ("mrref4", "STEPS(MRREF(" + M4 + "))", 0),
    ("factor_x4m1", "STEPS(FACTOR(X^4-1))", 0),
    ("desolve_ic_forced", "STEPS(DESOLVE(Y''-3Y'+2Y=X and Y(0)=1 and Y'(0)=0,X,Y))", 1),
    ("desolve_exp", "STEPS(DESOLVE(Y''+Y=e^(X),X,Y))", 0),
    ("desolve_sin", "STEPS(DESOLVE(Y''+Y=sin(X),X,Y))", 1),
    ("desolve_quad_forced", "STEPS(DESOLVE(Y''-3Y'+2Y=X^2,X,Y))", 0),
    ("desolve_lin1", "STEPS(DESOLVE(Y'+Y=X,X,Y))", 1),
    ("desolve_sep", "STEPS(DESOLVE(Y'=X/Y,X,Y))", 0),
    ("desolve_ic_plain", "DESOLVE(Y''-3Y'+2Y=X and Y(0)=1 and Y'(0)=0,X,Y)", 1),
    ("ic_x2", "STEPS(DESOLVE(Y''-3Y'+2Y=X^2 and Y(0)=1 and Y'(0)=0,X,Y))", 0),
    ("ic_x2_plain", "DESOLVE(Y''-3Y'+2Y=X^2 and Y(0)=1 and Y'(0)=0,X,Y)", 0),
    ("ic_sin", "STEPS(DESOLVE(Y''+Y=sin(X) and Y(0)=0 and Y'(0)=1,X,Y))", 1),
    ("ic_exp", "STEPS(DESOLVE(Y''+Y=e^(X) and Y(0)=1 and Y'(0)=0,X,Y))", 0),
    ("ic_repeated", "STEPS(DESOLVE(Y''-2Y'+Y=X and Y(0)=1 and Y'(0)=0,X,Y))", 1),
    ("ic_complex", "STEPS(DESOLVE(Y''+2Y'+5Y=X and Y(0)=1 and Y'(0)=0,X,Y))", 1),
    ("ic_first_x2", "STEPS(DESOLVE(Y'+Y=X^2 and Y(0)=1,X,Y))", 0),
    ("ic_xsin", "STEPS(DESOLVE(Y''+Y=X*sin(X) and Y(0)=1 and Y'(0)=0,X,Y))", 1),
    ("ic_cplx_sin", "STEPS(DESOLVE(Y''+2Y'+5Y=sin(X) and Y(0)=1 and Y'(0)=0,X,Y))", 1),
    ("ic_res_exp", "STEPS(DESOLVE(Y''-Y=X*e^(X) and Y(0)=1 and Y'(0)=0,X,Y))", 0),
    ("ic_dbl_exp", "STEPS(DESOLVE(Y''-2Y'+Y=e^(X) and Y(0)=1 and Y'(0)=0,X,Y))", 0),
    ("ic_multi", "STEPS(DESOLVE(Y''-3Y'+2Y=X+sin(X)+e^(X) and Y(0)=1 and Y'(0)=0,X,Y))", 0),
    ("ic_xcos2", "STEPS(DESOLVE(Y''+2Y'+5Y=X*cos(2X) and Y(0)=1 and Y'(0)=0,X,Y))", 1),
    ("ic_first_exp", "STEPS(DESOLVE(Y'+2Y=X*e^(X)+cos(X) and Y(0)=1,X,Y))", 0),
    ("ic_sep", "STEPS(DESOLVE(Y'=X/Y and Y(1)=2,X,Y))", 0),
    ("ic_sep_xy", "STEPS(DESOLVE(Y'=X*Y and Y(0)=1,X,Y))", 1),
    ("de_cplx", "STEPS(DESOLVE(Y''+Y'+Y=0,X,Y))", 1),
    ("de_tan", "STEPS(DESOLVE(Y'+Y*tan(X)=sin(X),X,Y))", 1),
    ("de_if_ic", "STEPS(DESOLVE(Y'+2XY=X and Y(0)=1,X,Y))", 1),
    ("de_logistic_ic", "STEPS(DESOLVE(Y'=Y(1-Y) and Y(0)=1/2,X,Y))", 0),
    ("de_expy_ic", "STEPS(DESOLVE(Y'=e^(X+Y) and Y(0)=0,X,Y))", 0),
    ("de_ysin", "STEPS(DESOLVE(Y'=Y*sin(X),X,Y))", 1),
    ("int_1_x2p1", "INTEGRAL(1/(X^2+1),X)", 0),
    ("int_1_x2p1_steps", "STEPS(INTEGRAL(1/(X^2+1),X))", 0),
    ("lim_conj", "STEPS(LIMIT((√(X+4)-2)/X,X,0))", 0),
    ("lim_scale", "STEPS(LIMIT(ln(X)/X,X,ᴇ99))", 0),
    ("lim_1inf", "STEPS(LIMIT((1+1/X)^X,X,ᴇ99))", 0),
    ("lim_0pow", "STEPS(LIMIT(X^X,X,0,1))", 0),
    ("sum_tel", "STEPS(SUM(1/(X(X+1)(X+2)),X,1,ᴇ99))", 0),
    ("sum_geo", "STEPS(SUM(5*(.9)^X,X,0,ᴇ99))", 0),
]


OUT = os.environ.get("OUTDIR", os.path.join(os.environ.get("TMPDIR", "/tmp"), "stk2"))
TS = screen.TEXTSHADOW


def text(buf):
    """Classic home screen text, rows joined by newline, blank rows dropped."""
    return "\n".join(l.rstrip() for l in screen.render(buf) if l.strip())


def norm(t):
    """Compare typed text with what the screen shows: no spaces or newlines, the glyphs the OS draws for e, ' and so on folded."""
    return re.sub(r"\s", "", t).replace("\u2032", "'").upper()


TOP = {"Classic": 0xD1A8AB, "MathPrint": 0xD1A8CA}   # editTop after the prologue (measured; checked every run)


def read2(*stages):
    """One boot: for each (keys, ranges) stage type the keys, then snapshot the ranges (phases a, b, c ...).
    screen.read only reads at the end, and typing differs a little from boot to boot, so a check
    made in an earlier boot proves nothing about the boot that is measured."""
    seq, hashes, plan = ["delay|1500"], {}, []
    def keys(ks):
        for k in ks.split():
            if k == ".": seq.append("delay|2000")
            elif k[0] == "w" and k[1:].isdigit(): seq.append("delay|" + k[1:])
            elif k == "L": seq.extend(["action|launch", "delay|4000"])
            elif k == "C": seq.extend(["action|useClassic", "delay|1500"])
            elif k == "dp": seq.extend(["key|.", "delay|200"])
            else: seq.extend(["key|" + k, "delay|200"])
    def hash_(rng, phase):
        for name, addr, length in rng:
            for off in range(0, length, 4):
                key = "%s%d" % (phase, len(plan))
                seq.append("hash|" + key)
                hashes[key] = {"description": key, "start": hex(addr + off), "size": str(min(4, length - off)), "expected_CRCs": ["00000000"]}
                plan.append((key, (phase, name), min(4, length - off)))
    for i, (ks, rng) in enumerate(stages):
        keys(ks); seq.append("delay|1000"); hash_(rng, "abcd"[i])
    cfg = {"transfer_files": list(e2e.FILES), "target": {"name": e2e.LAUNCH, "isASM": False}, "delay_after_step": 1,
           "sequence": seq, "hashes": hashes}
    path = os.path.join(screen.HERE, "_screen.json")
    json.dump(cfg, open(path, "w"))
    r = subprocess.run([screen.TESTER, path], capture_output=True, text=True, env=dict(os.environ, AUTOTESTER_ROM=screen.ROM), cwd=screen.HERE)
    if "unknown key" in r.stderr: sys.exit("BAD KEY:\n" + r.stderr)
    got = dict(re.findall(r'Hash #\S+ \("([^"]*)"\).*?got ([0-9A-F]{1,8})\)', r.stdout))
    out = {}
    for key, name, n in plan:
        out[name] = out.get(name, b"") + (screen.crcread.invert(int(got[key], 16), n) if key in got else b"\xff" * n)
    if len(got) < len(plan): print("WARNING: emulator stopped after %d/%d windows" % (len(got), len(plan)), flush=True)
    return out


def vrows(y0, y1):
    return [("%d_%d" % (x, y), vram.VRAM + 2 * (y * vram.W + x), 4) for y in range(y0, y1) for x in range(0, vram.W, 2)]


def px_of(r, phase, y0, y1):
    px = {}
    for y in range(y0, y1):
        for x in range(0, vram.W, 2):
            px[(x, y)], px[(x + 1, y)] = struct.unpack("<HH", r[(phase, "%d_%d" % (x, y))])
    return px


def run(pro, entry, mode, tag, slow=False):
    """Type one entry from a fresh boot; snapshot before ENTER, then ENTER (and CLEAR for steps( ) and snapshot again.
    -> (typed text, edit-buffer tokens or None, calls, lowest byte, entry SP, home text)"""
    top = TOP[mode]
    r = read2((pro + K(entry, slow) + " . . ", [("ts", TS, screen.COLS * screen.ROWS), ("p", 0xD02437, 12), ("buf", top, 120)] + vrows(22, 112)),
              (" enter . . . . ", [("ts", TS, screen.COLS * screen.ROWS)] + vrows(22, 240)),                   # the answer, error or steps list
              ("clear . . " if entry.startswith("STEPS") else "", [("stk", REC, 36), ("ts", TS, screen.COLS * screen.ROWS)]))
    vram.png(px_of(r, "a", 22, 112), "%s/%s_pre.png" % (OUT, tag))
    vram.png(px_of(r, "b", 22, 240), "%s/%s_res.png" % (OUT, tag))
    p = r[("a", "p")]
    t0, cur = (int.from_bytes(p[i:i + 3], "little") for i in (0, 3))
    buf = r[("a", "buf")][:cur - t0] if t0 == top and 0 < cur - t0 <= 120 else None   # gap buffer, nothing after the cursor
    b = r[("c", "stk")]
    n = lambda o: b[o] | b[o + 1] << 8 | b[o + 2] << 16
    calls, low, sp = (b[3], n(4), n(31)) if b[:3] == b"STK" else (0, 0, 0)
    return text(r[("a", "ts")]), buf, calls, low, sp, text(r[("b", "ts")]) + "\n=== after CLEAR:\n" + text(r[("c", "ts")])


def main():
    only = sys.argv[1:]
    todo = [c for c in CASES if not only or c[0] in only]
    os.makedirs(OUT, exist_ok=True)
    rows = []
    for name, entry, mp in todo:
        ref = None                                  # Classic's edit-buffer bytes: MathPrint must hold the same tokens
        for mode, pro in (("Classic", CLASSIC), ("MathPrint", MATHPRINT))[:1 + mp]:
            for attempt in range(1, TRIES + 1):         # keys are sometimes dropped: type again, and only a verified entry is a measurement
                t, buf, calls, low, sp, home = run(pro, entry, mode, "%s_%s" % (name, mode), attempt % 2 == 0)   # even attempts wait around ALPHA
                if mode == "Classic":
                    ok = buf is not None and norm(entry.replace("e^(", ".^(").replace("\u2264", ".").replace("[", ".").replace("\u03c0", ".").replace("\u1d07", ".").replace("\u221a", ".")) in norm(t)   # the home font draws e, <=, [ as .
                else:
                    ok = buf is not None and buf == ref
                if ok: break
                print("   %s %s: typed wrong (attempt %d): %r" % (name, mode, attempt, t.split("\n", 1)[-1] if mode == "Classic" else buf and buf.hex()), flush=True)
            if mode == "Classic": ref = buf
            used, left = sp - low, low - STACKBOT
            flag = "" if ok else "  INVALID ENTRY"
            if calls != 1: flag += "  %d engine calls" % calls
            if left < LEFT_MIN: flag += "  < %d LEFT" % LEFT_MIN
            if calls and low <= PAINTBOT: flag += "  DEEPER THAN THE PAINT (left <= %d)" % (PAINTBOT - STACKBOT)
            print("%-22s %-9s entry=%s sp=%06X low=%06X used=%4d left=%4d%s" % (name, mode, "ok" if ok else "BAD", sp, low, used, left, flag), flush=True)
            if not ok: print("   typed text: %r  buf %s ref %s" % (t, buf and buf.hex(), ref and ref.hex()))
            print("   home: %r" % home.replace("\n", " | ")[-160:], flush=True)
            rows.append((name, mode, used, left, flag, ok))
    if rows:
        w = min((r for r in rows if r[5]), key=lambda r: r[3], default=None)
        if w: print("worst valid: %s %s used %d, left %d" % (w[0], w[1], w[2], w[3]))
    sys.exit(1 if any(r[4] for r in rows) else 0)


main()
