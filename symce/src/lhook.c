/* SymCE localize hook body (localizeHookPtr 0xD02611, hookflags3 bit 1): the Evo
 * small font. The OS calls it for its own translations (A = 0, 0x1A.., 0xB0..
 * seen) and for glyphs: A = 0x75 the small glyph (Load_Sfont 0xA55D3, B = display
 * index, HL = B*25), 0x76 a large glyph the font hook passed on, 0x77 a small
 * width. Only 0x75 is answered: the record (width byte, then 24 glyph bytes) goes
 * to 0xD005C5 and Z with HL = 0xD005C5 says "use this". Event 0x0A gets HL = (HL), what the
 * OS does with no hook; every other event is NZ and untouched, except the Z table .Ltab (the A sites that branch on NZ). Widths are TI's, so the 0x77 query and the drawn advance agree.
 *
 * Event 0xD3 (the error screen's description lines, 0x622F7, a B site): for error
 * 0x2B, SymCE's own, menu_err (menu.c; the third word past the body) builds the
 * lines at 0xD0EF00 and they go back as HL, NZ: the OS draws them as its own.
 * hook.c arms this hook for that when the Evo font has not (tempArm), and the
 * 0xD3 call disarms it again; while tempArm is set the glyphs are TI's.
 *
 * Position independent, and laid out as fhook.c: the three words past the body are
 * font_map, the small records less one record, and menu_err.
 */
__asm__(
"	.globl _symce_loc\n"
"	.globl _symce_loc_end\n"
"	.equ	locHookPtr, 0x0D02611\n"
"	.equ	sRecord, 0x0D005C5\n"
"	.equ	errNo, 0x0D008DF\n"
"	.equ	hookFlags3, 0x0D000B5\n"       /* iy + 0x35: bit 1 = localize hook on */
"	.equ	tempArm, 0x0D0EE06\n"          /* set by hook.c: this hook is on for one error screen only */
"_symce_loc:\n"
"\t.byte\t0x83\n"
"\tcp\ta, 0x75\n"
"\tjr\tz, .Lglyph\n"
"\tcp\ta, 0xD3\n"
"\tjr\tz, .Ld3\n"
"\tcp\ta, 0x0A\n"
"\tjr\tz, .L0a\n"
/* Every other event: NZ with A and HL as they were (what this hook has always
   answered; right at the sites that branch Z = "hook did it" and at every site
   that ignores the flags) -- except the few events in .Ltab, whose site branches
   on the flags where NZ means "the hook supplied HL" (Catalog Help). Those get Z,
   what the OS gives with no hook. docs/TI84CE-KNOWLEDGE.md, "Localize call sites". */
"\tpush\thl\n"
"\tpush\tde\n"
"\tpush\tbc\n"
"\tld\thl, (locHookPtr)\n"
"\tld\tde, .Ltab - _symce_loc\n"
"\tadd\thl, de\n"
"\tld\tbc, .Ltab_end - .Ltab\n"
"\tcpir\n"
"\tpop\tbc\n"
"\tpop\tde\n"
"\tpop\thl\n"
"\tret\tz\n"                             /* in the table: Z, A and HL untouched */
"\tcp\ta, 0x0A\n"                       /* NZ, A kept (never 0x0A here) */
"\tret\n"
".Ld3:\n"
"\tpush\thl\n"
"\tpush\tde\n"
"\tpush\tbc\n"
"\tpush\tiy\n"
"\tld\ta, (errNo)\n"
"\tand\ta, 0x7F\n"
"\tcp\ta, 0x2B\n"
"\tjr\tnz, .Ld3x\n"
"\tld\thl, (locHookPtr)\n"
"\tld\tde, .Ld3b - _symce_loc\n"
"\tadd\thl, de\n"
"\tpush\thl\n"                          /* menu_err returns to .Ld3b */
"\tld\thl, (locHookPtr)\n"
"\tld\tde, _symce_loc_end - _symce_loc + 6\n"
"\tadd\thl, de\n"
"\tld\thl, (hl)\n"
"\tjp\t(hl)\n"
".Ld3x:\n"
"\txor\ta, a\n"                         /* not ours: no lines */
".Ld3b:\n"                                /* A = 1 when menu_err made the lines */
"\tpop\tiy\n"
"\tpop\tbc\n"
"\tpop\tde\n"
"\tpop\thl\n"
"\tpush\taf\n"
"\tld\ta, (tempArm)\n"
"\tor\ta, a\n"
"\tjr\tz, .Ld3k\n"
"\txor\ta, a\n"
"\tld\t(tempArm), a\n"
"\tld\ta, (hookFlags3)\n"
"\tand\ta, 0xFD\n"
"\tld\t(hookFlags3), a\n"
"\tpush\thl\n"
"\tld\thl, 0\n"
"\tld\t(locHookPtr), hl\n"
"\tpop\thl\n"
".Ld3k:\n"
"\tpop\taf\n"
"\tor\ta, a\n"
"\tjr\tz, .Ld3n\n"
"\tld\thl, 0x0D0EF00\n"
".Ld3n:\n"
"\txor\ta, a\n"
"\tinc\ta\n"
"\tret\n"
".L0a:\n"                                /* the menu-name fetch (0x7BB8B) goes on with whatever HL the hook leaves: */
"\tld\thl, (hl)\n"                       /* the default is HL = (HL) (2nd CALC drew 3-root for every row) */
"\tret\n"
".Lglyph:\n"
"\tld\ta, (tempArm)\n"                 /* armed for an error screen: TI's glyphs */
"\tor\ta, a\n"
"\tjr\tz, .Lg0\n"
"\tld\ta, 0x75\n"
"\tor\ta, a\n"
"\tret\n"
".Lg0:\n"
"\tpush\thl\n"
"\tpush\tde\n"
"\tpush\tbc\n"
"\tld\thl, (locHookPtr)\n"
"\tld\tde, _symce_loc_end - _symce_loc\n"
"\tadd\thl, de\n"
"\tpush\thl\n"
"\tld\thl, (hl)\n"
"\tld\tde, 0\n"
"\tld\te, b\n"
"\tadd\thl, de\n"
"\tld\ta, (hl)\n"
"\tpop\thl\n"
"\tor\ta, a\n"
"\tjr\tz, .Lmiss\n"
"\tinc\thl\n"
"\tinc\thl\n"
"\tinc\thl\n"
"\tld\thl, (hl)\n"                       /* records - 25 */
"\tld\td, a\n"
"\tld\te, 25\n"
"\tmlt\tde\n"
"\tadd\thl, de\n"
"\tld\tde, sRecord\n"
"\tld\tbc, 25\n"
"\tldir\n"
"\tpop\tbc\n"
"\tpop\taf\n"
"\tpop\taf\n"
"\tld\thl, sRecord\n"
"\tcp\ta, a\n"
"\tret\n"
".Lmiss:\n"
"\tpop\tbc\n"
"\tpop\tde\n"
"\tpop\thl\n"
"\tld\ta, 0x75\n"
"\tor\ta, a\n"
"\tret\n"
".Ltab:\n"
"\t.byte\t0x42, 0x43, 0x7A, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xD8\n"
".Ltab_end:\n"
"_symce_loc_end:\n"
);
