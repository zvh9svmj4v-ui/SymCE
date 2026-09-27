# Plan: ship SymCE's hook as a self-installing flash app (the real y= fix)

**Status: done** on branch `symce-flash-app` (`6531b25` flash app, `2fe4016`
compiled engine answered at A=0, `e219b0b` OS check, `2b9188f` re-run). Kept as
the record of why; `docs/TI84CE-KNOWLEDGE.md` describes what shipped, which
differs below in places (the engine is compiled C, and the answer is computed
at A=0 from `begPC`, not from the edit buffer).

## Why (root cause, confirmed three ways)

OS 5.8.3+ blocks any hook whose armed pointer is not inside a flash app. On every
context switch into a full-screen app (Y=/WINDOW/MODE/GRAPH/QUIT) and on any
`ERR:` screen the OS revalidates every armed hook slot and hard-resets unless the
pointer is `<= 0x0BD869` (low OS flash) or `> lowest_app_addr` (inside an app).

1. Measured on the real ROM 2026-09-14 (memory `symce-hook-must-live-in-fixed-ram`).
2. AsmHook2's README: "OS 5.8.3 made it so that the original ASMHOOK could not
   work anymore due to the OS blocking hooks installed outside of apps."
3. Cap'n Hook's README: hooks live in RAM ("a deleted appvar ... until the next
   garbage collect"). RAM-resident dispatcher, so the same sweep rejects it.
   Cap'n Hook is a dead end on 5.8.3+.

`symce/src/main.c` arms `homescreenHookPtr = 0xD0F000` (RAM), so v15 reboots on
the first menu key, like every earlier release. No in-body gate can help: the
hook is not called before the sweep reboots (measured). Only flash residency fixes it.

## Template: vendor/asmhook2 (RoccoLox + jacobly), ~470 lines

- `include/app.inc` (MateoConLechuga, BSD-3): `app_start` emits the app header;
  `app_create` finds free flash past `$3b0000` (walks headers via `$22044`),
  unlocks flash (`installer.portUnlock`, `call $02e0`, `portLock`), writes the
  app, then patches every `app.base`-relative address from the relocation table.
  So app code may use absolute labels.
- `src/ports.asm`: `portSetup` pattern-matches bytes at `KeypadScanFull+10`
  (`ED 79 78 FE A0 28 01 CF`) to find the flash-unlock routine; "Cannot use this
  boot code" if absent. Works on 5.8.3+/5.8.4. Reuse verbatim.
- `src/main.asm` `installHook`: `ld hl, parser - 1` / `call ti.SetParserHook`.
  `parser` is a flash label, so the armed pointer is flash and survives.
- `src/installer.asm`: install / already-installed / delete-installer UX.

Output `.8xp` is a self-installing app installer, bootstrapped once via a
jailbreak (arTIfiCE). The app then survives reboots and RAM clears.

## Two shapes

- **A.** SymCE as its own app arming `homescreenHookPtr`.
- **B.** Fork AsmHook2 into one app arming both its parser hook and SymCE's
  homescreen hook. One install, keep the RoccoLox/jacobly credit.

Recommend **B**: AsmHook2 is always present anyway (it is how ASM runs at all).

## Steps

1. Acquire `fasmg` (M0 blocker). Build `jacobly0/fasmg-ez80` from source, or a
   live mirror. Nothing below builds without it.
2. Port the hook body from `symce/src/hook.c` inline asm to fasmg `.asm`. It is
   already PIC eZ80, so it drops in; keep it PIC for the first build.
3. Arm from inside the app: `ld hl, symce_hook - 1` / `call ti.SetHomescreenHook`
   (`0x021410`; `homescreenHookActive` = bit 4 of `iy+0x34`).
4. Scratch stays in RAM as data only: `baseTop 0xD0EE00`, `baseOk 0xD0EE03`,
   `ansLen 0xD0EE04`, `ansBuf 0xD0EE05`. Re-check the range with `tools/emu/ramscan.py`.
5. Reuse `ports.asm` and `app.inc` verbatim.
6. Build: `fasmg src/main.asm SYMCE.8xp`.
7. Remove the RAM-arming path from `main.c` (`HOOK_RAM`, `HOOK_PTR = HOOK_RAM`).

## Test gates, in order

1. `make -C symce check` green against the ported bytes (0 disagreements).
2. CEmu + real ROM: after opening the app, `y=`, `mode`, `window`, `graph`,
   `quit`, `1/0 ENTER` must NOT reboot. The RAM build fails all of them.
3. `tools/emu/e2e.py` green (echo left, answer right, clean `2nd ENTRY`).
4. Real calc: bootstrap once, then survive a reboot and a RAM clear.

## Risks

- `portSetup` bails on an unknown boot code; confirm on the user's exact calc.
- Each install writes flash; do not loop it.
- AsmHook2 holds `parserHookPtr`, SymCE `homescreenHookPtr`: different slots.
- Keep Mateo's BSD-3 notice and RoccoLox/jacobly copyright when reusing files.

## Not the problem, do not touch

Engine (0 fuzz disagreements), echo/display via `0x91FC2`, MathPrint `editTop` gate.
