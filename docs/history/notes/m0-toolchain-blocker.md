# M0 — Cap'n Hook / fasmg incompatibility

**Found:** 2026-08-28, while building Task 3.

## The problem

Cap'n Hook assembles with **fasmg**:

```make
FASMG ?=fasmg
$(LIB_8XV) $(LIB): $(SRC)
	$(FASMG) $< $@
```

**CE toolchain v15.0 does not ship fasmg.** Its `bin/` contains only
`cedev-config, cedev-obj, cemu-autotester, convbin, convfont, convimg,
ez80-clang, ez80-link`; assembly now goes through `z80-none-elf-as` (GNU
binutils). Cap'n Hook was last pushed 2022-09-15, predating that change.

Acquiring fasmg is not trivial: there is no Homebrew formula, `jacobly0/fasmg-ez80`
publishes no releases, and `flatassembler.net/fasmg.zip` returns 404.

## Resolution: M0 does not need Cap'n Hook

Cap'n Hook's value is letting **multiple programs share one hook type** — TI-OS
allows only one hook per type, so without it, installing SymCE would silently
uninstall any other homescreen hook. That matters at **release**, not for a spike.

For M0 we already have everything required to install a hook directly:

```
_SetHomescreenHook = 0021410h     iy+34h bit 4 = homescreenHookActive
_ClrHomescreenHook = 0021414h
_SetCxReDispHook   = 00214F8h     iy+36h bit 5 = cxRedispHookActive
homescreenHookPtr  = 0D025E1h
```

(recorded in spec §4.2–4.3; originally extracted from `capnhook/src/hook_equates.inc`,
which is plain text and needs no assembler to read)

**Revised plan for Tasks 3–5:** write the spike as a small C program using
`ez80-clang` with an inline-asm hook body, calling `_SetHomescreenHook` directly.
No fasmg, no Cap'n Hook, fewer moving parts for a go/no-go test.

## Deferred to M2

Restore Cap'n Hook for real coexistence, by one of:

1. **Build fasmg from source.** Locate a live mirror; fasmg is small and portable.
2. **Pin an older CEdev** that still ships fasmg, for building the library only.
3. **Port `capnhook.asm` to GNU `as` syntax.** Most durable, most work (~24 KB of asm).
4. **Check the `appvars` / `api-changes` branches** — may target newer toolchains.

Option 4 is the cheapest thing to try first.

## Impact

None on M1, which is complete. M0 Tasks 1 and 3 change shape; Tasks 2, 4, 5 remain
blocked on the ROM dump regardless.
