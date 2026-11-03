#!/usr/bin/env python3
"""steps( on the real OS: the worked solution over the home screen after the answer.

    python3 steps.py [OUTDIR]

Types steps(solve(2X+3=7,X)) in MathPrint and steps(deriv(X^2*sin(X),X)) in Classic, takes the
VRAM below the status bar as a PNG (OUTDIR, default the scratchpad) for EVERY page (DOWN until the
screen stops changing: OUTDIR/<case>_p<n>.png, a contact sheet per case in ../sheets, the answer box
asserted on the last page), then checks CLEAR (and ALPHA)
give the home screen back with the answer on it and a later entry still answered, DOWN and UP scroll
a long solution, and steps( around something without steps says so. Home is read through textShadow
(Classic) or compared by CRC of the band under the status bar.
"""
import json, os, re, struct, subprocess, sys
import cmdkeys, crcread, e2e, screen, vram

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.environ.get("TMPDIR", "/tmp"), "steps")
SHEETS = os.path.join(os.path.dirname(OUT.rstrip("/")), "sheets")
os.makedirs(OUT, exist_ok=True)
os.makedirs(SHEETS, exist_ok=True)
CLASSIC = e2e.PROLOGUE
MATHPRINT = e2e.PROLOGUE.replace("C . ", "", 1)
BAND = ("vr", vram.VRAM + 30 * 640, 210 * 640)

SOLVE = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("SOLVE") + " 2 xton + 3 2nd math 1 7 , xton ) ) ")
DERIV = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("DERIV") + " xton ^ 2 * sin xton ) , xton ) ) ")
LONG = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("DERIV") + " xton ^ 5 + 2 xton ^ 4 + 3 xton ^ 3 + 4 xton ^ 2 + 5 xton + 6 , xton ) ) ")
NONE = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("EXPAND") + " xton ^ 2 + 2 xton ) ) ")
QUAD = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("SOLVE") + " xton ^ 2 - 5 xton + 6 2nd math 1 0 , xton ) ) ")
FACT = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("FACTOR") + " 2 xton ^ 3 - 8 xton ) ) ")
INTG = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("INTEGRAL") + " xton ^ 2 , xton , 0 , 2 ) ) ")
LIM = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("LIMIT") + " xton ^ 2 + 1 , xton , 3 ) ) ")
NEST = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("DERIV") + " " + cmdkeys.spell("INTEGRAL") + " xton , xton ) , xton ) ) ")
NOSOL = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("SOLVE") + " xton ^ 2 + 1 2nd math 1 0 , xton ) ) ")
LONGP = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("SOLVE") + " " + cmdkeys.spell("INTEGRAL") +
         " xton ^ 8 + 3 xton ^ 6 + 5 xton ^ 4 + 7 xton ^ 3 + 9 xton ^ 2 , xton , 0 , 2 ) 2nd math 1 0 xton , xton ) ) ")
RAD = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("SOLVE") + " 2nd ^2 xton + 1 ) 2nd math 1 3 , xton ) ) ")
SYS = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("SOLVE") + " 2nd ( xton + alpha 1 2nd math 1 3 , xton - alpha 1 2nd math 1 1 2nd ) , "
       "2nd ( xton , alpha 1 2nd ) , ) ) ").replace(", ) )", ") )")
PARTS = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("INTEGRAL") + " xton * sin xton ) , xton ) ) ")
USUB = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("INTEGRAL") + " 2nd ^2 2 xton + 1 ) , xton , 0 , 4 ) ) ")
ATAN = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("INTEGRAL") + " 1 / ( xton ^ 2 + 4 ) , xton ) ) ")
ASIN = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("INTEGRAL") + " 1 / 2nd ^2 4 - xton ^ 2 ) , xton ) ) ")
PIINT = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("INTEGRAL") + " sin xton ) , xton , 0 , 2nd ^ ) ) ")   # 2nd ^ is pi
PISIN = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("INTEGRAL") + " 1 / xton , xton , 1 , 2nd / ) ) ")   # 2nd / is e
PFRAC = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("INTEGRAL") + " xton / ( xton + 1 ) , xton ) ) ")
LIMINF = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("LIMIT") + " ( 3 xton ^ 2 + 1 ) / ( xton ^ 2 - 5 ) , xton , 2nd , 9 9 ) ) ")
LIMONE = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("LIMIT") + " ( 1 + 1 / xton ) ^ xton , xton , 2nd , 9 9 ) ) ")
SUMGEO = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("SUM") + " 5 ( 9 / 1 0 ) ^ xton , xton , 0 , 2nd , 9 9 ) ) ")
SUMTEL = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("SUM") + " 1 / ( xton ( xton + 1 ) ) , xton , 1 , 2nd , 9 9 ) ) ")
DSOL = (cmdkeys.spell("STEPS") + " " + cmdkeys.spell("DESOLVE") + " alpha 1 2nd apps 2 w400 2nd math 1 w400 alpha 1 ^ 2 2nd math right 1 w400 alpha 1 ( 0 ) 2nd math 1 w400 1 , xton , alpha 1 ) ) ")
fails = 0


def check(ok, what):
    global fails
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    fails += not ok


def home(pro, keys):
    """-> (text rows of the home screen, textShadow), Classic only"""
    r = screen.read([("txt", screen.TEXTSHADOW, screen.COLS * screen.ROWS)], keys=pro + keys, step=1,
                    files=e2e.FILES, launch=e2e.LAUNCH, lead=False, settle=1900, crcs=[BAND])
    return r["vr"], "\n".join(screen.render(r["txt"]))


def _seq(keys):
    """screen.read's key syntax -> autotester steps (. = 2 s, wNNN = NNN ms, C classic, L launch)"""
    seq = ["delay|1500"]
    for k in keys.split():
        if k == ".":
            seq += ["delay|2000"]
        elif k[0] == "w" and k[1:].isdigit():
            seq += ["delay|" + k[1:]]
        elif k == "L":
            seq += ["action|launch", "delay|10000"]    # the packed install (ZX0 + flash write) takes longer than 4 s: keys typed during it are lost
        elif k == "C":
            seq += ["action|useClassic", "delay|1500"]
        else:
            seq += ["key|" + k, "delay|350"]
    return seq


def _run(pro, keys, tail):
    """tail(seq, hashes) adds the steps after the keys; -> (CRCs by name in order, stdout)"""
    seq, hashes = _seq(pro + keys) + ["delay|1900"], {}
    tail(seq, hashes)
    cfg = {"transfer_files": list(e2e.FILES), "target": {"name": e2e.LAUNCH, "isASM": False},
           "delay_after_step": 1, "sequence": seq, "hashes": hashes}
    path = os.path.join(screen.HERE, "_steps.json")
    json.dump(cfg, open(path, "w"))
    r = subprocess.run([screen.TESTER, path], capture_output=True, text=True, env=dict(os.environ, AUTOTESTER_ROM=screen.ROM), cwd=screen.HERE)
    if "unknown key" in r.stderr:
        sys.exit("BAD KEY:\n" + r.stderr)
    return re.findall(r'Hash #\S+ \("([^"]*)"\).*?got ([0-9A-F]{1,8})\)', r.stdout)


def _hash(seq, hashes, name, addr, n):
    seq.append("hash|" + name)
    hashes[name] = {"description": name, "start": hex(addr), "size": str(n), "expected_CRCs": ["00000000"]}


DOWN = ["key|down", "delay|700"]
GBOX = 0xDFFB                       # the answer box's green


def pages(pro, keys):
    """Every page of the steps screen: press DOWN until it stops changing (30 at most).
    One run takes the band's CRC after each press, a second reads the pixels of each page."""
    def crcs(seq, hashes):
        for i in range(31):
            _hash(seq, hashes, "c%d" % i, BAND[1], BAND[2])
            seq += DOWN
    got = dict(_run(pro, keys, crcs))
    c = [got.get("c%d" % i) for i in range(31)]
    n = 1
    while n < 31 and c[n] and c[n] != c[n - 1]:
        n += 1
    if n > 30:
        check(False, "a steps screen of more than 30 pages: " + keys[:60])
        n = 30
    names = [(x, y) for y in range(30, 240) for x in range(0, 320, 2)]
    def pix(seq, hashes):
        for i in range(n):
            for x, y in names:
                _hash(seq, hashes, "%d_%d_%d" % (i, x, y), vram.VRAM + 2 * (y * 320 + x), 4)
            seq += DOWN
    got = _run(pro, keys, pix)
    if len(got) < n * len(names):
        sys.stderr.write("WARNING: emulator stopped after %d/%d windows\n" % (len(got), n * len(names)))
    d = dict(got)
    out = []
    for i in range(n):
        px = {}
        for x, y in names:
            v = d.get("%d_%d_%d" % (i, x, y))
            px[(x, y)], px[(x + 1, y)] = struct.unpack("<HH", crcread.invert(int(v, 16), 4)) if v else (0xFFFF, 0xFFFF)
        out.append(px)
    return out


def sheet(name, pxs):
    """the pages of one case side by side, 3 to a row, in OUT/../sheets"""
    try:
        from PIL import Image
    except ImportError:
        return
    ims = []
    for px in pxs:
        im = Image.new("RGB", (320, 210))
        for (x, y), v in px.items():
            r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
            im.putpixel((x, y - 30), ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
        ims.append(im)
    rows = (len(ims) + 2) // 3
    sh = Image.new("RGB", (3 * 324 + 4, rows * 214 + 4), (120, 120, 120))
    for i, im in enumerate(ims):
        sh.paste(im, (4 + i % 3 * 324, 4 + i // 3 * 214))
    sh.save(os.path.join(SHEETS, name + ".png"))


def shot(pro, keys, name, ans=True, scroll=True):
    """every page of the steps screen as OUT/<case>_p<n>.png (n from 1), a contact sheet, and the
    first page as text; ans: the last page shows the green answer box. scroll=False: just the one screen"""
    base = name[:-4]
    if scroll:
        pxs = pages(pro, keys)
    else:
        px, r = vram.grab(pro + keys, 30, 240, 1, 1, files=e2e.FILES, launch=e2e.LAUNCH, lead=False, settle=1900)
        pxs = [px]
    for i, px in enumerate(pxs):
        vram.png(px, os.path.join(OUT, "%s_p%d.png" % (base, i + 1)), color=True)
    sheet(base, pxs)
    if scroll and ans:
        check(sum(1 for v in pxs[-1].values() if v == GBOX) > 500, "%s: the last of %d pages shows the answer box" % (base, len(pxs)))
    return vram.ascii(pxs[0])


# stepsalg: one case per ALGEBRA / TRIG command, a screenshot of the steps and the answer read back after CLEAR
def st(name, body):
    return cmdkeys.spell("STEPS") + " " + (cmdkeys.spell(name) if name else "") + body + " ) "
EQ = " 2nd math 1 "
CASES = (
    ("expand", "EXPAND", " ( xton + 1 ) ( xton - 2 ) )", "X"),
    ("expand_pow", "EXPAND", " ( xton + 1 ) ^ 3 )", "3X"),
    ("comdenom", "COMDENOM", " 1 / xton + 1 / ( xton + 1 ) )", "X"),
    ("partfrac", "PARTFRAC", " 1 / ( xton ^ 2 - 1 ) , xton )", "X"),
    ("polyquotient", "POLYQUOTIENT", " xton ^ 3 + 2 xton + 1 , xton - 1 , xton )", "X"),
    ("polyremainder", "POLYREMAINDER", " xton ^ 3 + 2 xton + 1 , xton - 1 , xton )", "4"),
    ("polygcd", "POLYGCD", " xton ^ 2 - 1 , xton ^ 2 + 2 xton + 1 , xton )", "X+1"),
    ("polydegree", "POLYDEGREE", " ( xton + 1 ) ^ 3 , xton )", "3"),
    ("polycoeffs", "POLYCOEFFS", " xton ^ 3 + 2 xton , xton )", "1,0,2,0"),
    ("left", "LEFT", " xton + 1" + EQ + "3 )", "X+1"),
    ("right", "RIGHT", " xton + 1" + EQ + "3 )", "3"),
    ("nroots", "NROOTS", " xton ^ 2 - 2 , xton )", "1.414"),
    ("polyroots", "POLYROOTS", " xton ^ 2 - 1 , xton )", "1"),
    ("cpolyroots", "CPOLYROOTS", " xton ^ 2 + 1 , xton )", ","),
    ("czeros", "CZEROS", " xton ^ 2 + 4 , xton )", "2"),
    ("csolve", "CSOLVE", " xton ^ 2 + 2 xton + 5" + EQ + "0 , xton )", "orX="),
    ("cfactor", "CFACTOR", " xton ^ 2 + xton + 1 )", "2X+1"),
    ("tolnlog", "TOLN", " log xton ) )", "ln"),
    ("tologbase", "TOLOGBASE", " ln xton ) , 2 )", "2"),
    ("toexp", "TOEXP", " sin xton ) )", "e"),
    ("tosin", "TOSIN", " cos xton ) ^ 2 )", "sin"),
    ("tocos", "TOCOS", " sin xton ) ^ 2 )", "cos"),
    ("texpand", "TEXPAND", " sin 2 xton ) )", "sin"),
    ("texpand_sum", "TEXPAND", " sin xton + alpha 1 ) )", "cos"),
    ("tcollect", "TCOLLECT", " sin xton ) ^ 2 )", "cos"),
    ("solve_quad2", "SOLVE", " 2 xton ^ 2 + 7 xton + 3" + EQ + "0 , xton )", "orX="),
    ("solve_cubic", "SOLVE", " xton ^ 3 - 6 xton ^ 2 + 1 1 xton - 6" + EQ + "0 , xton )", "X=3"),
    ("solve_formula", "SOLVE", " xton ^ 2 - 2 xton - 1" + EQ + "0 , xton )", "X="),
    ("factor_quad", "FACTOR", " xton ^ 2 + 3 xton + 1 )", "X"),
    ("factor_group", "FACTOR", " 6 xton ^ 2 + xton - 1 )", "3X"),
    ("solve_ineq", "SOLVE", " 2 xton + 3 2nd math 5 7 , xton )", "X<2"),
    ("solve_ineq2", "SOLVE", " xton ^ 2 - 4 2nd math 3 0 , xton )", "X>2"),
    ("solve_abs", "SOLVE", " math right 1 xton - 1 ) " + EQ.strip() + " 3 , xton )", "X=4"),
    ("solve_rational", "SOLVE", " 1 / xton" + EQ + "2 , xton )", "X=1/2"),
    ("solve_exp", "SOLVE", " 2nd ln 2 xton ) - 3 2nd ln xton ) + 2" + EQ + "0 , xton )", "ln(2)"),
    ("solve_log", "SOLVE", " log xton ) + log xton - 3 )" + EQ + "1 , xton )", "X=5"),
    ("solve_trig", "SOLVE", " 2 sin xton ) " + EQ.strip() + " 1 , xton )", "orX="),
    ("solve_trig_tan", "SOLVE", " tan xton ) " + EQ.strip() + " 1 , xton )", "N"),
    ("solve_lnraise", "SOLVE", " ln 2 xton + 1 )" + EQ + "3 , xton )", "e"),
)
ONLY = sys.argv[2:]          # names to run alone: python3 steps.py OUTDIR name ...
for name, cmd, body, want in CASES:
    if ONLY and name not in ONLY:
        continue
    keys = st(cmd, body)
    shot(CLASSIC, keys + "enter . .", "alg_" + name + ".png")
    _, txt_c = home(CLASSIC, keys + "enter . . clear . .")
    check(want.replace(" ", "") in txt_c.replace(" ", ""), "steps(%s) answers (%r)" % (name, txt_c.strip()[-40:]))
if ONLY:
    print("ALL PASS" if not fails else "%d FAILED" % fails)
    sys.exit(1 if fails else 0)

shot(MATHPRINT, SOLVE + "enter . .", "solve_mathprint.png")
shot(CLASSIC, DERIV + "enter . .", "deriv_classic.png")
shot(MATHPRINT, SOLVE + "enter . . clear . .", "solve_mathprint_after_clear.png", scroll=False)
shot(CLASSIC, DERIV + "enter . . clear . .", "deriv_classic_after_clear.png", scroll=False)
vr_steps, txt_steps = home(CLASSIC, SOLVE + "enter . .")
vr_clear, txt_clear = home(CLASSIC, SOLVE + "enter . . clear . .")
check(vr_clear != vr_steps, "CLEAR leaves the steps screen")
check("X=2" in txt_clear, "CLEAR: the answer is on the home screen (%r)" % txt_clear.split("\n")[-3:])
vr_alpha, txt_alpha = home(CLASSIC, SOLVE + "enter . . alpha . .")
check(vr_alpha == vr_clear, "ALPHA leaves it the same way as CLEAR")
_, txt_next = home(CLASSIC, SOLVE + "enter . . clear . . 1 + 1 enter . .")
check([l for l in txt_next.split("\n") if l.strip(" .")][-1].strip(" .") == "2", "a later entry is answered after the steps (%r)" % txt_next.strip()[-30:])
vr_a, _ = home(CLASSIC, LONG + "enter . .")
vr_d, _ = home(CLASSIC, LONG + "enter . . down . .")
vr_u, _ = home(CLASSIC, LONG + "enter . . down . . up . .")
check(vr_d != vr_a, "DOWN scrolls a long solution")
check(vr_u == vr_a, "UP scrolls it back")
vr_n, txt_n = home(CLASSIC, NONE + "enter . . clear . .")
check("2X" in txt_n, "steps(EXPAND(...)) still answers (%r)" % txt_n.strip()[-30:])
shot(CLASSIC, NONE + "enter . .", "none_classic.png")
shot(CLASSIC, QUAD + "enter . .", "quad_classic.png")
shot(CLASSIC, FACT + "enter . .", "factor_classic.png")
_, txt_q = home(CLASSIC, QUAD + "enter . . clear . .")
check("X=2" in txt_q.replace(" ", "") and "X=3" in txt_q.replace(" ", ""), "steps(SOLVE quadratic) answers (%r)" % txt_q.strip()[-40:])
_, txt_f = home(CLASSIC, FACT + "enter . . clear . .")
check("X-2" in txt_f.replace(" ", ""), "steps(FACTOR) answers (%r)" % txt_f.strip()[-40:])
shot(CLASSIC, INTG + "enter . .", "integral_classic.png")
shot(CLASSIC, LIM + "enter . .", "limit_classic.png")
shot(CLASSIC, NEST + "enter . .", "nested_classic.png")
_, txt_i = home(CLASSIC, INTG + "enter . . clear . .")
check("8/3" in txt_i.replace(" ", ""), "steps(INTEGRAL) answers (%r)" % txt_i.strip()[-40:])
_, txt_l = home(CLASSIC, LIM + "enter . . clear . .")
check("10" in txt_l.replace(" ", ""), "steps(LIMIT) answers (%r)" % txt_l.strip()[-40:])
_, txt_x = home(CLASSIC, NEST + "enter . . clear . .")
check("X" in txt_x.replace(" ", ""), "steps(nested) answers (%r)" % txt_x.strip()[-40:])
px, _ = vram.grab(CLASSIC + DERIV + "enter . .", 50, 66, 1, 1, files=e2e.FILES, launch=e2e.LAUNCH, lead=False, settle=1900)
bx = [x for (x, y), v in px.items() if v == 0x001F]
check(bx and max(bx) < 80, "the command name on the entry line is blue (%d blue pixels, x %s..%s)" % (len(bx), min(bx or [0]), max(bx or [0])))
a1 = shot(CLASSIC, NOSOL + "enter . .", "nosol_classic.png")
a2 = shot(CLASSIC, NOSOL + "enter . . clear . .", "nosol_after_clear.png", scroll=False)
check("Discriminant" in a1 or a1 != a2, "steps( with no real roots shows the steps, then the error screen")
shot(CLASSIC, LONGP + "enter . .", "longparam_classic.png", ans=False)
_, txt_lp = home(CLASSIC, LONGP + "enter . . clear . .")
check(txt_lp.strip() != "", "a long First parameter: the steps screen came back to home without a crash (%r)" % txt_lp.strip()[-40:])
for name, keys, want in (("radical", RAD, "X=8"), ("system", SYS, "X=2"), ("parts", PARTS, "sin(X)"), ("usub", USUB, "26/3"),
                         ("pfrac", PFRAC, "ln"), ("desolve", DSOL, "Y="), ("atan", ATAN, "X/2"), ("asin", ASIN, "X/2"), ("ln e", PISIN, "1"), ("pi limit", PIINT, "2"),
                         ("limit inf", LIMINF, "3"), ("limit 1inf", LIMONE, "e"), ("sum geo", SUMGEO, "50"), ("sum tel", SUMTEL, "1")):
    shot(CLASSIC, keys + "enter . .", name + "_classic.png")
    _, txt_w = home(CLASSIC, keys + "enter . . clear . .")
    check(want.replace(" ", "") in txt_w.replace(" ", ""), "steps(%s) answers (%r)" % (name, txt_w.strip()[-40:]))
# stepsgeo: GEOMETRY and MATRIX under steps( : a shot of each, and the answer still on home after CLEAR
LB, RB = "2nd * ", "2nd - "
P = lambda x, y: "( %s , %s ) " % (x, y)
VEC = lambda *e: LB + " , ".join(map(str, e)) + " " + RB
MAT = lambda *rows: LB + " ".join(LB + " , ".join(map(str, r)) + " " + RB for r in rows) + RB
def st(name, args):
    return cmdkeys.spell("STEPS") + " " + cmdkeys.spell(name) + " " + re.sub(r"(?<=\d)(?=\d)", " ", args) + " ) ) "
seen = []
for name, args, want in (
        ("DISTANCE", P(1, 2) + ", " + P(4, 6), "5"), ("SLOPE", P(1, 2) + ", " + P(4, 8), "2"),
        ("LINE", P(1, 2) + ", " + P(3, 8), "Y=3X-1"), ("PERPBISECTOR", P(1, 2) + ", " + P(5, 6), "Y=7-X"),
        ("MIDPOINT", P(1, 2) + ", " + P(5, 6), "(3,4)"), ("PARTITION", P(1, 2) + ", " + P(6, 12) + ", 2 , 3", "(3,6)"),
        ("AREA", P(0, 0) + ", " + P(4, 0) + ", " + P(4, 3), "6"), ("PERIMETER", P(0, 0) + ", " + P(4, 0) + ", " + P(4, 3), "12"),
        ("CENTROID", P(0, 0) + ", " + P(6, 0) + ", " + P(0, 9), "(2,3)"),
        ("CIRCUMCENTER", P(0, 0) + ", " + P(4, 0) + ", " + P(0, 3), "(2,3/2)"),
        ("ORTHOCENTER", P(0, 0) + ", " + P(4, 0) + ", " + P(0, 3), "(0,0)"),
        ("INCENTER", P(0, 0) + ", " + P(4, 0) + ", " + P(0, 3), "(1,1)"),
        ("HYPOT", "3 , 4", "5"), ("LEG", "5 , 3", "4"),
        ("CIRCLE", P(1, 2) + ", 3", "(X-1)"), ("CIRCLE", P(0, 0) + ", " + P(4, 0) + ", " + P(0, 3), "25/4"),
        ("ROTATE", P(1, 0) + ", 90", "(0,1)"), ("DILATE", P(1, 2) + ", 3", "(3,6)"),
        ("MDET", MAT((2, 0, 1), (1, 3, 2), (1, 1, 1)), "0"), ("MINV", MAT((1, 2), (3, 4)), "3/2"),
        ("MTRANS", MAT((1, 2, 3), (4, 5, 6)), "1,4"), ("MRREF", MAT((3, 1, 2), (1, 4, 1), (2, 1, 5)), "1,0,0"),
        ("DOTP", VEC(1, 2, 3) + ", " + VEC(4, 5, 6), "32"), ("CROSSP", VEC(1, 2, 3) + ", " + VEC(4, 5, 6), "3,6,"),
        ("NORM", VEC(3, 4), "5")):
    keys = st(name, args) + "enter . ."
    tag = "%s_%d" % (name.lower(), sum(1 for k in seen if k == name))
    seen.append(name)
    shot(CLASSIC, keys, tag + "_classic.png")
    _, txt_g = home(CLASSIC, keys + " clear . .")
    check(want.replace(" ", "") in txt_g.replace(" ", ""), "steps(%s) answers (%r)" % (name, txt_g.strip()[-40:]))
shot(MATHPRINT, st("MINV", MAT((1, 2), (3, 4))) + "enter . .", "minv_mathprint.png")
MT26 = st("MTRANS", MAT((1234, 5678, 9012, 3456, 7890, 1234), (12, 34, 56, 78, 90, 12)))
shot(CLASSIC, MT26, "mtrans26_entry.png", ans=False, scroll=False)        # the entry line, before ENTER
shot(MATHPRINT, MT26 + "enter . .", "mtrans26_mathprint.png")   # Classic answers TOO WIDE for this 6x2 (engine.c width check)
shot(MATHPRINT, st("CIRCLE", P(0, 0) + ", " + P(4, 0) + ", " + P(0, 3)) + "enter . .", "circle3_mathprint.png")
shot(CLASSIC, st("MRREF", MAT((3, 1, 2), (1, 4, 1), (2, 1, 5))) + "enter . . down . . down . .", "mrref_scrolled.png")
# stepscalc: one case per calculus command (answer after CLEAR, and a steps screenshot each)
def calc(name, args):
    return cmdkeys.spell("STEPS") + " " + cmdkeys.spell(name) + " " + args + " ) ) "
CALC = (
    ("deriv_at", calc("DERIVAT", "xton ^ 3 , xton , 2"), "12"),
    ("dominant", calc("DOMINANTTERM", "sin xton ) / xton ^ 2 , xton"), "1/X"),
    ("tangent", calc("TANGENTLINE", "xton ^ 2 , xton , 1"), "2X-1"),
    ("normal", calc("NORMALLINE", "xton ^ 2 , xton , 1"), "3/2"),
    ("central", calc("CENTRALDIFF", "xton ^ 3 , xton , 2"), "3X"),
    ("taylor", calc("TAYLOR", "sin xton ) , xton , 3"), "X"),
    ("series", calc("SERIES", "1 / sin xton ) , xton , 3"), "1/X"),
    ("impdif", calc("IMPDIF", "xton ^ 2 + alpha 1 ^ 2 2nd math 1 w400 2 5 , xton , alpha 1"), "X/Y"),
    ("sum", calc("SUM", "xton ^ 2 , xton , 1 , 4"), "30"),
    ("product", calc("PRODUCT", "( xton + 1 ) , xton , 1 , 4"), "120"),
    ("fmin", calc("FMIN", "xton ^ 2 - 4 xton , xton"), "X=2"),
    ("fmax", calc("FMAX", "3 xton - xton ^ 3 , xton , 0 , 3"), "X=1"),
    ("arclen", calc("ARCLEN", "3 xton , xton , 0 , 1"), "10"),
    ("desolve_lin", calc("DESOLVE", "alpha 1 2nd apps 2 w400 2nd math 1 w400 2 alpha 1 2nd math right 1 w400 alpha 1 ( 0 ) 2nd math 1 w400 3 , xton , alpha 1"), "3"),
    ("desolve_ic2", calc("DESOLVE", "alpha 1 2nd apps 2 w400 2nd apps 2 w400 - 3 alpha 1 2nd apps 2 w400 + 2 alpha 1 2nd math 1 w400 0 w400 2nd math right 1 w400 w400 alpha 1 ( 0 ) 2nd math 1 w400 1 w400 2nd math right 1 w400 w400 alpha 1 2nd apps 2 w400 ( 0 ) 2nd math 1 w400 0 , xton , alpha 1"), "Y=2"),
    # gaps-de: integrating factor, tan(X+C), an explicit separable Y
    ("desolve_if", calc("DESOLVE", "xton w400 alpha w400 1 2nd apps 2 w400 - alpha 1 2nd math 1 w400 xton ^ 2 , xton , w400 alpha w400 1"), "CX"),
    ("desolve_tan", calc("DESOLVE", "alpha 1 2nd apps 2 w400 2nd math 1 w400 1 + alpha 1 ^ 2 , xton , w400 alpha w400 1"), "tan(X+C)"),
    ("desolve_sep", calc("DESOLVE", "xton alpha 1 2nd apps 2 w400 2nd math 1 w400 2 alpha 1 , xton , alpha 1"), "CX"),
)
for name, keys, want in CALC:
    if name in ("impdif", "series"):                    # MathPrint too: the 2D entry must type and answer
        shot(MATHPRINT, keys.replace("^ 2 +", "^ 2 right +").replace("^ 2 2nd math", "^ 2 right 2nd math") + "enter . .", name + "_mathprint.png")

    shot(CLASSIC, keys + "enter . .", name + "_classic.png")
    _, txt_w = home(CLASSIC, keys + "enter . . clear . .")
    check(want.replace(" ", "") in txt_w.replace(" ", ""), "steps(%s) answers (%r)" % (name, txt_w.strip()[-40:]))
# DOWN and UP are symmetric: on MRREF 3x3, 3 DOWN then 3 UP is page 1 again, and DOWN UP DOWN is DOWN
UP = ["key|up", "delay|700"]
def ud(seq, hashes):
    for i, mv in enumerate([None, DOWN, DOWN, DOWN, UP, UP, UP, DOWN, UP, DOWN]):
        if mv:
            seq += mv
        _hash(seq, hashes, "u%d" % i, BAND[1], BAND[2])
g = dict(_run(CLASSIC, st("MRREF", MAT((3, 1, 2), (1, 4, 1), (2, 1, 5))) + "enter . .", ud))
u = [g.get("u%d" % i) for i in range(10)]
check(all(u) and u[0] != u[1] and u[1] != u[2] and u[6] == u[0] and u[8] == u[0] and u[9] == u[1], "MRREF: DOWN x3 then UP x3 is page 1 again, DOWN UP DOWN is DOWN (%s)" % u)
# the content rules, in MathPrint (right leaves each exponent)
for name, keys in (
        ("factor_sq2", calc("FACTOR", "xton ^ 2 right - alpha 1 ^ 2 right")),
        ("factor_quartic", calc("FACTOR", "xton ^ 4 right + 4")),
        ("limit_ln", calc("LIMIT", "ln 1 + xton ) / xton right , xton , 0")),
        ("integral_sin2cos", calc("INTEGRAL", "sin xton ) ^ 2 right * cos xton ) , xton")),
        ("expand_pow5", calc("EXPAND", "( xton + 1 ) ^ 5 right"))):
    shot(MATHPRINT, keys + "enter . .", name + "_mathprint.png")
# complex roots with a root in them (Y''+2Y=0; the e^(-X)(...) ones are TOO WIDE in both modes): a shot, and the answer still on home after CLEAR
CPLX = calc("DESOLVE", "alpha 1 2nd apps 2 w400 2nd apps 2 w400 + 2 alpha 1 2nd math 1 w400 0 , xton , alpha 1")
shot(MATHPRINT, CPLX + "enter . .", "desolve_cplx_mathprint.png")
shot(MATHPRINT, CPLX + "enter . . down . . down . .", "desolve_cplx_scrolled.png")
print("ALL PASS" if not fails else "%d FAILED" % fails)
sys.exit(1 if fails else 0)
