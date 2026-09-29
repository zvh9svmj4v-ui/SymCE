/* SymCE font hook body (fontHookPtr 0xD025ED, hookflags3 bit 5): the Evo large
 * font. Called by Load_LFont (0x7BFEE) with A = 1, B = the glyph's display index
 * (not a token), HL = B*28; A = 2 is TRACE's width query, left to the OS. On a
 * glyph we draw: the 28 record bytes go to 0xD005A5, 0 to 0xD005A4 (the OS draws
 * that byte, with A3, as a row above the glyph: 0x0C is a 2-px tick over every
 * glyph, *measured*; the ROM's own loader leaves it zero),
 * and Z with HL = 0xD005A1 says "use this". NZ is the ROM glyph. The
 * OS reloads A from B afterwards, so B must survive (the rest is kept on NZ).
 *
 * Position independent, as hook.c. The OS is calling through the slot, so the
 * slot holds this body's base; the two words past the body (main.asm, relocated
 * by app_create) are font_map (256 bytes: 0 = not ours, else record number, 1
 * based) and the large records less one record. ~9 calls per home-screen key.
 * Measured and derived in docs/TI84CE-KNOWLEDGE.md section 4, "Font hook".
 */
__asm__(
"	.globl _symce_font\n"
"	.globl _symce_font_end\n"
"	.equ	fontHookPtr, 0x0D025ED\n"
"	.equ	lRecord, 0x0D005A4\n"          /* width byte, then 28 glyph bytes */
"_symce_font:\n"
"\t.byte\t0x83\n"
"\tcp\ta, 1\n"
"\tret\tnz\n"
"\tpush\thl\n"
"\tpush\tde\n"
"\tpush\tbc\n"
"\tld\thl, (fontHookPtr)\n"
"\tld\tde, _symce_font_end - _symce_font\n"
"\tadd\thl, de\n"
"\tpush\thl\n"
"\tld\thl, (hl)\n"                       /* font_map */
"\tld\tde, 0\n"
"\tld\te, b\n"
"\tadd\thl, de\n"
"\tld\ta, (hl)\n"                        /* record number */
"\tpop\thl\n"
"\tor\ta, a\n"
"\tjr\tz, .Lmiss\n"
"\tinc\thl\n"
"\tinc\thl\n"
"\tinc\thl\n"
"\tld\thl, (hl)\n"                       /* records - 28 */
"\tld\td, a\n"
"\tld\te, 28\n"
"\tmlt\tde\n"
"\tadd\thl, de\n"
"\tld\tde, lRecord + 1\n"
"\tld\tbc, 28\n"
"\tldir\n"
"\txor\ta, a\n"                            /* width byte 0: the OS sets 0x0C after drawing */
"\tld\t(lRecord), a\n"
"\tpop\tbc\n"
"\tpop\taf\n"                             /* de, hl: not kept on a hit */
"\tpop\taf\n"
"\tld\thl, lRecord - 3\n"
"\tcp\ta, a\n"
"\tret\n"
".Lmiss:\n"
"\tpop\tbc\n"
"\tpop\tde\n"
"\tpop\thl\n"
"\tld\ta, 1\n"                            /* A is 0 here; A = 1 and NZ, as it came */
"\tor\ta, a\n"
"\tret\n"
"_symce_font_end:\n"
);
