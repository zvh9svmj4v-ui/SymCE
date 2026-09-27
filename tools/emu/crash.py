#!/usr/bin/env python3
"""Does the armed hook survive the keys that used to reboot the calculator?

OS 5.8.3+ re-checks every armed hook pointer when it switches to a full-screen
app (Y=, WINDOW, MODE, GRAPH, QUIT) and when it shows an ERR: screen, and resets
unless the pointer is inside a flash app. A reset clears RAM, so the hook
pointer and its enable bit read back as zero. This arms SymCE, presses one
trigger per run, returns to the home screen, types 2X+2X, and checks that the
hook is still armed and still answering.

    python3 crash.py                 # the flash-app build (default)
    python3 crash.py --ram V15.8xp   # an old RAM-armed build, expected to fail
"""
import argparse, sys
import screen
from e2e import PROLOGUE, FILES, LAUNCH

TRIGGERS = [
    ("y=",      "y= . 2nd mode ."),
    ("window",  "window . 2nd mode ."),
    ("mode",    "mode . 2nd mode ."),
    ("graph",   "graph . 2nd mode ."),
    ("1/0 ERR", "1 / 0 enter . enter ."),
    ("stat",    "stat . 2nd mode ."),
    ("apps",    "apps . clear ."),
    ("table",   "y= xton 2nd graph . 2nd mode ."),
    ("tblset",  "2nd window . 2nd mode ."),
    ("format",  "2nd zoom . 2nd mode ."),
    ("zoom",    "zoom . 2nd mode ."),
    ("trace",   "trace . 2nd mode ."),
    ("catalog", "2nd 0 . 2nd mode ."),
    ("mem",     "2nd + . 2nd mode ."),
    ("list",    "2nd stat . 2nd mode ."),
    ("draw",    "2nd prgm . 2nd mode ."),
    ("vars",    "vars . 2nd mode ."),
    ("math",    "math . 2nd mode ."),
    ("statedit", "stat enter . 5 enter . 2nd mode ."),
    ("off/on",  "2nd on . . on . ."),
    ("on",      "on . on ."),
    # program editor: PRGM NEW, name A, a line that SymCE would answer at home
    ("prgmedit", "prgm left enter . math enter . 2 xton + 2 xton enter . 2nd mode ."),
    # CATALOG commands run at home: G is tan, H is ^ (Histogram, then Horiz)
    # GarbageCollect copies the archive, SymCE's appvars and app included: at
    # 67 KB, 2X+2X typed 4 s after it sat unanswered, hook still armed
    # (measured); 10 s is enough.
    ("gc",      "2nd 0 . tan . enter . enter . enter . . . . ."),
    ("horiz",   "2nd 0 . ^ . down . enter . enter . enter . ."),
    # SymCE's ALPHA+DOWN menu: closed by QUIT, a pick (then cleared), GRAPH
    ("menu",    "alpha down . 2nd mode . alpha down . 4 . clear . alpha down . graph . 2nd mode ."),
    # The SymCE Algebra tab in 2nd MATH (LEFT three times from TEST): QUIT on
    # it, a pick (then cleared), RIGHT three times back to TEST and its =, and
    # 2nd MATH in the program editor, where it is stock
    ("tab",     "2nd math left left left . 2nd mode . 2nd math left left left . down . enter . clear . "
                "2nd math left left left . right . right . right . enter . clear . "
                "2nd math left left left . graph . 2nd mode ."),
    ("tabprgm", "prgm left enter . math enter . 2nd math left left left . enter . "
                "2nd math left left left . 2nd mode ."),
    # Its submenus: QUIT in one, a pick from one, GRAPH in one, and one opened
    # from the tab, then QUIT, then LEFT and CLEAR out of another
    ("submenu", "alpha down . 5 . 2nd mode . alpha down . 7 . 3 . clear . alpha down . 9 . graph . "
                "2nd mode . 2nd math left left left . 9 . 2nd mode . 2nd math left left left . 0 . left . clear . clear ."),
    # The Calculus popup (RIGHT from Algebra): QUIT on it, GRAPH scrolled to
    # its end, LEFT out of Numeric Calculations, LEFT to Algebra, CLEAR; LEFT
    # twice from Algebra to it and a pick, cleared; an OS key picked, cleared
    ("calcmenu", "alpha down . right . 2nd mode . alpha down . right . up . up . graph . 2nd mode . "
                 "alpha down . right . -1 . left . left . clear . alpha down . left . left . 1 . clear . "
                 "alpha down . right . -1 . 3 . clear ."),
    # The Calculus tab (LEFT twice from TEST): QUIT and GRAPH on it, its Numeric
    # Calculations popup QUIT, then LEFT LEFT CLEAR; a pick, and nDeriv(
    # through the popup, each cleared
    ("calctab", "2nd math left left . 2nd mode . 2nd math left left . graph . 2nd mode . 2nd math left left . up . enter . "
                "2nd mode . 2nd math left left . up . enter . left . left . clear . 2nd math left left . 9 . clear . "
                "2nd math left left . up . enter . 1 . clear ."),
    # The Geometry popup (LEFT from Algebra) and tab (LEFT from TEST): QUIT on
    # it, in its Triangle submenu, GRAPH in Transformations (UP to C), a pick
    # cleared; the tab's QUIT, its Circle popup QUIT, a pick cleared
    ("geo",     "alpha down . left . 2nd mode . alpha down . left . math . 2nd mode . "
                "alpha down . left . up . enter . graph . 2nd mode . alpha down . left . 2 . clear . "
                "2nd math left . 2nd mode . 2nd math left . up . up . enter . 2nd mode . "
                "2nd math left . 1 . clear ."),
]


def check(prologue, files, launch, trigger, shift=""):
    r = screen.read([("hookptr", 0xD025E1, 3), ("flag", 0xD000B4, 1),
                     ("screen", screen.TEXTSHADOW, screen.COLS * screen.ROWS)],
                    keys=prologue + trigger + " clear . " + shift + "2 xton + 2 xton enter .",
                    files=files, launch=launch, lead=False)
    ptr = int.from_bytes(r["hookptr"], "little")
    armed = ptr != 0 and r["flag"][0] & 0x10
    rows = [r["screen"][i * screen.COLS:(i + 1) * screen.COLS].strip(b" ")
            for i in range(screen.ROWS)]
    answered = b"4X" in rows
    return ptr, armed, answered, b"2X+2X" in rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ram", metavar="FILE", help="an old RAM-armed installer .8xp")
    ap.add_argument("--only", help="run one trigger by label")
    a = ap.parse_args()
    if a.ram:
        prologue, files, launch = ("C . apps down enter . clear . L . clear . ",
                                   (a.ram,), "SYMCE")
    else:
        prologue, files, launch = PROLOGUE, FILES, LAUNCH
    ok = True
    for label, keys in TRIGGERS:
        if a.only and a.only != label:
            continue
        ptr, armed, answered, echoed = check(prologue, files, launch, keys)
        if armed and not echoed:
            # A key of 2X+2X never arrived: the OS's battery-icon refresh
            # (idle loop 0x03609B -> DrawBatteryIndicator, interrupts off) ate
            # the emulator's short tap. Its timing shifts with everything run
            # before it, stock or not, so type again 100 ms later. A hook that
            # does not answer still fails: its echo is right.
            print("%-8s (a key was lost to the battery refresh; typing again)" % label)
            ptr, armed, answered, echoed = check(prologue, files, launch, keys, "w100 ")
        good = armed and answered
        ok &= bool(good)
        print("%-8s hookptr=%06X armed=%s answered=%s %s" % (
            label, ptr, bool(armed), answered, "ok" if good else "FAIL"))
    print("\nALL SURVIVE" if ok else "\nCRASHES ABOVE")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
