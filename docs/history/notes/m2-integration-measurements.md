# M2 integration measurements

Numbers taken on 2026-08-29 against CEdev v15.0 and the current `symcore` fork.
These are measurements, not decisions. They constrain how the engine can reach
the hook.

## Where a CE program can live

From `~/CEdev/meta/makefile.mk` and the emitted `bin/SYMCE.map`:

```
LOAD_ADDR     ?= 0xD1A87F     program code+rodata+data
BSSHEAP_LOW   ?= 0xD052C6     bss, then the malloc heap
BSSHEAP_HIGH  ?= 0xD13FD8     -> 60,690 bytes of bss+heap
STACK_HIGH     = 0xD1A87E
```

All three are `?=`, so **all three are overridable on the make command line.**
That matters: the engine does not have to be linked for userMem. It can be
linked for whatever region is actually free at the moment a hook runs.

`~/CEdev/meta/linker_script.ld` links everything absolute from `LOAD_ADDR`, and
there is no `-fPIC` anywhere in the toolchain. eZ80 has no PC-relative data
addressing. So **copy-somewhere-and-jump cannot work for compiled C** - the code
must run at the address it was linked for. Relocation is not on the table;
choosing the link address is.

vRAM is at `0xD40000` (`~/CEdev/include/graphx.h:195`), 320x240x2 = 153,600
bytes. That is the largest contiguous region not owned by user data.

## Engine size

`make` in `symcore/` with `-Oz` and LTO already on:

```
.text      55,352
.rodata       691
.data      15,635
.bss            6
           ------
           71,684 bytes linked
           78,560 bytes as the loaded image
           29,603 bytes as the zx7-compressed .8xp   (37.7%)
```

The compressed figure is the interesting one. The CE toolchain already produces
and decompresses zx7, so an archived appvar can hold ~30 KB and expand into RAM,
rather than holding 78 KB and being copied byte for byte.

### .data is almost entirely identity tables

```
_id_trig_constants       4,240
_id_trig_identities      2,756
_id_trig_inv_constants   2,650
_id_derivative           1,908
_id_general              1,802
_id_complex              1,272
_id_hyperbolic             318
                        ------
                        14,946   = 96% of .data
```

Trig alone is 9,646 bytes. `simplify_identities()` in `src/cas/simplify.c:718`
already gates each table behind a `SIMP_ID_*` flag, but that is a RUNTIME gate -
the tables are referenced unconditionally from that function, so the linker keeps
them all. Excluding them needs a compile-time `#if`, not a flag.

Dropping all three trig tables would save ~9.6 KB of .data plus `simplify_periodic`
and its helpers from .text. That takes the engine to roughly 61 KB linked. It also
removes trig simplification, which is outside the stated CAS scope but is not
nothing on a calculator - **a scope call, not a size decision.** Not taken.

## Allocation

19 `malloc`/`calloc`/`realloc` sites outside `src/pc/`:

```
src/ast.c        62, 72, 82    one malloc per AST node
src/ast.c       170            calloc per string export
src/ast.c        39, 46
src/parser.c    115, 247       one malloc per token array
src/stack.c       8, 18
src/export.c    328
src/imath/imath.c   371, 1968, 1998, 2022
src/imath/imrat.c    67
src/cas/identities.c 346, 347  two callocs per identity match attempt
```

So the engine needs a working heap, and `identities.c:346` allocates inside the
match loop, which runs per identity per node. That is the first thing to look at
for latency, ahead of anything else.

## What this rules out

Spec section 6 chose "strategy (a): copy the engine from archive into a RAM
arena and jump to it". Compiled C cannot be relocated that way. The surviving
variants of (a) are:

  - link the engine for a region free during a hook (vRAM is the only one big
    enough), copy or decompress it there, and run it at that address;
  - do not run the engine in the hook at all, and arrange for it to run as an
    ordinary program with ordinary resources.

Both are open. The measurements above do not pick between them.
