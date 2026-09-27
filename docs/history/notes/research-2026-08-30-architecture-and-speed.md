# SymCE Engineering Report

Written against the repo at `<repo>` (commit `da9a75f`). Every file:line below was re-read, not inherited.

---

## 1. THE ARCHITECTURE DECISION

**Run the CAS inside the homescreen hook, out of a link-time-fixed arena that is reloaded from an archived appvar on every invocation, and keep the proven edit-buffer rewrite as the delivery mechanism.**

The deciding fact is not memory, it is the display. The only way to get a right-aligned answer into stock MathPrint history is to have it in the edit buffer *before* the OS parses. That is already working on hardware (`symce/src/hook.c:195-215`). Every architecture that runs the CAS after the hook returns has already lost the screen.

### The alternatives, one sentence each

- **Rewrite the entry to `prgmSYMCE` and launch the engine as a program** — `~/CEdev/lib/crt/crt0.S` exits through `ClrLCDFull` / `HomeUp` / `DrawStatusBar` and asmhook's cleanup thunk does `res ti.numOP1,(hl)`, so a launched program necessarily blanks the home screen and returns `Done`; the user's entry and the answer can never both appear.
- **A=0 "display result" substitution via `fmtString`** — the only shipping CE hook that does this (Cemetech t=17007) requires MathPrint OFF, OP1 has no encoding for `4X`, and WikiTI states plainly that changing OP1 does not affect Ans, so the screen would read `4X` while Ans held `0`.
- **CxReDisp hook painting** — it fires *before* the repaint, which is the same fight the `_PutS` build already lost.
- **Copy-anywhere-and-jump PIC compiled C** — `linker_script.ld` links absolute from `LOAD_ADDR`, there is no `-fPIC`, and eZ80 has no PC-relative data addressing.
- **Run under a shell / launcher** — that is the requirement being violated.

### The audit objection that blocked this, and why it falls

"You may not allocate memory while the edit buffer is open." The rule is narrower than that. `editTop`/`editCursor`/`editTail`/`editBtm` are pointers into user RAM, and `_InsertMem`/`_DelMem`/VAT create-delete shift that region without fixing them up. **This design performs no OS allocation at all** — the arena is a link-time absolute range outside user RAM, and the only OS calls are `_Mov9ToOP1` / `_ChkFindSym` / `_ChkInRam`, all pure VAT lookups. asmhook calls `ChkFindSym` from inside a hook on this exact OS family.

Corollary: **drop the no-`call` rule at the top of `hook.c`.** PIC forbids *self*-references, not absolute OS entry points. `call 0x02050C` is valid wherever the body is copied. The real constraint that survives is that the body executes from flash, so it must not trigger a garbage collect.

### Where the arena goes — one spike decides, and it is item #1

Two candidate regions, both with the same unverified assumption (what does the OS restore when our hook returns Z?):

| | vRAM `0xD40000`–`0xD65800` | OS shadow buffers `0xD031F6`–`0xD13FD8` |
|---|---|---|
| size | 153,600 | 69,090 |
| fits untrimmed engine (71,684 + heap + stack)? | yes, ~80 KB spare | no |
| fits trimmed engine (~54 KB est. + 8 KB arena + 4 KB stack ≈ 66 KB)? | trivially | 2.8 KB of margin on a ±15% estimate |
| cost | must blank the LCD or the user sees the image as pixels | none |
| risk | assumes the OS repaints all 320×240 after the entry line changes | MathPrint's own repaint path reads `pixelShadow` — the code we return *into* |

**Default to vRAM.** It needs no trimming to fit, and the OS demonstrably owns and repaints it. 2.8 KB of margin on an estimate is not a margin.

### Proposed vRAM layout (all four knobs are `?=` in `~/CEdev/meta/makefile.mk:27-30`)

```
0xD40000   9,600  blank 1bpp framebuffer, memset once per invocation
0xD42600  73,728  LOAD_ADDR: engine .text/.rodata/.data image
0xD54600  53,760  BSSHEAP_LOW .. BSSHEAP_HIGH = 0xD61800
0xD61800  16,384  engine stack, SP top = 0xD65800 = vRamEnd
```

`.data` is placed `>prgm` in `linker_script.ld` (LMA == VMA), so the flat image needs no relocation and no `___data_lma` copy. The entire remaining startup obligation is zeroing 6 bytes of `.bss`.

### The hook's new middle, after the existing gates and whitelist scan say "ours"

```
 1. call _CursorOff (0x0208A8), _RunIndicOff (0x020848)
 2. save lcd_UpBase (0xE30010), lcd_Control (0xE30018)
 3. di
 4. memset 0xD40000,0,9600 ; lcd_UpBase = 0xD40000 ; lcd_Control = 0x821
 5. flash wait states 0xE00305: save, set 3        (see §3)
 6. OP1 = "SYMCE0"; _Mov9ToOP1 (0x020320); _ChkFindSym (0x02050C);
    _ChkInRam (0x021F98); ldir from (DE + 9 + 1 + namelen + 2) -> 0xD42600
    repeat for "SYMCE1"
 7. memclear ___bss_low, 6
 8. ld (savedsp),sp ; ld sp,0xD65800
 9. call 0xD42600      ; symce_engine(gap buffer, out, outmax) -> length or 0
10. ld sp,(savedsp)
11. restore 0xE00305, lcd_Control, lcd_UpBase ; ei
12. success -> rewrite entry ; failure/watchdog/overflow -> touch nothing
13. cp a,a ; ret
```

Roughly 120 bytes of eZ80. The abort path costs nothing because the buffer is untouched until the answer exists.

### Reload the image every invocation — this is a feature, not a cost

~72 KB `ldir` from flash-mapped archive is ~10 ms, not seconds. Ship it **uncompressed** as two appvars (`convbin -k 8xv-split -r`) rather than putting a zx7 decompressor in the hook body. And reloading is simultaneously the fix for two whole classes of bug the earlier audit flagged as blockers:

- `_heap_ptr` lives in `.data` and is relocated to `___heap_low` at link time (`allocator_standard.a` has `.rela.data._heap_ptr`), so there is no runtime heap init — reloading `.data` resets the allocator to empty for free.
- `pcas_id_t` caches parsed identity ASTs in `.data` (`symcore/src/cas/identities.h:11`), and `id_UnloadAll()` is called from exactly one place — `symcore/src/pc/main.c:468`, inside `#ifdef COMPILE_PC`. The calculator build never nulls them. Reloading `.data` un-caches them. Use-after-free becomes structurally impossible.

### Pieces to build

1. `symce/engine.makefile` — `LOAD_ADDR=0xD42600 BSSHEAP_LOW=0xD54600 BSSHEAP_HIGH=0xD61800`, `--entry=_symce_engine`, drop `libcrt`, ~20-byte `.init` stub that zeroes `.bss` and calls the entry. Then `convbin -j elf -k bin` → `convbin -j bin -k 8xv-split -r -n SYMCE`.
2. `symce/src/engine_entry.c` — one exported function at offset 0 of the image: copy the gap buffer into an arena-local flat buffer, `parse(..., ti_table, &err)` → `simplify(e, SIMP_ALL)` → **`simplify_canonical_form(e, CANONICAL_ALL)`** (see §3) → `export_to_binary(..., ti_table)`, return length or 0. `tools/tokentrip.c` already proves the round trip on raw edit-buffer bytes.
3. Hook body extension in `symce/src/hook.c` — steps 1-12.
4. Installer in `symce/src/main.c` — write and archive `SYMCE0`/`SYMCE1` alongside `SYMCEHK`.
5. Link-time assert that `__init_array_start == __init_array_end`, so a future static initialiser in symcore fails the build instead of failing silently on hardware.

### Two corrections in existing code, both one-liners

- `symce/src/main.c:21` — "AsmHook may hold this slot" is **wrong**. asmhook holds `parserHookPtr` (0xD025F9) and `menuHookPtr` (0xD02608); SymCE holds `homescreenHookPtr` (0xD025E1). No conflict. Keep the save/restore machinery (some other program could hold it), fix the comment.
- `symce/src/main.c:25-28` `KNOWN HAZARD` — the cached flash pointer invalidated by an archive GC. Same fix as the engine: `_Mov9ToOP1` + `_ChkFindSym` from inside the hook rather than a pointer cached at install time.

---

## 2. THE ENTRY-ECHO FIX

**There is no earlier buffer holding the user's tokens.** Both stores capture *after* the hook: the 2nd-ENTRY stack (`lastEntryStkPtr` 0xD01508, `lastEntryStk` 0xD0150B, 2048 bytes) and the MathPrint history (`_AddHistoryEntryString` 0x021810) are both filled downstream of `_CloseEditBuf` (0x020CB8), from the closed buffer contents. The "rewrite for parsing only, restore afterwards" plan has nowhere to hide.

**There is a second, worse defect nobody has logged.** After the rewrite, Ans is a `StrngObj` (tag 4):

- `Ans+1`, `Ans*2`, `2Ans` → **ERR:DATA TYPE**
- ENTER on an empty entry line re-executes the last entry, which is now the string `"4X"` — so it echoes `4X` instead of re-running `2X+2X`

That breaks a stock workflow and fails semi-silently. It is the stronger argument for Option A below.

### Option A (the real fix) — own the history

Return NZ at A=1, `_CloseEditBuf` (0x020CB8), compute, then place the pair *(user's exact entry text, our answer)* into history via `_DeleteHistoryEntry` (0x021914) / `_AddHistoryEntryString` (0x021810), `_StoAns` (0x020F30) for Ans, re-open the entry line (`_SetUpEditCmd` 0x020D78 / `_SetEmptyEditPtr` 0x020D80), let the OS repaint. No repaint fight — you put the data where the repaint reads from. Input preserved verbatim, answer clean, Ans a real value.

**Both BCALL signatures are completely undocumented.** This is gated on the spike in §4 item 1, and it is a bounded measurement, not open-ended research. While you are in there, watch `entryString` (0xD008E6) and `entryResult` (0xD008EA) — named in `vendor/capnhook/include/ti84pce.inc` with no comment and appearing in no documentation anywhere, 4 bytes apart (fits a 3-byte pointer plus a flag byte). If those are the CE's handles for current entry text and current result, they are the whole problem solved.

### Option B (the fallback, ~40 bytes, write it anyway)

Rewrite as `2A <original tokens> 2A 3E 2A <result tokens> 2A` — `"2X+2X":"4X"`. Two `LDIR`s: make room at the front, normalise the gap to the end, append.

- Entry line and 2nd-ENTRY preserve the user's tokens verbatim.
- Answer line reads exactly `4X`.
- **Quote the original**; leaving it bare makes the OS evaluate it, and `1/X` then throws ERR:DIVIDE BY 0 and kills the entry.
- Safe against escape by construction: `hook.c:185-186` (`cp 0x2B / jr c, .Lbail`) already rejects both `0x2A` (") and `0x04` (STO), so no accepted entry can break out of the literal or have side effects.
- Residual cost: quotes visible, the answer appears twice (once quoted on the entry line, once on the answer line), and Ans is still a string. Not stock. Better than destroying the input.

### The length guard is a bug right now

`symce/src/hook.c:77-88` checks `editBtm - editTop >= 4` because the placeholder is 4 bytes. **That constant must become the actual result length.** Writing past `editBtm` walks into the VAT. The gap is not tight — WikiTI: "When a variable is edited, all of available RAM is inserted into it", and `editTop`/`editBtm` do not move during the session — so you have tens of KB. But the guard has to track the real length.

Keep scan and write strictly sequential (scan the whole entry, then write). The current code already does; interleaving would let a long result overwrite its own source at `editTop`.

---

## 3. SPEED

### There is no overclock. That section is one paragraph, honestly.

`pCpuSpeed` (port `0x0001`, `ti84pce.inc:3041`) is two bits: `11b` = 48 MHz = the hardware ceiling, and the boot code already sets it. There is no PLL, no multiplier register, and no analog trim — the pencil/resistor mod on the 84+/83+SE does not transfer, because that trims an RC oscillator this chip does not have. **Do not write port 1.** It is a genuine clock change that shifts every `TIMER_CPU` timer and every OS software delay loop, for zero gain. Read it with `in0 a,(1)` if you want the data point.

The LCD is likewise not worth it: DrDnar's figure for what LCD DMA costs the CPU is ~8% of bus bandwidth, that is an upper bound on what disabling it recovers, and the price is a strobing screen on every ENTER on a project whose requirement is "exactly like stock".

### The one real hardware knob: flash wait states

Port `0x1005`, memory-mapped `0xE00305`. **Total cycles per flash byte = value + 6.** OS ships `04` (10 cycles). RAM read is 4 cycles, RAM write 2.

CEdev's `crt0.S:194-196` already does this for every C program:

```asm
    ld   hl, 0xE00305
    ld   a, (hl)
    ld   (_exit.flash_wait_states), a
    ld   (hl), h            ; h = 0x03 -- 9 cycles/flash byte
```

and restores it at `crt0.S:451-457`. **We are not using crt0, so the hook must do it** — two instructions in, one out, saved in a named RAM byte (not `push af`; eZ80 ADL push widths for `AF` are a foot-gun I did not verify).

Values and their real failure mode: `03` is the toolchain's value with no crash attributed to it since Feb 2017. `01` buys ~30% on flash fetches but was reverted from the toolchain in April 2017 for occasional crashes across all revisions. `00` is an instant hard crash. **There is no brick risk** — only read timing changes, nothing is written to flash. The failure is a reboot with a full RAM clear: Ans, programs, variables, window settings gone; archive survives, so `SYMCEHK` itself is safe. Use `03`, never lower.

Two guards worth the four bytes:

```asm
    in0  a, (3)          ; hardware ID
    bit  4, a            ; set = serial flash (rev M+): port 1005 is inert,
    jr   nz, .skip_ws    ; those units already run 1 hardware wait state
```

and **restore `0xE00305` before returning to the OS**, read-modify-restore rather than hardcoding `04`. Reason: TI's flash programming and garbage-collect routines poll the flash status register, which is itself a wait-state-timed read, and SymCE's whole install lives in an archived appvar.

Expected payoff for us: **small.** The engine runs from RAM; only the OS calls and the `ldir` source are flash fetches. Do it because it is four instructions, not because it moves the needle.

### CAS optimisations, ranked by payoff per unit of effort

All counts below are measured on an instrumented host build of the actual tree, for `2X+2X` at `SIMP_ALL` unless noted.

**1. `simplify_canonical_form(e, CANONICAL_ALL)` — one line.** Declared `symcore/src/cas/cas.h:121`, implemented `symcore/src/cas/simplify.c:361`, reaches the radical renderer already present in `export.c:202-215`. It is never called — `tools/tokentrip.c:75` calls only `simplify(e, SIMP_ALL)`. Adding it fixes two logged defects at once:

| | now | with canonical form |
|---|---|---|
| `√(2)` | `32 F0 10 31 EF 2E 32 11` (2^(1/2)) | `BC 32 11` |
| `√(2)/2` | `31 EF 2E 32 F0 10 31 EF 2E 32 11` | `BC 32 11 EF 2E 32` |
| `X^2+2X+1` | ascending | `58 F0 32 70 32 58 70 31` (descending) |

Full corpus re-run: no regressions. Remaining gap: `√2+√2` still gives 2^(3/2), because `CANONICAL_POWERS_TO_ROOTS` at `simplify.c:427` only matches exponents of the form `1/q` — exponent p/q with p>1 needs one more branch there.

**2. `den==1` fast path in `s_rat_reduce` — six lines, `symcore/src/imath/imrat.c:886`,** immediately after the zero-numerator early-out at 882-885. imath is not paying bignum cost for the arithmetic (`mpz_t` carries an inline `single` digit), it is paying it for the *bookkeeping*: 515 full Stein GCDs to perform 2 additions and 148 multiplications on integers smaller than ten, because `mp_rat_set_value` (`imrat.c:109-125`) unconditionally ends in `s_rat_reduce` and `num_FromInt(1)` runs 209 times per entry to normalise `1/1`.

```c
/* ponytail: den==1 is the overwhelming case on a home screen; gcd(n,1)==1
   so skip the Stein GCD entirely and just fix signs. */
if (MP_USED(MP_DENOM_P(r)) == 1 && MP_DIGITS(MP_DENOM_P(r))[0] == 1) {
    if (MP_SIGN(MP_NUMER_P(r)) == MP_SIGN(MP_DENOM_P(r)))
        MP_SIGN(MP_NUMER_P(r)) = MP_SIGN(MP_DENOM_P(r)) = MP_ZPOS;
    else { MP_SIGN(MP_NUMER_P(r)) = MP_NEG; MP_SIGN(MP_DENOM_P(r)) = MP_ZPOS; }
    return MP_OK;
}
```

Measured `mp_int_gcd` calls: `2X+2X` 515→2, `X*X` 717→0, `(A+B)(B+C)` 820→0, `X^3+2X^2+X` 7,037→47. `mp_int_copy` 3,121→1,582. Tests: **256/256 upstream, 12/12 SymCE guards.** ~7.5 ms per entry at 48 MHz.

**3. Trig gate — ~15 lines.** `simplify_identities` (`symcore/src/cas/simplify.c:718-744`) fires all six identity tables per pass, and `simplify` calls it twice per outer iteration. For `2X+2X` — an expression with no trig, no hyperbolic, no `i` — that is **123 table sweeps and 378 `matches()` calls**. Add a `has_trig(e)` pre-walk (scan for `OP_SIN..OP_TANH_INV`) and mask `SIMP_ID_TRIG | SIMP_ID_TRIG_CONSTANTS | SIMP_ID_TRIG_INV_CONSTANTS | SIMP_ID_HYPERBOLIC`:

| | before | after |
|---|---|---|
| malloc | 1,423 | 361 |
| table sweeps | 123 | 29 |
| `matches()` | 378 | 88 |
| nested `simplify()` | 149 | 49 |
| cold `id_Load` | 123 | 29 |
| host µs | 95 | 20 |

Provably safe: I walked every `from` AST — all 26/26, 40/40, 25/25 and 3/3 entries of the four gated tables contain a trig or hyperbolic operator, so no reachable match is lost, and `simplify_periodic` (`simplify.c:618-624`) already early-returns on non-sin/cos/tan. **Do not extend the gate to `id_complex`**: `id_complex[0]` matches `(-A)^(X/Y)` with no `i` present.

It also hands back memory. The cached identity trees, converted to eZ80 sizes, total **34,416 bytes against a 60,684-byte heap — 56.7%** — of which 32,388 bytes is trig/complex no algebra entry ever touches. And deleting rather than gating the four tables is a measured **−12,442 bytes linked** (71,678 → 59,236).

**4. Bump arena — the biggest job, biggest structural win.** `_malloc` (0xD27F4A) and `_free` (0xD2800C) are both O(free-list length) first-fit/address-ordered walks; at the measured 26-81 live blocks that is roughly **1,500 cycles per malloc/free pair, ~31 µs** — about 45 ms of the current `2X+2X` entry. Replace with `arena_Alloc` (add, compare, store; ~30 cycles) and make `ast_Cleanup` (`ast.c:212`), `num_Cleanup` (`ast.c:56`), `mp_rat_free`, `mp_int_free`, `mp_int_clear`, and the bare `free(b)` at `simplify.c:65` no-ops. Cheapest route is `#define malloc arena_Alloc` plus a no-op `free`, which covers all 19 sites at once.

Two hazards, both mandatory:
- **Two arenas, not one.** Permanent for identity trees (`id_Load`, `identities.c:516`), scratch reset per entry. One shared arena gives a use-after-free on entry #2 via the `.data` identity cache described in §1.
- `stack.c:18`'s `realloc` growth path has no bump equivalent. Size both stacks once from `tokenizer.amount` (known at `parser.c:241`) and delete the growth path.

Sizing: a bump arena must hold the *total churn* of one entry, not the peak. With the trig gate, ~361 blocks × ~16 B ≈ **8 KB**. Without it, budget 24 KB.

**5. Watchdog on `arena_top` — one compare, and it is not optional.** The fixpoint converges (outer-loop iteration ratio 1.07-1.30 across every input; 400 fuzzed expressions, no hang) but the *cost distribution* is brutal: `2+2` = 502 mallocs, `2X+2X` = 1,423, `X^3+2X^2+X` = 25,328, worst fuzz case `X/Y*(Y+X)*(A_X)` = 75,091. A 150× spread over things a student types casually. The arena top pointer is already the cheapest possible budget counter, and it doubles as the OOM guard. Blow the budget → return Z, let the OS handle the entry as stock.

**6. Single-pass `export_to_binary`.** `export.c:316-331` runs the whole recursive `_to_binary` walk twice — once with `data == NULL` to size, once to fill — so every `NODE_NUMBER` pays two `num_ToString` calls (`export.c:78`) and every `OP_ADD` pays two full `ast_Copy` of its subtree (`export.c:108`). Size into a fixed 128-byte buffer with a truncation check that falls back to Z.

### One optimisation to explicitly NOT make

`identities.c:558` runs `simplify(e, SIMP_COMMUTATIVE | SIMP_EVAL)` after **every** identity attempt, matched or not — 123 of the 149 nested `simplify()` calls on `2X+2X`. It looks like obvious waste. I changed it to fire only on a match and measured: `2X+2X` got **worse**, 1,423 → 2,809 mallocs and 123 → 369 sweeps. It is load-bearing — it renormalises the tree so a later identity can fire and the `break` at `identities.c:560` can end the sweep early. It *does* help the polynomial cases (`X^3+2X^2+X` 25,328 → 22,130), so someone benchmarking only polynomials will "confirm" it as a win and regress the case that matters. Leave a comment in the source recording the measurement.

### Build flags: `-Oz` is already right, stop looking

Measured full rebuilds with `ez80-clang` v15.0:

| | `.text` | linked | `.8xp` |
|---|---|---|---|
| `-Oz` (current) | 55,352 | 71,678 | 29,603 |
| `-Os` | 74,797 | 91,123 | 37,120 |
| `-O2` | 105,229 | 121,618 | 51,733 |
| `-O1` (worst) | 112,017 | 128,418 | 53,319 |

LTO is a wash (`LTO=NO`: `.text` +75 B, `.8xp` −206 B). `-ffunction-sections`/`-fdata-sections`/`--gc-sections` are already on (`makefile.mk:334-335`) and ~11 KB is already being stripped. **Cutting the image means deleting code, not tuning flags.**

### Latency: the number, and its honesty warning

Bottom-up cycle model (no ROM present at `~/CEdev/ti84pce.rom`, so CEmu could not be run):

- **`2X+2X` today, warm: ~96 ms.** Cold first entry after every re-arm: 250-450 ms, because `id_Load` lazily parses all 123 identity strings (5,264 mallocs vs 1,423).
- `X^3+2X^2+X`: 1.5-2.5 s. Fuzz worst case: 2.5-5 s.
- **After items 1-5: `2X+2X` ≈ 13 ms warm, ≈ 15 ms cold.**

The absolute figures are a model. The *relative* figures — 4× from the trig gate, gcd 515→2, −12,442 bytes — are all measured and do not depend on it.

---

## 4. WHAT TO BUILD NEXT

**1. The one spike that gates everything. One hardware session.**
From the current working hook, on a symbolic entry: fill `0xD40000..0xD65800` *and* `0xD031F6..0xD13FD8` with two distinct patterns, do the existing `"4X"` rewrite, return Z. Then look at the screen, and read both regions back on the next entry. This answers, in one go: does the OS repaint all of vRAM (→ vRAM arena is unblocked), does it survive the shadow buffers being trashed (→ the cheaper no-LCD-blanking arena is available), and is a `call` from a hook body safe (put a trivial `call` in the spike and confirm). While you are in CEmu, breakpoint `_AddHistoryEntryString` (0x021810) and `_DeleteHistoryEntry` (0x021914) on a *stock* ENTER, record A/HL/DE/BC and what they point at, and watch `entryString` (0xD008E6) / `entryResult` (0xD008EA) — that decides §2 Option A. Also check whether the A=0 event fires at all under MathPrint; ten minutes inside a session you are already running.

**2. Host-side CAS wins, in parallel, no hardware needed.** Add `simplify_canonical_form(e, CANONICAL_ALL)` after `simplify()` in `tools/tokentrip.c` and the engine entry (one line). Add the `den==1` fast path at `imrat.c:886` (six lines). Run the suites. ~20 lines total, takes `2X+2X` from ~96 ms to ~22 ms by the model, breaks none of the 268 tests.

**3. Fix `hook.c`'s length guard.** `hook.c:77-88` — `ld de, 4` becomes the real result length. Small, and it is a live memory-corruption bug on any answer longer than 4 bytes, which is the moment the CAS is wired.

**4. Trig gate in symcore.** `has_trig(e)` pre-walk, mask the four flags. ~15 lines, 4× on everything, −12,442 bytes, −32 KB heap.

**5. `symce/src/engine_entry.c` + `engine.makefile`.** One exported function, custom `.init` stub, `LOAD_ADDR` set by the spike result. Verify by round-tripping through the appvar on the host before touching hardware.

**6. Hook body extension** — steps 1-12 of §1, including the flash wait-state save/restore and the rev-M+ guard from §3.

**7. Installer update** — `SYMCE0`/`SYMCE1` written and archived alongside `SYMCEHK`; kill the wrong asmhook comment at `main.c:21`; fix the cached-flash-pointer hazard at `main.c:25-28` by moving the lookup into the hook.

**8. Bump arena + watchdog.** Two arenas. This is the point where the long-tail inputs stop being a hang risk.

**9. §2 delivery fix.** Option A if the spike says the BCALLs are reachable; Option B (~40 bytes) otherwise. Write B regardless — it is a small change to code that already exists and it is the fallback if A dies.

**10. Deferred, only if load time bites:** pre-parse identity trees at build time into static `.data` (kills the 3,841-malloc cold start and moves 34 KB off the heap), single-pass export, and the remaining ~6 KB of imath decimal-I/O and derivative-trig deletions.

### The scope question, stated so it can be rejected on purpose

`2X+2X → 4X`, `X*X → X^2`, `2/4 → 1/2` is a single-variable polynomial collector over rationals: fixed arrays, no malloc, ~4-6 KB, runs in the hook on the OS's own stack, needs none of the arena, the LCD blanking, the stack switch, or the reload-per-entry. PineappleCAS is 72 KB because it does trig identities, derivatives, complex numbers and factoring. If the product is "the home screen does algebra", the small engine ships this week. If it is "a real CAS on the home screen", everything above is the price. That is a product decision, and it should be made out loud rather than inherited from the `fork PineappleCAS` commit.

---

## 5. OPEN QUESTIONS

Genuinely unresolved. Each needs a ROM dump or hardware, not more reading.

- **Does the OS repaint the whole 320×240 home screen after the entry line changes, or only the entry line?** `hook.c` documents a full repaint, but that observation came from `_PutS` output being wiped, which only proves the entry-line region. The vRAM arena needs the whole screen restored. This is the single load-bearing assumption in the architecture. Spike #1.
- **`_AddHistoryEntryString` (0x021810) and `_DeleteHistoryEntry` (0x021914) register conventions.** Completely undocumented — no register contracts, no example code anywhere. §2 Option A rests entirely on measuring them.
- **`entryString` (0xD008E6) / `entryResult` (0xD008EA)** are named in `ti84pce.inc` with no comment and appear in no documentation. That they are handles for the current entry text and result is inference from their names and their position after the error block.
- **End-to-end latency on real silicon has never been measured.** All timing here is a bottom-up cycle model; no ROM is present. If real `2X+2X` exceeds roughly half a second the calculator feels broken even with the screen blanked.
- **Is a multi-second `di` inside a key hook safe?** crt0 holds `di` for a whole program lifetime, so it is accepted practice, but a normal program exit cleans up APD/RTC/USB in ways a hook return does not.
- **Does writing `lcd_Control` mid-frame to swap 16bpp↔1bpp produce artifacts, an underflow interrupt, or a controller state the OS does not restore?** The simpler alternative — leave the LCD alone and let vRAM garble visibly for ~15 ms — has not been compared side by side.
- **`_BufInsert` (0x020D00) / `_BufClear` (0x020D3C) register contracts, and whether they are safe mid-kEnter from a key hook.** Only needed for answers longer than the original entry. The direct `editTop`/`editCursor`/`editTail` write is proven; this path is not.
- **Does `ws < 4` break a flash erase or archive garbage collect?** TI's flash routines poll a status register, which is itself a wait-state-timed flash read. No documentation either way — hence "restore before any archive operation".
- **Does `EF 2E` (the n/d fraction token) render flat or stacked inside a string literal?** WikiTI describes it as a distinct font codepoint (0F6h), and MathPrint 2-D layout applies to parsed expressions rather than string contents, so it should render flat — which degrades exactly the results a CAS produces most. Not confirmed on hardware. If it happens to render stacked, §2 Option B gets meaningfully better.
- **Does OS 5.8.4 still ship `04` in port `0x1005`?** The WikiTI figure predates it. One read of `0xE00305` settles it, and also reveals whether another program has already changed it.
- **The ~18 KB deletion total in §3** is a sum of individually measured pieces, not one measured build of the fully-stripped engine. ±15%. That uncertainty is precisely what makes the shadow-buffer arena a coin flip rather than a plan.