# ROM image

CEmu needs a ROM dumped from your own TI-84 Plus CE. It is not distributable,
is gitignored, and must never be committed.

1. CEmu: **Calculator → Create ROM image**.
2. The wizard produces a `.8xp` dumper and tells you where it saved it.
3. Send that `.8xp` to the calculator with TI Connect CE.
4. Run it on the calculator **via arTIfiCE** — `Asm(` is blocked on 5.8.4 without
   the jailbreak.
5. It streams ROM segments back over USB; the wizard reassembles them.
6. Save as `~/CEdev/ti84pce.rom` (outside this repo).

**Verify before trusting any measurement:** CEmu boots to the home screen, and
`2nd` `+` → About reports **5.8.4.0058**. A different OS version means the hook
offsets measured in M0 will not apply to your physical calculator.

## Sanity baseline

Once booted, type `2` `X,T,θ,n` `+` `2` `X,T,θ,n` `ENTER`.

Expect `0`. That is the bug SymCE exists to fix, and the before-picture for M0.
