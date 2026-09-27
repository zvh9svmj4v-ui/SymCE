# SymCE — Design Spec

**Date:** 2026-08-28
**Target:** TI-84 Plus CE, OS 5.8.4.0058, jailbroken (arTIfiCE v2.1 + AsmHook)
**Status:** Approved design, pre-implementation

---

## 1. Problem

The stock TI-84 Plus CE evaluates numerically. Every variable `A`–`Z` and `θ` always
holds a real value, defaulting to `0`. So `2X+2X` parses as `2(0)+2(0)` and returns `0`.

We want symbolic evaluation — `2X+2X` → `4X` — **without giving up the stock OS**.
The menus, the catalog, TI-BASIC, graphing, stat vars, and the general feel of the
machine all stay exactly as they are. The CAS is additive.

## 2. Goals

- `2X+2X` → `4X` on the normal home screen, with no mode switch and no launcher.
- Numeric behavior is **bit-identical** to stock when no free symbolic variable is present.
- Pretty-printed 2D output (stacked fractions, raised exponents, radical signs) that
  survives scrolling.
- Exact arithmetic: `2/4` → `1/2`, not `0.5`.
- Simplify, expand, factor, derivative, solve (linear/quadratic/rational), and exact
  radical handling including denesting.
- Never hang, never brick, never eat user variables.

## 3. Non-goals (v1)

- Symbolic radicands (`√(X²)` → `|X|`). **Architected for, not built** — see §8.3.
- Symbolic integration, limits, series, Gröbner bases, differential equations.
- Matrices/lists as symbolic objects. Numeric only, via OS passthrough.
- Replacing the OS home screen, equation editor, or any menu.
- Polynomial solving above degree 2.

## 4. Verified facts

Everything below was confirmed against the CE toolchain headers or MIT-licensed CE
source, not extrapolated from TI-83 Plus documentation. Sources in §15.

### 4.1 Jailbreak

arTIfiCE v2.1 supports OS 5.3 → 5.8.4. The target calculator is in range and already
running it with AsmHook.

### 4.2 Hook syscalls exist on the CE

From `CE-Programming/toolchain` → `src/include/ti84pceg.inc`:

```
_SetHomescreenHook := 0021410h    _ClrHomescreenHook := 0021414h
_SetCxReDispHook   := 00214F8h    _ClrCxReDispHook   := 00214FCh
_SetParserHook     := 002149Ch    _ClrParserHook     := 00214A0h
_SetTokenHook      := 00213F8h    _ClrTokenHook      := 00213FCh
```

### 4.3 Hook pointers and enable bits

From `commandblockguy/capnhook` → `src/hook_equates.inc`:

```
homescreenHookPtr = 0D025E1h      hookflags2 = iy+34h, bit 4 = homescreenHookActive
cxRedispHookPtr   = 0D02605h      hookflags4 = iy+36h, bit 5 = cxRedispHookActive
parserHookPtr     = 0D025F9h      hookflags4 = iy+36h, bit 1 = parserHookActive
tokenHookPtr      = 0D0260Eh      hookflags3 = iy+35h, bit 0 = tokenHookActive
```

### 4.4 Home screen edit buffer — a gap buffer

From `ti84pceg.inc`:

```
editTop    = 0D02437h    ; start of entry
editCursor = 0D0243Ah    ; end of pre-cursor text
editTail   = 0D0243Dh    ; start of post-cursor text
editBtm    = 0D02440h    ; end of entry
editSym    = 0D0244Eh    ; VAT ptr of variable being edited
textShadow = 0D006C0h    ; 260 bytes, screen text shadow
cxCurApp   = 0D007E0h    ; current context
```

The current entry is the concatenation of `[editTop, editCursor)` and
`[editTail, editBtm)`. Contents are **OS tokens, not ASCII** — `sin(` is one token,
`X` is one token. This is easier to parse than text: no lexing required.

### 4.5 Homescreen hook event codes

Documented for the TI-83 Plus; Cap'n Hook's own API docs point at the same page for CE
semantics, so the table is expected to transfer. **M0 verifies this empirically rather
than assuming it.**

| A | Event | Override mechanism |
|---|-------|--------------------|
| 0 | Display result; `OP1` = value | return NZ to suppress display |
| 1 | Key pressed; `B` = keycode | return NZ to swallow the key |
| 2 | Expression evaluation; `OP1` = prgm | return NZ to cancel |
| 3 | Context switch to home screen; `B` = previous context | must return Z |

Events 1 and 0 are the two integration points.

### 4.6 Hook ABI constraint — this shapes the architecture

From `capnhook/examples/logger/src/hook.asm`:

```asm
_hook:
        db      $83                       ; required magic; without it, not a hook
        ...                               ; ix = address of first byte after $83
        set     0,(iy-flag_continue)      ; do not return a value to TI-OS
```

Three consequences:

1. A hook must begin with byte `$83`.
2. `hook_Install(..., size != 0)` **copies** the hook, so the body must be
   **position-independent** — all internal references go through `ix`-relative
   addressing (`lea hl, ix+label-(_hook+1)`).
3. Chain control is `bit 0 of (iy-flag_continue)`: set = let other hooks of this type
   also run, reset = we are returning a value to the OS.

**A ~100 KB C engine therefore cannot be the hook.** The hook is a small PIC assembly
stub. See §6.

### 4.7 Cap'n Hook covers the plumbing

MIT licensed (© 2020 John Cesarz). Provides `HOOK_TYPE_HOMESCREEN` and
`HOOK_TYPE_CX_REDISP` as first-class types, plus:

```c
hook_error_t hook_Install(uint24_t id, hook_t *hook, size_t size,
                          hook_type_t type, uint8_t priority, const char *description);
hook_error_t hook_Sync(void);
hook_error_t hook_Uninstall(uint24_t id);
hook_error_t hook_SetPriority(uint24_t id, uint8_t priority);
```

It handles chaining, priority ordering, enable/disable, and validity checks. It stores
its hook database in an appvar that it archives itself (`_CreateAppVar` + `_Arc_Unarc`),
so hook code survives in flash. Re-arming the RAM-side pointers after a RAM clear needs
a boot trigger — AsmHook is the intended vehicle. Verified in M2.

Hook IDs must be globally unique and registered at the Cap'n Hook Hook ID Registry.

### 4.8 PineappleCAS

MIT licensed, C, targets the CE, last pushed 2022-11-28. Layout:

```
 25412  src/cas/simplify.c         17936  src/cas/identities.c
 19129  src/cas/eval.c              6908  src/cas/factor.c
  6302  src/cas/expand.c            4726  src/cas/derivative.c
  7566  src/cas/cas.h
 74308  src/imath/imath.c          23649  src/imath/imrat.c     <- exact int + rational
 18682  src/parser.c                8876  src/ast.c             11254  src/export.c
 28241  src/calc/gui.c             17911  src/calc/interface.c  <- its own UI, deleted
 10094  src/pc/main.c               7550  src/pc/tests.c        <- macOS test harness
```

Two things matter most. `src/imath/` is ~98 KB of arbitrary-precision integer and
rational arithmetic — that is why the engine produces `1/2` and not `0.5`, and it is the
single largest chunk of work we avoid rewriting. `src/pc/` builds and tests the whole
engine natively on macOS, breaking the usual calculator edit-build-flash-test loop.

Known limitation from its own docs: expand is "unoptimized, and very, very slow."

### 4.9 Host environment

macOS 27.0 on arm64. `git`, `make`, `brew`, `python3`, `curl`, `gh` present. `gh` is not
authenticated; `cmake` and `wget` are absent. CEmu ships native Apple Silicon builds with
an eZ80 debugger.

---

## 5. Architecture

Four components.

| Name | Language | Runs on | Role |
|------|----------|---------|------|
| `symstub` | PIC eZ80 asm | calculator, resident | The hooks. Tiny. Detects, delegates, draws. |
| `symcore` | C | calculator **and** macOS | Forked PineappleCAS. Parse, simplify, solve, render-layout. |
| `symvar` | 4 bytes | calculator, in appvar | Shadow table of free symbolic variables. |
| `symsend` | Python | macOS | build -> send -> screenshot, one command. |

`symcore` builds for two targets from one source tree. That is the core of the testing
strategy (§11) and is inherited from PineappleCAS, not invented here.

### 5.1 Fork plan for `symcore`

**Keep:** `src/cas/`, `src/ast.c`, `src/parser.c`, `src/export.c`, `src/imath/`, `src/pc/`.
**Delete:** `src/calc/` (gui.c, interface.c — 46 KB of UI we are replacing with hooks).
**Add:**

| File | Purpose |
|------|---------|
| `src/cas/radical.c` | Surd simplify, combine, rationalize, denest |
| `src/cas/solve.c` | Linear, quadratic, rational equations |
| `src/render.c` | AST -> 2D box layout (measure + draw) |
| `src/shadow.c` | Symbolic variable table |

---

## 6. The stub/engine split

Forced by §4.6. The hook body must be small and position-independent; the engine is
neither.

`symstub` is PIC assembly. Its total job:

1. Read the edit buffer via the gap-buffer pointers (§4.4).
2. Scan the token stream against the shadow table. **No free symbolic variable → return
   Z immediately.** This is the hot path and must be fast; it is a token scan with a
   32-bit mask test.
3. Otherwise, bring `symcore` in and call it.
4. Draw the returned layout.
5. Return NZ to swallow the key.

Step 3 is the one open design question. Three candidates, decided by the M1 spike once
`symcore`'s compiled size is known:

- **(a) Copy from archive to a RAM arena per invocation.** Simplest and safest. Costs a
  memcpy of the engine on every symbolic entry. Acceptable if the engine is small enough
  and the copy is under ~50 ms.
- **(b) Execute in place from archive.** The eZ80 memory-maps flash read-only, so an
  archived appvar is directly addressable. Requires the engine be built fully
  position-independent with all mutable state in a separate RAM arena. Fastest, most
  build-system work.
- **(c) Resident in RAM.** Engine stays loaded. Fastest and simplest to call, but
  permanently consumes RAM the user would otherwise have for programs.

**RESOLVED (M1, 2026-08-28): strategy (a).** The engine measures 78,560 bytes, which
`ldir` copies in roughly 3–4 ms at 48 MHz — far under the 50 ms threshold. The binary also
exceeds the ~64 KB single-appvar cap and is split across two archived appvars, which makes
(b) execute-in-place awkward and (a) copy-and-reassemble natural. (c) would cost ~51% of
user RAM permanently and is rejected. See `docs/history/notes/m1-size-report.md`.

---

## 7. Critical path

```
[ENTER] pressed on home screen
   |
   v
homescreen hook fires, A=1, B=kEnter
   |
   v
read entry:  [editTop,editCursor) ++ [editTail,editBtm)      tokens, not ASCII
   |
   v
scan tokens vs shadow table
   |
   +-- no free symbolic var --> return Z --> OS parses normally. Stock behavior, untouched.
   |
   v  has free symbolic var
load/enter symcore                                            §6
   |
   v
tokens -> AST                       parser.c        reused
simplify / expand / factor /
  derivative / solve / radical      cas/            reused + new
   |
   v
cache result AST in appvar                                    for redraw
render AST -> 2D layout -> draw     render.c        new
export AST -> token string -> Ans   export.c        reused
   |
   v
return NZ  -->  key swallowed. The OS parser never runs, so it never substitutes X=0.

  --- later ---

OS repaints the home screen
   |
   v
CxReDisp hook fires --> re-render cached AST at the correct row
```

The load-bearing move is **swallowing the key**. We do not race the OS parser or correct
its output after the fact — we prevent it from running. This is precisely why numeric
behavior stays bit-identical: on the numeric path, the OS executes the exact same code it
always did, with a single mask test added ahead of it.

---

## 8. Semantics

### 8.1 Shadow table

The OS has no "unassigned variable" state — `A`–`Z` and `θ` always hold a real, default
`0`. So SymCE maintains its own notion of which letters are *free*.

- 27 bits in 4 bytes, stored in the SymCE appvar so it survives power cycles.
- **Default: all free.** So `2X+2X` → `4X` on a fresh calculator with no setup.
- `5→X` clears X's bit. `X+X` then → `10`, exactly like stock.
- `DelVar X` sets the bit again, returning X to symbolic.

Store detection: the `→` token in the entry stream is detected by the same stub scan
that classifies the entry, before delegating. No second hook needed.

This mirrors TI-89 semantics and is what allows a working CAS and working TI-BASIC to
coexist on one machine. Without it, you must choose one.

### 8.2 Result representation

The pretty 2D form is drawn by us. **Additionally**, the linear token string is written
to `Ans` via the existing `export.c`. This is near-free and buys three things:

- `Ans+1` chains symbolically, because the next entry re-parses the string.
- TI-BASIC programs can read results.
- If the CxReDisp hook ever misbehaves, history degrades to readable text rather than a
  blank line.

It is a safety net, not a second feature.

### 8.3 Deferred: symbolic radicands

`√(X²)` → `|X|` requires sign and domain reasoning the engine does not do. It is
deliberately out of scope, because `√(X²) = X` is *wrong* for negative `X`, and shipping
the naive rule produces contradictions deep inside later expressions.

To keep the deferral cheap, the AST node gets a **sign/domain assumption field now**,
unused in v1. Adding it later, after `simplify.c` has grown a hundred call sites, is
expensive; adding it now is a struct field.

---

## 9. Safety invariants

These are non-negotiable and each has a test.

1. **Every failure falls through to the OS.** Parse failure, out of memory, timeout,
   unsupported construct → return Z, OS evaluates normally. No entry can hang or brick.
2. **Watchdog.** Expand is documented-slow. The engine polls `[ON]` and a tick counter
   inside its main loops and aborts to the OS on press or deadline.
3. **Bounded arena.** The engine allocates from a fixed-size block, never the OS heap. On
   this machine the "heap" *is* the user's saved variables — an allocator bug would not
   crash a program, it would delete homework.
4. **Numeric path is a mask test.** The non-symbolic path adds one token scan and a
   32-bit test. If that scan is ever not obviously cheap, it is a bug.
5. **Uninstall always works.** A user must be able to remove SymCE and get a stock
   calculator back, including after a failed install.

---

## 10. Memory budget

~154 KB user RAM total. Allocation is a spike output (M1), not a guess. Known pressures:

- `imath` is the largest single consumer and is not optional.
- The result AST is cached for redraw and must be bounded — deep expressions get
  truncated with an ellipsis rather than growing without limit.
- The render layout tree is transient and freed after drawing.

If the engine does not fit comfortably, the first cut is `expand.c` (slowest and least
used), then `factor.c`.

---

## 11. Testing

| Layer | Where | How |
|-------|-------|-----|
| CAS engine | **macOS**, native | Extend `src/pc/tests.c` + `tests.txt` with radical and solve cases. Runs at full speed under lldb. No calculator involved. |
| Renderer | **macOS**, native | Golden-file tests. 2D layout dumped as ASCII art, diffed. |
| Hook semantics | CEmu | The only thing that genuinely cannot be tested on the host. |
| Integration | CEmu, then hardware | Scripted entry sequences; compare against expected screen state. |

The great majority of the work — radicals, solve, rendering — is developed and debugged
on macOS. Hardware is for confirmation, not iteration.

`make test` on macOS must fail if the simplifier, solver, or renderer regresses.

---

## 12. Milestones

| | Deliverable | Exit criterion |
|---|---|---|
| **M0** | **Hook reconnaissance in CEmu.** Build `capnhook/examples/logger`, install the homescreen hook, press ENTER on the home screen, read `af/bc/de/hl/ix/iy` from the CEmu console. | The real CE event-code table for the homescreen hook, written down. Go / redesign decision. |
| M1 | Toolchain up. Fork builds for macOS **and** eZ80, existing PineappleCAS tests green on both. Engine size measured. | §6 stub/engine strategy chosen on data. |
| M2 | `symstub` + shadow table + numeric passthrough. `2X+2X` → `4X` as plain text in Ans. Survives a RAM clear via AsmHook. | **A calculator worth using daily.** |
| M3 | `render.c` + CxReDisp hook. Pretty printing that survives scrolling. | 2D output persists through history scroll. |
| M4 | `radical.c` — simplify, combine, rationalize, denest. | `√8`→`2√2`, `√2+√2`→`2√2`, `1/√2`→`√2/2`, `√(3+2√2)`→`1+√2`. |
| M5 | `solve.c` — linear, quadratic, rational with extraneous-root checking. | Exact radical roots from the quadratic formula. |
| M6 | `symsend` macOS CLI. | `make && send && screenshot` in one command. |

**M0 exists to kill or redirect the project in week one.** Everything downstream assumes
the homescreen hook can swallow `[ENTER]` on OS 5.8.4. If it cannot, the answer is a
full-takeover shell, and that is worth learning immediately rather than after the engine
work.

**M2 is the real ship date.** Pretty printing is M3, not a prerequisite.

---

## 13. Risks

| # | Risk | Mitigation | Status |
|---|------|------------|--------|
| 1 | ~~Engine too large to invoke from a PIC hook stub~~ (§4.6, §6) | Measured 78,560 bytes; strategy (a) chosen, copy costs ~3–4 ms | **Closed (M1)** |
| 2 | Homescreen hook event codes unverified on CE 5.8.4 | M0 measures them directly with a prebuilt logger | Open, cheap to close |
| 3 | CxReDisp hook semantics thinly documented | Fallback already designed: the Ans string (§8.2) | Mitigated |
| 4 | Expand/solve too slow at 48 MHz | Watchdog (§9.2); expand is first cut if needed | Accepted |
| 5 | RAM budget | M1 measurement; documented cut order (§10) | Open, cheap to close |
| 6 | Hooks lost on RAM clear | Cap'n Hook archives its database; AsmHook re-arms | Verify in M2 |
| 7 | ~~Edit buffer location unknown~~ | ~~Closed: §4.4~~ | **Closed** |

Note the risk profile inverted during research. The concern going in was *"can we hook at
all?"* — that is now cheap to answer with a prebuilt tool. The real risk is the ABI
constraint that a hook must be small and position-independent while the engine is neither.

---

## 14. Open questions

1. **Stub->engine invocation strategy** (§6). Decided at M1 on measured size and copy cost.
2. **Hook ID.** Must be registered in the Cap'n Hook Hook ID Registry to avoid collisions.
3. **Pretty-print row accounting.** The OS owns home screen scroll position; the CxReDisp
   hook must map a cached result to the correct row. Investigated at M3.
4. **`textShadow` interaction.** 260 bytes of screen text shadow — whether drawing over it
   confuses the OS's own redraw is an M3 question.

---

## 15. References

- PineappleCAS — https://github.com/nathanfarlow/PineappleCAS (MIT)
- Cap'n Hook — https://github.com/commandblockguy/capnhook (MIT, © 2020 John Cesarz)
- AsmHook — https://github.com/jacobly0/asmhook
- arTIfiCE — https://github.com/YvanTT/arTIfiCE
- CE toolchain — https://github.com/CE-Programming/toolchain (`src/include/ti84pceg.inc`)
- CEmu — https://github.com/CE-Programming/CEmu (native Apple Silicon builds)
- WikiTI 84PCE OS Include File — https://wikiti.brandonw.net/index.php?title=84PCE%3AOS%3AInclude_File
- WikiTI 83Plus Homescreen Hook — https://wikiti.brandonw.net/index.php?title=83Plus:Hooks:9B8C
