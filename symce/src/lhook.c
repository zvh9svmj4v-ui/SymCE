/* SymCE localize hook body (localizeHookPtr 0xD02611, hookflags3 bit 1): the Evo
 * small font. The OS calls it for its own translations (A = 0, 0x1A.., 0xB0..
 * seen) and for glyphs: A = 0x75 the small glyph (Load_Sfont 0xA55D3, B = display
 * index, HL = B*25), 0x76 a large glyph the font hook passed on, 0x77 a small
 * width. Only 0x75 is answered: the record (width byte, then 24 glyph bytes) goes
 * to 0xD005C5 and Z with HL = 0xD005C5 says "use this". Everything else is NZ and
 * untouched. Widths are TI's, so the 0x77 query and the drawn advance agree.
 *
 * Position independent, and laid out as fhook.c: the two words past the body are
 * font_map and the small records less one record.
 */
__asm__(
"	.globl _symce_loc\n"
"	.globl _symce_loc_end\n"
"	.equ	locHookPtr, 0x0D02611\n"
"	.equ	sRecord, 0x0D005C5\n"
"_symce_loc:\n"
"\t.byte\t0x83\n"
"\tcp\ta, 0x75\n"
"\tret\tnz\n"
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
"_symce_loc_end:\n"
);
