/* SymCE menu hook body (menuHookPtr): the SymCE tab in 2nd MATH (TEST).
 *
 * A shim into menu.c's menu_tab(event, index), which does the work in C. The
 * OS calls every menu hook event with A = event and C = tab or item index; see
 * docs/TI84CE-KNOWLEDGE.md. menu_tab returns 0 to leave the OS alone, and then
 * every register goes back as it came with Z, so the OS carries on as stock.
 * Anything else it returns goes back in HL with NZ: at A=0 that is the menu
 * table the OS then uses; at A=1/A=2 it means "drawn, skip yours". BC and DE
 * come back either way: after an NZ at A=2 the OS steps to the next tab in C.
 *
 * Position independent. The OS calls it with ix = body + 1 (0x257C1), so
 * menu_tab's address is read from the word just past this body, which
 * app_create relocates (app/main.asm).
 */
__asm__(
"	.globl _symce_mhook\n"
"	.globl _symce_mhook_end\n"
"_symce_mhook:\n"
"\t.byte\t0x83\n"
"\tpush\thl\n"
"\tpush\tde\n"
"\tpush\tbc\n"
"\tpush\taf\n"
"\tpush\tiy\n"                    /* C does not keep the OS's iy */
"\tpush\tbc\n"                    /* index = C */
"\tld\tc, a\n"
"\tpush\tbc\n"                    /* event = A */
"\tpush\tix\n"
"\tpop\thl\n"
"\tld\tde, .Lback - _symce_mhook - 1\n"
"\tadd\thl, de\n"
"\tpush\thl\n"                    /* menu_tab returns to .Lback */
"\tld\tde, _symce_mhook_end - .Lback\n"
"\tadd\thl, de\n"
"\tld\thl, (hl)\n"
"\tjp\t(hl)\n"
".Lback:\n"
"\tpop\tbc\n"
"\tpop\tbc\n"
"\tpop\tiy\n"
"\tld\tde, 0\n"
"\tor\ta, a\n"
"\tsbc\thl, de\n"
"\tjr\tnz, .Lown\n"
"\tpop\taf\n"
"\tpop\tbc\n"
"\tpop\tde\n"
"\tpop\thl\n"
"\tcp\ta, a\n"
"\tret\n"
".Lown:\n"
"\tpop\taf\n"
"\tpop\tbc\n"
"\tpop\tde\n"
"\tex\t(sp), hl\n"
"\tpop\thl\n"
"\txor\ta, a\n"
"\tinc\ta\n"
"\tret\n"
"_symce_mhook_end:\n"
);
