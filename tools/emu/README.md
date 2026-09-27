# Real-ROM measurement scripts

These run against the real TI-84 Plus CE ROM under `cemu-autotester`. They are
how the fixed addresses and gates in `symce/src/hook.c` were established, so
they are here to be re-run rather than trusted. `make -C symce emu` runs the
first four.

    export AUTOTESTER_ROM=~/CEdev/ti84pce.rom      # gitignored, never committed
    python3 crash.py         # Y=, WINDOW, MODE, GRAPH, ERR, STAT, APPS: hook survives, still answers
    python3 e2e.py           # ACCEPTANCE: type on the real home screen, assert the answer
    python3 lifecycle.py     # install, toggle from APPS, RAM clear, re-arm, re-install
    python3 engine_device.py # the shipped engine.s on the real OS vs the host build
    python3 screen.py        # dump the home screen text after any key sequence
    python3 ramscan.py       # which fixed RAM does the OS leave alone
    python3 edittop.py       # does editTop move when the cursor enters a box

Start with `e2e.py`. It is the only thing here that answers "does the product
work", and it passes: `2X+2X`->`4X`, `X*X`->`X²`, `X^4`->`X^4`,
`2X-5X`->`-3X`, `(X+1)^2`->`X²+2X+1`, `2X+3Y+X`->`3X+3Y`, `3X/2-X`->`X/2`,
`2+2`->`4` through the stock parser, and more. Per case it also checks that the
echoed entry is exactly what was typed and that the answer ends in the last
column (right-aligned, as a number; the old string answer was not), then that
`2nd ENTRY` recalls the entry alone and that ENTER on an empty line (re-run)
answers again.

MathPrint cannot be read through `textShadow`, but VRAM can: hash 4-byte
windows of `0xD40000` (640 bytes per pixel row, RGB565) with
`delay_after_step` set to 1 rather than 0 -- at 0 the emulator trips its
scheduler assertion after ~230 windows. A coarse render of every other row and
every fourth pixel is enough to see where text landed.

## What they produced

`ramscan.py` hashes every 2K window of `0xD02600..0xD1A000` at boot, drives the
home screen, and hashes again. Intersected over arithmetic, every menu, MODE,
graph/window/zoom/trace, editing and history recall, `0xD0EE00..0xD13E00` (20K)
came back byte-identical. SymCE's scratch (`pending`, the answer, the engine's
work area) lives at its start.

`edittop.py` reads the edit pointers in each MathPrint state. Virgin and flat
entries agree on `editTop`; every box type differs; the value itself moves as
the history grows. That is the gate.

## Traps

- Pass an **absolute** path to the JSON *and* set `cwd` to this directory, or
  you get `[Error] Couldn't change directory path`.
- **An unknown key name is skipped, not rejected.** stderr gets
  `[Error] unknown key "x2" was not pressed.` and the run continues and returns
  a perfectly plausible CRC. This silently invalidated a whole measurement round
  and shipped a crash. Every script here greps stderr for `unknown key`.
  There is no key name for x2 or x^-1: use `xton` for X, and `math 4` /
  `math 5` for the radicals.
- **ASM code DOES run on OS 5.8.4** -- via arTIfiCE, see `art.py`. `Asm(` is
  blocked and `action|launch` on an ASM target runs nothing, but launching
  arTIfiCE (BASIC `prgmA`, `isASM: false`) opens a shell that runs ASM programs.
  The shell picks by ON-CALC NAME: `down down down enter` runs one named `DEMO`,
  `down enter` runs one named `ASMHOOK`. Build test programs with `NAME=DEMO`.
- **arTIfiCE ZEROES the whole hook block on its only exit.** `mode` (or
  `2nd mode`) leaves the shell for the home screen and clears homescreenHookPtr,
  parserHookPtr, menuHookPtr, hooks1 and hooks4 to zero. Every other key keeps
  the hooks but stays in the shell, where keypresses never reach the OS entry
  line (editCursor does not move). It is a wipe, not a snapshot restore:
  re-launching arTIfiCE while a hook is armed does not preserve it. It also
  zeroes the BODY at `0xD0F000`, not just the pointers. **Never test through
  arTIfiCE.**
- **There IS an end-to-end test now: `e2e.py`.** The gap above is real but it
  was never the only route. This ROM already has the **AsmHook2 app in flash**
  (apps menu position 1: `apps down enter`). It prints "Installation
  successful!" and arms the parser hook, after which `prgmSYMCE` launches from
  the home screen with arTIfiCE never involved. A flash app does move when an
  app above it is deleted, and the OS moves a hook pointing into it along with
  it (`lifecycle.py`). That is the real user's install path, and it is what
  `e2e.py` drives.
- **The home screen text only exists in CLASSIC mode.** MathPrint draws glyphs
  straight to VRAM, so `textShadow` reads back blank and menus read back as
  non-ASCII. `screen.py` takes a `C` token in its key string for
  `action|useClassic`. Everything verified so far is therefore Classic-only.
- **`textShadow` holds display glyphs, not tokens.** The hook writes
  `OS_TOK_SQUARE` 0x0D and `OS_TOK_CUBE` 0x0F; they read back as **0x12** and
  **0xD5**. The cube is not the byte after the square -- measure, do not infer.
  `screen.GLYPHS` maps both.
- **CEmu's `sched_active(id)` assertion can also stop a read partway**, at a
  fixed emulated moment for a given key sequence. The warning says
  `emulator stopped after 30/67 windows`; unread bytes come back `FF`. The same
  keys with a different `settle` read fine, so `e2e.run` retries.
- In a key string `.` is a 2 s pause, `C` switches to Classic, `L` launches the
  target *there*. SymCE's tests pass `lead=False` and launch with `L` after
  `apps down enter` has armed AsmHook2; launched first, an ASM program runs
  nothing.
- For read-only measurement the `transfer_files` entry is a formality -- any
  `.8xp` will do, since nothing needs to run.
- The CRC is CRC-32C, which is affine, so `crcread.invert` turns the CRC of a
  range of 4 bytes or fewer back into the bytes. Larger ranges can only be
  compared, not read. This build does not write `failure_hash*_dump.bin`.
