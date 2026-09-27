/* SymCE homescreen hook - position-independent body.
 *
 * Shipped as raw bytes inside the SymCE flash app (app/main.asm), with no
 * relocation entries, so NO absolute self-references: relative jr only, no
 * call or jp into itself, no internal data tables. Absolute references to
 * FIXED OS ADDRESSES are fine -- those do not move.
 *
 * The algebra is not here. It is src/engine.c, ordinary compiled C that the
 * makefile links on its own and relocates with the app. This body is the part
 * that has to be hand-written: it runs on every key press, it decides whether
 * the entry is ours at all, and it is what finds the engine -- through the
 * relocated pointer main.asm places right after these bytes.
 *
 * The SymCE menu (src/menu.c) is reached the same way, through the second
 * relocated pointer: at A=1 on the home screen, ALPHA+DOWN (key code 0x08,
 * which the stock OS ignores) with the cursor on the entry, not in a box and
 * not on a history line, calls symce_menu and swallows the key (NZ). The menu
 * draws, reads keys and types the chosen command through (cxMain) itself. So
 * does key code 0x21 anywhere on the home screen: every item of the Algebra tab
 * in 2nd MATH (src/mhook.c), which no key makes, the item's number riding in
 * keyExtend.
 *
 * DESIGN NOTE - the answer is computed from the OS's own copy of the entry,
 * before the OS evaluates it, and displayed through the OS's own result path:
 * The hook is called three times per ENTER. At A=1 (the key) it only decides
 * whether this ENTER is a candidate and marks it pending; the entry is never
 * touched, so what the OS echoes into the history and what 2nd ENTRY recalls
 * is exactly what was typed. The OS then flattens the entry into plain tokens
 * in the temp program # and calls the hook with A=2 ("about to evaluate").
 * There, and only if this ENTER was marked, the hook runs the engine over #.
 * On an answer it keeps it at lastAns and returns NZ, which makes the OS skip
 * the evaluation (0x58763 jumps past ParseInp): X's value never matters, so
 * X^2/X with X = 0 is X, not ERR:DIVIDE BY 0. The OS then calls the hook with
 * A=0 ("display the result"), and the hook hands the answer to 0x91FC2, the
 * routine the OS itself uses to put a real result on the answer line and file
 * it in the MathPrint history, with the type byte saying REAL (Classic) or
 * FRACTION (0x18, MathPrint, drawn 2D). Either is right-aligned; a string is
 * not. Returning NZ from A=0 tells the OS to draw
 * nothing itself. The OS's own Ans is left as it was, since nothing was
 * evaluated; the Ans token means lastAns to the engine: 4X, then *3 (which the
 * OS types as Ans*3), is 12X. When the OS shows its own result, that is Ans
 * again: the hook keeps a copy of OP1, where the OS has the result at A=0,
 * marked 0xFF, so 2+2, then *X, is 4X.
 *
 * Why # and not the edit buffer (measured on OS 5.8.4): in MathPrint an
 * exponent or n/d box turns the edit buffer into a tree -- X^7Y is stored as
 * 58 EF2A 0E00 EF2D 59 with the 7 in a separate node -- and while the cursor is
 * inside a box, editTop..editCursor holds only the box. By A=2 the OS has
 * flattened all of it into #: X^2 reads 58 0D, X^(1+2)Y comes with the
 * parentheses the OS adds itself, and an n/d box is (num) EF2E (den). On an
 * empty line, ENTER re-runs the last entry, and # holds that entry again.
 *
 * Earlier designs lost to this one. Painting the answer with _PutS from the key
 * hook lost a repaint fight with the OS. Rewriting the entry to 2X+2X:"4X" gave
 * the right answer line but echoed the rewrite into the history, and a string
 * literal is left-aligned. Reading the edit buffer at A=1 could not see into
 * MathPrint boxes, so X^3 typed with the ^ key was never simplified.
 *
 * 0x91FC2 is undocumented and OS-version specific (5.8.4). Its contract was
 * read out of the OS's own real-number path at 0x97C7F: A = type (0 = real),
 * HL = the NUL-terminated text (the OS passes OP3; any RAM works, measured),
 * BC = its length in characters,
 * and the flag prologue at 0x97C7F run first. Inside, 0xB19FB rewrites the
 * text from LFont characters to TOKENS in place (Lneg to tChs, Ldash to tSub,
 * caret to tPower; anything not in its table is kept), and the record it
 * files and draws is that token string. Measured: the OS's own -3 is filed as
 * B0 33. So the answer is emitted as tokens, which the rewrite leaves alone;
 * emitting Lsquare instead of tSqr filed 0x12 = round( and drew "Xround(".
 *
 * ponytail: the engine takes about 1.1K of stack at its nesting limit. A=2
 * runs shallow and the CE has room, so there is no headroom check. Add one if
 * a deeper caller ever appears.
 */
__asm__(
"	.globl _symce_hook\n"
"	.globl _symce_hook_end\n"
"\n"
/* ---- fixed OS addresses (these never move) ---- */
"	.equ	cxCurApp,   0x0D007E0\n"
"	.equ	editFlags,  0x0D00081\n"      /* OS_FLAGS_EDIT      = iy + 1    */
"	.equ	textFlags,  0x0D00085\n"
"	.equ	apdFlags,   0x0D00088\n"      /* OS_FLAGS_APD       = iy + 8    */
"	.equ	onFlags,    0x0D00089\n"      /* OS_FLAGS_ON        = iy + 9    */
"	.equ	cmdFlags,   0x0D0008C\n"      /* OS_FLAGS_CMD       = iy + 0x0C */
"	.equ	apiFlg1,    0x0D000A8\n"      /* OS_FLAGS_API1      = iy + 0x28 */
"	.equ	editTop,    0x0D02437\n"
"	.equ	baseTop,    0x0D0EE00\n"      /* editTop captured while the entry line was empty */
"	.equ	baseOk,     0x0D0EE03\n"      /* 0xA5 once baseTop belongs to THIS entry */
"	.equ	pending,    0x0D0EE04\n"      /* 0xFF: ENTER, for A=2 to try; 0xFE: A=2 answered, A=0 shows it */
"	.equ	ansBuf,     0x0D0EE80\n"      /* the answer, as tokens (MAXOUT = 64 at most); then the text 0x91FC2 draws */
"	.equ	engineWork, 0x0D0EF00\n"      /* the engine's scratch, SYMCE_WORK bytes */
"	.equ	lastAns,    0x0D0EE20\n"      /* what Ans is: length and tokens, or 0xFF and the OS's real */
"	.equ	OP1,        0x0D005F8\n"      /* the OS's result, at A=0 */
"	.equ	dispResult, 0x091FC2\n"      /* OS: draw the text at HL as a result of type A, file it in history */
"	.equ	chkFindSym, 0x02050C\n"      /* OS jump table: find the variable named in OP1 */
"\n"
/* homescreenHookPtr is a FIXED OS address holding OUR OWN base: the OS is
   calling us through it right now, so it is correct by construction. base plus
   a link-time constant offset reaches any label in the body, and the word just
   past the body. */
"	.equ	hookPtr, 0x0D025E1\n"
"	.equ	hookFlag, 0x0D000B4\n"      /* hookflags2: bit4 = homescreenHookActive */
"	.equ	mpFlags,  0x0D000C4\n"      /* iy + 0x44: bit5 = MathPrint on */
"	.equ	ansFlags, 0x0D0009A\n"      /* iy + 0x1A: bit0 = MODE ANSWERS: DEC (measured) */
"	.equ	trigFlags, 0x0D00080\n"     /* iy + 0: bit2 = MODE DEGREE */
"_symce_hook:\n"
"\t.byte\t0x83\n"
"\tcp\ta, 1\n"
"\tjr\tnz, .Lnotkey0\n"
/* Any key ends the ENTER that was marked, so nothing stale can reach a later
   A=2 or A=0. Cleared before the context check on purpose: a key in a menu counts
   too. */
"\txor\ta, a\n"
"\tld\t(pending), a\n"
"\tld\ta, (cxCurApp)\n"
"\tcp\ta, 0x40\n"
"\tjr\tnz, .Lpass\n"
/* Insert mode, on every home-screen key -- but only while editTop is where it
   was when this line was empty, i.e. the cursor is on the entry itself and not
   inside a MathPrint box or a menu. */
"\tpush\thl\n"
"\tpush\tde\n"
"\tld\ta, (cmdFlags)\n"
"\tand\ta, 0x20\n"
"\tjr\tz, .Lnotnew\n"
"\tld\thl, (editTop)\n"
"\tld\t(baseTop), hl\n"
"\tld\ta, 0xA5\n"
"\tld\t(baseOk), a\n"
".Lnotnew:\n"
"\tld\ta, (baseOk)\n"
"\tcp\ta, 0xA5\n"
"\tjr\tnz, .Lnoins\n"
"\tld\thl, (baseTop)\n"
"\tex\tde, hl\n"
"\tld\thl, (editTop)\n"
"\tor\ta, a\n"
"\tsbc\thl, de\n"
"\tjr\tnz, .Lnoins\n"
"\tld\thl, textFlags\n"
"\tset\t4, (hl)\n"
/* ALPHA+DOWN there, and not on a selected history line (cmdFlags bit 4, which
   leaves editTop alone in Classic): the menu. */
"\tld\ta, b\n"
"\tcp\ta, 0x08\n"
"\tjr\tnz, .Lnoins\n"
"\tld\ta, (cmdFlags)\n"
"\tand\ta, 0x10\n"
"\tjr\tz, .Lmenu\n"
".Lnoins:\n"
"\tpop\tde\n"
"\tpop\thl\n"
"\tld\ta, b\n"
"\tcp\ta, 0x05\n"
"\tjr\tnz, .Lnot5\n"
/* ENTER: mark it unless the OS is somewhere an answer does not belong. An
   empty line is not one of those: ENTER there re-runs the last entry, # holds
   that entry by A=2, and it gets the answer it got the first time rather than
   the OS's number. ENTER pasting from history never reaches A=2, and the next
   key clears the mark. */
"\tld\ta, (apdFlags)\n"
"\tand\ta, 0x10\n"
"\tjr\tnz, .Lpass\n"
"\tld\ta, (onFlags)\n"
"\tand\ta, 0x02\n"
"\tjr\tnz, .Lpass\n"
"\tld\ta, (apiFlg1)\n"
"\tand\ta, 0x10\n"
"\tjr\tnz, .Lpass\n"
"\tld\ta, (editFlags)\n"
"\tand\ta, 0x04\n"
"\tjr\tz, .Lpass\n"
"\tld\ta, 0x0FF\n"
"\tld\t(pending), a\n"
".Lpass:\n"
"\tcp\ta, a\n"
"\tret\n"
".Lnotkey0:\n"                   /* .Lnotkey, in jr range of the dispatch */
"\tjr\t.Lnotkey\n"
/* 0x21, the Algebra tab's items, wherever the cursor is: the menu too. */
".Lnot5:\n"
"\tcp\ta, 0x21\n"
"\tjr\tnz, .Lpass\n"
"\tpush\thl\n"
"\tpush\tde\n"
/* The menu, src/menu.c: symce_menu(bc), reached through the second word past
   the body once hookPtr checks out as in .Leval. It draws the popup, reads keys,
   has the home screen redraw itself, and types the chosen command through
   (cxMain). hl and de are on the stack. NZ swallows the key; a bad pointer
   passes it on, and stock ignores it. */
".Lmenu:\n"
"\tld\thl, (hookPtr)\n"
"\tld\ta, (hl)\n"
"\tcp\ta, 0x83\n"
"\tjr\tnz, .Lmbad\n"
"\tinc\thl\n"
"\tld\ta, (hl)\n"
"\tcp\ta, 0x0FE\n"
"\tjr\tnz, .Lmbad\n"
"\tinc\thl\n"
"\tld\ta, (hl)\n"
"\tcp\ta, 1\n"
"\tjr\tnz, .Lmbad\n"
"\tpush\tiy\n"
"\tpush\tbc\n"
"\tld\thl, (hookPtr)\n"
"\tld\tde, .Lmback - _symce_hook\n"
"\tadd\thl, de\n"
"\tpush\thl\n"
"\tld\thl, (hookPtr)\n"
"\tld\tde, _symce_hook_end + 3 - _symce_hook\n"
"\tadd\thl, de\n"
"\tld\thl, (hl)\n"
"\tjp\t(hl)\n"
".Lmback:\n"
"\tpop\tbc\n"
"\tpop\tiy\n"
"\tpop\tde\n"
"\tpop\thl\n"
"\txor\ta, a\n"
"\tinc\ta\n"
"\tret\n"
".Lmbad:\n"
"\tpop\tde\n"
"\tpop\thl\n"
"\tcp\ta, a\n"
"\tret\n"
".Lnotkey:\n"
"\tcp\ta, 2\n"
"\tjr\tz, .Leval0\n"
"\tor\ta, a\n"
"\tjr\tnz, .Lpass0\n"
/* A=0: the OS is about to show a result. Ours if A=2 answered this ENTER. */
"\tld\ta, (cxCurApp)\n"
"\tcp\ta, 0x40\n"
"\tjr\tnz, .Lpass0\n"
"\tld\ta, (pending)\n"
"\tcp\ta, 0x0FE\n"
"\tjr\tz, .Lshow\n"
"\tinc\ta\n"
"\tjr\tnz, .Lpass0\n"
"\tld\t(pending), a\n"
/* A=2 refused it, so the OS evaluated it and shows its own result, which is
   Ans now. */
"\tpush\thl\n"
"\tpush\tde\n"
"\tpush\tbc\n"
"\tld\thl, OP1\n"
"\tld\tde, lastAns + 1\n"
"\tld\tbc, 9\n"
"\tldir\n"
"\tld\ta, 0x0FF\n"
"\tld\t(lastAns), a\n"
".Lz:\n"
"\tpop\tbc\n"
"\tpop\tde\n"
"\tpop\thl\n"
".Lpass0:\n"
"\tcp\ta, a\n"
"\tret\n"
".Lshow:\n"
"\txor\ta, a\n"
"\tld\t(pending), a\n"
"\tpush\thl\n"
"\tpush\tde\n"
"\tpush\tbc\n"
/* Run the flag prologue the OS runs at 0x97C7F before its own result
   display, so dispResult sees the state it was written against. */
"\tld\thl, 0x0D000D3\n"        /* iy+83 bit 7: "replace the last history entry" */
"\tres\t7, (hl)\n"
"\tld\thl, 0x0D0009F\n"        /* iy+31: number formatter state */
"\tres\t0, (hl)\n"
"\tres\t4, (hl)\n"
"\tld\thl, 0x0D000C9\n"        /* iy+73: what the OS sets around FormDisp */
"\tset\t2, (hl)\n"
"\tres\t6, (hl)\n"
"\tld\t(0x0D00338), a\n"
"\tld\t(0x0D00339), a\n"
"\tld\t(0x0D02713), a\n"
"\tld\t(0x0D00334), a\n"
"\tld\t(0x0D02506), a\n"
"\tld\thl, 0x0D0033A\n"
"\tld\t(0x0D00335), hl\n"
"\tjr\t.Lanswer\n"
".Leval0:\n"                     /* the way to .Leval from A's dispatch, in jr range */
"\tjr\t.Leval\n"
".Lanswer:\n"
/* A=2 left the answer at lastAns: its length (1..64), then its tokens.
   They are copied to ansBuf, not OP3: past 26 they would run over OP4..OP7.
   0x91FC2 reads the text as glyphs and turns them into tokens (0xB19FB), which
   leaves all of ours alone but one: 0x10, the '(' token, is the root glyph
   there, and becomes sqrt( and swallows the next byte. So '(' goes in as its
   glyph, 0x28, which that pass turns back into the token.
   In MathPrint the type is 0x18, what the OS gives a fraction result, and '/'
   goes in as the n/d glyph 0xF6 (filed as EF 2E): the OS then draws the
   answer in 2D, fractions stacked and every power raised. Type 0 draws flat,
   and in Classic 0xF6 would show as itself, so Classic keeps both as they
   were. lastAns keeps '/' either way.
   Classic right-aligns by BC, which must be the columns drawn: sqrt( is 2,
   " or " and sin( are 4, and the rest (measured). The engine counts them and
   leaves the count at ansBuf+64 (its out[MAXOUT]); it is read before the
   copy below, which may put the NUL there. Counting bytes here would miscount
   cos(, which goes in as the letters c o s. MathPrint ignores BC. */
"\tld\tbc, 0\n"
"\tld\ta, (ansBuf + 64)\n"
"\tld\tc, a\n"
"\tpush\tbc\n"
"\tld\thl, lastAns\n"
"\tld\tb, (hl)\n"
"\tinc\thl\n"
"\tld\tde, ansBuf\n"
"\tld\tc, 0x83\n"                /* what '/' goes in as */
"\tld\ta, (mpFlags)\n"
"\tand\ta, 0x20\n"
"\tjr\tz, .Lcopy\n"
"\tld\tc, 0x0F6\n"
".Lcopy:\n"
"\tld\ta, (hl)\n"
"\tinc\thl\n"
"\tcp\ta, 0x10\n"
"\tjr\tnz, .Lnotlp\n"
"\tld\ta, 0x28\n"
".Lnotlp:\n"
"\tcp\ta, 0x83\n"
"\tjr\tnz, .Lput\n"
"\tld\ta, c\n"
".Lput:\n"
"\tld\t(de), a\n"
"\tinc\tde\n"
"\tdjnz\t.Lcopy\n"
"\txor\ta, a\n"
"\tld\t(de), a\n"
"\tld\ta, c\n"                  /* 0x83 -> type 0, 0xF6 -> 0x18 */
"\tsub\ta, 0x83\n"
"\tjr\tz, .Ltype\n"
"\tld\ta, 0x18\n"
".Ltype:\n"
"\tpop\tbc\n"
"\tld\thl, ansBuf\n"
"\tcall\tdispResult\n"        /* A = 0: a REAL, right-aligned; 0x18: MathPrint 2D */
/* The OS sets donePrgm (iy+0 bit 5) before every parse and, when the hook
   reports NZ here, prints "Done" if it is still set (0x587D6). Its own result
   path never looks at it. Measured: without this, Done follows the answer. */
"\tld\thl, 0x0D00080\n"
"\tres\t5, (hl)\n"
"\tpop\tbc\n"
"\tpop\tde\n"
"\tpop\thl\n"
"\txor\ta, a\n"
"\tinc\ta\n"
"\tret\n"
".Lpass2:\n"                     /* .Lpass0, in jr range of .Leval; no jp: the app moves */
"\tcp\ta, a\n"
"\tret\n"
/* A=2: the OS is about to evaluate the entry. */
".Leval:\n"
"\tld\ta, (cxCurApp)\n"
"\tcp\ta, 0x40\n"
"\tjr\tnz, .Lpass2\n"
"\tld\ta, (pending)\n"
"\tinc\ta\n"
"\tjr\tnz, .Lpass2\n"
/* The engine is reached through hookPtr, so check it really aims at this body
   before trusting it: magic byte, then cp a,1. */
"\tpush\thl\n"
"\tld\thl, (hookPtr)\n"
"\tld\ta, (hl)\n"
"\tcp\ta, 0x83\n"
"\tjr\tnz, .Lnotus\n"
"\tinc\thl\n"
"\tld\ta, (hl)\n"
"\tcp\ta, 0x0FE\n"
"\tjr\tnz, .Lnotus\n"
"\tinc\thl\n"
"\tld\ta, (hl)\n"
"\tcp\ta, 1\n"
"\tjr\tz, .Lisus\n"
".Lnotus:\n"
"\tpop\thl\n"
"\tcp\ta, a\n"
"\tret\n"
".Lisus:\n"
"\tpush\tde\n"
"\tpush\tbc\n"
/* The entry, flattened, is the temp program # by now (measured). Look it up
   by name in OP1, and give the OS its OP1 back; ldir keeps the carry. */
"\tld\thl, OP1\n"
"\tld\tde, ansBuf\n"
"\tld\tbc, 9\n"
"\tldir\n"
"\tld\thl, OP1\n"
"\tld\t(hl), 5\n"                 /* ProgObj */
"\tinc\thl\n"
"\tld\t(hl), 0x23\n"              /* # */
"\tinc\thl\n"
"\tld\t(hl), 0\n"
"\tcall\tchkFindSym\n"
"\tpush\tde\n"
"\tld\thl, ansBuf\n"
"\tld\tde, OP1\n"
"\tld\tbc, 9\n"
"\tldir\n"
"\tpop\thl\n"
"\tjr\tc, .Lz2\n"
/* symce_engine(tokens, size, ansBuf, engineWork, lastAns, mode). It is compiled
   C, so the C convention: arguments on the stack, last one pushed first, three
   bytes each, popped by the caller; the result in A; ix preserved and
   everything else clobbered -- iy included, which the OS needs back as its
   flags pointer. */
"\tld\tde, 0\n"
"\tld\te, (hl)\n"
"\tinc\thl\n"
"\tld\td, (hl)\n"
"\tinc\thl\n"
"\tpush\tiy\n"
"\tld\ta, (ansFlags)\n"            /* mode: DEC wants decimals where they end, */
"\tand\ta, 1\n"
"\tld\tc, a\n"
"\tld\ta, (trigFlags)\n"           /* DEGREE refuses TOEXP( */
"\tand\ta, 4\n"
"\tor\ta, c\n"
"\tld\tc, a\n"
"\tld\ta, (mpFlags)\n"             /* and MathPrint fits wider answers in 26 columns */
"\tand\ta, 0x20\n"
"\tor\ta, c\n"
"\tld\tbc, 0\n"
"\tld\tc, a\n"
"\tpush\tbc\n"
"\tld\tbc, lastAns\n"
"\tpush\tbc\n"
"\tld\tbc, engineWork\n"
"\tpush\tbc\n"
"\tld\tbc, ansBuf\n"
"\tpush\tbc\n"
"\tpush\tde\n"
"\tpush\thl\n"
/* No call (hl) on the eZ80, and call nn needs our own absolute address: push
   a computed return address instead, then jump through the pointer. */
"\tld\thl, (hookPtr)\n"
"\tld\tde, .Lback - _symce_hook\n"
"\tadd\thl, de\n"
"\tpush\thl\n"
"\tld\thl, (hookPtr)\n"
"\tld\tde, _symce_hook_end - _symce_hook\n"
"\tadd\thl, de\n"
"\tld\thl, (hl)\n"
"\tjp\t(hl)\n"
".Lback:\n"
"\tld\thl, 18\n"
"\tadd\thl, sp\n"
"\tld\tsp, hl\n"
"\tpop\tiy\n"
"\tor\ta, a\n"
"\tjr\tz, .Lz2\n"
"\tinc\ta\n"
"\tjr\tz, .Lerr\n"
"\tdec\ta\n"
/* An answer: it is Ans from now on, and A=0 shows it. NZ makes the OS skip
   evaluating the entry (0x58763), so X's value never matters: X^2/X with
   X = 0 is X, not ERR:DIVIDE BY 0. */
"\tld\t(lastAns), a\n"
"\tld\tbc, 0\n"
"\tld\tc, a\n"
"\tld\thl, ansBuf\n"
"\tld\tde, lastAns + 1\n"
"\tldir\n"
"\tld\ta, 0x0FE\n"
"\tld\t(pending), a\n"
"\tjr\t.Lnz2\n"
".Lnz2:\n"
"\tpop\tbc\n"
"\tpop\tde\n"
"\tpop\thl\n"
"\txor\ta, a\n"
"\tinc\ta\n"
"\tret\n"
".Lz2:\n"
"\tpop\tbc\n"
"\tpop\tde\n"
"\tpop\thl\n"
"\tcp\ta, a\n"
"\tret\n"
/* 0xFF: a command it cannot do. The engine left "MESSAGE",0,"LINE",0,0 at
   ansBuf, 26 bytes at most: appErr1 then appErr2, which error 0x2B shows as
   ERROR: MESSAGE with LINE below and 1:Quit only. _JError reloads sp from
   errSP, so what is on the stack does not matter. Measured, both modes. */
".Lerr:\n"
"\tld\thl, ansBuf\n"
"\tld\tde, 0x0D025A9\n"           /* appErr1, appErr2 right after */
"\tld\tbc, 26\n"
"\tldir\n"
"\txor\ta, a\n"
"\tld\t(pending), a\n"
"\tld\ta, 0x2B\n"
"\tjp\t0x020790\n"                /* _JError */
"_symce_hook_end:\n"
);
