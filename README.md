# SymCE

DISCLAIMER: This application was written using Claude code. I have little to no actual coding experience, and this was created as a side project for fun. I cannot guarantee this code works or does as described below. Use at your own risk.

A computer algebra system for the **TI-84 Plus CE** (OS 5.8.4), living
inside the home screen. Type `2X+2X` and press ENTER: the stock OS says `0`
(it plugs in X), SymCE says `4X`.

```
(X+1)^2                 -> X²+2X+1
(X²+5X+6)/(X²-4)        -> (X+3)/(X-2)
FACTOR(X^4-1)           -> (X-1)(X+1)(X²+1)
SOLVE(√(X+1)=X,X)       -> X=(1+√(5))/2
INTEGRAL(√(X+1),X)      -> 2X√(X+1)/3+2√(X+1)/3
MIDPOINT((1,2),(4,7))   -> (5/2,9/2)
```

- Exact answers: fractions of polynomials in lowest terms, square roots,
  `abs(`, several variables; MathPrint answers drawn 2D.
- **ALPHA + DOWN** opens a TI-Nspire-style menu with three tabs: Algebra
  (solve, factor, expand, polynomial tools, complex), Calculus (derivatives,
  integrals, limits, sums, series) and Geometry (distance, midpoint, lines,
  triangle centres, transformations). The same tabs sit in **2nd MATH**.
- Anything SymCE can't answer exactly is left to the OS, unchanged.

Full command list, install steps and known limits: [docs/USAGE.md](docs/USAGE.md).

## Install

1. Download `SYMCE.8xp` and the `SYMCE#.8xv` files from the latest
   [release](../../releases/latest), or build them (below).
2. Send `SYMCE.8xp` and every `SYMCE#.8xv` to the calculator's **Archive**
   (together they no longer fit in RAM).
3. Run `prgmSYMCE` through an ASM launcher (OS 5.8.4 blocks `Asm(`; AsmHook
   works). It installs the SymCE flash app, about 160 KB, and needs about
   twice that in free archive while installing. Later upgrades: APPS → SymCE → 5.
4. **APPS → SymCE** opens the settings: `1` CAS on/off, `2` insert or TI cursor, `3` Evo or TI font, `4` the graph viewer, `5` run `prgmSYMCE` (upgrade with no AsmHook2).

## Build

Needs the [CE C/C++ toolchain](https://github.com/CE-Programming/toolchain)
(CEdev, expected at `~/CEdev`) and fasmg:

```sh
tools/get-fasmg.sh          # fetches fasmg into tools/bin
make -C symce               # symce/bin/SYMCE.8xp + SYMCE1.8xv, SYMCE2.8xv, ...
```

## Tests

```sh
make -C symce check         # host: engine vs sympy, hook bytes in an eZ80 simulator (python3, sympy)
make -C symce emu           # real ROM in CEmu: crashes, end-to-end keys, install, engine on-device
```

`make check` fuzzes the engine against sympy (and against its predecessor,
byte for byte) and runs the assembled hook under `tools/ez80sim.py`.
`make emu` needs [CEmu](https://github.com/CE-Programming/CEmu) and a ROM
dumped from your own calculator: see [tools/rom-setup.md](tools/rom-setup.md)
and [tools/emu/README.md](tools/emu/README.md). No ROM or TI OS file is in
this repository.

## Layout

| Path | What |
|---|---|
| `symce/src/engine.c` | The CAS: parser, polynomial/rational arithmetic, roots, calculus, geometry, printer. Plain C, compiled with ez80-clang. |
| `symce/src/hook.c`, `mhook.c` | Home-screen hook bodies, inline eZ80 assembly. |
| `symce/src/menu.c` | The ALPHA+DOWN popup and the 2nd MATH tabs. |
| `symce/app/` | fasmg sources for the flash app and its installer. |
| `tools/` | Host tests, the eZ80 simulator, `relocs.py` (turns the engine into relocatable app code). |
| `tools/emu/` | CEmu-driven tests on a real ROM. |
| `docs/TI84CE-KNOWLEDGE.md` | Reverse-engineering notebook: OS 5.8.4 addresses, hooks, flags, traps. |
| `docs/history/` | Original design spec, plans and research notes (their `symcore/` is the PineappleCAS prototype, since removed). |

## Credits

- The first prototype was built on [PineappleCAS](https://github.com/nathanfarlow/PineappleCAS)
  (MIT); the current engine is a from-scratch replacement.
- Factoring and rewrite ideas from [KhiCAS](https://www-fourier.univ-grenoble-alpes.fr/~parisse/giac.html);
  no code copied.
- The optional Evo-style font (`symce/font/`) is derived from
  [Roboto](https://github.com/googlefonts/roboto) (SIL Open Font License 1.1,
  `symce/font/OFL.txt`).
- `symce/app/include/` holds files from the CE toolchain under their own
  licenses (see each header).

Not affiliated with or endorsed by Texas Instruments.

## License

[MIT](LICENSE), except the third-party files noted above.
