# M1 — symcore size report

**Toolchain:** CE toolchain v15.0, `-Oz`, LTO on, `COMPRESSED = NO`

> The committed `symcore/makefile` now sets `COMPRESSED = YES` so the test build
> ships as a single 29 KB file rather than three. To reproduce the 78,560-byte
> figure below, set `COMPRESSED = NO` and rebuild.
**Date:** 2026-08-28

## Measurement

| Artifact | Bytes |
|----------|-------|
| **`bin/SYMCORE.obj` (linked eZ80 binary)** | **78,560** |
| `SYMCORE.8xp.0.8xv` (appvar 1) | 65,308 |
| `SYMCORE.8xp.1.8xv` (appvar 2) | 5,099 |
| `SYMCORE.8xp` (loader) | 319 |

The toolchain emitted `[warning] Input too large; split across 2 appvars`. A single
CE variable caps near 64 KB, so the engine does not fit in one.

### Largest pre-LTO objects

```
104792  imath/imath.o          <- as predicted in spec 4.8
 35904  export.o
 33480  cas/identities.o
 32880  imath/imrat.o
 26416  cas/simplify.o
 22608  cas/eval.o
 22008  parser.o
```

### Largest linked symbols

```
  9597  _export_to_binary
  5364  _eval
  4240  _id_trig_constants
  3778  _mp_int_div
  2756  _id_trig_identities
  2650  _id_trig_inv_constants
  2078  _parse
  1908  _id_derivative
  1802  _id_general
```

The trig and derivative identity tables total roughly 13 KB. They are the cheapest
non-structural cut if RAM gets tight, ahead of spec 10's proposed `expand.c` cut.

### Measurement caveat

`src/main.c` deliberately calls `simplify`, `expand`, `factor`, and `derivative` so LTO
cannot strip them. If M2's hook path ends up needing only `parse` + `simplify` +
`export_to_binary`, the shipped binary will be smaller than 78,560 bytes. Re-measure once
the real hook entry point exists.

## Available budget

TI-84 Plus CE user RAM: ~154 KB. Copying the engine in costs ~51% of it, held only for
the duration of a symbolic evaluation.

## Strategy decision (spec section 6)

**Chosen: (a) copy from archive into a RAM arena per invocation.**

Rationale:

1. **The copy is cheap.** `ldir` on the eZ80 moves roughly 2 cycles/byte, so 78,560 bytes
   is on the order of 157k cycles — about **3–4 ms at 48 MHz**. Far below the 50 ms
   threshold set in the plan, and imperceptible next to the CAS work itself.
2. **The appvar split argues *for* (a), not against it.** The engine spans two
   non-contiguous archived appvars. Strategy (b), execute-in-place, would have to cope
   with a discontinuity mid-binary. Copying reassembles both parts into contiguous RAM
   and makes the split a non-issue.
3. **(c) is ruled out.** Holding 78.5 KB resident permanently costs the user half their
   RAM even when they are doing ordinary arithmetic. Unacceptable for a calculator whose
   selling point is that it still feels stock.

**Confirm before relying on it:** the 3–4 ms figure is calculated, not measured. Time the
actual copy in CEmu during M2 and record the result here. If it exceeds 50 ms, revisit (b).

## Risk 1 status

Spec section 13 risk 1 ("engine too large to invoke from a PIC hook stub") is **closed**.
The engine is large but not prohibitive, the copy is fast, and the delegation strategy is
decided on measurement rather than estimate.
