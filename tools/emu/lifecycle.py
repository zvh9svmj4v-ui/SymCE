#!/usr/bin/env python3
"""The SymCE app's life on the calculator, on the real ROM.

    python3 lifecycle.py

  * opening the app from APPS turns the hook off, and again turns it back on
  * a RAM clear drops the hook; opening the app re-arms it
  * running the installer again replaces the app (RAM clear or not) and arms it
  * the installer deletes v15's leftover appvars SYMCEHK and SYMCEPV
  * deleting an app above SymCE moves SymCE up in flash; the hook moves with it
  * deleting the app while it is on (2nd mem 2 A, del) disarms the hook
  * the upgrade path: delete, run the installer again, and it answers
  * the menu hook (the SymCE tab in 2nd MATH) goes on, off and moves with it
"""
import os, subprocess, sys
import screen
from e2e import PROLOGUE, FILES, LAUNCH, armed_in_flash

# SymCE's slot in this ROM's APPS menu, measured by trying each position.
# ponytail: fixed; if the ROM's app list changes, re-measure.
OPEN_APP = "apps " + "down " * 16 + "enter . . clear . "
RAM_CLEAR = "2nd + 7 1 2 . . clear . C . "
TYPE = "2 xton + 2 xton enter . "
# 2nd mem, Mem Mgmt/Del, A:Apps, SymCE (22nd in this ROM's list), del, 2:Yes.
DELETE_APP = "2nd + 2 " + "down " * 10 + "enter . " + "down " * 21 + "del . . 2 . . 2nd mode . clear . "
# The same for AsmHook2: first in that list, and installed just above SymCE.
DELETE_ABOVE = "2nd + 2 " + "down " * 10 + "enter . del . . 2 . . 2nd mode . clear . "
CONVBIN = os.path.expanduser("~/CEdev/bin/convbin")
BODY = os.path.join(screen.HERE, "../../symce/obj/hook/hook.bin")


def old_appvar(name):
    """An archived appvar named like v15's leftovers; the contents don't matter."""
    path = "_%s.8xv" % name
    subprocess.run([CONVBIN, "-j", "bin", "-k", "8xv", "-r", "-n", name,
                    "-i", BODY, "-o", path], cwd=screen.HERE, check=True,
                   capture_output=True)
    return path


def state(keys, files=FILES, prologue=PROLOGUE):
    r = screen.read([("hookptr", 0xD025E1, 3), ("flag", 0xD000B4, 1),
                     ("ptemp", 0xD0259A, 3), ("mhook", 0xD02608, 3), ("hf4", 0xD000B6, 1),
                     ("screen", screen.TEXTSHADOW, screen.COLS * screen.ROWS)],
                    keys=prologue + keys, files=files, launch=LAUNCH, lead=False)
    raw = [r["screen"][i * screen.COLS:(i + 1) * screen.COLS] for i in range(screen.ROWS)]
    rows = [l.strip() for l, x in zip(screen.render(r["screen"]), raw)
            if l.strip() and x.strip(b" ") != b"\xe0"]      # not the cursor alone (e2e.run)
    if keys.endswith(TYPE) and "2X+2X" not in rows and not keys.startswith("w100"):
        # A key lost to the battery refresh (crash.py), maybe one before 2X+2X:
        # CLEAR on the app's screen, and 2 then closed it. Once more, all of the
        # keys 100 ms later.
        return state("w100 " + keys, files, prologue)
    # the menu hook's pointer while it is armed (hookflags4 bit 6), else 0
    state.menu = int.from_bytes(r["mhook"], "little") if r["hf4"][0] & 0x40 else 0
    return (int.from_bytes(r["hookptr"], "little"), r["flag"][0],
            int.from_bytes(r["ptemp"], "little"), rows)


def main():
    ok = True

    def check(label, good, detail):
        nonlocal ok
        ok &= bool(good)
        print("%-24s %s  %s" % (label, "ok  " if good else "FAIL", detail))

    ptr0, flag, _, _ = state("")
    m0 = state.menu
    check("installed and armed", armed_in_flash(ptr0, flag) and 0 < m0 < 0x3B0000,
          "hookptr=%06X menu=%06X" % (ptr0, m0))

    ptr, flag, _, rows = state(OPEN_APP + TYPE)
    check("app toggles off", ptr == ptr0 and not flag & 0x10 and "4X" not in rows and not state.menu,
          "flag=%02X rows=%s" % (flag, rows[-2:]))

    ptr, flag, _, rows = state(OPEN_APP + OPEN_APP + TYPE)
    check("app toggles back on", ptr == ptr0 and flag & 0x10 and "4X" in rows and state.menu == m0,
          "flag=%02X rows=%s" % (flag, rows[-2:]))

    ptr, flag, _, _ = state(RAM_CLEAR)
    check("RAM clear drops hook", ptr == 0 and not state.menu, "hookptr=%06X" % ptr)

    ptr, flag, _, rows = state(RAM_CLEAR + OPEN_APP + TYPE)
    check("app re-arms after clear", ptr == ptr0 and flag & 0x10 and "4X" in rows and state.menu == m0,
          "hookptr=%06X rows=%s" % (ptr, rows[-2:]))

    # A RAM clear keeps the app, so the installer used to refuse ("already
    # installed") and leave the hook off. Now it deletes the old app, packs
    # flash, writes this build in its place and arms it. After a RAM clear the
    # OS runs no asm program until AsmHook2 is reopened. (The OS pack leaves
    # PutS drawing without saving to textShadow, so the installer's message
    # can't be read back; the hook being armed again is the proof.)
    again = RAM_CLEAR + "apps down enter . clear . L . . . . clear . "
    ptr, flag, _, rows = state(again + TYPE + "y= . 2nd mode . ")
    check("installer replaces app", armed_in_flash(ptr, flag) and rows[-1:] == ["4X"]
          and state.menu == m0,
          "hookptr %06X -> %06X rows=%s" % (ptr0, ptr, rows[-2:]))

    ptr, flag, _, rows = state(again * 3 + TYPE + "y= . 2nd mode . ")
    check("replaced three times", ptr == ptr0 and flag & 0x10 and rows[-1:] == ["4X"],
          "hookptr %06X -> %06X rows=%s" % (ptr0, ptr, rows[-2:]))

    # Each leftover is a 14-byte entry in the program VAT, which grows DOWN to
    # pTemp, so deleting both moves pTemp up by 28.
    old = FILES + (old_appvar("SYMCEHK"), old_appvar("SYMCEPV"))
    _, _, before, _ = state("", files=old, prologue="C . apps down enter . clear . ")
    _, _, after, _ = state("", files=old)
    check("v15 appvars deleted", after - before == 28,
          "pTemp %06X -> %06X" % (before, after))

    # Leaving Mem Mgmt after deleting an app packs the apps below it up into
    # the hole, no GarbageCollect needed: SymCE moves up by AsmHook2's size.
    # The OS moves the hook pointer with it and redoes SymCE's relocations
    # (the engine address after the hook body moves too), so it still answers.
    ptr, flag, _, rows = state(DELETE_ABOVE + "y= . 2nd mode . clear . " + TYPE)
    check("app moved up by delete", armed_in_flash(ptr, flag) and ptr > ptr0 and
          rows[-1:] == ["4X"] and state.menu - ptr == m0 - ptr0, "hookptr %06X -> %06X rows=%s" % (ptr0, ptr, rows[-2:]))

    # The OS clears a hook that points into an app it deletes; no reset.
    ptr, flag, _, _ = state(DELETE_APP)
    check("delete app disarms", ptr == 0 and not flag & 0x10 and not state.menu,
          "hookptr=%06X flag=%02X menu=%06X" % (ptr, flag, state.menu))

    ptr, flag, _, rows = state(DELETE_APP + "L . . . clear . " + TYPE + "y= . 2nd mode . ")
    check("reinstall after delete", armed_in_flash(ptr, flag) and rows[-1:] == ["4X"],
          "hookptr=%06X rows=%s" % (ptr, rows[-2:]))

    print("\nALL PASS" if ok else "\nFAILURES ABOVE")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
