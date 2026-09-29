#!/usr/bin/env python3
"""Runs the ASSEMBLED hook body under tools/ez80sim.py -- no ROM needed.

This owns what the engine tests cannot see: the event gates, the pending mark
that ties an A=0 to its ENTER, the engine call itself, the A=0 display hand-off,
insert mode, and what happens when the pointers are nonsense. The arithmetic is
owned by tools/engine_check.py, which diffs engine.c against sympy.

One ENTER is three hook calls. A=1 is the key: the hook only decides whether
this ENTER is a candidate and marks it pending; it never touches the entry,
which is what the OS echoes and what 2nd ENTRY recalls. A=2 is the OS about to
evaluate: the hook runs the engine over the entry as the OS flattened it (the
temp program #), keeps any answer, and answers NZ so the OS never evaluates it.
A=0 is the OS asking who displays the result: the hook hands the answer to the
OS's own result routine as a REAL and answers NZ so the OS draws nothing else.
The engine here is the host build of the same engine.c, behind a stub at the
address the body jumps to.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ez80sim
from ez80sim import load_hook, run_hook, evaluate, display, drawn_text, os_tokens, Trap, Sim

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BODY = load_hook(os.path.join(ROOT, 'symce/obj/hook/hook.bin'),
                 os.path.join(ROOT, 'symce/bin/SYMCE1.8xv'))
print("hook body: %d bytes, magic %02x\n" % (len(BODY), BODY[0]))

fails = []


def h(s):
    return bytes.fromhex(s.replace(' ', ''))


# The answer is a token string: the OS result routine files tokens and draws
# each as its glyph. Measured -- its own -3 is filed as B0 33.
GLYPH = {0xB0: '-', 0x71: '-', 0x70: '+', 0x0D: '²', 0x0F: '³',
         0xF0: '^', 0x83: '/', 0x5B: 'θ'}


def render(b):
    return ''.join(GLYPH.get(c, chr(c) if 0x30 <= c <= 0x5A else '[%02X]' % c)
                   for c in b)


def classic_cols(t):
    """BC: the columns Classic draws (measured): sqrt( 2; " or ", sin(, tan(,
    log( 4; ln(, e^( 3; logBASE( 8; anything else 1, a two-byte letter too."""
    w = k = 0
    while k < len(t):
        w += {0xBC: 2, 0x3C: 4, 0xC2: 4, 0xC6: 4, 0xC0: 4, 0xBE: 3, 0xBF: 3, 0xEF: 8}.get(t[k], 1)
        k += 2 if t[k] in (0xBB, 0xEF) else 1
    return w


def fail(name, why):
    print("FAIL %-24s %s" % (name, why))
    fails.append(name)
    return False


def check_display(name, s, want):
    """A=2 then A=0. want is the answer the OS must be handed, or None."""
    marked = s.r8(ez80sim.PENDING) == 0xFF
    try:
        skip = evaluate(s)
        kept = bytes(s.r8(ez80sim.LAST_ANS + i) for i in range(len(want or b'') + 1))
        call, nz = display(s)
    except Trap as ex:
        return fail(name, "%s" % ex)
    if s.r8(ez80sim.PENDING):
        return fail(name, "A=0 left the pending mark set")
    if want is None:
        if call or nz or skip:
            return fail(name, "%s with nothing to show"
                        % ("A=0 called the OS" if call else "returned NZ"))
        # The OS shows its own result, so that is Ans now: a copy of OP1.
        kept = bytes(s.r8(ez80sim.LAST_ANS + i) for i in range(10))
        op1 = bytes(s.r8(ez80sim.OP1 + i) for i in range(9))
        if marked and kept != b'\xff' + op1:
            return fail(name, "the OS showed its result; lastAns holds %s, not OP1"
                        % kept.hex())
        return True
    if not marked:
        return fail(name, "the ENTER was never marked")
    if s.engine_calls != [(ez80sim.ENTRY_AT, len(s.entry), ez80sim.ANS_BUF,
                           ez80sim.ENGINE_WORK, ez80sim.LAST_ANS,
                           s.r8(ez80sim.MP_FLAGS) & 0x20 | s.r8(ez80sim.ANS_FLAGS) & 1 |
                           s.r8(ez80sim.IY_OS) & 4)]:
        return fail(name, "engine called as %r" % s.engine_calls)
    if not skip:
        return fail(name, "A=2 returned Z, so the OS would evaluate it (X=0: ERR:DIVIDE BY 0)")
    if kept != bytes([len(want)]) + want:
        return fail(name, "after A=2 lastAns holds %s, want the answer" % kept.hex())
    if call is None:
        return fail(name, "A=0 never called the OS")
    # MathPrint: type 0x18, a fraction result, which the OS draws in 2D, and
    # '/' as the n/d bar. Classic: a real, drawn flat as it is typed.
    target, a, hl, de, bc = call
    mp = s.r8(ez80sim.MP_FLAGS) & 0x20
    # A list's comma and i go in as their glyphs, 2C and D7, filed as 2B and 2C.
    filed = want.translate(bytes.maketrans(b'\x2c\xd7', b'\x2b\x2c'))
    kind, filed = (0x18, filed.replace(b'\x83', b'\xef\x2e')) if mp else (0, filed)
    cols = classic_cols(want)
    if target != ez80sim.DISP_RESULT or a != kind or hl != ez80sim.ANS_BUF or bc != cols:
        return fail(name, "A=0 called %06X with a=%d hl=%06X bc=%d, want %06X a=%d hl=%06X bc=%d"
                    % (target, a, hl, bc, ez80sim.DISP_RESULT, kind, ez80sim.ANS_BUF, cols))
    if os_tokens(drawn_text(s)) != filed:
        return fail(name, "the text holds %s, which the OS files as %s, want %s"
                    % (drawn_text(s).hex(), os_tokens(drawn_text(s)).hex(), filed.hex()))
    if not nz:
        return fail(name, "A=0 returned Z, so the OS would draw its own 0 too")
    # The flag prologue the OS runs before its own result display, and donePrgm
    # cleared so no "Done" follows the answer.
    if s.r8(0xD000D3) & 0x80 or not (s.r8(0xD000C9) & 0x04) \
            or s.r24(0xD00335) != 0xD0033A or s.r8(0xD00080) & 0x20:
        return fail(name, "result-display prologue not run")
    kept = bytes(s.r8(ez80sim.LAST_ANS + i) for i in range(len(want) + 1))
    if kept != bytes([len(want)]) + want:
        return fail(name, "lastAns holds %s, want the answer shown" % kept.hex())
    return True


def case(name, entry, expect_out=None, **kw):
    """expect_out is the answer as token hex; None means the OS must be left to
    show its own result."""
    e = h(entry)
    try:
        s = run_hook(BODY, e, **kw)
    except Trap as ex:
        fail(name, ex); return
    s.entry = e
    if not s.fz:
        fail(name, "returned NZ, which swallows the key"); return
    want = None if expect_out is None else h(expect_out)
    if check_display(name, s, want):
        if want is None:
            print("ok   %-24s %-22s left to the OS" % (name, entry or "(empty)"))
        else:
            print("ok   %-18s %-18s |%s|" % (name, entry, render(want)))


print("--- it answers; the entry stays as typed and the OS draws the answer ---")
case("2X+2X",          "3258703258",     "3458")
case("X",              "58",             "58")
case("A+A",            "417041",         "3241")
case("theta",          "5b",             "5b")
case("X-X cancels",    "587158",         "30")
case("X*X",            "5858",           "580d")
case("3X-9",           "33587139",       "33587139")
case("X^2+2X+1",       "58f03270325870 31", "580d70325870 31")
case("2X*3X",          "32588233 58",    "36580d")
case("-3X",            "b03358",         "b03358")
case("X^4 keeps a caret", "58f034",      "58f034")
case("X/2",            "588332",         "588332")
case("X+Y",            "587059",         "587059")
case("(X²-1)/(X-1)",   "10580d713111 83 1058713111", "587031")
# 26 tokens: exactly one screen line
case("answer 26 wide", "3939393958f037703939393958f0367039393958f03570393939",
     "3939393958f037703939393958f0367039393958f03570393939")

# A variable below the bar: a fraction in lowest terms.
case("2/X",              "328358",             "328358")
case("1/X+1/X",          "31835870318358",       "328358")
case("X/(X+1)",          "58831058703111",     "58831058703111")
case("(X+Y)/(X-Y)",      "1058705911831058715911", "1058705911831058715911")

print("\n--- MathPrint boxes arrive flattened: prog # holds what the OS will parse ---")
# Measured on OS 5.8.4: X^7Y typed with a box is 58 EF2A 0E00 EF2D 59 in the edit
# buffer but 58 F0 37 59 once flattened; the OS adds the parentheses a box
# implies, and an n/d box becomes (numerator) EF2E denominator.
case("X^7Y from a box",  "58f03759",           "58f03759")
case("X^(1+2)Y",         "58f0103170321159",   "580f59")
case("n/d: (X)/2+1",     "105811ef2e327031",   "5883327031")
case("MathPrint entry, insert on", "3258703258", "3458", text_flags=0x20)
case("MathPrint 2D: X/2+1", "58833270 31", "58833270 31", mathprint=True)
case("MathPrint 2D: (X+Y)/(X-Y)", "1058705911831058715911", "1058705911831058715911",
     mathprint=True)
case("MathPrint 2D: 4X", "3258703258", "3458", mathprint=True)
# 29 tokens: past Classic's 26 columns, but MathPrint raises the powers.
case("(X+1)^6, Classic", "1058703111f036")
case("MathPrint: (X+1)^6", "1058703111f036",
     "58f036703658f03570313558f034703230580f703135580d7036587031", mathprint=True)
case("(X²-Y²+X-Y)/(X²-Y²), one side of one degree", "10580d71590d705871591183 10580d71590d11",
     "10587059703111831058705911")
# MODE ANSWERS: DEC: decimals where they end, as if a decimal point were typed
case("DEC: X/4", "588334", "303a323558", dec=True)
case("DEC, MathPrint: X/4", "588334", "303a323558", mathprint=True, dec=True)
case("DEC: X/3+1", "5883337031", "5883337031", dec=True)
case("DEC: sqrt(8) left to the OS", "bc3811", dec=True)
# ENTER on an empty line re-runs the last entry, which is in prog # by A=2
# (measured): it gets the answer it got the first time, not the OS's 0.
case("re-run, empty line", "3258703258", "3458", virgin=True)

print("\n--- Ans is the answer SymCE showed last ---")
# *3 right after an answer is typed by the OS as Ans*3 (72 82 33).
FOUR_X = h("3458")
case("Ans*3 after 4X",     "728233",   "313258", last_ans=FOUR_X)
case("Ans alone",          "72",       "3458",   last_ans=FOUR_X)
case("Ans+2X",             "72703258", "3658",   last_ans=FOUR_X)
case("Ans*Ans after X",    "728272",   "580d",   last_ans=h("58"))
case("3Ans implied",       "3372",     "313258", last_ans=FOUR_X)
case("Ans, none kept",     "728233")
case("2+2 after 4X",       "327032",   last_ans=FOUR_X, op1=h("00 80 40 00000000000000"))
# ... and that is what Ans is next: 2+2 (the OS's 4), then *X.
case("Ans*X after the OS's 4", "728258", "3458", last_ans=h("ff 00 80 40 00000000000000"))
case("Ans*X after the OS's -0.25", "728258", "b0588334",
     last_ans=h("ff 80 7f 25 00000000000000"))
case("Ans*X after 1/3",    "728258", last_ans=h("ff 00 7f 33333333333333"))
case("Ans*X after a list", "728258", last_ans=h("ff 01 80 40 00000000000000"))
case("Ans*3 after 2X/3 kept", "728233", "3258", last_ans=h("32588333"))
# SymCE's own Ans is never the OS's, which was not set: a number is answered.
case("Ans+1 after POLYDEGREE's 3", "727031", "34", last_ans=h("33"))
case("Ans*3 after 2√(2)",   "728233", "36bc3211", last_ans=h("32bc3211"))
case("sqrt(8X)",           "bc385811", "32bc325811")
case("1/(1+sqrt(2))",      "318310317 0bc3211".replace(" ", ""), "bc321171 31".replace(" ", ""))

print("\n--- commands; one it cannot do is an ERR screen the hook throws ---")
case("FACTOR(X²-4)",  "464143544f52 10 580d7134 11".replace(" ", ""), "10587132111058703211")
case("SOLVE(X²=4,X)", "534f4c5645 10 580d6a34 2b58 11".replace(" ", ""),
     "586ab032 3c 586a32".replace(" ", ""))
case("SOLVE(X²=2,X), Classic columns", "534f4c5645 10 580d6a32 2b58 11".replace(" ", ""),
     "586ab0bc3211 3c 586abc3211".replace(" ", ""))


def thrown(name, entry, msg, **kw):
    """ENTER on entry; at A=2 the hook must throw error 0x2B (1:Quit only)
    with msg -- "MESSAGE",0,"LINE",0 -- as appErr1, and leave nothing for A=0."""
    try:
        s = run_hook(BODY, h(entry), **kw)
        evaluate(s)
    except Trap as ex:
        return fail(name, ex)
    got = bytes(s.r8(ez80sim.APP_ERR1 + i) for i in range(len(msg) + 1))
    if s.thrown != 0x2B:
        return fail(name, "threw %r, want 0x2B" % s.thrown)
    if got != msg + b'\0':
        return fail(name, "appErr1 holds %r" % got)
    if s.r8(ez80sim.PENDING):
        return fail(name, "left the pending mark set")
    print("ok   %-24s ERROR: %s" % (name, msg.replace(b'\0', b' | ').decode()))


thrown("SOLVE(X²+1=0,X)", "534f4c5645 10 580d7031 6a30 2b58 11".replace(" ", ""), b"NO SOLUTION\0")
thrown("SOLVE(X=X,X)", "534f4c5645 10 586a58 2b58 11".replace(" ", ""), b"ALWAYS TRUE\0")
thrown("SOLVE(X²+X=1,X), Classic", "534f4c5645 10 580d7058 6a31 2b58 11".replace(" ", ""),
       b"TOO WIDE\0TRY MATHPRINT")
thrown("SOLVE(X³=2,X)", "534f4c5645 10 580f6a32 2b58 11".replace(" ", ""), b"SYMCE LIMIT\0SOLVE")
thrown("SOLVE(X²-4), no = or ,", "534f4c5645 10 580d7134 11".replace(" ", ""), b"SYNTAX\0SOLVE(A=B,X)")
thrown("SOLVE(X²=4), no ,X", "534f4c5645 10 580d6a34 11".replace(" ", ""), b"SYNTAX\0SOLVE(A=B,X)")

# The Algebra menu's commands: lists, i, and the errors they add.
case('POLYROOTS(X²-2)', '504f4c59524f4f545310580d713211', '08b0bc32112cbc321109')
case('CPOLYROOTS(X²+1)', '43504f4c59524f4f545310580d703111', '08b0d72cd709')
case('CSOLVE(X²+1=0,X)', '43534f4c564510580d70316a302b5811', '586ab0d73c586ad7')
case('CFACTOR(X²+1)', '43464143544f5210580d703111', '105871d711105870d711')
case('NROOTS(X²-2)', '4e524f4f545310580d713211', '08b0313a3431343231333536322c313a34313432313335363209')
case('POLYQUOTIENT(X²,2X+1)', '504f4c5951554f5449454e5410580d2b3258703111', '58833271318334')
case('... in MathPrint', '504f4c5951554f5449454e5410580d2b3258703111', '58833271318334', mathprint=True)
case('CZEROS(X²+X+1), MP', '435a45524f5310580d7058703111', '0810b03171bc3311d71183322c10b03170bc3311d711833209', mathprint=True)
thrown('POLYQUOTIENT(X²,0)', '504f4c5951554f5449454e5410580d2b3011', b'DIVIDE BY 0\x00')
thrown('POLYROOTS(0)', '504f4c59524f4f5453103011', b'ALWAYS TRUE\x00')
thrown('POLYGCD(X), one argument', '504f4c59474344105811', b'ARGUMENT\x00POLYGCD')
thrown('LEFT(X²), no =', '4c45465410580d11', b'SYNTAX\x00LEFT(A=B)')
thrown('POLYROOTS(1/X)', '504f4c59524f4f54531031835811', b'DATA TYPE\x00POLYROOTS')
thrown('CPOLYROOTS(X²+X+1), Classic', '43504f4c59524f4f545310580d7058703111', b'TOO WIDE\x00TRY MATHPRINT')
thrown("Ans*3 after a list", "728233", b"SYMCE LIMIT\0ANS", last_ans=h("08b0322c3209"))
# Its functions: sin( and ln( as tokens, cos( as the letters c o s, whose
# 0xB2 (c) is not abs(: BC comes from the engine's count, 13 here.
case('TEXPAND(sin(2X))', '54455850414e4410c232581111', '32c25811bbb2bbbfbbc3105811')
case('TOLN(logBASE(X,3))', '544f4c4e10ef34582b331111', 'be581183be3311')
case('TOSIN(cos(X)²)', '544f53494e10c458110d11', '3171c258110d')
case('TCOLLECT(sin(X)cos(X)), MP', '54434f4c4c45435410c25811c4581111', 'c23258118332', mathprint=True)
thrown('TOEXP(sin(X)), DEGREE', '544f45585010c2581111', b'MODE\0USE RADIAN', deg=True)
thrown('TOLOGBASE(ln(X)), no base', '544f4c4f474241534510be581111', b'ARGUMENT\0TOLOGBASE')

print("\n--- it refuses, and the OS behaves exactly as stock ---")
for name, tok in [("prgmA", "5f41"), ("prgmZZZ", "5f5a5a5a"),
                  ("DelVar X", "bb5458"), ("ClrAllLists", "bb52"),
                  ("Blue", "ef41"), ("Image1", "ef50"), ("r2T", "5e41"),
                  ("5->X  (STO)", "350458"), ("Str1", "aa00"),
                  ("matrix [A]", "5c00"), ("list L1", "5d00"),
                  ("recalled \"4X\"", "2a34582a"), ("2+2 numeric", "327032"),
                  ("(X²+X-Y²+Y)/(X²+2XY+Y²+X+Y)", "10580d7058 71590d7059118310580d70325859 70590d70587059 11".replace(" ", "")), ("X/0", "588330"), ("sqrt(sqrt(2)-X)", "bcbc32117158 11".replace(" ", "")),
                  ("X^16 power", "58f03136"), ("1000000X", "3130303030303058"),
                  ("trailing +", "3258 70"), ("leading *", "8258"),
                  ("empty entry", ""), ("lone EF prefix", "58ef"),
                  ("edit-buffer box tokens", "58ef2a0400ef2d7032587031"),
                  ("answer 27 wide", "3939393958f037703939393958f0367039393958f0357039393939"),
                  ("65 tokens", "58" + "7058" * 32)]:
    case(name, tok)

print("\n--- gates: anything but ENTER on the home screen is not ours ---")
case("A=0 display",    "3258703258", a=0)
case("A=2 eval",       "3258703258", a=2)
case("A=3 ctx switch", "3258703258", a=3)
case("not ENTER",      "3258703258", b=0x0B)
case("not home screen","3258703258", cx=0x0B)
case("edit buf closed","3258703258", edit_open=False)
# The home screen being the current context does not mean the buffer holds
# something the user typed and meant to run.
case("waking from APD",   "3258703258", warm=True)
case("BASIC Input open",  "3258703258", parse_inp=True)
case("an app is running", "3258703258", app_running=True)

print("\n--- the pending mark belongs to one ENTER only ---")


def stash(name, keys, expect_answer, display_cx=0x40):
    """Hook calls in order, then the A=0 call. Only an ENTER whose own A=2
    and A=0 follow may show anything."""
    s = None
    try:
        for a, b, cx in keys:
            if s is None:
                s = run_hook(BODY, h("3258703258"), a=a, b=b, cx=cx)
            else:
                s.w8(ez80sim.CX_CUR_APP, cx)
                s.a, s.b, s.pc = a, b, s.org + 1
                s.run()
        call, nz = display(s, cx=display_cx)
    except Trap as ex:
        fail(name, ex); return
    shown = call is not None
    if shown != expect_answer or nz != expect_answer:
        fail(name, "displayed" if shown else "stayed silent")
    else:
        print("ok   %-24s %s" % (name, "displayed" if shown else "stayed silent"))


stash("ENTER, A=2 eval, display",  [(1, 0x05, 0x40), (2, 0x00, 0x40)], True)
# A=0 only shows what A=2 answered.
stash("ENTER then display",        [(1, 0x05, 0x40)], False)
stash("ENTER, A=2, another key",   [(1, 0x05, 0x40), (2, 0x00, 0x40), (1, 0x02, 0x40)], False)
stash("ENTER, a key in a menu",    [(1, 0x05, 0x40), (1, 0x05, 0x0B), (2, 0x00, 0x40)], False)
stash("A=2 not on home screen",    [(1, 0x05, 0x40), (2, 0x00, 0x0B)], False)
stash("A=0 not on home screen",    [(1, 0x05, 0x40), (2, 0x00, 0x40)], False, display_cx=0x0B)


def display_twice():
    s = run_hook(BODY, h("3258703258"))
    evaluate(s)
    display(s)
    call, nz = display(s)
    if call or nz:
        fail("second A=0 call", "the answer was displayed again")
    else:
        print("ok   %-24s consumed by the first" % "second A=0 call")


display_twice()

print("\n--- the cursor inserts instead of overwriting ---")


def ins(name, expect, entry="3258703258", **kw):
    """textFlags insert has to come out set for every key on the home screen --
    not just ENTER -- and has to be left alone everywhere else."""
    try:
        s = run_hook(BODY, h(entry), **kw)
    except Trap as ex:
        fail(name, ex); return
    got = bool(s.r8(ez80sim.TEXT_FLAGS) & 0x10)
    if got != expect:
        fail(name, "insert mode came out %s" % ("on" if got else "off"))
    else:
        print("ok   %-24s insert mode %s" % (name, "on" if got else "untouched"))


ins("ENTER",              True)
ins("an ordinary key",    True,  b=0x02)
ins("a key on an entry we refuse", True, entry="5f41")
ins("not the home screen", False, cx=0x0B)
ins("A=0 display",        False, a=0)
ins("A=3 ctx switch",     False, a=3)
# cxCurApp is still 0x40 in both of these. Inside a MathPrint box editTop has
# moved off the per-entry baseline; with no baseline captured (history recall,
# or a hook armed mid-entry) we cannot tell. Both leave the OS's mode alone.
ins("cursor inside a box",    False, base_delta=0x45)
ins("no baseline captured",   False, base_ok=False)


def ins_key(name, key, runs, insert=True, **kw):
    """CLEAR, DEL and the cursor keys end insert mode in the OS, after the hook
    set it, and the cursor showed as a block. On the entry the hook runs them
    through (cxMain) itself -- once, its own A=1 call back passing the key on --
    sets insert again and swallows the key, giving every register back.
    Anywhere else, and for any other key, it leaves cxMain alone."""
    try:
        s = Sim(BODY, tail=kw.pop("tail", 0))
        s.w8(ez80sim.CX_CUR_APP, 0x40)
        top = kw.pop("top", 0xD1A8CA)
        s.w24(ez80sim.EDIT_TOP, top)
        s.w24(ez80sim.BASE_TOP, 0xD1A8CA)
        s.w8(ez80sim.BASE_OK, 0xA5)
        s.w8(ez80sim.CMD_FLAGS, kw.pop("cmd", 0x04))
        s.w8(ez80sim.TEXT_FLAGS, 0x02)
        s.w24(ez80sim.HOOK_PTR, s.org)
        s.a, s.pc = 1, s.org + 1
        s.hl, s.de, s.ix, s.iy, sp = 0x123456, 0x654321, 0xABCDEF, ez80sim.IY_OS, s.sp
        s.bcset(0x0A0009 | key << 8)
        s.run()
    except Trap as ex:
        fail(name, ex); return
    got = (s.cx_keys, s.cx_inner, not s.fz, bool(s.r8(ez80sim.TEXT_FLAGS) & 0x10))
    want = ([key], [True], True, insert) if runs else ([], [], False, insert)
    if got != want:
        fail(name, "cxMain keys %s, its hook call Z=%s, swallowed %s, insert %s" % (
            got[0], got[1], got[2], got[3]))
    elif (s.hl, s.de, s.ix, s.iy, s.bc(), s.sp) != (0x123456, 0x654321, 0xABCDEF, ez80sim.IY_OS,
                                                    0x0A0009 | key << 8, sp):
        fail(name, "registers or stack not given back")
    elif s.r8(ez80sim.IN_KEY):
        fail(name, "left its guard set")
    else:
        print("ok   %-24s %s" % (name, "run through cxMain, insert on" if runs else "passed on"))


for k, n in [(0x09, "CLEAR"), (0x0A, "DEL"), (0x01, "RIGHT"), (0x02, "LEFT"), (0x03, "UP"), (0x04, "DOWN"),
             (0x0E, "2nd LEFT"), (0x0F, "2nd RIGHT")]:
    ins_key(n + " keeps insert", k, True)
for k, n in [(0x05, "ENTER"), (0x0B, "INS"), (0x9A, "a letter")]:
    ins_key(n + " not wrapped", k, False)
ins_key("CLEAR in a box", 0x09, False, insert=False, top=0xD1A90F)
ins_key("DOWN in a box", 0x04, False, insert=False, top=0xD1A90F)
ins_key("DOWN on a history line", 0x04, True, insert=False, cmd=0x12)


def key_writes():
    """A key may write its pending mark, the baseline, and textFlags -- nothing
    else, and never the entry."""
    allowed = {ez80sim.PENDING, ez80sim.TEXT_FLAGS} | \
              set(range(ez80sim.BASE_TOP, ez80sim.BASE_OK + 1))
    for name, kw in [("ENTER", {}), ("a letter", {"b": 0x9A}),
                     ("new line", {"virgin": True})]:
        s = run_hook(BODY, h("3258703258"), **kw)
        stray = sorted({a for a, _ in s.writes} - allowed)
        if stray:
            fail("key writes: " + name, "wrote %s" % ", ".join("%06X" % a for a in stray))
        else:
            print("ok   %-24s writes only its own state" % ("key writes: " + name))


key_writes()

print("\n--- ALPHA+DOWN opens the menu ---")

K_ALPHA_DOWN = 0x08


def alpha_down(ptr=None, b=K_ALPHA_DOWN):
    """ALPHA+DOWN on the entry, run by hand so every register starts known.
    ptr: what homescreenHookPtr holds, the body itself unless given."""
    s = Sim(BODY)
    s.w8(ez80sim.CX_CUR_APP, 0x40)
    s.w24(ez80sim.EDIT_TOP, 0xD1A8CA)
    s.w24(ez80sim.BASE_TOP, 0xD1A8CA)
    s.w8(ez80sim.BASE_OK, 0xA5)
    s.w24(ez80sim.HOOK_PTR, s.org if ptr is None else ptr)
    s.a, s.pc = 1, s.org + 1
    s.hl, s.de, s.ix = 0x123456, 0x654321, 0xABCDEF
    s.bcset(0x0A0009 | b << 8)
    s.run()
    return s


def menu(name, expect, b=K_ALPHA_DOWN, stolen=False, **kw):
    """kAlphaDown with the cursor on the entry itself calls symce_menu through
    the second word past the body and swallows the key (NZ). Anywhere else --
    another key, a MathPrint box, no baseline, a selected history line, not the
    home screen, an impostor hook -- the menu is not called and the key passes
    on (Z)."""
    try:
        if stolen:
            s = alpha_down(ptr=0xD40000, b=b)      # right magic, wrong code
        else:
            s = run_hook(BODY, h("3258703258"), b=b, **kw)
    except Trap as ex:
        fail(name, ex); return
    swallowed = not s.fz
    if expect and s.menu_key != b:
        fail(name, "menu got key %02X" % s.menu_key); return
    if (s.menu_calls, swallowed) != ((1, True) if expect else (0, False)):
        fail(name, "menu called %d times, key %s" % (s.menu_calls,
                                                     "swallowed" if swallowed else "passed on"))
    else:
        print("ok   %-24s %s" % (name, "menu, key swallowed" if expect else "key passed on"))


menu("ALPHA+DOWN on the entry", True)
menu("... in MathPrint",        True, mathprint=True, text_flags=0x20)
menu("... on a new line",       True, virgin=True)
menu("plain INS",               False, b=0x0B)
menu("ALPHA+UP",                False, b=0x07)
menu("cursor inside a box",     False, base_delta=0x45)
menu("no baseline captured",    False, base_ok=False)
menu("history line selected",   False, cmd=0x12)
menu("not the home screen",     False, cx=0x0B)
menu("A=0 display",             False, a=0)
menu("impostor hook",           False, stolen=True)
# The SymCE tab's items in 2nd MATH, all key code 0x21 (the item's number is in
# keyExtend, for menu.c): anywhere on the home screen, boxes and history lines
# included. Nothing else near it: 0x22-0x24 were the old tab's, now stock's.
menu("SymCE tab 0x21",          True, b=0x21)
menu("... inside a box",        True, b=0x21, base_delta=0x45)
menu("... history line",        True, b=0x21, cmd=0x12)
menu("... no baseline",         True, b=0x21, base_ok=False)
menu("0x20",                    False, b=0x20)
menu("0x22",                    False, b=0x22)
menu("0x24",                    False, b=0x24)
menu("0x25",                    False, b=0x25)
menu("0x21 not home",           False, b=0x21, cx=0x0B)
menu("0x21 impostor hook",      False, b=0x21, stolen=True)

# The second copy of the body (tail byte 1) is TI's own cursor: no insert mode,
# no wrapped cursor keys, but the menu still opens.
ins("TI cursor: a normal key", False, b=0x9A, tail=1)
ins_key("TI cursor: LEFT passed on", 0x02, False, insert=False, tail=1)
menu("TI cursor: ALPHA+DOWN", True, tail=1)


def menu_regs():
    """Every register the OS may still hold comes back from the menu call, which
    trashes all but ix: NZ returns straight to Mon, but nothing may rely on it."""
    try:
        s = alpha_down()
    except Trap as ex:
        fail("menu keeps registers", ex); return
    got = (s.menu_calls, s.hl, s.de, s.ix, s.bc(), s.iy, s.sp)
    want = (1, 0x123456, 0x654321, 0xABCDEF, 0x0A0009 | K_ALPHA_DOWN << 8, ez80sim.IY_OS,
            Sim(BODY).sp)
    if got != want:
        fail("menu keeps registers", "got %s" % " ".join("%X" % v for v in got))
    else:
        print("ok   %-24s hl de bc ix iy sp" % "menu keeps registers")


menu_regs()

print("\n--- the menu hook shim (mhook.bin) ---")
MHOOK = load_hook(os.path.join(ROOT, 'symce/obj/hook/mhook.bin'),
                  os.path.join(ROOT, 'symce/bin/SYMCE1.8xv'))


def mhook(name, ret):
    """mhook.bin, entered as the OS does (ix = body + 1), calls menu_tab(event,
    index) through the word just past it. menu_tab's 0 gives the OS back every
    register with Z; anything else goes back in HL with NZ, BC and DE kept."""
    s = Sim(MHOOK)
    s.w24(s.org + len(MHOOK), s.menu_at)
    s.menu_ret = ret
    s.a, s.pc, s.ix, s.iy = 2, s.org + 1, s.org + 1, ez80sim.IY_OS
    s.hl, s.de = 0x123456, 0x654321
    s.bcset(0x0A0B03)
    sp = s.sp
    try:
        s.run()
    except Trap as ex:
        fail(name, ex); return
    got = (s.menu_calls, s.menu_args, s.fz, s.hl, s.de, s.bc(), s.ix, s.iy, s.sp)
    want = (1, (2, 3), ret == 0, ret or 0x123456, 0x654321, 0x0A0B03, s.org + 1, ez80sim.IY_OS, sp)
    if got != want or (ret == 0 and s.a != 2):
        fail(name, "got %r a=%X" % (got, s.a))
    else:
        print("ok   %-24s %s" % (name, "Z, all kept" if ret == 0 else "NZ, HL = its answer"))


mhook("menu_tab returns 0", 0)
mhook("menu_tab returns a table", 0x0B1234)

print("\n--- nonsense pointers and an impostor hook ---")


def raw(name, size, found=True):
    """prog # as no sane OS leaves it: missing, or a size word that is not the
    entry's. Nothing may be answered or displayed."""
    s = run_hook(BODY, h("3258703258"))
    s.w8(ez80sim.ENTRY_AT - 2, size & 0xFF)
    s.w8(ez80sim.ENTRY_AT - 1, size >> 8)
    if not found:
        s.prog = None
    try:
        skip = evaluate(s)
        call, nz = display(s)
    except Trap as ex:
        fail(name, ex); return
    if call or nz or skip:
        fail(name, "answered something")
    else:
        n = s.engine_calls[0][1] if s.engine_calls else None
        print("ok   %-24s len %s refused" % (name, "%06X" % n if n is not None else "-"))


raw("prog # empty",       0)
raw("prog # 64K",         0xFFFF)
raw("prog # missing",     5, found=False)


def stolen_pointer():
    """The engine is reached through the hook pointer, so the body checks that
    the pointer really aims at itself before trusting it. Give it a plausible
    impostor -- right magic byte, wrong code -- and it must fall through to
    stock rather than jump into someone else's code."""
    s = run_hook(BODY, h("3258703258"))
    s.w24(ez80sim.HOOK_PTR, 0xD40000)
    s.w8(0xD40000, 0x83)
    try:
        skip = evaluate(s)
        call, nz = display(s)
    except Trap as ex:
        fail("stolen hook pointer", ex); return
    if call or nz or skip or s.engine_calls or s.r8(ez80sim.PENDING):
        fail("stolen hook pointer", "did not fall through to stock")
    else:
        print("ok   %-24s fell through to stock" % "stolen hook pointer")


stolen_pointer()


def self_write():
    """The body runs from flash on a calculator, but nothing may write inside
    it on either call: a write into flash does nothing, so a bug here would be
    invisible on hardware until the one path that trusts the written value."""
    org = 0x100000
    for name, entry, kw in [("answers", "3258703258", {}),
                            ("refused", "5f41", {}),
                            ("in a box", "3258703258", {"base_delta": 0x45}),
                            ("no base", "3258703258", {"base_ok": False})]:
        try:
            s = run_hook(BODY, h(entry), org=org, **kw)
            w = list(s.writes)
            evaluate(s)
            w += s.writes
            n1 = len(w)
            display(s)
            w += s.writes
        except Trap as ex:
            fail("self-write " + name, ex); continue
        hits = [a for a, _ in w if org <= a < org + len(BODY) + 6]
        if hits:
            fail("self-write " + name, "wrote into its own body at %06X" % hits[0])
        else:
            print("ok   %-24s %d + %d writes, none into the body"
                  % ("self-write " + name, n1, len(w) - n1))


self_write()


print("\n--- the popup's pixel codec: save_px/restore_px in the menu blob ---")


def elf_syms(path):
    """name -> value from an ELF32 .symtab, for the blob's own labels."""
    import struct
    d = open(path, 'rb').read()
    shoff, = struct.unpack_from('<I', d, 0x20)
    sh = [struct.unpack_from('<10I', d, shoff + 40 * i)
          for i in range(struct.unpack_from('<H', d, 0x30)[0])]
    sym = next(s for s in sh if s[1] == 2)              # SHT_SYMTAB
    strs, out = sh[sym[6]][4], {}
    for o in range(sym[4], sym[4] + sym[5], 16):
        n, v = struct.unpack_from('<II', d, o)
        out[d[strs + n:d.index(b'\0', strs + n)].decode()] = v
    return out


MENU_DIR = os.path.join(ROOT, 'symce/obj/menu/')
MSYM = elf_syms(MENU_DIR + '0x100000.elf')
MBLOB = open(MENU_DIR + '0x100000.bin', 'rb').read()
SAVE, SAVE_END, P = 0xD0EF00, 0xD13E00, 0xD40000 + 50 * 640 + 8


def codec(name, rows, lit=None, fits=True):
    """save_px then restore_px over `rows` (lists of RGB565 pixels, 4 per quad)
    at P; the region must come back exact and neither may write outside its own."""
    w4, h = len(rows[0]) // 4, len(rows)
    skip, lit = 640 - 8 * w4, lit or SAVE + 12 + w4 * h
    s = Sim(MBLOB, org=0x100000)
    s.jp_in = True
    region = set()
    for y, row in enumerate(rows):
        for x, c in enumerate(row):
            s.w8(P + y * 640 + 2 * x, c); s.w8(P + y * 640 + 2 * x + 1, c >> 8)
            region |= {P + y * 640 + 2 * x, P + y * 640 + 2 * x + 1}
    want = {a: s.r8(a) for a in region}

    def call(fn):
        s.writes, s.ix, s.iy, sp = [], 0x5A5A5A, ez80sim.IY_OS, s.sp
        for v in (lit, h, w4, skip, P, 0xC0FFEE):
            s.push(v)
        s.pc = MSYM[fn]
        s.run()
        if (s.ix, s.iy, s.sp) != (0x5A5A5A, ez80sim.IY_OS, sp - 18):
            raise Trap("%s did not keep ix/iy/sp" % fn)
        return s.hl, [a for a, _ in s.writes if not sp - 18 <= a < sp]  # its own argument slots

    try:
        ok, w = call('_save_px')
        if any(not SAVE <= a < SAVE_END for a in w):
            return fail(name, "save_px wrote outside engineWork")
        if bool(ok) != fits:
            return fail(name, "save_px said %s" % ("fits" if ok else "full"))
        if not fits:
            return print("ok   %-24s refused, nothing past %06X" % (name, SAVE_END))
        for a in region:
            s.w8(a, 0xA5)                               # the popup drew here
        _, w = call('_restore_px')
    except Trap as ex:
        return fail(name, ex)
    if any(a not in region for a in w):
        return fail(name, "restore_px wrote outside the region")
    bad = [a for a in region if s.r8(a) != want[a]]
    if bad:
        return fail(name, "pixel at %06X came back %02X" % (min(bad), s.r8(min(bad))))
    print("ok   %-24s %d px round trip" % (name, len(region) // 2))


W, K, G, R, B = 0xFFFF, 0x0000, 0xE71C, 0xF800, 0x001F
codec("codec: one colour", [[W] * 8] * 3)
# Colour 0 differing only in the last pixel, or in one byte, must not take
# the all-colour-0 fast path; then two more colours, then literals.
codec("codec: three colours", [[W, W, W, K, W, 0xFF7F, W, W], [G, K, W, G, K, K, K, K]])
codec("codec: literals", [[W, R, K, B, G, R, 0x1234, W], [R] * 8, [W, W, W, W, B, W, R, W]])
codec("codec: last literal fits", [[W, K, G, R, W, W, W, B]], lit=SAVE_END - 4)
codec("codec: literal overflow", [[W, K, G, R, W, B, W, 0x1234]], lit=SAVE_END - 4, fits=False)

print("\n--- the font hook and the localize hook (fhook.bin, lhook.bin) ---")
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import mkfont
LARGE, SMALL = mkfont.load()
FSLOT, LSLOT, MAP_AT, REC_AT = 0xD025ED, 0xD02611, 0x120000, 0x121000


def fontcase(name, binname, slot, a, b, code_ok, recs, rec_size, dest, width, hl_out):
    """One call, entered as the OS does. The table is mkfont's own, put where
    the two trailer words say. A hit: Z, HL = the record's address, 28/25 bytes
    there equal the table, B kept. Anything else: NZ, every register as it came."""
    body = load_hook(os.path.join(ROOT, 'symce/obj/hook/' + binname),
                     os.path.join(ROOT, 'symce/bin/SYMCE1.8xv'))
    s = Sim(body)
    s.w24(s.org + len(body), MAP_AT)
    s.w24(s.org + len(body) + 3, REC_AT - rec_size)
    for c in range(256):
        s.mem[MAP_AT + c] = mkfont.CODES.index(c) + 1 if c in recs else 0
    for i, c in enumerate(mkfont.CODES):
        for j, v in enumerate(recs[c]):
            s.mem[REC_AT + i * rec_size + j] = v
    s.w24(slot, s.org)
    s.a, s.b, s.pc, s.ix, s.iy = a, b, s.org + 1, s.org + 1, ez80sim.IY_OS
    s.hl, s.de, s.c = 0x123456, 0x654321, 0x33
    s.w8(0xD005A4, 0x0C)               # as 0xA256F leaves it after the last glyph
    s.writes = []
    sp = s.sp
    try:
        s.run()
    except Trap as ex:
        return fail(name, ex)
    if s.sp != sp or s.b != b:
        return fail(name, "sp or B moved")
    if code_ok:
        got = bytes(s.r8(dest + i) for i in range(rec_size))
        want = recs[b]
        if not s.fz or s.hl != hl_out or got != want:
            return fail(name, "hit wrong: Z=%s HL=%06X" % (s.fz, s.hl))
        if width is not None and s.r8(0xD005A4) != width:
            return fail(name, "width byte 0x%02X, want %02X" % (s.r8(0xD005A4), width))
        print("ok   %-24s Z, HL=%06X, record == table" % (name, s.hl))
    else:
        if s.fz or (s.hl, s.de, s.c) != (0x123456, 0x654321, 0x33) or s.writes:
            return fail(name, "miss must be NZ, regs kept, no writes")
        print("ok   %-24s NZ, regs kept" % name)


for nm, code in [("A", 0x41), ("theta 0x5B", 0x5B), ("pi 0xC4", 0xC4), ("0x21 !", 0x21)]:
    fontcase("font hit " + nm, 'fhook.bin', FSLOT, 1, code, True, LARGE, 28, 0xD005A5, 0, 0xD005A1)      # width byte 0: the OS draws it as a row
for nm, code in [("0x20 space", 0x20), ("0x01", 0x01), ("0xFF", 0xFF)]:
    fontcase("font miss " + nm, 'fhook.bin', FSLOT, 1, code, False, LARGE, 28, 0, 0, 0)
fontcase("font A=2 (TRACE width)", 'fhook.bin', FSLOT, 2, 0x41, False, LARGE, 28, 0, 0, 0)
for nm, code in [("A", 0x41), ("W", 0x57), ("not-equal 0x18", 0x18)]:
    fontcase("loc hit " + nm, 'lhook.bin', LSLOT, 0x75, code, True, SMALL, 25, 0xD005C5, None, 0xD005C5)
for nm, code in [("0x20 space", 0x20), ("0xFF", 0xFF)]:
    fontcase("loc miss " + nm, 'lhook.bin', LSLOT, 0x75, code, False, SMALL, 25, 0, 0, 0)
for a in (0x76, 0x77, 0x00, 0x01, 0x74):
    fontcase("loc A=%02X" % a, 'lhook.bin', LSLOT, a, 0x41, False, SMALL, 25, 0, 0, 0)

print()
if fails:
    print("%d FAILING: %s" % (len(fails), ", ".join(map(str, fails))))
    sys.exit(1)
print("all hook cases pass, against the assembled bytes")
