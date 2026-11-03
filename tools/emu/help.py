#!/usr/bin/env python3
"""+ on an item of SymCE's tabs in 2nd MATH: its own help screen (menu.c, menu_help,
reached from the raw key hook), not the OS's, which explains the item's key code.

    python3 help.py

For an Algebra, Calculus and Geometry command (Classic, so the entry reads back through
textShadow): the screen under the help is whole again after CLEAR and after ALPHA, with the
same item highlighted (menuSelected); ENTER types the command; 2nd QUIT goes home; a submenu
item eats the +. Then once with the Evo font off, which arms a different raw key hook body.
"""
import sys
import screen
import e2e

VRAM = 0xD40000
BAND = ("vr", VRAM + 30 * 640, 210 * 640)         # everything under the status bar
TAB = {"alg": "left left left", "calc": "left left", "geo": "left"}
FONT_OFF = "apps " + "down " * 16 + "enter . . 3 . clear . "


def run(keys, pre="", lead="2nd math "):
    r = screen.read([("menu", 0xD00824, 3), ("txt", screen.TEXTSHADOW, screen.COLS * screen.ROWS)],
                    keys=e2e.PROLOGUE + pre + lead + keys, step=1, files=e2e.FILES, launch=e2e.LAUNCH,
                    lead=False, settle=1900, crcs=[BAND])
    return r["vr"], r["menu"], "".join(screen.render(r["txt"]))


fails = 0


def check(ok, what):
    global fails
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    fails += not ok


def case(tab, n, cmd, pre="", label=""):
    open_ = TAB[tab] + " . " + "down " * n
    vr0, menu0, _ = run(open_, pre)
    vr1, menu1, _ = run(open_ + " + . .", pre)
    check(vr1 != vr0 and menu1 == menu0, "%s %s: + draws a screen over the menu" % (label, cmd))
    for key in ("clear", "alpha"):
        vr, menu, _ = run(open_ + " + . . %s . ." % key, pre)
        check(vr == vr0 and menu == menu0, "%s %s: %s puts the menu back as it was" % (label, cmd, key))
    _, menu, txt = run(open_ + " + . . enter . .", pre)
    check(menu[0] == 0 and cmd.lower() in txt.lower(), "%s %s: ENTER types it (%r)" % (label, cmd, txt.strip()[:26]))
    _, menu, txt = run(open_ + " + . . 2nd mode . .", pre)
    check(menu[0] == 0 and cmd.lower() not in txt.lower(), "%s %s: 2nd QUIT goes home" % (label, cmd))


POP = "alpha down "


def popup_case(path, cmd, pre, label):
    """+ in the ALPHA+DOWN popup (path: keys from the open popup to the item)."""
    run0 = lambda k: run(POP + path + k, pre, "")
    vr0, menu0, _ = run0("")
    vr1, menu1, _ = run0(" + . .")
    check(vr1 != vr0, "%s popup %s: + draws the help screen" % (label, cmd))
    vr, menu, _ = run0(" + . . clear . .")
    check(vr == vr0 and menu == menu0, "%s popup %s: CLEAR is back to the popup, same item, no residue" % (label, cmd))
    vr, _, _ = run0(" + . . alpha . .")
    check(vr == vr0, "%s popup %s: ALPHA is back to the popup" % (label, cmd))
    _, _, txt = run0(" + . . clear . . enter . .")
    check(cmd.lower() in txt.lower(), "%s popup %s: CLEAR then ENTER picks the same item (%r)" % (label, cmd, txt.strip()[:26]))
    _, _, txt = run0(" + . . enter . .")
    check(cmd.lower() in txt.lower(), "%s popup %s: ENTER pastes it (%r)" % (label, cmd, txt.strip()[:26]))
    _, _, txt = run0(" + . . 2nd mode . .")
    check(cmd.lower() not in txt.lower(), "%s popup %s: 2nd QUIT pastes nothing" % (label, cmd))
    home, _, _ = run0(" clear . .")           # the same one level up, no detour
    vr, _, _ = run0(" + . . clear . . clear . .")   # help detour + one level up
    check(vr == home, "%s popup %s: closing it after help leaves the home screen clean" % (label, cmd))


for label, pre in (("Evo on", ""), ("Evo off", FONT_OFF)):
    if label == "Evo on":
        case("alg", 0, "SOLVE(", pre, label)
        case("calc", 6, "FMIN(", pre, label)
        case("calc", 14, "DESOLVE(", pre, label)
        case("geo", 9, "PARTITION(", pre, label)
        open_ = "left left left . down down down down"          # 5: Polynomial Tools >
        vr0, menu0, _ = run(open_, pre)
        vr1, menu1, _ = run(open_ + " + . .", pre)
        check(vr1 == vr0 and menu1 == menu0, "%s submenu item: + changes nothing" % label)
        open_ = "left left left . " + "down " * 10             # B: Matrix >, the 11th Algebra item
        vr0, menu0, _ = run(open_, pre)
        vr1, menu1, _ = run(open_ + " + . .", pre)
        check(vr1 == vr0 and menu1 == menu0, "%s Matrix > item: + changes nothing" % label)
    else:
        case("calc", 12, "IMPDIF(", pre, label)
    popup_case("", "SOLVE(", pre, label)                                    # Algebra, first item
    popup_case("down " * 4 + "enter . " + "down " * 8, "PARTFRAC(", pre, label)   # Algebra > Polynomial Tools, last
    popup_case("right " + "down " * 6, "FMIN(", pre, label)                  # Calculus tab
    popup_case("down " * 4 + "enter . " + "down " * 5, "POLYGCD(", pre, label)      # Algebra > Polynomial Tools
    popup_case("right " + "down " * 11 + "enter . down", "SERIES(", pre, label)    # Calculus > Series
    popup_case("left " + "down " * 10 + "enter . " + "down " * 3, "CIRCUMCENTER(", pre, label)  # Geometry > Triangle
    popup_case("math ", "MDET(", pre, label)                                  # Algebra > Matrix
    popup_case("math " + "down " * 6, "NORM(", pre, label)                    # the last one
    v0, _, _ = run(POP + "down " * 4, pre, "")                              # a submenu's name: + does nothing
    v1, _, _ = run(POP + "down " * 4 + "+ . .", pre, "")
    check(v0 == v1, "%s popup submenu item: + changes nothing" % label)
print("ALL PASS" if not fails else "%d FAILED" % fails)
sys.exit(1 if fails else 0)
