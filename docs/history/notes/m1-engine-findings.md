# M1 — symcore fork findings

**Date:** 2026-08-28
**Status:** Tasks 6–8 complete. Task 9 (eZ80 build + size) pending toolchain install.

## Baseline

- Upstream suite: **256/256 passing** in 601 microseconds on macOS arm64.
- SymCE guards: **12/12 passing** (`symcore/symce-tests.txt`).
- Host binary: 218 KB, built with plain `cc`. No CE toolchain required.

## Plan corrections (discovered during execution)

| Plan said | Reality |
|-----------|---------|
| "Upstream has no PC target; author `makefile.pc`" | Upstream ships **`pc.makefile`** *and* `CMakeLists.txt`. Reused `pc.makefile` instead of writing one. |
| Target switch macro is `__TICE__` | It is **`COMPILE_PC`**. |
| Task 9 must move `src/pc/` out of tree for the eZ80 build | Unnecessary. Every file in `src/pc/` is already wrapped in `#ifdef COMPILE_PC`, so it compiles to nothing for the calculator. |
| Fork lives at `src/symcore/` | Lives at **`symcore/`** (repo root), preserving upstream's internal layout so both upstream makefiles work unmodified and upstream merges stay clean. |
| Test delimiter is `\|` | It is `;` — `operation; input; expected`. `deriv` takes four fields: `deriv; expr; var; expected`. |

## Test-format hazards

1. **`-` is NEGATE, `_` is SUBTRACT.** `3-5` evaluates to `3 * -5 = -15`. This mirrors the TI keyboard's distinct `(-)` and `-` keys.
2. **The runner silently skips lines it cannot parse.** A malformed guard reads as green rather than failing. Two of our `deriv` guards were dropped this way before being caught.

Mitigation: `pc.makefile`'s `test` target asserts `SYMCE_EXPECTED = 12` guards actually *ran*, not merely that none failed. Verified by deliberately malforming a line — the guard reports `expected 12 ... 11 ran` and exits 1.

## Capability probe — what the engine already does

Probed directly against the spec's v1 targets.

| Target | Status | Actual output |
|--------|--------|---------------|
| `2X+2X` → `4X` | **works** | — |
| `X+X+X` → `3X`, `X*X` → `X^2` | works | — |
| `2/4` → `1/2` (exact rationals) | works | — |
| `1/sqrt(2)` → `sqrt(2)/2` (rationalize) | **works** | — |
| `(A+B)(B+C)` expand | works | — |
| `deriv X^3` → `3X^2` | works | — |
| `sqrt(2)+sqrt(2)` → `2sqrt(2)` | **missing** | `2^(3/2)` |
| `sqrt(2)*sqrt(3)` → `sqrt(6)` | **missing** | `2^(1/2)3^(1/2)` |
| `sqrt(3+2sqrt(2))` → `1+sqrt(2)` | **missing** | unchanged |

**The core promise of the project already works.** `2X+2X` → `4X` needs no engine work at all — only the hook layer (M2).

## This reframes M4

The engine has **no surd representation**. Radicals are stored as rational powers:
`sqrt(2)` is `2^(1/2)`. So `2^(3/2)` is *mathematically correct* for `2sqrt(2)` — it
is a display and canonicalization difference, not a wrong answer.

M4 therefore splits into two unrelated problems:

- **(a) Canonicalization — real CAS work.** Combining radical products
  (`2^(1/2)·3^(1/2)` → `6^(1/2)`) and denesting `sqrt(3+2sqrt(2))`. Belongs in
  `cas/radical.c` as spec'd.
- **(b) Surd display — belongs to `render.c` (M3), not the CAS.** Deciding that
  `2^(3/2)` prints as `2√2` is a rendering choice. Building it into the simplifier
  would fight the engine's own canonical form.

Spec §5.1 assigns all of this to `cas/radical.c`. Half of it should move to M3's
renderer. Recommend revisiting spec §5.1 and the M3/M4 split before starting M4.

## Not yet done

- Task 9: eZ80 build and size measurement. Toolchain downloaded
  (`~/Downloads/symce/CEdev-macOS-arm.dmg`) but not installed — installing writes to
  `/Applications` and `~/.zshrc`, deferred pending explicit go-ahead.
- Task 10: §6 strategy decision, blocked on Task 9's measurement.
