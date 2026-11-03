# SymCE: a computer algebra system (CAS) for the TI-84 Plus CE

DISCLAIMER: This application was written using Claude code. I have little to no actual coding experience, and this was created as a side project for fun. I cannot guarantee this code works or does as described below. Use at your own risk.

A computer algebra system for the **TI-84 Plus CE** (OS 5.8.4), living
inside the home screen. Type `2X+2X` and press ENTER: the stock OS says `0`
(it plugs in X), SymCE says `4X`.

If you are looking for a CAS program for the TI-84 Plus CE, this is one: exact
algebra, factoring, equation solving, derivatives, integrals and limits, like
the CAS on a TI-89 or TI-Nspire CX CAS, but on the TI-84 Plus CE you already
have. It is a flash app, so it works on the calculator itself, with no computer
needed after install.

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
- **Step-by-step solutions:** `steps(solve(2X+3=7,X))`, `steps(deriv(X²sin(X),X))`,
  `steps(factor(X²-5X+6))` show one move per step (2nd MATH, Algebra tab).
- Anything SymCE can't answer exactly is left to the OS, unchanged.

## Solving equations

`SOLVE` gives exact solutions, square roots included: `SOLVE(X²=2,X)` gives
`X=-√(2) or X=√(2)`. It also solves inequalities (`SOLVE(X²-2X-3≤0,X)` gives
`-1≤X≤3`) and systems of equations (`SOLVE({X+Y=3,X-Y=1},{X,Y})` gives
`X=2 and Y=1`). `CSOLVE` finds complex roots.

## Derivatives, integrals and limits

`DERIV(X³)` gives `3X²`, `INTEGRAL(X²,X,0,3)` gives `9`, and
`LIMIT(sin(X)/X,X,0)` gives `1`. The Calculus menu also has tangent and
normal lines, Taylor polynomials, sums, products, arc length, and
differential equations (`DESOLVE(Y'=2Y,X,Y)` gives `Y=Ce^(2X)`).

## TI-BASIC programs

A TI-BASIC program can use the CAS. Write SymCE commands straight into the
program, for example `FACTOR(X²-1)→Str1` or `Disp FACTOR(X²-1)`. In the program
editor, type the command name in capitals with ALPHA, then `(`. The answer is a
string. `expr("FACTOR(X²-1)")` works too. Your program is never changed: SymCE
runs a hidden copy of it. A program whose last line is a SymCE command shows
that answer in place of `Done`.

Full command list, install steps and known limits: [docs/USAGE.md](docs/USAGE.md).

## Install

1. Download `SYMCE.8xp` and the `SYMCE#.8xv` files from the latest
   [release](../../releases/latest), or build them (below).
2. Send `SYMCE.8xp` and every `SYMCE#.8xv` to the calculator's **Archive**
   (together they no longer fit in RAM).
3. Run `prgmSYMCE` through an ASM launcher (OS 5.8.4 blocks `Asm(`; AsmHook
   works). It installs the SymCE flash app, about 660 KB, and needs about
   1.1 MB of free archive while installing. Later upgrades: APPS → SymCE → 5.
4. **APPS → SymCE** opens the settings: `1` CAS on/off, `2` insert or TI cursor, `3` Evo or TI font, `4` the GRAPH key (SymCE's full-screen grapher or TI's own), `5` run `prgmSYMCE` (upgrade with no AsmHook2).
5. The **GRAPH** key now opens SymCE's own full-screen grapher (all 320×240 pixels); CLEAR or 2nd QUIT leaves it for the home screen. It has TRACE, ZOOM, 2nd CALC and 2nd DRAW, in Function, Parametric, Polar and Seq mode, and draws the stat plots; what it leaves to TI is in [docs/USAGE.md](docs/USAGE.md). APPS → SymCE → 4 switches the key back to TI's graph.

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
| `symce/src/graph.c`, `graph_draw.c`, `graph_stat.c` | The full-screen grapher: Function/Par/Pol/Seq, TRACE, ZOOM, CALC, DRAW, stat plots. |
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
