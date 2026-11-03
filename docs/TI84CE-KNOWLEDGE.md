# TI-84 Plus CE internals — what we know

A running notebook of how the calculator works, as learned while building SymCE.
Target: **TI-84 Plus CE, OS 5.8.4**, eZ80 CPU. Everything marked *measured* was
read off the real ROM under CEmu (`tools/emu/`). Everything else is sourced or
inferred and says so. **Append to this file whenever something new is learned.**

Last updated: 2026-09-26.

---

## 1. The big picture in one paragraph

The calculator runs TI-OS from flash. User programs (`.8xp`) are copied into RAM
and run there. The OS lets code plug into it through **hooks**: a 3-byte pointer
in RAM plus an enable bit; when the event happens, the OS jumps to the pointer.
SymCE is a **homescreen hook**: it sees every ENTER on the home screen, simplifies
`2X+2X` to `4X`, and hands the answer back to the OS to draw. Since OS 5.5 TI has
been locking this down; on 5.8.3+ a hook must live **inside a flash app** or the
OS resets the calculator. That rule is the source of the "y= crash". So SymCE
ships as a flash app: `prgmSYMCE` is only the installer, which writes the app
to flash and arms the hook inside it.

---

## 2. Memory map

| Range | What | Notes |
|---|---|---|
| `0x000000..0x3FFFFF` | Flash (OS + archive + apps) | Read-only at runtime; writes need the flash unlock (§8) |
| `<= 0x0BD869` | Low OS flash | A hook pointer here passes the 5.8.x validator |
| `0x3B0000..` | Flash **apps** region | Apps grow down from here, end to end (§8); the installer walks them with `0x22044` |
| `0x0C0000..` | Archive | 64 KB sectors growing up; an in-use sector starts `F0`. Apps may not enter the archive's last sector in use (§8) |
| `0xD00000..` | RAM | Anything `>= 0xD00000` is RAM |
| `0xD00080` | OS flags base (`iy` points here) | Flag bytes in §3 are `iy + offset` |
| `0xD005F8` | `OP1` | Float register 1 (9-byte real, §6). At home-screen hook `A=0` it holds the result the OS is about to show (*measured*: `2+2` then `*X` read it as 4) |
| `0xD0060E` | `OP3` | Scratch/float register; SymCE passes the answer text here |
| `0xD006C0` | `textShadow` | Home screen text, 10 rows x 26 cols. **Classic mode only**; holds display glyphs, not tokens |
| `0xD007E0` | `cxCurApp` | Current app: `0x40` home, `0x45` MODE, `0x44` graph (*measured*) |
| `0xD008F2` | History record | Where `0x91FC2` files the answer, as tokens |
| `0xD02317` | `begPC` | By `A=0`: first token of the entry **as the OS parsed it**, MathPrint boxes flattened (§5). Still the previous entry at `A=2` (*measured*); read prog `#` there instead |
| `0xD0231A` | `curPC` | Parser position |
| `0xD0231D` | `endPC` | Last token of that entry, inclusive |
| `0xD02437` | `editTop` | Edit buffer start |
| `0xD0243A` | `editCursor` | Cursor position |
| `0xD0243D` | `editTail` | Start of text after the gap |
| `0xD02440` | `editBtm` | End of edit buffer |
| `0xD025D2..0xD0261A` | Hook pointer table | §4 |
| `0xD0EE00..0xD13E00` | 20K the home screen never touches (*measured*, `ramscan.py`); inside `saveSScreen` | SymCE scratch lives here (§4). Unused RAM is **not** a legal hook location. A CEdev C program's bss+heap (`0xD052C6..0xD13FD8`) covers it, so nothing here survives running one |
| `0xD1787C` | App data org (from `app.inc`) | |
| `0xD1A87F..` | `userMem` | Where `.8xp` programs run |
| `0xD40000` | VRAM | 320x240 RGB565, 640 bytes per row. Only way to read MathPrint output |
| `0xE00005` | Flash wait states (port `0x1005`; `0xE00305` is a mirror, CEdev's crt0 uses it) | **Before rev M only** (parallel flash): cycles per flash byte = value + 6. OS ships `04`; `03` safe (CEdev since 2017), `02` untested, `01` flaky, `00` hard crash. Rev M+ (serial flash, boot code >= 5.3.6; this ROM is 5.6.1) ignores writes and reads `00` (*measured*, CEmu). SymCE sets `03` around each engine run (§10) |

CEdev C program layout (*m1-size-report*): code+rodata+data from `0xD1A87F`,
then bss, then the malloc heap; ~60 KB bss+heap.

---

## 3. OS flags (`iy = 0xD00080`)

| Addr | iy+ | Name | Bits we use |
|---|---|---|---|
| `0xD00080` | 0 | `trigFlags` | bit 5 `donePrgm`: OS sets before every parse; if still set when the hook returns NZ the OS prints "Done". **Clear it**. Bit 2 `trigDeg`: MODE DEGREE (*measured*: `0x24` after MODE `down down right enter`, at the ERR screen; SymCE passes it as mode bit `0x04`, and TOEXP( is `ERROR: MODE` / `USE RADIAN` there) |
| `0xD00081` | 1 | `editFlags` | bit 2 `editOpen`: set virgin, typing, menu open AND inside a box. Does not discriminate. SymCE refuses ENTER when clear |
| `0xD00085` | 5 | `textFlags` | bit 4 `textInsMode` (1 = insert; SymCE sets it). Whole byte: 22 in exponent and n/d, 20 in roots **and** flat. Useless for box detection |
| `0xD00088` | 8 | `apdFlags` | bit 4 `apdWarmStart`: turning on from APD or power loss. SymCE refuses ENTER |
| `0xD00089` | 9 | `onFlags` | bit 1 `parseInput` ("parse input when done"). SymCE refuses ENTER |
| `0xD0008C` | 0x0C | `cmdFlags` | bit 5 `cmdVirgin` (nothing typed on the line). `0x24/0x26` virgin, `0x04/0x0E` typing, `0x10` menu open. ENTER while virgin re-runs the last entry, or pastes a history line if one is selected. Bit 2 cursor able, bit 3 cursor shown right now (blink phase), bit 4 locked: menu open or a history line selected (`0x12`) (*measured*) |
| `0xD0009A` | 0x1A | — | bit 0 = MODE `ANSWERS: DEC` (*measured*: AUTO and FRAC-APPROX leave it 0). In Classic, AUTO already shows `1/4` as 0.25; MathPrint AUTO shows `1/4`. SymCE passes it on: `X/4` -> `0.25X` |
| `0xD000A4` | 0x24 | selftest | bit 2: force RAM reset on next power-on |
| `0xD000A8` | 0x28 | `APIFlg` | bit 4 `appRunning`. SymCE refuses ENTER |
| `0xD000B4` | 0x34 | `hookflags2` | bit 4 `homescreenHookActive`. **The same bit gates "OS calls the hook" and "sweep validates it"** |
| `0xD000B5` | 0x35 | `hookflags3` | window, graph, Y=, font hooks |
| `0xD000B6` | 0x36 | `hookflags4` | bit 1 parser, bit 6 menu |
| `0xD000C4` | 0x44 | MathPrint | bit 5 = MathPrint on |
| `0xD000C9` | 0x49 | — | result prologue: set bit 2, res bit 6 |

`0xD000B4` holds six other hook-enable bits: read-modify-write it.

---

## 4. Hooks

### Slots (3-byte pointer each; `vendor/capnhook/src/hook_equates.inc`)

`cursor 0xD025D5`, `library 0xD025D8`, `rawKey 0xD025DB`, `getKey 0xD025DE`,
**`homescreen 0xD025E1`**, `window 0xD025E4`, `graph 0xD025E7`, `yEquals 0xD025EA`,
`font 0xD025ED`, `regraph 0xD025F0`, `graphics 0xD025F3`, `trace 0xD025F6`,
**`parser 0xD025F9`**, `appChange 0xD025FC`, `catalog1 0xD025FF`, `help 0xD02602`,
`cxRedisp 0xD02605`, **`menu 0xD02608`**, `catalog2 0xD0260B`, `token 0xD0260E`,
`localize 0xD02611`, `silentLink 0xD02614`, `USBActivity 0xD0261A`.

Setters: `_SetHomescreenHook 0x021410`, `_ClrHomescreenHook 0x021414`,
`_SetParserHook 0x02149C`, `_SetMenuHook 0x021518`, `_SetCxReDispHook 0x0214F8`.
Full list in `hook_equates.inc`.

### Rules

- Hook body's **first byte must be `0x83`**. AsmHook2 arms `parser - 1`, where
  `parser` follows a `db $83`.
- Not taken: answering from the **parser** hook at A=0. It sees the flattened
  entry too, but AsmHook2 owns that slot (its hook `0x2241DA` acts on A=0 for
  `EF 7B` programs and on A=2 with `B = 0x5D`). Chaining to it needs its address
  somewhere that survives, and `0xD0EE00..` does not survive a C program --
  exactly what AsmHook2 runs. Homescreen A=2 plus prog `#` needs neither.
- One pointer per hook type. AsmHook2 holds `parser`; SymCE holds `homescreen`
  and `menu`. No collision. (*Measured*: with AsmHook2 armed and SymCE not,
  `menuHookPtr` reads `000000` and `hookflags4` `02`; its source only calls
  `SetParserHook`.)
- **The 5.8.x validator (*measured* 2026-09-14).** On every context switch into a
  full-screen app (Y=, WINDOW, MODE, GRAPH, QUIT) and on any `ERR:` screen, the
  OS walks every armed hook (`0x8C85C` -> sweep `0x8CB18` -> per-hook `0x8CAE6`)
  and resets (`rst 08`) unless `ptr <= 0x0BD869` or `ptr > lowest_app_addr`.
  - RAM body `0xD0F000` -> reset. Archived appvar `0x10E8F2` -> reset (in the
    gap). AsmHook2's body `0x2241DA` -> survives everything.
  - It checks only the pointer, not what the code jumps to afterwards.
  - The hook is **never called** on those keys before the sweep; self-disarm from
    inside the body is impossible (instrumented body: 0 invocations on `mode`/`y=`).
- Archived variables **move** on a garbage collect, which the OS runs whenever
  it wants contiguous archive. Never cache a pointer into the archive.

### Homescreen hook events (register A on entry)

- `A=0` display result. Return NZ = "I drew it, draw nothing". `begPC..endPC`
  hold the entry just evaluated.
- `A=1` key press, `B` = key code (`kEnter` = 5). Return Z to let the OS have
  it; NZ swallows the key.
- `A=2` evaluate. `begPC` is **stale** here: still the previous entry. The
  entry is in the temp program **`#`** (`OP1 = 05 23 00`, `ChkFindSym`), already
  flattened (*measured*): a size word, then plain tokens (`X^2` typed with a
  box is `58 0D`, n/d is `(num) EF2E (den)`). On an empty line (re-run) `#`
  holds the last entry again. OP1 itself names prog `!`, size 0: not it.
  NZ here skips the evaluation (`0x58763 jr nz,0x5878A` jumps over
  `0xA2180` (saves textShadow to `0xD0232D`), `0xA298C` (busy indicator) and
  ParseInp `0x99ABF`) and goes straight to A=0. The OS's `Ans` is then left as
  it was. SymCE answers here.
- `A=3` context switch.

*Measured* on `2 enter`: A=1 key, then A=2 eval, then A=0, `cxCurApp=0x40`.
The OS evaluates the entry numerically (with X's value) between A=2 and A=0, so
an entry that errors there never reaches A=0: `X²/X` with X = 0 is
`ERR:DIVIDE BY 0` unless the hook answers at A=2 (SymCE does). After an error,
`errNo` (`0xD008DF`) is `0x82` for DIVIDE BY 0 and `cxCurApp` is `0x52`; the
ERR screen is not in `textShadow`. ParseInp (`0x99ABF`) finds its input by `ChkFindSym`
(`0x838ED`, jump table `0x02050C`) on OP1, then sets `begPC` = data + 2,
`endPC` = data + 1 + size (`0x99CC7`).
ENTER on an empty line re-runs the last entry, and A=0 sees it at `begPC`.
ENTER that pastes a selected history line gets A=1 only: no evaluation, no A=0.
A program run from the home screen: at its A=0, `begPC` holds the home entry
(`prgmAA` = `5F 41 41`), not the program's lines, so a program's `Disp` and its
last-line value are never SymCE's (*measured*: `Disp 2X+2X` shows `0`).

### Menu hook (*measured* 2026-09-26, 5.8.4)

Slot `menuHookPtr 0xD02608`, enabled by `hookflags4` (`iy+0x36` = `0xD000B6`)
bit 6. `_SetMenuHook 0x021518` -> `0x23BE5` (`ld (0xD02608),hl`,
`set 6,(iy+54)`); `_ClrMenuHook 0x02151C` -> `0x23B27` (`res 6,(iy+54)`). The
OS calls it through `0x23B12`: `push ix`/`af`/`hl`, `0x257C1` (magic check,
`ix = ptr + 1`), `0x257CB` (depth counter `0xD0265B`, `jp (ix)`). Same 5.8.x
validator rule: it must point into a flash app. Deleting the app clears it, a
RAM clear drops it, and it moves with the app when an app above is deleted
(`tools/emu/lifecycle.py`).

Events, register A (from the call sites, and logged by a probe hook on
2nd MATH at home):

| A | Where | Registers | NZ return |
|---|---|---|---|
| 4 | `0x84F4F`, menu opening | `B` = menu id; `menuCurrent` not yet set | `ret nz`: the menu does not open |
| 0 | `0x850D3`, "which table" | `menuCurrent` set | the OS uses the hook's `HL` as the table (default: pointer table `0x86D23` + 3 x id; ids 56/57/58/5B special). Every table read goes through here (header `0x85357`, item `0x853EB`, `0x8948B`) |
| 2 | `0x853AB`, draw tab title | `C` = tab, `HL` -> its title id byte | skip the OS's draw (`0x853C8`: `res 3,(iy+5)`, `pop hl`, `inc c`, next tab); `C` must survive |
| 1 | `0x855BB`, draw item | `C` = item, `DE` = its key code (D prefix), `HL = 0x872F0`; the OS has drawn `n:` already | skip the item's text |
| 3 | `0x86098`, key in the menu | `B` = key | `ret nz`; on Z the OS reloads `A` from `B` |
| 5 | after the selection moves | | |
| 6 | `0x85955`, RIGHT/LEFT tab switch | | `ret nz` |

Opening TEST logged: A=4 B=09; A=0 twice; A=2 for C=0,1,2 (HL `0x86F8A..C`);
A=0; A=1 for C=0..5 with DE `FE0A..FE0F`. `A` after an NZ is never read.

Menu RAM: `menuCurrent 0xD00824`, `menuCurrentSub 0xD00825` (tab),
`menuSelected 0xD00826`, `menuNumMenus 0xD00827`, `menuNumItems 0xD00828`.
TEST open at home: `09 00 00 03 06`; `cxCurApp` stays `0x40`. The menu does
**not** remember its tab: CLEAR then 2nd MATH reopens on tab 0, item 0.

**Menu table format.** `numTabs`, one item count per tab, one title string id
per tab, then every item's **key code** (2 bytes, prefix first) in tab order.
Item i of tab s is at `1 + 2*numTabs + 2*(counts before s + i)`, computed in
8 bits. TEST (menu 9, and 10 shares it) is at `0x86F86`:
`03 06 04 10 54 33 74`, then `FE0A..FE0F` (`= ≠ > ≥ < ≤`, kE1BT+10..),
`FE10..FE13` (and or xor not), `0001..0010`. **5.8.4's TEST menu has three
tabs: TEST LOGIC CONDITIONS** (title ids `54 33 74`); CONDITIONS' 16 items are
special-cased by `0x894B2` (`menuCurrent == 9 && sub == 2`, item draw
`0x8840B`, ENTER at `0x85CD1`). MATH (8) is at `0x87028`: 5 tabs,
`0D 0E 07 08 04`, titles `35 3B 25 43 6D`.

**Tab titles** are string ids: `0x8A01A` looks up `0x8A03D + 3*id` and prints
it with `_PutPS` (`0xA1E9B`, jump table `0x0207C8`), `PutC` (`0xA1D0A`) per
character, wrapping at column 26. The header loop (`0x85357`) sets
`(iy+8)` bit 0 once and `textInverse` (`iy+5` bit 3) for the current tab, and
puts a space between tabs. `PutMap` (`0xA1948`) draws a 12x18 cell at
`x = 2 + 12*col` (shifted 2 px left when either flag is set), row 0 at
`y = 37..54`, next row at 57, and leaves `(0xD0059C)` = VRAM address of the
cell's **bottom** row. "TEST LOGIC CONDITIONS" is 21 columns (CONDITIONS ends
near x 251); a fourth title gets columns 22..25, so at most 4 characters.
Items: `_PutPSB` `0xA1E77` (`0x0207C4`), `_PutS` `0xA1E5B` (`0x0207C0`).

Key codes `0x21-0x24` (and `0x2A`) are unassigned (between `kOverWAll 0x20`
and `kNo 0x25`). As a menu item one is drawn by nobody (the hook must, at
A=1), and picking it closes the menu and hands the code to the home screen:
the homescreen hook sees A=1, `B = 0x21..0x24`.

**A pick carries a second byte** (*measured*, `0x867AB`/`0x867CD`). An item
is (prefix, code). A prefix of `FF FE FC FB FA` is the key and the code goes
to `keyExtend 0xD0058E`; **any other prefix goes to `keyExtend` and the code
is the key**. So items `00 21`, `01 21`, ... `09 21` are all key `0x21`, told
apart by `keyExtend` = 0..9, which still holds it when the homescreen hook
sees the key. One unassigned code covers a whole tab.

More, all *measured* with SymCE's tab: at A=1, `C` is the item's **absolute**
index, also after the list scrolls; the OS does not erase the rest of the row
when the hook draws an item, so a shorter label leaves the old one's tail
unless the hook calls `EraseEOL 0x020820` after it; and by the time the picked
key reaches the homescreen hook's A=1, the menu is gone and the home screen
redrawn, so a popup opened then saves the right pixels. The title need not fit
4 columns: drawn by the hook at A=2 with VPutS in the **small** font (`(iy+0x32)`
bit 2 clear), "Algebra" fits after CONDITIONS.

**Six tabs** (*measured* 2026-09-27, SymCE's Algebra, Calculus and
Geometry tabs, table `06 06 04 10 0A 0E 0D 54 33 74 00 00 00`, 139 bytes:
offsets stay under 256). LEFT from TEST wraps to the **last** tab, RIGHT
from it to TEST. The OS puts a space before every tab after the first,
**also after a tab whose A=2 returned NZ without drawing**. The hook draws
the **whole** title row itself at the last tab's A=2 (index 5, called
whichever tab is current): `(0xD0059C)` is the last cell's bottom row, 17
rows below the row's top; it fills 320×18 white and writes all six titles
in the small font from x 2, 4 px apart, the current one white on black.
The small font is wide: after CONDITIONS 146 px are left (x 173..319) and
"Algebra" "Calculus" "Geometry" take 176, so the titles are `Alg`, `Calc`,
`Geo` (ending at x 272; full names clipped Geometry to a `G` at the edge).
**VPutS draws inverted while `textInverse` (`0xD00085` bit 3) is set**, and
the OS sets it around the current tab's A=2: black on white drawn by the
hook came out white on black, so clear it while drawing and put it back. A
tab of 14 items scrolls like the OS's own: `↓` after the 9th row's number,
`↑` on the top row, items past 10 labelled `0 A B C D` by the OS, and `C`
at A=1 is the index in the tab. With `keyExtend` codes `00..09` (Algebra),
`10..1D` (Calculus) and `20..2C` (Geometry) one key code, `0x21`, still
covers all three tabs.

### Font hook (measured 2026-09-28, 5.8.4)

Measured with a temporary probe build (since removed; the shipped
implementation is the "Implementation" paragraph at the end of this
subsection). Verdict: a new
system-wide font is **feasible with two hooks, not one**. The font hook
(`0xD025ED`, `hookflags3` bit 5) only reaches the LARGE font; the small font's
glyphs go through the LOCALIZE hook (`0xD02611`, bit 1).

**Who calls what (*measured*, static + probe).** Only one `_CallFontHook`
(`0x020130` -> `0x23A1C`) caller exists in the jump-table form; the OS calls
`0x23A1C` directly from four places, each guarded by `bit 5,(iy+53)` (the OS
checks the flag; the hook is not called with the bit clear):

| A in | Routine | Registers in | Default (hook returns NZ) |
|---|---|---|---|
| 1 | `Load_LFont` `0x7BFEE` (also a copy at `0x18C0AB`) | `B` = char, `HL` = char*28 | copy 28 bytes from `boot.GetLFontPtr` (`0x3D6E`, = `0x380`) + `HL` to `0xD005A5`, zero-pad, return `HL = 0xD005A1` |
| 2 | small-font WIDTH, `0xA55A9` (copy `0x1B56AE`) | `B` = char, `HL` = char*25 | `ld b,(0xA3CA9 + HL)`: `B` = width |

If the font hook returns NZ, `Load_LFont` then calls the localize hook
(`0x2398E`, `bit 1,(iy+53)`) with A = `0x76` (large glyph), and the small
routines call it with A = `0x75` (`Load_Sfont` `0xA55D3`: small GLYPH, `B` =
char, `HL` = char*25) and `0x77` (small width). **`Load_Sfont` never calls the
font hook.** Z from the localize hook = "done". The localize hook also sees the
OS's own translation calls (A = `00`, `1A..1C`, `B0..B9`, `D9`, `E3`, `E7`,
`E9`, `EC`, `F2` seen): answer `75`, `0A` as below, NZ for the rest except the few
events in "Localize call sites" below.

**Localize events whose call site ignores Z (*measured* 2026-10-05, 5.8.4; found
by logging A and the caller's return address in the hook).** The wrapper is
transparent (A, F, HL, DE, BC reach the caller as the hook left them), so a
site that does not test Z uses whatever the hook returns. The no-hook result is
what the hook must give for such an event:
- **A=0x0A, site `0x7BB8B`** (menu-name fetch, `jr` on without a Z test): uses HL
  and then `dec hl`. No hook: HL = (HL). NZ with HL untouched left every row on
  the same pointer, so 2nd CALC drew `3-root(` for all 7 rows with the Evo font
  on (CALC from the grapher, TI's graph and Y=). The hook now does `ld hl,(hl)`
  for 0x0A (d033a8c). The neighbour, A=9 at `0x7BB5C`, does test Z.
- **A=0x49, 0x4A, 0x4B, sites `0x80D8C`, `0x80E3B`, `0x81037`**: store the A the
  call returns to curCol (`0xD00596`) with no Z test. No-hook values 0x0A, 0x0D,
  0x0C. Our hook leaves A = the event number. Never seen firing on any screen
  logged with Evo on (Y=, WINDOW, MODE, TABLE, STAT and edit, 1-Var Stats, APPS,
  FORMAT, ZOOM, CATALOG, MEM, LIST, DRAW, VARS, MATH, program editor, error,
  matrix), so left alone.
- **A=0x4C, site `0x819B0`**: `jr`s over its default code (curCol = 0x0A, then a
  text routine) whenever any localize hook is armed; no return value restores
  the default. Also never seen firing.
- **A=0xE1** (token names; sites `0x882C4`, `0x882EE`, `0x8833F`, `0x88393`,
  `0x883A9`) does `ret z` after the call, so NZ is correct and the default runs.
- The `call nz,wrapper` sites (e.g. `0x6B52B`, `0x7BB0B`) follow the hook-off bit
  test, so the hook-off state is Z there; none was observed to matter.
Rule: any localize event whose site ignores Z must get its no-hook result from
the hook. Evo off clears the localize hook (fontClr), so none of this applies
then. Regression: `tools/emu/graph.py menus` (2nd CALC rows must differ).

**Wrapper (`0x23A1C`, *measured* from disassembly).** Saves IX/AF/HL, loads
the slot, reads its first byte, `cp 0x83`. Mismatch: it does `res 5,(iy+53)`
(hook disables itself) and returns to the default. Match: `A` restored,
`HL` = the caller's, `IX` = ptr+1, `call` it, `0x23955` (depth counter
`0xD0265B`). The hook may clobber AF, DE, HL, **but must keep B** (the callers
do `ld a,b` afterwards). Return **Z = "I filled the record, use it"**, NZ =
"use your default". The localize wrapper is identical (slot `0xD02611`, bit 1);
on a bad first byte it also writes `0x0109` to `0xD025CF` (`ld.sis`).

**Large glyph (font hook A=1), *measured*.** Record at `lFont_record`:
`0xD005A1..A3` zero, **`0xD005A4` = width byte**, `0xD005A5..C0` = 28 glyph
bytes, `0xD005C1..C4` zero (`0xA256F` sets `(0xD005A4) = 0x0C` after
`Load_LFont` returns, whatever the hook did, and ignores the returned `HL`:
so the hook fills `0xD005A5..` and returns Z, **and must leave `0xD005A4` = 0**:
the draw takes a 15th row, 2 bytes at `0xD005A3..A4`, ABOVE the glyph
(*measured*: a hook that wrote the 0x0C itself put a 2-px tick, cols 9-10 of
0x0C, one row over every glyph in menus; the ROM path never does, the default
loader leaves `A1..A4` zero at draw time). Zeroing it fixed it. **14 rows x 2 bytes, top row
first.** A row is 12 columns: **byte 0 bits 7..3 = columns 0-4, byte 1 bits
7..1 = columns 5-11** (byte 0 bits 2..0 and byte 1 bit 0 are ignored). A solid
`FF` glyph is a 12 x 14 block; the OS's own glyphs use columns 0-9 only (a 5+5
split: 10 px wide, 2 px of spacing), advance is 12 px (monospace). The 256 ROM
glyphs are at `0x3D6E + char*28` (ends exactly at `0x596E`); *measured*:
'2' drawn matches its ROM bytes `38E0 78F0 C018 ...`. Char codes are DISPLAY
glyph indices (0x12 = squared, 0x5B = theta, 0x10 = sqrt), not tokens.

**Small glyph (localize A=0x75), *measured*.** Record `sFont_record`
`0xD005C5`: **byte 0 = width (advance)**, then glyph bytes; on Z the OS
zero-pads `0xD005DE..E1` itself and returns `HL = 0xD005C5`. The hook must
fill all of `0xD005C5..DD` (25 bytes). Layout depends on the width:
**width <= 8: 1 byte per row, 14 rows, MSB = left column; width > 8: 2 bytes
per row, 12 rows, columns 0-7 from byte 0 and 8..width-1 from byte 1** (probed
with width 11: bits past the width are clipped). Advance = the width byte
(11 -> next glyph exactly 11 px on; the A=2 width query with B forced to 30
did NOT move the next glyph). Glyphs at ROM `0xA3CA9 + char*25` (256 of them,
ends at `0xA55A9`). ROM 'A': `08 | 00 00 30 78 CC CC FC FC CC CC CC CC CC 00
..` (8 wide, drawn rows 2-12); sqrt (0x10) `0B | ...` (2 bytes/row).

**MathPrint reduced glyphs are the SMALL font, cropped (*measured*).** A
numerator/denominator `A` came from localize A=0x75 (probed with a pattern:
period-4 rows, i.e. the 2-byte/row small record; solid `FF` gave an 8 x 10
block). Only **rows 2..11 of the record are drawn (10 rows)**; the status bar
draws rows 0..11. There is no separate reduced font and no font-hook call for
them; `fracDrawLFont`/`fracTallLFont` (`fontFlags`, `iy+0x32` bits 2, 3) were
never seen to matter for the fraction template (*inferred*: `0xA256F` takes the
large path only when bit 2 or bit 6 of `iy+0x32` is set, and neither was set
while the template drew).

**Surfaces (solid 'A' seen in VRAM, counts from the probe log):**

| Surface | Hook that drew it | Evidence |
|---|---|---|
| Home entry line + answer, Classic | font A=1 (12 x 14) | block, +11 calls per key |
| Home entry line + answer, MathPrint | font A=1 | block 12 wide |
| MathPrint n/d numerator/denominator | localize 0x75 (8 x 10, small) | block, 0x75 count up |
| Status bar (NORMAL FLOAT AUTO ...) | localize 0x75 (small, rows 0-11) | block 8 x 12; A=1 histogram has none of its letters |
| MATH menu (tabs + items) | font A=1 | `M#TH`, `FR#C`; A=1 +60, 0x75 +0 |
| ERR:SYNTAX title + Quit/Goto | font A=1 | `SYNT#X` |
| Y= editor | `Y1=` font A=1; `Plot1`, subscripts localize 0x75 | +46 / +50 calls (no capital A on screen) |
| MODE screen body | localize 0x75 (proportional small font), A=1 +0 | +677 calls; `M#THPRINT`, `FLO#T` |
| Graph text (`Y1=A` label, `X=`, `Y=`) | localize 0x75; font A=2 in TRACE only | solid small block; A=2 seen 2x per TRACE |

Costs: one call per non-blank glyph (spaces and the cursor's `0xE0` count);
prologue (boot, two APPS screens, installer, home) = 71 large-glyph calls;
a key press on the home screen = 8-11; the MODE screen = ~740 small-glyph
calls; the MATH menu ~60 large. With both hooks armed an unanswered large
glyph costs two calls (font A=1, then localize 0x76). QUIT from Y= redraws the
home screen with only ~11 large-glyph calls (it does not re-render every
character), so no per-screen redraw figure is reliable.

**Survival (*measured*).** Both hook pointers were inside the flash app
(`0x1F45xx`). All 33 triggers of `crash.py` (Y=, WINDOW, MODE, GRAPH, ERR, STAT,
APPS, TABLE, FORMAT, ZOOM, TRACE, CATALOG, MEM, LIST, DRAW, VARS, MATH, STAT
EDIT, off/on, PRGM editor, GC, HORIZ, SymCE's menus/tabs) left both armed
(`hookflags3 = 0x22`), left SymCE answering `4X`, and reset nothing.

**Traps.** (1) (probe only: counts were read once at the end of a key
sequence.) (2) A font pointer
that does not start with `0x83` silently disarms itself. (3) Keep `B`; the
callers restore `A` from it. (4) Hook Z with a wrong record, and no crash:
you just get whatever is in `0xD005A4..` / `0xD005C5..`, so always fill the
whole record (width byte first). (5) The large-font hook cannot change the
advance (12, forced by `0xA256F`); the small font's advance is its width byte.
(6) Small font needs the localize hook, which other code (translations) also
calls: leave every A but `0x75..0x77` to the OS (return NZ). (7) SymCE
takes the localize hook over while the Evo font is on (TI's language apps use
it); the settings screen turns the font on and off with key 3, and CAS off (key 1) only clears the hooks that still point into SymCE.

**The insert cursor is a font glyph: a vertical bar instead (*measured* 2026-10-06, 5.8.4, `tools/emu/cursor.py`).**
The cursor is drawn through `Load_LFont` like any character: large glyph `0xE4` =
insert, `0xE5` = insert after 2nd (arrow), `0xE6` = after ALPHA (A), `0xE7` = ALPHA
lock (a). ROM glyphs (`0x3D6E + g*28`): `0xE4` is only a 10 x 2 underline at rows
12-13; the others are the arrow / letter on rows 0-9 plus the same underline. The OS
draws the glyph with its columns starting 2 px into the cell (so cols 0-1 of the
record land at cell x+2, where a character's own first column lands). Overwrite mode
and CAS off use the solid `0xE0` block (10 x 14), which the hook leaves alone.
Classic and MathPrint use the same glyphs on the entry line. The char under an
insert cursor is not drawn while the cursor is on (TI's underline did the same; the
glyph replaces the cell). `fhook.c` answers `0xE4..0xE7` itself: the ROM glyph, rows
12-13 zeroed, byte 0 bit 7 and 6 set on all 14 rows (a 2 x 14 px bar, cols 0-1),
width byte 0 as always. It needs no table: the hook copies from the ROM font.
Two copies of the body in the app, told apart by a byte after the two words
(`_end + 6`): `symce_fhook` (1: the Evo glyphs too) and `symce_fhook_c` (0: the bar
only, every other glyph NZ at once). Armed: Evo on = `symce_fhook` (+ localize hook);
Evo off and CAS on = `symce_fhook_c`; CAS off and Evo off = none. `fontIsOn` still
means "our pointer is `symce_fhook`", so the settings screen shows EVO/TI correctly;
`fontFlip`/the CAS toggle re-arm the cursor body (`fontCur`, `fcurClr`). The TI-cursor
setting (key 2) does not change this: any CAS-on state gets the bar. Inside a
MathPrint fraction/exponent the cursor is a solid 8 x 10 block of the small size
(*measured*: not ROM small glyph `0xE0`, which is 11 wide and a different shape),
so no glyph to hook: left as TI's block. **Box cursor measured 2026-10-07 (not solved):** inside a box (exponent, n/d) textFlags bit 4 (insert) is already set, the cursor is a solid 8 x 10 black block right after the last glyph, and it is NOT produced by the font hook (no E0..E7 large call) or by a small glyph. Localize A=0x75 B=0xEF (ROM width 10, all-zero record) is called ~4 times per 3 s while a box cursor is up, but replacing its record (0x3F/0xFF rows, FF/00 rows, a per-row diagonal) changed the picture only in the first ~500 ms after a key (a white blob 'staircase' inside the block, 6 rows, 2-4 px wide, also present with TI's own record); the steady block stayed 8 x 10 solid with every record. So 0xEF is some transient overlay, not the cursor. The steady block is drawn by OS code that no SymCE hook reaches (a fill, not a glyph; routine not found: 0x5C80A's home blink path draws glyph 0xE4 through 0xA1948/Load_LFont only for the entry line). Patching the ROM image with call counters (jp to a stub in the 0x132DA0 free flash) did not boot in CEmu. **Cursor hook probe (*measured* 2026-10-07):** a counting body in `cursorHookPtr` (0xD025D5, flag `iy+52` bit 7) fires in Classic (A = 0x22 on, 0x23 off, 0x24) and NEVER in MathPrint, neither for the entry line nor in a box (0 calls in 5 s). So the cursor hook cannot reach the box cursor. Useful: tools for this need hook-side counters (inc a byte per glyph index at 0xD0EF80 / 0xD0F080).
(0x50): the hook uses `ld a,b / ld d,a` for that.

### Localize call sites (*measured* 2026-10-06, 5.8.4; ROM scan of `ld a,ev; call 0x2398E | 0x802F4`, the OS image is stored twice, `0x0xxxxx` and `0x1xxxxx`)
Two call shapes. **A** (`bit 1,(iy+53)` then `call nz,0x2398E`, or `call 0x802F4`, the
wrapper doing the same): no hook = Z with A and HL untouched, then the site runs its
own default. **B** (`bit 1,(iy+53); jr z,default; call 0x2398E; jp z,done`): Z = "the
hook did it", so the no-hook default is NZ.

SymCE's `lhook.c` answers **NZ, A and HL untouched** (what it always did, the safe
answer at every B site and at every A site that ignores the flags, dynamic events
included) except for the A sites that branch on the flags, where NZ reads as "the hook
supplied HL"/"handled" and those get **Z**. The Z table, with the site and its branch:
`42` 0x9E44F `jr nz` (skip the default DE); `43` 0x9E2BA `jr z`; `7A` 0x62037 `jr nz`;
`C8` 0x9E17A `ret nz`; `C9` 0x51BE7 `jr z` (Catalog Help token table, else it returns
the hook's A-1); `CA` (no static site: the "ARGUMENTS SELECTED FROM GRAPH SCREEN" box of 2nd CALC, found by the probe, a wrapper caller with A from a register); `CB` 0x51F08; `CC` 0x51E81; `D8` 0x4EC8D, 0x51DDB (the help text).
(Each also has a copy at +0x110000-ish in the second OS image.) `B3` (0x51C91) is an A site
that ignores the flags (uses HL), so it stays NZ. Events `75` (small glyph) and `0A`
(HL = (HL), site 0x7BB8B) are answered. All other statically found events (`00 01 07
09 0D 14 28 29 30 36 3A 3B 41 47 76 77 79 BC D3 DD E1` are B sites; the rest are A
sites that ignore the flags) stay NZ. Dynamic events (A loaded from a register): the
status bar words are B = `E3 E4 E5 E7 E9 EA` at the single B site 0x8BF30 (set at
0x8C0DC..0x8C18D; B = 0 skips it), which is why "Z for everything" garbled the status
bar. `D3` (error description lines, 0x622F7, B): NZ = draw the OS lines, Z = the hook
drew them. Found by logging A in a probe hook into a 256-byte map at `0xD13B00`: home
+ MATH + `+` sees `07 65 75 76 B3 C9 CC CD CE D8 E1 E3 E7 E9 EC`; `graph` sees `00 65
75 76 B0 B4 B5 B9 BA D9 E3 E7 E9 EC F2`.

### Raw key hook: taking over a key (measured 2026-09-30, 5.8.4)

SymCE uses it to send the GRAPH key to its own grapher (`symce_khook` in
`symce/app/main.asm`).

- Slot `rawKeyHookPtr 0xD025DB`, enable bit `hookflags2` (iy+0x34) bit 5.
- **Misnomer:** the include's `ti.SetGetKeyHook` (0x213CC -> 0x23B55) arms
  THIS slot, not getKey (0xD025DE). `ti.ClrRawKeyHook` is 0x213D8.
- Call site: the monitor loop only (`0x8C482 bit 5,(iy+52)` / `0x8C488 call
  0x2392F`). It runs once per key, with A = the key, after `RunIndicOn` and
  *before* the monitor dispatches context keys (kGraph 0x44, kYequ, ...). The
  dispatcher 0x2392F checks the 0x83 byte (0x257C1) and calls the body
  (0x257CB). It bumps the depth counter `0xD0265B` around the call.
- Return A = the key the OS should process. `ret nz` with A unchanged passes
  it through.
- The body may simply not return. SymCE runs a whole full-screen program from
  it, then `jp ti.JForceCmdNoChar`:
  - `JForceCmdNoChar 0x8C6E1` (`xor a`, then `JForceCmd 0x8C6E2`) reloads SP
    from `0xD007FA` (the monitor's own), so the abandoned hook frame doesn't
    matter.
  - It re-enters home at 0x8C5BA, which never calls the raw key hook.
  - The counter `0xD0265B` left at 1 is zeroed by home init (0x5830D).
- Leaving another context first: `ld a,kQuit` / `call ti.NewContext0`
  (0x8C85E) runs the current screen's putaway, so Y= files an equation still
  being edited. NewContext0 from home to home re-inits home. Switching to the
  context you are already in (not home) does nothing.
- `cxCurApp 0xD007E0` tells you where the key came from:
  - 0x40 home, 0x44 graph, 0x50 PrgmInput, 0x52 Error, >= 0x58 flash apps.
  - Leave GRAPH to the OS on ERR (TI ignores it there), on a program's Input,
    and in flash apps.
- **Busy dots.** The monitor calls `RunIndicOn 0xA298C` at 0x8C459, before the
  hook. It sets indicFlags `0xD00092` bit 0 (`indicRun`, set only at 0xA29A9).
  The timer ISR 0x3D0DC -> 0x3D25B -> 0xA34A8 then draws the dots at x 293-300,
  y 0-12 over whatever a non-returning hook draws.
  - `RunIndicOff 0xA3649` returns early without clearing when apdFlags2
    (`0xD0009B`) bit 6 is set.
  - What works: `res indicRun,(iy+indicFlags)` before drawing. Pixel diff of
    the top rows went from 16 rows to 0.
- The raw key hook survives the 5.8.x validator only because the body is in
  the flash app (see Rules).
- A RAM clear disarms it, like every hook.
- One slot: arming it replaces any other app's raw key hook.
- Build trap hit while measuring this: `make PAD=132000` writes to
  `bin/pad132000/`, so copying `bin/SYMCE*` afterwards tests a stale build.

### SymCE's hook (`symce/src/hook.c`)

State, fixed RAM inside `saveSScreen` (zeroed when armed):

| Addr | Name | What |
|---|---|---|
| `0xD0EE00` | `baseTop` | `editTop` captured while `cmdVirgin` is set (insert mode) |
| `0xD0EE03` | `baseOk` | `0xA5` once `baseTop` belongs to this entry |
| `0xD0EE04` | `pending` | `0xFF`: ENTER, for A=2 to try. `0xFE`: A=2 answered, A=0 shows it |
| `0xD0EE05` | `inKey` | Nonzero while A=1 runs a cursor key through `(cxMain)` itself: the OS's own A=1 call back passes the key on, and clears it |
| `0xD0EE20` | `lastAns` | What `Ans` stands for: length + SymCE's tokens (64 at most), or `0xFF` + a copy of `OP1` (the OS's own result); `0` = nothing |
| `0xD0EE80` | `ansBuf` | The engine's answer as tokens, 64 at most; at A=0 the text `0x91FC2` draws. At A=2 it also holds OP1 for a moment |
| `0xD0EEC0` | `ansBuf+64` | The Classic columns the engine counted for its answer (capped at 255), for A=0's `BC`. A=0 reads it **before** copying `lastAns` into `ansBuf`: a 64-token answer's NUL lands on it |
| `0xD0EF00` | `engineWork` | Engine scratch, `SYMCE_WORK` bytes (under 0x4000; the free region allows 0x4F00). The ALPHA+DOWN popup also keeps the VRAM under it here while it is open (§7, "A popup over the home screen"): 12 header bytes, 2-bit codes from `+12`, 16-bit literals up to `0xD13E00`; the engine only runs at A=2, never then |

The app places two relocated words right after the body's last byte:
`symce_engine`, then `symce_menu` (`src/menu.c`, linked on its own like the
engine; its image starts `jp menu_key`, `jp menu_tab`). The body reaches each with `ld hl,(hookPtr)` + offset, `ld hl,(hl)`,
`jp (hl)`, after the same `83 FE 01` check.

Then one byte, the cursor (`_symce_hook_end + 6`, read through `(hookPtr)`):
the app carries the body twice, `symce_hook` with 0 (insert cursor, the
default the installer arms) and `symce_hook_ti` with 1 (TI's own cursor). The
settings screen (APPS > SymCE, key 2) arms one or the other with
`SetHomescreenHook`: no RAM flag, no appvar, so the choice is lost with the
hook on a RAM clear. Nonzero skips `set 4,(textFlags)` and the `(cxMain)`
wrapping of the cursor keys in item 1 below: the keys pass to the OS with Z as
if SymCE never touched the cursor. Only ALPHA+DOWN on the entry still goes to
the menu. Everything else (ENTER marking, A=2, A=0) is the same in both.

1. **A=1, any key:** `pending = 0`. On the home screen, set `textInsMode` if
   `editTop == baseTop` (cursor on the entry, not in a box or menu). There,
   RIGHT LEFT UP DOWN CLEAR DEL 2nd-LEFT 2nd-RIGHT (`01 02 03 04 09 0A 0E 0F`) go
   through `(cxMain)` from the hook (`inKey` set, so the nested A=1 passes
   them on), `textInsMode` is set again if the cursor is still on the entry
   (not a box it moved into, not a history line UP selected; `baseTop` retaken
   if the line is virgin), and the key is swallowed (NZ): the OS ends insert
   mode on those keys, after the hook, and drew the block cursor until the
   next key (fixed 2026-09-28). If the key is ENTER and none of `apdWarmStart`, `parseInput`, `appRunning` is set and
   `editOpen` is: `pending = 0xFF`. Return Z. The entry is never touched, so the
   echo and `2nd ENTRY` show exactly what was typed.
2. **A=2, `cxCurApp = 0x40`, `pending = 0xFF`:** check `(homescreenHookPtr)`
   still points at a body starting `83 FE 01`. Save OP1, find prog `#` with
   `ChkFindSym`, give OP1 back. Call `symce_engine(data+2, size, ansBuf,
   engineWork, lastAns, mode, look)` (mode: `mpFlags & 0x20 | (iy+0x1A) & 1 | trigFlags & 4`; `look`: the body's own `.Llook`, see §7 `_ChkFindSym`, real variables) through the relocated pointer stored right after the
   body's last byte. On an answer: copy it to `lastAns`, `pending = 0xFE`,
   return NZ, so the OS never evaluates it. No answer: return Z. `A = 0xFF`
   (a command the engine cannot finish): copy 26 bytes of `ansBuf`
   (`"MESSAGE",0,"LINE",0,0`) to `appErr1`, `pending = 0`, then error `0x2B`
   through `_JError` (§7, "Custom errors from the hook").
3. **A=0, `cxCurApp = 0x40`:** `pending = 0xFE`: `pending = 0`, run the result
   prologue (§7), copy `lastAns`'s tokens to `ansBuf`, `call 0x91FC2` with
   `HL = ansBuf`, `A = 0` (Classic) or `0x18` (MathPrint, `/` as `F6`), `BC` =
   columns drawn, as the engine counted them at `ansBuf+64` (§7, "Radicals,
   equations" and "Functions"), `res donePrgm`, return NZ. `pending = 0xFF` (A=2
   refused): `pending = 0`, `lastAns = 0xFF` + the 9 bytes of `OP1`, return Z,
   and the OS draws its own number.
4. **A=1, `B = 0x08` (ALPHA+DOWN), on the entry** (the step 1 test
   `editTop == baseTop`, and `cmdFlags` bit 4 clear): call `symce_menu()`, return
   NZ so the OS never sees the key. The menu draws a popup like the OS's
   ALPHA+Y= ones, Nspire CAS's **Algebra** menu: `solve( factor( expand(
   deriv(`, `comDenom(` and five submenus marked `►` (Polynomial Tools,
   Convert Expression, Trigonometry, Complex, Extract), and beside it on a
   second tab the **Calculus** menu (Derivative .. Numeric Calculations, 14
   items, two submenus) and on a third the **Geometry** menu (Distance ..
   Transformations, 13 items, three submenus). It reads `GetCSC`: a digit, or past 10 items a letter
   key without ALPHA (`A`-`D`: MATH `0x2F`, APPS `0x27`, PRGM `0x1F`, x⁻¹
   `0x2E`), or UP/DOWN + ENTER picks; it shows 10 rows and scrolls, with the
   large font's `↑` (`0x1E`) and `↓` (`0x1F`) in place of the `:`; ENTER or
   RIGHT on a submenu opens it; at the top LEFT/RIGHT switch tabs, wrapping; in a
   submenu LEFT or CLEAR goes back up onto its item; CLEAR at the top closes;
   2nd is ignored and any other key closes. It puts the pixels back, and on a pick hands each key
   code of `FACTOR(`, `POLYDEGREE(` etc. to `(cxMain)`, which types it (and
   calls the hook at A=1 again, harmlessly). The numerical items hand
   `(cxMain)` the OS's own key instead, as MATH's table has them (`0x87028`,
   prefix 00): nDeriv( `0xC7`, fnInt( `0xC8`, fMin( `0xF1`, fMax( `0xF2`.
   In MathPrint that is the same template, screen for screen, as MATH 8 by
   hand (*measured*, `e2e.py`); the tokens (`25 24 27 28`) fail the engine's
   parse, so the OS evaluates them: `nDeriv(X^3,X,2)` 12, `fnInt(X,X,0,1)`
   0.5, `fMin(X²-2X,X,-5,5)` 0.99999785. Inside a MathPrint box, on a
   selected history line, or off the home screen, the key goes to the OS
   unchanged.
5. **A=1, `B = 0x21`, anywhere on the home screen** (boxes and history
   lines too): the same call, `symce_menu(bc)`, with the item number in
   `keyExtend` (§4, menu hook), plus `0x10` for Calculus, `0x20` for
   Geometry. These are the items of the **Algebra, Calculus and Geometry
   tabs in 2nd MATH** (fourth to sixth, §4 "Six tabs"): the menu hook (`src/mhook.c`, a shim into `menu_tab` in
   `src/menu.c`, reached through the relocated word after its body, as
   `symce_menu + 4`) answers A=0 for menu 9 at `cxCurApp 0x40` with a copy of
   the OS's TEST table plus three tabs whose items are `00 21..09 21`,
   `10 21..1D 21`, `20 21..2C 21`; draws the title row at the last tab's A=2
   ("Six tabs") and each item's text at A=1 with `_PutS` + `EraseEOL`. A
   command item types; a submenu item opens the popup at that submenu, where
   LEFT/CLEAR go up to its tab's menu. Everything else returns Z.
   Off the home screen (program editor) TEST is stock.

**Why ALPHA+DOWN** (*measured*, 5.8.4, both modes). At the home screen:
ALPHA+GRAPH opens the graph (`cxCurApp 0x44`) before any hook call;
ALPHA+Y=/TRACE/VARS/STAT open OS menus (`cmdFlags 0x10`) without an A=1;
ALPHA+UP is `B = 0x07` and selects a history line in MathPrint
(`cmdFlags = 0x12`). ALPHA+DOWN is `B = 0x08` (kAlphaDown) and the stock OS
does nothing with it, history line selected or not. A-LOCK then DOWN is also
`0x08`. Other ALPHA codes: ENTER `05`, RIGHT/LEFT `01`/`02`, CLEAR `09`,
DEL `0A`. With a history line selected in Classic, `editTop` still equals
`baseTop`, so the gate needs `cmdFlags` bit 4 too.

The OS's `Ans` is not touched when SymCE answers (nothing was evaluated), so an
entry SymCE refuses sees the one before: `3→X`, `2X+2X`, then `Ans→Y` stores 3
(*measured*). That is why SymCE keeps `lastAns`: the `Ans` **token** in an
entry it answers is read as the last answer shown, SymCE's or the OS's. `4X`, then `*3`
(which the OS types as `Ans*3`) is `12X`; `2+2`, then `*X`, is `4X`. A real is
used only when exact in six digits (`0.25` yes, `0.3333333333` no).

---

## 5. The edit buffer and MathPrint

Gap buffer: text = `[editTop, editCursor) ++ [editTail, editBtm)`.

**MathPrint trap (*measured*).** While the cursor is inside a 2D box (exponent,
fraction, radical), `editTop..editCursor` holds **only the box**. Typing `X ^ 2`
leaves just `32` there. Rewriting it corrupts the entry (the old `sqrt(x)` crash).

Detect it with `editTop` captured per entry, never a flag:

| state | virgin | flat `2X+2X` | cube root | x-th root | exponent |
|---|---|---|---|---|---|
| no history | D1A8CC | D1A8CC | D1A911 | D1A8FD | D1A8FC |
| after 1 entry | D1A8CA | D1A8CA | D1A90F | | |
| after 3 entries | D1A8CA | D1A8CA | D1A90F | | |

The baseline moves with history, so never hardcode it. Record `editTop` while
`cmdFlags` bit 5 is set; compare at the key. Equal = cursor on the whole entry.
Only test that gets `MATH 4, CLEAR, 2+2` (flat-looking `2+2` inside a radical)
right. SymCE uses it only for insert mode now; the answer comes from prog `#`.

**Insert mode ends on cursor keys (*measured* 2026-09-28, 5.8.4).** `textFlags`
bit 4 is cleared by the OS's handling of RIGHT, LEFT, UP, DOWN, CLEAR, DEL,
2nd-LEFT/RIGHT (`0x0E`/`0x0F`) and ENTER, Classic and MathPrint alike (DEL:
`res 4,(iy+5)` at `0x58A8A`; CLEAR and ENTER through the new-line routine,
`set 5,(iy+12)` / `res 4,(iy+5)` at `0x58D0D`/`0x58D11`). Typing
leaves it; 2nd INS (`0x0B`) toggles it. The cursor draw (`0x5C80A`) picks
underline or block from that bit on every blink; the MathPrint branch skips
the cursor hook, so the flag is the only lever. SymCE sets it back after those
keys (§4, A=1). After ENTER it stays clear until the next key: ENTER is not
run through `cxMain`, since A=2 and A=0 nest inside it.

**Prog `#` (and `begPC`) solve the tree (*measured*).** In MathPrint an
exponent or n/d box makes the edit buffer a tree: `X^7Y` typed with a box is
`58 EF2A 0E00 EF2D 59`, the 7 in a separate node. By A=2 the OS has flattened
it into prog `#`, and by A=0 `begPC..endPC` reads the same: `58 F0 37 59`. It adds the parentheses a box implies
(`X^(1+2)Y` reads `58 F0 10 31 70 32 11 59`), and an n/d box becomes
`(numerator) EF 2E denominator`, which parses exactly like `/`. It is the
entry as parsed even after it ran a program (`prgmAA` reads back `5F 41 41`).

---

## 6. Tokens vs display glyphs

The OS stores entries as **tokens**; the screen shows **glyphs** (LFont).

| Meaning | Token | Glyph in `textShadow` |
|---|---|---|
| negative `(-)` | `0xB0` tChs | `0x1A` |
| minus | `0x71` tSub | `0x2D` |
| plus | `0x70` tAdd | `0x2B` |
| squared | `0x0D` tSqr | `0x12` |
| inverse `x⁻¹` | `0x0C` tRecip | not measured. Postfix like `²`: `2X⁻¹` is 2/X, `X^2⁻¹` is X^(1/2) |
| decimal point | `0x3A` tDecPt | not in `0xB19FB`'s table, so it passes through unchanged when a 2D answer is filed |
| cubed | `0x0F` tCube | `0xD5` (not `0x13`: measure, never infer) |
| power `^` | `0xF0` tPower | `0x5E` |
| digits, `A..Z` | same byte | same byte |
| `prgm` | `0x5F` + name | |
| `(` `)` `*` `/` | `0x10` `0x11` `0x82` `0x83` | `(` is `0x28` (glyph `0x10` is the root sign, see `0xB19FB` in §7) |
| `A..Z`, `θ` | `0x41..0x5A`, `0x5B` | `xton` key types `X` (`0x58`) |
| n/d bar, flattened | `0xEF 0x2E` | In prog `#` and at `begPC`; the edit buffer holds the box instead |
| `Ans` | `0x72` | Draws `Ans`. An operator key first on an empty line types `Ans` before it (*measured*: `*3` echoes `Ans*3`) |
| MathPrint box markers | `0xEF 0x2A` .. `0xEF 0x2D` | Edit buffer only; never reach prog `#` or `begPC` |
| ASM program header | `0xEF 0x7B` (tExtTok, tAsm84CeCmp) | |
| `=` (TEST 1, `2nd math 1`), ` or ` (TEST LOGIC) | `0x6A`, `0x3C` | `=` is `0x3D` (*measured*, echo of `SOLVE(X+2=4,X)`); ` or ` is four characters (§7) |
| comma `,` | `0x2B` | `0x2C` (*measured*). The byte `0x2B` is the **glyph** `+`: never confuse the two tables |
| list braces `{` `}` | `0x08` `0x09` | `0x7B` `0x7D` (*measured*, SymCE's list answers). `0xB19FB` leaves `08`/`09` as they are, so SymCE passes them as tokens |
| `i` (2nd `.`) | `0x2C` | `0xD7` (*measured*). SymCE passes glyph `D7` and glyph `2C` for a list's comma; `0x91FC2` files them as tokens `2C` and `2B`: Classic `{-i,i}` is filed type 0, `08 B0 2C 2B 2C 09` (*measured*) |
| `√(` | `0xBC` | `0x10 0x28`, two columns (*measured*) |
| a word typed with ALPHA | its letters, `0x41..0x5A` | `FACTOR(` is `46 41 43 54 4F 52 10`: SymCE's commands compare as ASCII |
| `ᴇ` (2nd `,`, EE) | `0x3B` | glyph not measured. The CE has **no ∞ token**: SymCE takes `1ᴇ99` / `-1ᴇ99` (`31 3B 39 39`, `B0 31 3B 39 39`) as ±∞, but only as LIMIT's and DOMINANTTERM's point |

`0x12` as a **token** is `round(`. Passing a glyph where a token belongs once
drew `Xround(`.

**A real (9 bytes, as in `OP1`).** Byte 0: bit 7 sign, low 5 bits type (0 =
real; a list, complex, etc. is nonzero). Byte 1: exponent, `0x80 + e`. Bytes
2..8: 14 BCD digits `d.ddddddddddddd`, value = that × 10^e. `4` is
`00 80 40 00..`, `-0.25` is `80 7F 25 00..`, zero is `00 80 00..`. SymCE's
engine turns it into an exact fraction when its digits span six places at
most (`preal()` in `engine.c`).

---

## 7. OS routines (5.8.4; undocumented unless noted)

| Addr | What | Contract |
|---|---|---|
| `0x091FC2` | draw result | A=type (0 real), HL=OP3 text NUL-terminated, BC=length. Draws right-aligned AND files history (type at `0xD008EE`, length `0xD008F0`, text `0xD008F2`). Inside, `0xB19FB` converts glyphs to tokens in place (table `0xB1AB2`), so **pass tokens**, except `(`: see the next row. At `0x9211C`, `and 0x3F; jr z` sends type 0 to the flat one-row drawer (`0x92158`); any other type goes to the MathPrint measure/draw path (`0x9229A`, `0x8CC13`, `0x7F858`, `0x802C6`), which reads only A, not OP1. The OS's own `1/3►Frac` is type **0x18** with OP3 `31 F6 33`. SymCE in MathPrint: type 0x18 and `/` as glyph `F6` (filed `EF 2E`) draws 2D: stacked fractions, `(X+3)/(X-2)` without its parentheses, `2/X²` as 2 over X², raised `X⁴` (*measured*, VRAM). Pasting such an answer back from history gives an n/d box that re-runs as the same value, and `^` keeps Classic precedence (the exponent box takes only the digit): pasted `X^4/2`, `X^4Y`, `X^5/(X+1)`, `5X/6`, `X^4Y/2` evaluate at X=2, Y=3 to 8, 48, 32/3, 5/3, 24 (*measured*); 34 of them, 20 scroll-ups, paste, then `2X+2X` all fine. In Classic, `F6` draws as itself, so Classic keeps type 0 and `/` |
| `0x0B19FB` | glyphs to tokens, in place on OP3 | Table `0xB1AB2`: 20 pairs glyph -> token (`2E`->`3A`, `1A`->`B0`, `2D`->`71`, `2B`->`70`, `28`->`10`, `29`->`11`, `5E`->`F0`, ...), then 5 glyph -> two-byte token (`F6` -> `EF 2E`, the n/d bar). Special cases: glyph `0x10` (root sign) becomes `BC` (`sqrt(`) and **swallows the next byte**, its drawn `(`; `DB 5E 28` becomes `BF`. Anything else passes through, which is why most tokens survive. The `(` token `0x10` does not: `X/(X+1)` drew `X/√(+1)` (*measured*). Pass `(` as glyph `0x28` |
| `0x097C7F` | OS real-result path | Where the prologue below was copied from |
| `0x058733` | parse start | Sets `donePrgm` |
| `0x0587D6` | NZ-return path | Prints "Done" if `donePrgm` still set |
| `0x02050C` | `_ChkFindSym` | Pure VAT lookup, safe from a hook. OP1 = type + name; DE = data (size word first), carry = not found. Takes OP1 as input only: SymCE saves and restores it anyway |
| `0x02050C` | `_ChkFindSym`, real variables | **Measured** (real ROM, e2e.py): OP1 = `00`, letter token (`A`..`Z` = `41`..`5A`, theta `5B`), `00` finds a real variable, DE = its 9-byte real in RAM with no size word (5 and 2.5 read back exactly); carry = not found. A complex is type `0C` (18 bytes, each half typed `0C`), which `.Llook` retries with. Used by the hook's `look(letter)` (SOLVE's stored values, called from the engine in C convention: letter at sp+3, result HL, ix kept), which saves OP1 on the stack and puts it back. `DelVar X` takes X out of the VAT (SOLVE then leaves X symbolic; the OS does not recreate it as 0). A real A-Z cannot be archived on OS 5.8.4 (`Archive X` and MEM Mgmt both give ERR:VARIABLE), so DE is never a flash pointer here. CATALOG (2nd 0) jumps with a letter key with no ALPHA (it opens in alpha lock; ALPHA would switch it off), DelVar is the fifth entry after dayOfWk( |
| `0x0257C1` | hook pointer check | `HL` = address of a hook pointer: reads it, `IX` = pointer + 1, Z if the first byte is `0x83`. `0x257CB` then calls `(ix)`, so a hook gets `IX` = its own entry |
| `0x023A6A` | parser hook caller | `bit 1,(iy+0x36)`; ParseInp calls it with A=0 at `0x99AEB`, after `begPC` is set. NZ there `jp 0x9B022`, the parser's unwind |
| `0x020320` | `_Mov9ToOP1` | |
| `0x021100` | `os.FindAppStart(name)` | C call, name pushed: HL = the app's start (its `81 0F` header), NZ if found. Walks headers with `0x023FDA`. Used by `app_create` |
| `0x02126C` | `DeleteApp` (jump table) | `jp 0x0247E0`. HL = an app start as `FindAppStart` returns it (it re-walks with `0x023FDA` and compares). Marks it deleted; the space stays used until the pack below. Mem Mgmt (`0x5CE6A`) and `0x218B1E` follow it with `set 3,(iy+0x25)` (*measured* from ROM bytes) |
| `0x035E5D` | pack apps (5.8.4, not in the jump table) | Mem Mgmt's exit runs it when `(iy+0x25)` bit 3 is set (`0x5CDF7`), then clears the bit. `di`, moves the apps below deleted ones up into the holes (fixing hook pointers and relocations), `ei`, `ret`. Callable from an asm program: the SymCE installer does (*measured*). Afterwards `PutS` draws but does not save to textShadow |
| `0x022044` | next app down | HL = an app start (or `0x3B0000` to begin): HL = the next lower app's start, **Z** past the last (via `0x023FDA`) (*measured*: `bigapp.py`) |
| `0x027EB8` | archive's last sector | HL = start of the last of the consecutive `F0` sectors from `0x0C0000`; keeps DE and AF (*measured* from ROM bytes; the installer's room check relies on it) |
| `0x023EC5` | app allocator | What the OS runs before receiving an app. The new app's blank slot (its start - 3) must be `>= 0x27EB8() + 0x10000` and blank up to the lowest app; resets bit 7 of `(iy+0x24)` at `0x23ED2` (*measured* from ROM bytes) |
| `0x027E61` | archive top | Sets the archive's top to the sector below the lowest app's trailer, minus 1; called from 9 places in the archive code |
| `0x035BB9` | garbage collect (5.8.4) | A = what: bit 2 prompt, bit 0 pack apps (`0x35EC5`, `0x24EC8`), bit 1 archive (`0x35C34`, unlocks flash itself). Starts `res 7,(iy+0x24)`, ends `ei`, `call 0xA2A03`, `set 1,(iy+0x0D)`. **A=2: archive only, no prompt**; the SymCE installer calls it with interrupts off and `di`s after (*measured*: `bigapp.py` "GC makes room") |
| `0x0002E0` | flash write | Only between port unlock/lock (§8) |
| `0x092158` | flat one-row answer drawer | Type 0 only. Right-aligns on a width of **12 × C** px, C from the caller's BC: BC must be the drawn column count, not the byte count (*measured*, see "Radicals, equations" below) |
| `0x020790` | `_JError` (jump table) | `jp 0x61E62`: stores A in `errNo` (`0xD008DF`), `ld sp,(errSP)` (`0xD008E0`), `pop af`, `ret` into the current error handler. From the homescreen hook at A=2 it lands on the ERR screen (*measured*) |
| `0x021C80` | `_os_ThrowError` (CEdev) | `jp 0x61EE3`: sets `iy = 0xD00080`, then the same path |
| `0x062340` | error message table | 3-byte pointers, index `(errNo & 0x7F) - 1`. Code 43 (`0x2B`) points at `appErr1` `0xD025A9`, code 44 (`0x2C`) at `appErr2` `0xD025B6` (13 bytes each; CEdev `os_AppErr1/2`, "max 12 characters"). Codes >= `0x3A` show `?` |
| `0x062210` | ERR screen builder | Title `ERROR: ` + message; each further NUL-terminated string after the message's NUL is a description line under the menu, until an empty string. So a custom message needs **two** NULs, or the bytes after it print. Bit 7 of `errNo` (`E_EDIT`) adds `2:Goto` |
| `0x02014C` | `_GetCSC` | A = scan code, 0 if none. Raw: ALPHA is `0x30`, 2nd `0x36`, no key-code translation. SymCE's menu loops on it (no APD in that loop) |
| `0x020834` | `_VPutS` (`0xA28C7`) | HL = text at `penCol` (`0xD008D2`, 3 bytes) / `penRow` (`0xD008D5`). Calls VPutMap `0xA2594` per character, which draws with **`drawFG` `0xD026AC` / `drawBG` `0xD026AA`** (CEdev `os_DrawFGColor`/`os_DrawBGColor`), even in the large font; `textFG`/`textBG` (`0xD02688`/`0xD0268A`) do nothing here. `fontFlags` (`iy+0x32`, `0xD000B2`) bit 2 = large font, 16-row cells. "SymCE" in the large font is 60 px wide (*measured*) |
| `(0xD007CA)` | `cxMain` (home: `0x58680`) | A = key code: handles it exactly as a key press, MathPrint included, and calls the homescreen hook with A=1 first. From inside the hook at A=1 this types keys: FACTOR( via `9A`+letter and `85` gives the same edit buffer as ALPHA-typing it (*measured*). The hook can also run the key it was called with through it and return NZ: the outer call then does nothing more (*measured*, the insert-cursor fix) |
| `0x020178` | `_PPutAway` (`0x8C73A`) | Calls `(cxPPutAway)`; home: `0x58BB0` saves the entry into `cmdShadow` (`0xD0232D`, 260 bytes) and, in MathPrint (`0x80168`, `bit 5,(iy+0x44)`), the cursor offset at `0xD02435` |
| `0x02111C` | `_CxReDisp` (`0x8C7AB`) | Calls `(cxRedisp)`; home: `0x58353` copies `cmdShadow` back into `textShadow` and reopens the entry. Alone it redraws a stale `cmdShadow`: the answer line and the entry vanish (*measured*) |

Home context vectors (*measured*, `cxCurApp 0x40`): `cxMain 0xD007CA -> 0x58680`,
`cxPPutAway 0xD007CD -> 0x58BB0`, `cxPutAway 0xD007D0 -> 0x58C15`,
`cxRedisp 0xD007D3 -> 0x58353`, `cxErrorEP 0xD007D6 -> 0x58C40`,
`cxSizeWind 0xD007D9 -> 0x58C98`.

**A popup over the home screen** (*measured*, SymCE's menu). PPutAway then
CxReDisp, as around the OS's own menus, is pixel-exact in Classic, but in
MathPrint it reopens the entry with the cursor at its **start** (`editCursor
= editTop`, all text in the tail), while the stock MATH menu keeps the cursor
where it was. So SymCE saves the VRAM under its popup in `engineWork` and
puts it back on close: exact in both modes, full screen included. With
submenus it saves **once**, the union of every level's box (x 4..311, y
44..239, 60368 px: the widest menu is Polynomial Tools, 25 columns, the
tallest Algebra, 10 items; Calculus's 14 scroll in 10 rows, and the tab
bar, Algebra, Calculus and Geometry, is 308 px, x 4..311), so moving between levels only
redraws. 60368 px
at 2 bytes is 118 KB, far past `engineWork`, so it is packed: a palette of
the first 3 colours met (`+0`), colour 0 as byte patterns `lo hi lo`/`hi lo
hi` (`+6`, `+9`), one code byte per 4 pixels from `+12` (2 bits each, 3 = a
literal), then the 16-bit literals up to `0xD13E00` (room for ~2560). The home
screen is 2-3 colours, so literals are rare; if they would overflow, the
popup does not open. `tools/hook_sim_test.py` runs the shipped codec
(3 colours, literals, the last literal that fits, overflow). The cursor is already off: Mon
calls CursorOff before handing the key to `cxMain`, so no cursor pixels are
saved. The OS popup look: 2 px black border, grey `e0e0e0` (`0xE71C`),
highlight `181c18` (`0x18E3`) behind `N:`, tabs at y 220..239.

**Pixel shadows** (*measured* 2026-10-05). `_VPutS`/PutMap also record every glyph they draw in two identical
1-bit bitmaps, `0xD031F6` and `0xD07396` (8400 bytes each): 40 bytes a row, row = y - 30, bit 7 = leftmost, set where
the pixel is not white (exact on the home screen). A later full-screen redraw of home (a closed menu, MODE) replays
them, so text SymCE draws over home with VPutS and then wipes by putting the saved pixels back comes back on the
next such redraw (bug: the ALPHA+DOWN popup's text stayed after 2nd MATH). `menu.c` `sync_px` rebuilds the bits of the
popup's rows from VRAM when it closes.
The grapher's text does it too (*measured*): the X= box (large font) and the bar under a CALC result are recorded
in Classic and replayed over home after leaving (TRACE's labels were not). `graph.c` `symce_text` now parks the
shadow rows a text can touch in 0xD13800 and puts them back. Flag `(iy+0x4A)` bit 3 (0xD000CA) picks the shadow
bank the OS records into (0xD052C6 instead of 0xD031F6), but home replays that bank too, so it is no way out.

**Result prologue** before `0x91FC2`: `res 7,(0xD000D3)`; `res 0,(0xD0009F)`,
`res 4,(0xD0009F)`; `set 2,(0xD000C9)`, `res 6,(0xD000C9)`; zero `0xD00338`,
`0xD00339`, `0xD02713`, `0xD00334`, `0xD02506`; `(0xD00335)=0xD0033A`. Then
`res 5,(iy+0)` and return NZ.

**Answers wider than the screen** (*measured* 2026-09-26, answers widened to
48 as an experiment, then reverted). `0x91FC2` reads the string at `HL`, not
at `OP3` by fixed address: a buffer in `saveSScreen` works the same. Classic,
type 0: past 26 glyphs it wraps onto the next row but moves the cursor down
only one row, so the next entry is typed over the wrapped part
(`2X+2X7X+1`). MathPrint, type 0x18: one line, drawn from the left when it
does not fit right-aligned. Raised powers are narrow, so `(X+1)^6` (29
tokens) fits. Past 320 px it is cut off at the edge with no ellipsis:
`(X+1)^7` shows up to `...+21X²+7`. So in Classic SymCE keeps answers at 26
tokens; in MathPrint it counts columns (`width()` in engine.c): `^` none, a
stacked A/B its wider side plus one, a sign one, anything else one, 26 at most.
That is conservative: the sides of a stacked fraction draw in small type, and
`AB/11+AX/13+BX/17+A/2+B/3+X/5+Y/7` (23 columns) drew 201 px, x 104..305.
The text goes to `0x91FC2` from `ansBuf`, since 64 tokens at `OP3` would run
over `OP4`..`OP7`.

**Lists and i** (*measured* 2026-09-26, SymCE's Algebra commands). Classic,
type 0: `{`, `}`, the comma and `i` each take one column; `{-1.414213562,
1.414213562}` without the space is exactly 26 and draws right-aligned on one
row. MathPrint, type 0x18: the list draws with a space after each comma when
it fits (`{-1-i, -1+i, 1-i, 1+i}`) and without when that would pass the edge
(`{-1,.5-.86603i,.5+.86603i}` fills the row), stacked fractions inside it at
their own height and the braces grown to match; so `width()` counts `{`, `}`
and `,` as one column each. Filed: `{(1-√5)/2,(1+√5)/2}` is type 0x18,
`08 10 31 71 BC 35 11 11 EF 2E 32 2B 10 31 70 BC 35 11 11 EF 2E 32 09`.

**The OS's Ans is stale after a SymCE answer** (*measured*): SymCE answers at
A=2 and the OS never evaluates, so the OS's own Ans (not the hook's `lastAns`,
`0xD0EE20`) is whatever the OS last computed. Before the engine kept every entry with its own Ans,
POLYDEGREE(X³+X) = `3`, then `+1`, showed `1`, and `2√(2)`, then `*3`, showed
`3`: the entry had no variable, so it went to the OS. Now such an entry is the
engine's, answered or ERROR: SYMCE LIMIT / ANS (a list or an equation as Ans).

**NROOTS on the calculator** (*measured* in CEmu, engine_device): degree 5
about 0.5 s, `999999X^7+X-1` about 3.5 s (Durand-Kerner in int64 through
libcrt's helpers). engine_device's 2071 vectors need more than 120 s of
emulated time, so it now waits 300 s.

**OS fingerprints.** Both routines are undocumented and move between OS
builds; their first bytes hold absolute `CD xx xx xx` calls into the rest of the
OS, so any rebuild changes them. The installer and the app compare 16 bytes of
each before arming, and on a mismatch say "SymCE is OFF / built for OS 5.8.4"
and write nothing:

- `0x91FC2`: `FD CB 45 8E FE 15 20 25 21 4D 22 09 3E 65 CD F4`
- `0x97C7F`: `FD CB 53 BE FD CB 1F 86 CD 93 88 09 CD 66 7C 09`

**Radicals, equations, `abs(`, factored forms** (*measured* 2026-09-26: a
scratch engine returned fixed token strings through the real hook; VRAM and
`textShadow` read back). Tokens: `√(` `0xBC`, `=` `0x6A`, ` or ` `0x3C`,
`abs(` `0xB2`, the rest as in §6.

- **Classic (type 0).** `0xB19FB` leaves `BC`, `6A`, `3C`, `B2` alone and the
  history record is the tokens (`2√(2)` files `32 BC 32 11`). But three
  tokens draw wider than one column: `√(` is 2 (`textShadow` `10 28`), ` or `
  is 4 (`20 6F 72 20`), `abs(` is 4 (`61 62 73 28`). With BC = token count the
  drawer (`0x92158`) right-aligns too far and cuts the tail (`2√(2` without its
  `)`). With **BC = columns** (+1 per `BC`, +3 per `3C` and `B2`) every shape
  below up to 26 columns lands right-aligned. Past 26 columns
  (`X=(1+√(5))/2 or X=(1-√(5))/2` is 30) it wraps and the next entry is typed
  over it, so the engine's Classic limit has to count these columns, not
  tokens. Pasting `2√(2)` back (`up enter`) gives `2√(2)`; ENTER evaluates it
  to `2.828427125`.
- **MathPrint (type 0x18).** No new byte change: the same four tokens pass
  `0xB19FB`; `(` as `0x28` and `/` as `F6` are still needed. BC is ignored.
  The radical is drawn as a box over its radicand, and the `)` that closes it
  is not drawn; `abs(X)` draws `|X|`; ` or ` draws as text; stacked fractions
  drop their outer parentheses. Right edge x 310 (fraction bars end at 308).
  Widths in px: `2√(2)` 34, `√(2)/2` 22, `(-1+√(5))/2` 44, `X√(3)+1` 57,
  `√(X)` 22, `X√(X)` 34, `√(X²+1)` 54, `√(X+1)/X` 39, `X=2` 34,
  `X=-2 or X=2` 130, `X=-2 or X=2 or X=3` 214, `X=-√(2) or X=√(2)` 154,
  `X=(1+√(5))/2 or X=(1-√(5))/2` 180 (32 px tall), the quadratic formula
  `X=(-B+√(B²-4AC))/(2A) or X=(-B-...)/(2A)` 272, `(X-1)(X+1)` 114,
  `2(X-1)(X+1)` 128, `(X+1)²` 64, `(X+1)^4` 64, `X(X+1)` 68,
  `(X-1)/(X+1)²` 59, `|X|` 26. Four roots,
  `X=-√(2) or X=√(2) or X=-√(3) or X=√(3)`, start at x 2 and stop after the
  third ` or ` (x 299): the last root is dropped whole, no ellipsis.
- **What `/` takes in MathPrint.** The numerator is the whole implied product
  before it, the denominator the next single factor: `2√(2)/3X` draws
  `2√2 over 3`, then `X`. Over `3X` needs `(2√(2))/(3X)` (30 px).
- **MathPrint `width()`.** A radical costs 1 column plus its content, its `)`
  none; ` or ` 4; `abs(` and its `)` 1 each (each bar is about 7 px, so this
  is conservative). `BC` and `B2` open a group closed by `0x11`, like `(`.
- Type 0 in MathPrint draws flat and depends on BC: not usable.
- **Paste from history in MathPrint.** The edit buffer gets boxes: radical
  `EF 27`, n/d `EF 20`, abs `EF 21`, each followed by a 2-byte node reference
  and closed by `EF 2D`. Pasted `2√2` evaluates to `2.828427125`. An equation
  pastes flat and the OS evaluates it as a test: `X=-2 or X=2` and `|X|` give
  `0` with X=0. A pasted factored polynomial goes back through SymCE and comes
  back expanded: `(X-1)(X+1)` gives `X²-1`, `(X-1)/(X+1)²` gives
  `(X-1)/(X²+2X+1)`.
- `2nd ENTRY` still recalls the typed entry (`1X`; Classic `24X`).

**Functions: `sin(` `cos(` `tan(` `ln(` `log(` `logBASE(` `e^(` `e`**
(*measured* 2026-09-26 in CEmu, the Convert Expression and Trigonometry
answers through the real hook, `textShadow` and VRAM). Tokens: `sin(` `C2`,
`cos(` `C4`, `tan(` `C6`, `ln(` `BE`, `log(` `C0`, `e^(` `BF`, `logBASE(`
`EF 34` (MATH A), `e` `BB 31` (2nd ÷); the comma inside `logBASE(` is typed
as token `2B`.

- **`0xB19FB` rewrites `C4`.** Its byte table (5.8.4), glyph -> token:
  `2E`->`3A`, `1B`->`3B`, `20`->`29`, `1A`->`B0`, `2D`->`71`, `2B`->`70`,
  `7B`->`08`, `7D`->`09`, `C1`->`06`, `5D`->`07`, `2C`->`2B`, `28`->`10`,
  `29`->`11`, `22`->`2A`, `14`->`0B`, `5E`->`F0`, `D7`->`2C`, `27`->`AE`,
  `C4`->`AC`. So an answer holding the `cos(` token `C4` is filed and drawn
  as `π` (`AC`). SymCE sends cos as the letters `c o s` (`BB B2 BB BF BB
  C3`) and then `(`, which draws and pastes back as `cos(` in both modes.
  `C2`, `C6`, `BE`, `C0`, `BF`, `EF 34`, `BB 31` pass unchanged; a comma
  is sent as glyph `2C` and filed as token `2B`.
- **Classic (type 0) columns**, right-aligned flush when BC counts them:
  `sin(` `tan(` `log(` 4, `ln(` `e^(` 3, `logBASE(` 8, each letter of
  `cos` 1 (so `cos(` is 4), `e` 1. `textShadow` holds the names as their
  ASCII letters and `(`, `e` as `DB`, and a negative sign in an exponent as
  `1A`: `2sin(X)cos(X)` is 13 columns and fills 13 cells. Counting bytes in
  the hook would miscount the two-byte letters, so the engine counts and
  leaves the number at `ansBuf+64` (§4).
- **MathPrint (type 0x18)** draws them in 2D: `logBASE(X,5)` as `log₅(X)`
  (base lowered), each `A/B` stacked (`log₅(X)` over `log₅(e)`), `e^(…)`
  with the exponent raised and its `)` not drawn, letter-cos as `cos`.
  The engine's MathPrint `width()` counts a name at its Classic columns and
  every parenthesis as one: never fewer than drawn. So
  `log₃(10)-log₅(5) ▶logbase(5)` = `log₅(10)/log₅(3)-1` (Classic 28 columns,
  `ERROR: TOO WIDE`) fits in MathPrint.

- **A fraction inside a function is stacked too** (*measured* 2026-10-10, MathPrint,
  VRAM, SymCE answers): `2sin(X/2)` draws `2sin(` + `X` over `2` + `)`, the
  parentheses grown to the fraction's height, 83 px (ink); `sin(X/2)cos(X/3)` 126 px;
  four such factors 256 px; `4sin(X/2)cos(X/2)²-sin(X/2)` 233 px, `+1` 258 px
  (10.5 to 12 px a column; 11.1 typical). `e^(X/2)` in `2e^(X/2)` is a raised
  stacked `X`/`2` with no ')', 33 px. Classic draws the `/` flat, one column.
  Past the edge MathPrint draws left-aligned from x 4 with a `▸` at x 310 and the
  tail cut off (DESOLVE `Y''+Y'+Y=0` answer, `...+sin(X/2)+sin(X/5)` at 28
  columns). So in `width()` a `/` inside parentheses is a fraction whose width is
  its wider side from the `(`, or the last sign or comma, with no bar column;
  counting it as the term's one bar (top/side) lost every column before a second
  fraction (`sin(X/2)cos(X/3)` counted 8 of its 12, DESOLVE 15 of 31).
  Counting the `/` as one flat column (the Classic way) is wrong too: it adds the
  two sides. A top-level stacked numerator stays at full-size columns (drawn small,
  so conservative: `(8sin(X/2)cos(X/2)²+X-2sin(X/2))/2` is 27 columns, 205 px).

**Custom errors from the hook** (*measured* 2026-09-26, prototype in the
scratch hook, MathPrint and Classic). At A=2, after `pop iy` (so
`iy = 0xD00080`), throwing works:

```
; engine wrote "MESSAGE",0,"LINE",0,0 to ansBuf and returned A=0xFF (hook.c .Lerr)
    ld   hl, ansBuf
    ld   de, 0xD025A9        ; appErr1 (appErr2 at 0xD025B6 follows it)
    ld   bc, 26              ; appErr1 + appErr2
    ldir
    xor  a, a
    ld   (pending), a        ; no answer for A=0 to show
    ld   a, 0x2B             ; E_AppErr1 without E_EDIT: 1:Quit only
    jp   0x020790            ; _JError, never returns
```

- A 13-character description line shows in full: `TOO WIDE`,0,`TRY MATHPRINT`
  draws `ERROR: TOO WIDE`, `1:Quit`, then `TRY MATHPRINT` (*measured*, VRAM,
  Classic). The line runs on into `appErr2`; nothing cuts it at 13 bytes.
- ERR screen: `ERROR: NO SOLUTION` with `1:Quit` only for `0x2B`; `0xAB`
  adds `2:Goto`. Message `SYMCE`,0 then `FACTOR`,0,0 shows `ERROR: SYMCE` with
  `FACTOR` as a description line below the menu. Same in Classic.
- Meanwhile `cxCurApp` = `0x52`, `errNo` = `0x2B`, the hook stays armed
  (`0xD025E1` = hook, flag `0xD000B4` bit 4), and the hook-depth counter
  `0xD0265B` is left at 1.
- `1` or ENTER (Quit) returns to the home screen: history above intact, the
  failed entry filed as the OS files its own errors (see below; before 2026-10-10 it was not filed and MathPrint's
  history lost the line), `cxCurApp` `0x40`, `0xD0265B` back to 0, and
  `2X+2X` answers `4X`. No reset. **A failed entry is filed with "Error".**
  Stock `1/0` leaves the entry and a right-aligned `Error` in history: record type `0x15`, length 5, ASCII
  `Error` (`0xD008EE`). `0x91FC2` does that for its type `0x0E` (`cp 0x0E` loads the ROM string `Error` at
  `0x92253`; type `0x15` loads `Done` at `0x9224D`; HL is ignored for both). `.Lerr` (hook.c) runs the answer's
  flag prologue and calls it with A=`0x0E` before `_JError`. In MathPrint UP once lands on the Error line
  (pastes nothing), UP twice on the entry; 2nd ENTRY works in both modes (tools/emu/hist.py).
- `2nd ENTRY` after Quit recalls the failed entry (`90X`).
- `2:Goto` (with `0xAB`) goes home with the entry in the edit buffer and the cursor at `errOffset` (see the SymCE error screens section; earlier SymCE left it at the start).

---

## 8. Flash apps and the lockdown

- OS 5.5 blocked `Asm(`; 5.8.3 also blocks hooks outside apps.
- **arTIfiCE** (yvantt) is the jailbreak: launches as BASIC, opens a shell that
  runs ASM. Its only exit (`mode`) **zeroes every hook pointer and the RAM at
  `0xD0F000`**. Never test through it.
- **AsmHook2** (RoccoLox + jacobly, source in `vendor/asmhook2`): flash app that
  re-enables running ASM programs. Bootstrapped once via arTIfiCE, then survives
  reboots, but **not a RAM clear**: after `2nd mem 7 1 2` any asm program gives
  `ERROR: INVALID` until AsmHook2 is opened from APPS again (it then says
  "Installation successful!") (*measured*). Its parser hook spots `EF 7B`
  programs and copies them to `userMem`. **No API to host other programs' hooks.**
- How an app installs itself (AsmHook2's `app_create`, now SymCE's
  `installer.asm`): find the blank flash below the lowest app, `portUnlock` ->
  `call 0x2E0` -> `portLock`, write the app, patch the relocation table.
- Flash unlock (`ports.asm`): check `KeypadScanFull+10` holds
  `ED 79 78 FE A0 28 01 CF`, jump into it with a crafted stack, then
  `out0 ($24),$8C` and set bit 2 of port `$06`. Lock: `$28=0`, clear bit 2 of
  `$06`, `$24=$88`, `$22=$D1`.
- Apps are built with **fasmg**, which CEdev v15 no longer ships;
  `tools/get-fasmg.sh` fetches it into `tools/bin`. fasmg notes: `virtual as
  'ext'` writes another output file, but `ext` may not contain `.` (hence
  `SYMCE.v1` renamed by make); no `org` inside `virtual`; `postpone ?`
  symbols can't be used earlier in the source, plain `postpone` ones can;
  `-i 'PAD := n'` defines a symbol from the command line.
- **SymCE app.** Appvars `SYMCE1`, `SYMCE2`, ... carry the app image (ZX0-packed, at most 21,700 B
  each with a 15-byte header, see "Packed install appvars" below); `prgmSYMCE` (a few KB) writes it below
  the last app and arms `symce_hook` inside it. Opening SymCE from APPS shows the settings
  (1 CAS on/off, 2 insert/TI cursor = which body is armed, 3 Evo/TI font). A RAM
  clear zeroes the hook pointer but keeps the app (flash); APPS > SymCE > 1 re-arms it.
- **The installer replaces an installed SymCE** (*measured*): `FindAppStart`,
  `DeleteApp`, the pack `0x035E5D`, then the usual write below the last app and
  arm. Old builds refused ("already installed") and left the hook off, so a user
  who RAM-cleared and reran the installer got plain OS answers (`2Z+2X` -> `0`)
  from an old app forever. Delete first, so it needs room for one copy only
  (a write-first version answered "No blank flash" on the ROM: the rule below,
  the archive's last sector is off limits). Checked on the ROM: a 4560b5a app upgraded with
  and without a RAM clear, three installs in a row, and the app lands at the
  same address each time.
- **Free flash in the ROM image under test** (*measured*, spot reads and a CRC
  equal to all-`FF`): the archive's last sector in use is `0x1E0000`, used to
  `0x1ED46D`; blank from there to the lowest app, SymCE at `0x201E67`: 84 KB.
  Reopening AsmHook2 writes nothing there. Beware: a whole-flash read through
  `screen.read` that hits CEmu's `sched_active` assertion returns plausible
  garbage (an earlier read claimed 8.7 KB); check for the WARNING line.
- **Correction to the 84 KB above** (*measured*): an app may not start in the
  archive's last sector in use, so the room for one is from `0x27EB8() +
  0x10000` up to the lowest app, not from the archive's last byte. On this ROM
  (archive `0x0C..0x1F` all in use, lowest app CEaShell at `0x20EF61`) that
  allows an app (trailer included) of 61,278 bytes, or 126,814 once a
  garbage collect frees a sector; `bigapp.py`'s ROMs erase sectors to fit
  132,000.
- **The GC packs small variables into other sectors' slack** (*measured*):
  with ROMDataC's sector `0x1F` erased (it holds that one appvar alone, from
  `0x1F0001`) and ROMDataD live, the archive is `0x0C..0x1E` and nothing is
  deleted, yet the installer's GC left `0x1E` blank (`FF`) and the archive
  ending at `0x1D`: ROMData1 (78 bytes, alone in `0x1C`) and ROMDataB (24 KB
  in `0x1E`) went into the tails of earlier sectors. So "nothing deleted"
  does not mean "GC frees nothing"; a partly used sector at the end can go.
  The stock layout (all 20 sectors in use) freed no more than one. Each
  ROMData appvar is `FFF9` bytes, its entry at byte 1 of its own sector,
  address bytes pointing at itself (`01 00 <sector>`).
- **Where apps sit** (*measured*): end to end below `0x3B0000`. The app under
  one starting at U occupies `[U-3-size, U-3)` and its size is the 3-byte
  trailer at `[U-3, U)`; the walk stops at a trailer slot reading `FFFFFF`,
  so that slot must stay blank.
- **The OS's allocator** (`0x23EC5`, ROM bytes): the new app's slot (start - 3)
  `>= 0x27EB8() + 0x10000`, and blank from there to the lowest app. The
  archive grows up to one below the lowest app's trailer's sector (`0x27E61`).
  The SymCE installer applies the same test before it deletes anything; if it
  fails it garbage collects the archive (`0x35BB9`, A=2) and asks once more.
  After writing it resets bit 7 of `(iy+0x24)`, ArcChk's cached free space
  (tested at `0x2794A`), as the allocator and GC do.
- **The app size ceiling was the installer, now lifted** (*measured*): one TI
  variable holds about 64 KB, so the image rides in appvars `SYMCE1..9`, then
  `SYMCEA..G` since inst10 (65,000 bytes each, up to 16 parts = 1,040,000 B by format; was 9 = 585 KB) that `prgmSYMCE` reads where
  they lie, archive or RAM, and writes through `pixelShadow` 8 KB at a time
  (flash cannot be written from flash). Every part is checked (there, right
  size, same FNV-1a build id) before anything is deleted, and each is found
  again right before it is copied, since the GC moves them. A 132,000-byte
  app (`make PAD=132000`, three appvars) installed at `0x1EEBC1`, across
  sectors `0x1E..0x20`, with a CRC equal to the image relocated for there,
  and answers `2X+2X` -> `4X`, also after a RAM clear and reinstall.
- **Installer ceiling raised to 16 parts / ~1 MB (inst10, 2026-10-10, *measured*).** Part names are one
  character: `SYMCE1`..`SYMCE9`, then `SYMCEA`..`SYMCEG` (`findPart` adds 7 past '9'; `main.asm` asserts
  `PARTS <= 16`; more would be `H..Z`, a longer `irp` list). Names of builds up to 9 parts are unchanged,
  and the normal-size app image, every part file and the 5e37b66 ones are byte-identical (`cmp`); only
  `SYMCE.8xp` differs (the name step and a longer "No blank flash" text). Old installs keep working (the app
  is in flash; nothing at run time reads an appvar: with the app installed and NO appvars on the calculator
  APPS > SymCE > 1 re-arms and answers 4X), and a new installer over an old SymCE deletes and replaces it
  (`bigapp.py` "replaces older SymCE"). A build that has fewer parts than the last one leaves its higher
  parts (`SYMCEA..`) in the archive, unread: delete them in Mem Mgmt.
- **Real limits** (*derived from the allocator rule above, ends confirmed on the ROM*):
  - *OS accepts any size flash can hold.* The app header's two length fields and the trailer are 3 bytes
    (16 MB); 1,000,000-byte and 700,000-byte padded builds installed, armed, answered `2X+2X` -> `4X`,
    survived `crash.py` (33 entry paths) and passed `e2e.py`, `lifecycle.py`, `bigapp.py`. No OS size limit
    found below the flash.
  - *The flash is the limit:* archive and apps share `0x0C0000..0x3B0000` = 2,949,120 B. The stock CE's TI
    apps (Python 551 KB, EasyData, 4 languages, CabriJr, ...) take `0x22444F..0x3B0000` = 1,619,889 B,
    leaving **1,460,303 B** from the first archive sector to the lowest TI app on a clean CE, minus whatever
    the user archived and any third-party apps (CEaShell, Cesium, AsmHook2 sit below the TI ones).
  - *The install needs both copies at once:* each part fills one archive sector (65,080 B appvar in a
    65,536 B sector), the app slot must lie above the archive's last sector in use + 64 KB, so the need is
    `n` sectors of parts + the app + one guard sector, about 2 x app + 64 KB. On a clean stock CE with
    nothing else archived that fits **at most 10 parts (~650 KB)**; today's 525 KB (9 parts) leaves ~280 KB
    spare, about 1.1 MB used while installing (parts are freed again by deleting them after).
    11-16 parts (715 KB - 1 MB) needs TI apps deleted first: ~2 x app + 64 KB archive free, e.g. Python
    (551 KB) plus the languages for a 1 MB app. The parts cannot be sent into RAM instead (about 150 KB).
  - *Too little free archive* (all tested, none leaves a half-installed app): (1) sending the parts: the OS
    leaves a part it cannot archive as an empty RAM appvar, and the installer answers "Appvar SYMCEn is
    empty: the archive had no room"; a missing part or a part of another build is named
    ("is missing"/"is from another SymCE build"), nothing written; (2) parts all there, app does not fit:
    one GC, then "No blank flash below the last app. Nothing was changed. Delete an app or archived files
    (the SYMCE files count too), then run prgmSYMCE again." with the old SymCE still installed
    (`bigapp.py` "no room keeps old SymCE" at 9, 11 and 16 parts).
- **Packed install appvars (branch zpack, 2026-10-10, *measured*).** The parts are ZX0-compressed, so the
  install needs far less archive. Source and format:
  - *Format* (`tools/zpack.py` docstring, `installer.asm` `findPart`/`unpack`): 15-byte header = build id
    (FNV-1a of the whole image, 4), `"ZX"` marker (2), ulen (2), last-part flag (1), s1 (3, 16-bit byte
    sum), s2 (3, running sum of s1), then a ZX0 v2 stream. Names `SYMCE1..9`, `SYMCEA..Z` = up to 35 parts
    (`findPart` adds 7 past '9' as before).
  - *Compressor:* salvador (Emmanuel Marty, zlib licence, vendored `tools/salvador/`, built by `make` to
    `symce/bin/host/salvador`); 31.5 KB window, about 0.9 s per 65 KB (Einar's reference packer: 21 s), output
    within 0.02 % of it. `zpack.py` decodes every stream again with its own port of dzx0.c before writing.
  - *Decompressor:* CEdev libce `zx0.o` ("turbo": Einar Saukas, introspec), disassembled with
    `z80-none-elf-objdump -M ez80` and re-written as `zx0:` in `installer.asm` (in HL, out DE; it clobbers A,
    BC, HL **and IY**, so the installer saves IY around it).
  - *RAM buffer:* the free gap `(FPS)=0xD0258D` .. `(OPS)=0xD02593` (3-byte pointers), needed = ulen + 1024.
    The CPU stack is below userMem so it is not in the gap. Free RAM measured 86,616 / 87,282 B, so a 65,000 B
    slice fits with about 21 KB spare; with less the installer stops with "Not enough RAM. Free/Need" (hex)
    before anything is deleted (`bigapp.py` "low RAM refused"). The buffer is dead before GC and flash write.
    Known risk: a corrupt stream could write past ulen before the length/checksum test catches it (the margin
    is 1 KB; the OS is not protected against a bad stream, only a flipped bit is realistic and that fails the
    sum test or the length test, which `bigapp.py` flips on purpose).
  - *Checks before any flash write:* pre-flight loop unpacks every part (RAM check, zx0, length = ulen, s1 and
    s2), the last flag ends the chain and the lengths must add up to the image size. Messages: "is missing",
    "is from another SymCE build" (8 hex digits Has/Want), "is not a packed part" (old uncompressed set, checked
    before the id), "A SYMCE appvar is damaged", size mismatch, "Not enough RAM", then the old room messages.
    The app write re-finds and re-unpacks each part (the GC moves them) and writes from the buffer, 8 KB per
    WriteFlash.
  - *Sector economics (the reason parts are not 65,000 B):* a 64 KB archive sector holds whole entries only, a
    65,000 B slice packs to about 38 KB so one fits per sector and compressing would save nothing. Parts are cut
    to the longest slice that packs under CAP = 21,700 B (three per sector, header included): variable slice
    lengths, up to 35 parts. The OS allocator is first-fit and also tucks parts into the slack of sectors that
    hold deleted entries, so the packed set takes about `ceil(parts/3)` sectors (13 parts: 4).
  - *Numbers, today's app (525,494 B image):* raw 9 parts = 526,214 B in 9 sectors; packed 13 parts = 240,031 B
    (45.7 %) in 4 sectors (`bigapp.py sectors()`). Sectors needed while installing = parts + app + 2
    (bigapp's `need`): before 9 + 9 + 2 = 20 (1.31 MB), now 4 + 9 + 2 = 15 (0.98 MB). The saving grows with the
    app: a 700,000 B padded build is 11 raw parts (11 sectors) or 16 packed parts (327,678 B, 46.8 %, 6 sectors).
    Padded fixtures repeat the image's own bytes, so they pack like code. Former open item (e2e failing two cases at PAD=700000) **solved, 2026-10-11, branch bigpad**: nothing in the app grows wrong with its size. `screen.read` pressed plain keys as `key|k` taps (0 ms at delay_after_step 0), and a tap the OS never polls five times is lost; the image size only moves the phase. Proof at PAD=800000 on b7e6dff: e2e "MathPrint nested" failed in all three settle retries (last Ans 0, errNo 0xAB); dumping the edit buffer showed `...11 11 03 30 2b...` where the passing run has `...11 11 6a 30 2b...`: token 03 is `>Frac`, the first item of MATH, so the `2nd` of `2nd math 1 0` (TEST, item 1 `=` = 0x6A) was dropped. `per_key` 310 instead of 300 passed, so did the same keys held 120 ms. Fix: `screen.read` holds every plain key 200 ms (`SYMCE_HOLD_MS`). The hold length is itself a phase: 120 ms made hist.py "steps( no roots Classic recall" hit CEmu's sched_active abort at every settle (60, 90, 150, 200 passed), 150 ms made lifecycle.py "replaced three times" read a blank screen (90, 120, 200 passed); 200 passes both (`hold|k`, `delay|200`, `release|k`), which used to be only the delay_after_step 1 path. Also at PAD=700000 the unmodified harness passed e2e and wide (the flake moves with image size, see "Harness phase flukes").
  - *Install time (emulated, `tools/emu/insttime.py`, bisecting the "installed" text):* 4.5 s raw, 7.0 s
    packed; `lifecycle.py` waits `4 + SYMCE_WAIT_EXTRA (default 3)` dots after "5" for it.
- **Installer ceiling, tested (2026-10-11, bigpad, *measured*).** `make -C symce emubig` = PAD=1630000: 35 parts (`SYMCE1..9, A..Z`, 748,650 B of appvars, 46 %), the app lands at 0x1C61C9 and runs crash (all survive), e2e (131 ok) and lifecycle (32 ok) with `SYMCE_ROOMY_TO=0x33A1CF SYMCE_MENU_SKIP=10 SYMCE_MEM_SKIP=15` (the APPS menu then lists Finance, AsmHook2, CabriJr, CelSheet, Conics, EasyData, SymCE; Mem Mgmt 6 downs to SymCE). PAD=1680000 is refused by zpack ("image too big", 35 parts hold about 1.65 MB). Also green: PAD=700000 (15 parts), 800000 (18), 1000000 (22). No flash sector, 64 KB bank or address-bit limit showed up (apps at 0x180C54 .. 0x1F4B98 all fine). Two waits scale with the app and are harness-only: after deleting the app below SymCE the OS redoes the 1.6 MB app's relocations for tens of seconds and keys typed meanwhile are lost (`SYMCE_RELOC_MS`, 8000 default, 40000 at 1.63 MB), and the install needs more than the default dots (`SYMCE_WAIT_EXTRA`, 14 used).
- **Testing 11-16 parts** (*test-only fixtures*): `make -C symce PAD=700000` (11 parts) and `PAD=1000000`
  (16); `bigapp.py` picks the first stock app boundary that leaves room (`need = 0x0C0000 + (2n+2)*64K`),
  erases everything below it from sector 0x0C (TI's apps too), puts AsmHook2 back relocated as the lowest
  app (APPS > AsmHook2 runs the installer), and runs all its cases there (`app_boundaries()`, `run()`);
  the 9-part case stays on the original fixture (lowest app CEaShell). For `e2e.py`/`crash.py`/`lifecycle.py`
  at those sizes copy `bin/pad<n>/SYMCE*` into `tools/emu/` and set `SYMCE_ROOMY_TO=0x244563` (700K) or
  `0x2E91E7` (1M) for `screen.py roomy()`, plus the APPS-menu offsets `SYMCE_MENU_SKIP` (4 / 6) and the
  Mem Mgmt list `SYMCE_MEM_SKIP` (5 / 8): the menu loses the erased apps. A 1 MB write finishes in <12 s of
  emulation. Fixture lessons: CEmu's sched_active abort cuts a `lifecycle.state` read short and the late
  windows read `FF` (cx first now); after a pack the answer may need `y= . 2nd mode .` before textShadow shows it.
- **The relocation table cannot shrink** (*measured* earlier): the OS's pack
  (`0x035E5D`) re-reads it to redo an app's relocations when it moves the
  app, so it must stay in the app in the format `app.inc` writes (6 bytes an
  entry, ~10 KB of the 54 KB app).
- **Deleting an app clears any hook pointing into it** (*measured*): SymCE
  deleted while on leaves `homescreenHookPtr = 0`, flag off, no reset. The
  installer then reinstalls at the same address.
- **Deleting an app ABOVE another moves the one below, hook and all**
  (*measured*): delete AsmHook2 (1039 bytes, just above SymCE) and on leaving
  Mem Mgmt (`2nd mode`) the OS packs the apps below up into the hole, no
  `GarbageCollect` needed. SymCE went `0x20BFCE` -> `0x20C3DD` (+0x40F); the OS
  moved `homescreenHookPtr` by the same amount, kept the flag, and redid the
  app's relocations (the engine address after the hook body moved too). Y=,
  WINDOW, MODE survive; it still answers. So never cache an app address in RAM
  (SymCE keeps only data there): nothing says the OS would fix that up.
- Cap'n Hook keeps hooks in RAM, so 5.8.3+ rejects it too.
- **An app can run an asm program itself** (*measured* 2026-09-29, settings row
  `5: Run prgmSYMCE`, `runPrgm` in main.asm): 5.8.4's block is only in the
  parser, so an app that copies the program to `userMem` and jumps to it runs it
  with AsmHook2 out of the picture, even after a RAM clear (`prgmSYMCE` is
  archived, so it survives one). Recipe, as AsmHook2's parser does it:
  `_ChkFindSym` (OP1 = `ProtProgObj "SYMCE"`), skip the archive entry header if
  archived, size word, check `EF 7B`; `_EnoughMem`; `_InsertMem`; `ldir`; `jp
  userMem`. ABIs *measured*: `_EnoughMem` HL = bytes, NC = fits, returns (no
  throw, unlike `_ErrNotEnoughMem`); `_InsertMem` **HL = bytes, DE = address**
  (HL = address, DE = bytes resets the calculator) and returns DE = the address;
  `_DelMem` HL = address, DE = bytes. `asm_prgm_size` = `0xD0118C`, 3 bytes;
  set it to the inserted length and clear it again after `_DelMem`.
- **Never return into an app that a program deletes.** The installer deletes and
  rewrites the app that ran it (at another address if the size changed), so
  `runPrgm` copies a 25-byte stub after the program, inside the same inserted
  block (the installer's `DelVarArc` only moves RAM above it), and pushes the
  stub as the program's return address. The stub sets `asm_prgm_size` 0, pushes
  `_JForceCmdNoChar` and tail-jumps `_DelMem(userMem, size)`, freeing program and
  stub in one call and "returning" to the home screen. It uses only absolute OS
  addresses, so it needs no relocation. Free RAM `(OPS)-(FPS)` and
  `asm_prgm_size` came back exactly (86,616 before and after, 0), and the
  installer screen matched the one shown when AsmHook2 launches it, pixel for
  pixel.

---

## 9. Programs, appvars, VAT

- `.8xp` = program, `.8xv` = appvar (data). fileioc: `ti_Open`, `ti_Write`,
  `ti_SetArchiveStatus`, `ti_GetDataPtr`.
- Archived vars live in flash and move on garbage collect (*measured*: the
  installer's GC moved SYMCE1/2 from sectors `0x1D`/`0x1E` to `0x1B`/`0x1C`).
  So never keep a pointer into the archive across a GC; find the var again.
- An archive entry (*measured*): flag (`FC` live, `F0` deleted, `FE`
  partial), size (2), type, ?, version, address (3), name length at `+9`,
  name, then the data (size word first, as in RAM). From `ChkFindSym`'s DE
  when `ChkInRam` says NZ: `+9`, `+ namelen + 1` is the size word. Entries
  never span a 64 KB sector. A transfer puts archived vars in the next free
  sectors without a GC when there is room, and GCs first when not.
- **An archived var the archive cannot hold arrives EMPTY, in RAM**
  (*measured* 2026-09-27: CEmu with bigapp's full "tight" ROM, and a real
  5.8.4 calculator). The VAT entry exists, its data pointer in RAM
  (`0xD1B0xx` on both) and its size word `0000`: no data. So after sending,
  check Mem Mgmt for the `*` and the size. The SymCE installer names it
  ("SYMCE2 is empty: the archive had no room"); before 2026-09-27 it called
  it "from another SymCE build".
- `_InsertMem` / `_DelMem` / VAT create-delete shift user RAM and do **not** fix
  the edit pointers. Never allocate while the edit buffer is open.
- **Creating a variable under the live home screen resets the next put-away** (*measured*
  2026-10-04). SymCE's grapher called StoX/StoY (CreateReal) from the home context: with an
  entry of 2..6 tokens pending, no history and X/Y absent, the next Y=/WINDOW/MODE/CALC
  gave "RAM Cleared". 1 token, 7+ tokens, a history line, or X/Y already there: fine, and
  any variable (A as well as X) triggers it. Cause not traced into the OS; the cure is TI's
  own order: `NewContext0(kMode)` (home put away), create, `NewContext0(kQuit)`. SymCE does
  it in `graphMain` only when a variable it may store is missing (`graphNeed`). During that
  first graph cxCurApp reads 45h, not 40h.
- SymCE v15 appvars `SYMCEHK` (hook master copy) and `SYMCEPV` (previous hook
  pointer + armed flag): the flash-app installer deletes them; nothing reads them.

---

## 10. eZ80 notes

- 24-bit registers/addresses (`HL`, `DE`, `BC` = 3 bytes). `.sis` suffix = 16-bit.
- `jr` reaches +/-127 bytes; `jp nn` is absolute, so relocatable code uses `jr`
  and computed jumps.
- **ez80-clang C ABI** (the hook calls the engine through it): arguments pushed
  right to left, 3 bytes each (even a `uint8_t`), caller pops. 8-bit result in
  `A`, 24-bit in `HL`. `ix` is callee-saved; `iy` and everything else may be
  clobbered, so the hook restores `iy = 0xD00080` before touching OS flags.
- `int` is **24 bits**. Anything that can pass 8388607 needs `int32_t`/`long`,
  which go through libcrt helpers (`__lmulu`, `__ldivs`).
- **Relocating compiled C.** ez80-clang emits absolute calls to its runtime and
  its own functions. The makefile links `engine.c` at three bases
  (`0x100000 0x223456 0x0ABCDE`); `tools/relocs.py` diffs the first two to find
  every absolute self-address and writes it as `dl symce_engine + off`, which
  the app packaging turns into an app relocation. Relocating image 1 to base 3
  must reproduce link 3 byte for byte, or the build fails.
- The hook body stays hand-written: position independent (`jr` only, no
  internal tables), no relocations. It finds the engine through the relocated
  `dl` placed right after its last byte.
- Code running from flash has **no writable statics**: engine state lives on the
  stack and in the `work` block the hook hands in.
- **Flash is slow for per-pixel loops** (*measured*, SymCE's popup in CEmu):
  every instruction byte fetched costs 10 cycles (port `0x1005` = 4, + 6).
  Compiled C over the 60368-px popup took 300-400 ms per step, long enough for
  `GetCSC`'s single latched key to be overwritten by the next press (a lost
  key). Hand asm whose common case (4 pixels of colour 0) is three 24-bit
  compares or stores brought opening under ~120 ms and a submenu step under
  60 ms; UP/DOWN redraw only the `N:` labels.
- **There is no overclock** (*measured* 2026-09-27, CEmu, FACTOR(X^4-5X^2+6)
  from the SymCE app, 32 kHz timer 3). The eZ80 already runs at its 48 MHz
  ceiling (port `0x0001` bits 0-1), with no PLL. Every "overclock" program
  (DrDnar's FASTER = 2 / FASTEST = 1, Advanced Wait State Changer, TI-Boy CE)
  only lowers the flash wait states, `0xE00005` (§2):

  | Hardware | Setting | Engine time |
  |---|---|---|
  | rev M+ (serial flash, this ROM) | any: port ignored | 216 ms |
  | before rev M (parallel) | OS `04` | 729 ms |
  | before rev M | `03` (what SymCE sets) | 661 ms |
  | before rev M | `02` | 594 ms: faster, unproven on silicon |
  | before rev M | engine in RAM (a CEdev program) | 318 ms |

  - **Rev M+ flash is faster than RAM.** It has an 8 KB cache (2-way, 32-byte
    lines). A hit costs 2-3 cycles and a miss about 197; RAM costs 4 per byte.
    The same engine ran 216 ms from flash and 318 ms from RAM, so copying hot
    code to RAM would be a loss there.
  - **Pre-rev-M hardware is ~3.4× slower** at the OS setting. On it, only RAM
    (2.3×) or wait states (−9% at `03`) help.
  - **CEmu never misreads flash**, so a speed test there can't show `02` is
    safe. A misread code byte crashes; a misread data byte could be a wrong
    answer.
- **App code is unprivileged too.** `in` from a flash app or from userMem
  reads `00` in CEmu, and `out` there triggers an NMI reset. Port 3 bit 4
  (serial flash) is therefore unreadable from SymCE. Reading the
  memory-mapped register instead works: it reads `00` exactly when the port
  is ignored. Memory-mapped timers (`0xF20000`) do read from app code. An
  `in a,(c)` loop over ports 0-0x3F also made the engine run right after it
  35× slower in CEmu (cause unknown). Never do port I/O in SymCE.
- **CEdev's runtime is slow wherever it shifts** (*measured* 2026-09-27, CEmu,
  code in RAM, timer 1 at the CPU clock). Every shift helper (`__ishl`,
  `__lshrs`, `__lshru`, `__lshl`, `__llshrs`, `__llshru`, `__llshl`) loops
  one bit per pass, so shifting by 31 costs 31 passes. `__llmulu` (64-bit
  multiply) is shift-and-add, about 8,800 cycles; `__lmulu` uses `mlt`, about
  680. A 32-bit divide costs about 3,300 cycles and a 24-bit one about 1,600.
  64-bit `/` and `%` are bit-serial too.
- **ez80-clang turns innocent C into those loops.** Each case has a fix:
  - `x < 0 ? -x : x` becomes `(x + (x >> 31)) ^ (x >> 31)`, which is two
    31-bit shift loops.
  - `zext(x < 0)` becomes `x >> 31`.
  - SROA promotes a `union { int64_t; uint32_t w[2]; }` to an i64 and reads
    `w[1]` as `>> 32`.
  - **Fixes:** put an empty `__asm__ volatile ("")` inside the branch, which
    keeps it a real branch (`__lcmpzero`), and declare the union `volatile`
    so its halves are real loads. Masks beat shifts: `m <<= 4` over
    `15u << 4*k`.
- **`mlt` multiplies bytes.** A 32×32→64 product is 16 `mlt de` summed
  column by column in HL, with carries passed through the stack: 1,683 cycles
  versus 7,900 for `__llmulu`. SymCE's float core is asm built on it
  (`symce/src/engine.c`, *measured* cycles from RAM):

  | Routine | C | asm |
  |---|---|---|
  | multiply | 19k | 9.5k |
  | normalise | 3.9k | 1.4k |
  | 62-bit divide | 146k | 63k |
  | complex multiply | 87k | 45k |
  | printing a root | 1.5M | 0.8M |

  The host build keeps plain C versions of the same integer arithmetic.
- **Inline asm in the engine.** Inline asm in the relocated engine (not the
  hook) may `jp`/`call` its own labels, since `relocs.py` relocates them like
  clang's. `jr`/`djnz` reach only ±127 bytes, and the assembler rejects
  anything longer ("8-bit signed offset out of range"). Numeric local labels
  (`1:` `jr 1b`) may repeat. The syntax is clang's gas style: `ld (ix + 4), hl`,
  `lea hl, ix + 16`, `ld (iy), 0`.
- **Plain rationals dominate SUM/PRODUCT.** Every term there is a plain
  rational, and `reduce()`'s `monic()` multiplies each one by 1 twice.
  Short-cuts help, but only if they reproduce the general path's failures.
  Take the fast way only when it succeeds; otherwise fall through to the full
  code, so that overflow and pool exhaustion fail exactly as before.
- **Inlining into a big function bloats** (*measured* 2026-09-27, ez80-clang
  -Oz). -Oz inlines a static function into its only caller. Inside a huge
  caller (`symce_engine`, `command`) the frame outgrows `(ix+d)`'s ±127
  reach and every access gets longer: `rsplit`+`rok` are 1.5 KB on their own
  but cost 3.9 KB inlined. `NOINLINE` on `command`, `ipfactor`, `putfl`,
  `rsplit` and `rok` took the engine from 110,386 to 101,996 bytes. It also
  lowered the peak stack from 1,689 to 1,451 bytes, since the merged frames
  were bigger than the chain of separate ones. Where it pays: a big callee
  whose caller is big too. A small callee inlined into a small caller
  (`unroot` into `mulinto`, `lsub` into `calc2`) is smaller inlined.
  Measure: `z80-none-elf-nm -S` on the linked elf, before and after.
- **Powers are 4-bit nibbles, and the carry says when one overflows**
  (2026-09-28). A monomial is six 4-bit exponents in one `unsigned` (24 bits
  on the eZ80, 32 on the host). They used to stop at 7, with `0x888888`
  marking an overflow into bit 3. Now `madd()` adds two monomials and spots a
  carry out of any nibble: `(a ^ b ^ sum) & 0x111110` (a nibble's low bit
  that is not the xor of the inputs' low bits got a carry from below), and
  `(sum & 0xFFFFFF) < a` for the top one, which leaves 24 bits on the eZ80 and
  shows as a sum below an input, and stays past bit 23 on the host, where the
  mask drops it the same way. So powers go to 15 on both builds, and anything
  sized by the exponent (`ip_t.a[16]`, `group()`'s counts, `nroots()`'s 16
  roots) grew to 16.
- **Past the IX reach, every local costs bytes** (*measured* 2026-09-28).
  `ip_t` at 16 coefficients made `ipdiv()`'s frame (two of them plus a
  remainder) and `ipfactor()`'s cross the ±127 bytes of `(ix+d)`, and the
  engine grew 1.7 KB. Fixed without shrinking anything: `ipdiv()` keeps only
  the 3 live remainder words (the divisor is degree 1 or 2) instead of a
  whole `ip_t`, and `irred()` hands `ipfactor()` its polynomial plus two
  scratch `ip_t`s from the rational pool (`tmp()`), off the stack. `ipfactor`
  2,456 → 1,816 bytes; `ipdiv` 893.
- **The engine's stack** (*measured* 2026-09-27, engine_device on 5.8.4):
  at most 1,451 bytes below the caller's frame over 2,100 vectors, the
  deepest being `INTEGRAL(1/(X²+1),X)`. The CE stack is 4 KB. The largest
  frames: `symce_engine` 448, `irred` 218, `cfmin` 194. 2026-09-28, with
  `ipfactor`'s scratch in the pool: at most 1,340 bytes, same vectors.
  2026-10-09, real 5.8.4 ROM, a0e3e1b (`tools/emu/stack.py`): stackBot
  `0xD1987E`, stackTop `0xD1A87E` (4096 B). The engine's entry SP is about
  `0xD1A82C` in Classic and MathPrint (the OS plus hook use ~82 B). Worst
  measured: `steps(DESOLVE(Y''-3Y'+2Y=X^2,X,Y))` 3,328 B used, 686 B left;
  DESOLVE with a forcing term is the deepest chain (desolve > dssecond >
  dspart > piter). `steps(EXPAND((X+1)^7))` only 1,048 B. **Trap:** painting
  `0xD1987E..0xD198FF` resets the calculator (cause unknown); paint from
  `0xD19900`. To measure: `make -C symce XFLAGS=-DSTKPAINT`, then
  `tools/emu/stack.py` via emu-run; the lowest byte reached is kept at
  `0xD09566` (plotSScreen; 0xD0EE40 in saveSScreen is overwritten by the MathPrint answer drawing for wide answers, which lost the record).

---

## 11. Testing without the calculator (`tools/emu/`)

- `cemu-autotester` + `AUTOTESTER_ROM=~/CEdev/ti84pce.rom` (never commit the ROM).
- **Emulating a pre-rev-M CE** (parallel flash, wait states honoured). CEmu
  picks the flash type from the boot code version: jump-table entry `0x80`
  jumps to `ld a,5 / ld b,6 / ret` at ROM `0x1768` (major.minor 5.6). Setting
  byte `0x176B` to 0 in a scratch copy (5.0.1 < 5.3.6) boots it as parallel.
  `screen.read(rom=...)` takes it. It is ~3.4× slower, so waits must grow: the
  installer needs `L . . . . . .`, and `C` must come after the install, not
  before, or MathPrint stays on.
- **Stopwatch:** timer 3 on the 32 kHz clock, independent of CPU speed and wait
  states. Clear bits 6-8 and 11 of `0xF20030` (16-bit), zero `0xF20020`, set
  bits 6, 7 and 11, then read `0xF20020`: 1 tick = 30.5 µs, repeatable to ±2
  ms per run.
- It only reports **CRC-32C** hashes of memory. CRC is affine, so
  `crcread.invert` recovers any 4-byte window; chain windows to read any length
  in one run.
- Hash phase needs `delay_after_step: 0` (snapshot). VRAM needs `1`: `0` trips
  CEmu's `sched_active` assertion after ~230 windows (emulator bug).
- `vram.py` reads the LCD (`0xD40000`, RGB565) through `screen.read(step=1)`:
  `grab()` returns pixels plus any extra ranges read in the same run, `png()`
  writes a grey PNG with the stdlib, `ascii()` prints it. The full screen at
  full resolution adds about 3 s to a run. It is the only way to check a
  MathPrint answer or the ERR screen, which never reach `textShadow`.
- Unknown key names are **skipped silently** (stderr warning only); always grep
  stderr for `unknown key`. x² is `^2` and x⁻¹ is `-1` (*measured* 2026-09-26;
  `2nd ^2` is `√(`). In MathPrint `^` opens an exponent box that keeps what
  follows until `right`: type `xton ^2` or `xton ^ 3 right`.
  `alpha y=` is the fraction template. Use `+`, not `add`. `y=` is valid and reproduces the crash.
  ALPHA then a key types a letter, all 26 *measured*: `math apps prgm -1 sin
  cos tan ^ ^2 , ( ) / log 7 8 9 * ln 4 5 6 - sto 1 2` are A..Z (`e2e.spell`).
  Used and valid: `xton`, `alpha` (`alpha 1` = Y), `2nd`, `up`, `down`, `enter`,
  `clear`, `apps`, `math`, `^`, `(`, `)`, `*`, `/`, `+`, `-`, `zoom`, `trace`,
  `vars`, `sto`, `ln`, `on`, `del`, `,`, `(-)`, `.` (decimal point, but see
  below). Unknown: `mem`, `catalog`, `matrix`, `quit`, `ins`, `x^-1`, `^-1`, `inv`, `recip`,
  `x^2`, `square`, `chs`, `neg`, `ee`, `comma`, `dot`; reach them with `2nd`
  (`2nd +` MEM, `2nd 0` CATALOG, `2nd mode` QUIT). A `.` in a `screen.py` key
  string is a 2 s pause; type the decimal point as `dp` (screen.py sends `.`).
- CATALOG (`2nd 0`) is in alpha: a letter key jumps to that letter, e.g. `tan`
  (G) then `enter` pastes `GarbageCollect`, `^` (H) `down` `enter` pastes
  `Horiz`. Split screen sets `sGrFlags` (`0xD00094`) bit 0; the home screen is
  then textShadow rows 6-9.
- MODE: `mode`, then `down` x10 reaches `ANSWERS:`; `right` `enter` picks DEC,
  and `clear` goes back home. Visiting MODE also writes `0xD000D1` (08) and
  `0xD0017F` (3C), which are not the setting.
- `sched_active(id)` also fires **mid-read** on some key sequences, at a fixed
  emulated moment (not only with `delay_after_step: 0`). Emulator-side: the same
  keys with another `settle` read fine, calculator included. Unread bytes come
  back `FF`; `e2e.run` retries with settle 1500/1900/2300.
- In the autotester the first ENTER after a menu paste (`prgm enter`) is
  swallowed, stock OS too: `prgm enter . enter . enter` runs the program.
- **Long runs drop keys.** In MathPrint, after ~27 entries the OS takes longer
  than one `.` (2 s) after ENTER and the next keys typed 200 ms apart are lost:
  entry 28 of `(5+d)/(5-2)` gives `ERR:SYNTAX` on the stock OS too. Put
  `. . .` after each ENTER in long sequences. Multi-digit key names (`10`) are
  unknown keys: type digits one by one.
- **One key tap can drop at a fixed moment** (*measured* 2026-09-26). With the
  SymCE-tab build, e2e's batch `(X^2-Y^2)/..`, `(X^2-Y^2+X-Y)/..`, `.5X+.25X`
  lost the last ENTER. Typing 48 digits there instead lost exactly one, the 9th;
  one extra key before it moved the loss to the 8th, two to the 7th: a fixed
  emulated time, whatever is typed. The same keys with the previous build (and
  with this build's menu hook left unarmed: still lost) lose nothing, so the
  moment moves with the app's size or install time, not with the hook. `e2e.main`
  reruns a failed case alone, at another time, before calling it a failure.
  **Cause** (*measured* 2026-09-26, stack read at the lost tap): the OS's
  battery-icon refresh. The idle loop at `0x03609B` does `bit 5,(iy+3Fh)` /
  `call nz,0x056C74`, which clears the bit and calls `DrawBatteryIndicator`
  (`0x027182`, jump table `0x021A50`) -> boot `0x0003B0` (-> `0x003B05`), which
  measures the battery through ports `0x00`/`0x0A` with interrupts off. The bit
  is set by the timer routine's countdown at `0xD00590` (8-bit) / `0xD00591`
  (16-bit), code at `0x03D174`, and by an interrupt path at `0x01028F`
  (`0xD177D6` bit 1). `apdFlags2` (`iy+1Bh`, `0xD0009B`) bits 5/6 change with
  it; `bit 6` set makes `0x027182` return at once. The emulator's tap is shorter
  than that window, so it is lost; where the refresh lands depends on everything
  run before it (installer flash writes run with interrupts off), so a build that
  installs differently moves it. crash.py hit it in Horiz: the 5th key after
  `clear .` vanished (`2X+2` -> `2X+2`). Stock, on its own timeline, loses
  nothing only because its refresh lands elsewhere. `crash.py` retypes 100 ms
  later when the echo shows a lost key (`w100`: `screen.py` key `wN` waits N
  ms); a wrong answer to a correct echo still fails. The key lost can be one
  before the entry (*measured*, 82 KB build on `_base.rom`): lifecycle's
  CLEAR on SymCE's "is ON" screen went, the `2` of `2X+2X` closed the screen
  instead, and the echo read `X+2X` -> `3X` with any pause before `2X+2X`.
  `lifecycle.state` retries with the whole case 100 ms later (`w100` first).
- **The cursor blink moves with the app's size too** (*measured* 2026-09-26).
  A `textShadow` row that is only `E0` is the cursor, caught blinking on. The
  82 KB build shows it after `2X+2X` ENTER and after the insert test; HEAD
  (69 KB) doesn't, with its appvars archived or in RAM, and HEAD padded to
  82 KB does: the install's flash writes (interrupts off) shift the blink.
  `e2e.run` and `lifecycle.state` drop a cursor-only row.
- `screen.read(..., crcs=[(name, addr, length)])` hashes whole ranges (VRAM
  bands) as one CRC each, for pixel-for-pixel comparisons; it runs them first
  and switches to `delay_after_step: 1`, as VRAM needs (above).
- **ENGTEST measures the engine's stack** (2026-09-27). Before each call it
  paints the 3,900 bytes below its frame with `A5`; after the call it scans
  up from the bottom for the first byte changed. engine_device prints the
  deepest call and its entry. The paint must use a `volatile uint8_t *`
  made from an integer (`(uint24_t)&mark - N`). As `&mark - N` it is out of
  bounds of any object, clang drops or forwards the accesses, and every call
  reads "all 3,900 bytes" (seen for `999999X+1`).
- MathPrint can only be checked in the edit buffer or VRAM (`textShadow` is
  not kept). `e2e.mathprint(keys)` reads the edit pointers (`0xD02437`
  top, `0xD0243A` cursor, `0xD0243D` tail, `0xD02440` bottom), then the text
  before the cursor from a window at `0xD1A800` and after it from `0xD2A800`,
  where 5.8.4 keeps them in these tests (*measured*). The cursor blink
  (`cmdFlags` bit 3) differs between two otherwise identical runs, so compare
  the buffer, not VRAM, when the cursor is showing.
- A CEdev program over 64 KB builds as a small launcher `NAME.8xp` plus
  appvars `NAME.8xp.0.8xv`, `.1.8xv`. Send them all: the launcher alone runs
  nothing, and a report in RAM stays zero (`engine_device.py` globs
  `ENGTEST.8xp*`; the 40 KB engine and its vectors crossed the line).
  The launcher then copies the whole program into free RAM, so it must fit
  there too: 147 KB ran, 157 KB (the 107 KB engine with Geometry plus 50 KB
  of vectors) never started, the report all zero and no error on screen
  (*measured* 2026-09-27). `engine_device.py` now sends the vectors in parts
  of under 40 KB, one program each.
- `e2e.basic_program(name, tokens)` writes a TI-BASIC `.8xp` (`DE` = `Disp`,
  `3F` = newline); pass it in `run(keys, files)`.
- `screen.py` key strings: `C` = `action|useClassic`, `L` = `action|launch`
  at that point. The SymCE tests pass `lead=False` and put `L` after
  `apps down enter` has armed AsmHook2: launched before that, an ASM program
  runs nothing.
- Pass an absolute JSON path and set `cwd` to the script dir.
- Commands: `delay|N`, `key|NAME`, `hash|N`, `hashWait`, `action|launch`,
  `action|reset`, `action|useClassic`.
- Test ROM has AsmHook2: `apps down enter` opens it, then `prgmSYMCE` runs.
- Check the echo with `2nd enter` recall or VRAM, never the freshly painted row.
- Offline, `make -C symce check`: `engine_check.py` runs `engine.c` (host build,
  `engine_cli`) against sympy, an independent TI parser and `minipoly.c` byte
  for byte; `hook_sim_test.py` runs the **assembled** hook bytes under
  `tools/ez80sim.py`, with the engine call served by `engine_cli`. Neither can
  see the validator sweep. ez80sim decodes only the opcodes the hook uses
  (`ld b,(hl)`, `46`, came with the column byte); an unknown one stops it with
  "unimplemented opcode". The function commands (TOLN .. TCOLLECT) are
  checked by value: the answer parsed back to sympy (`ln(` log, `logBASE(u,b)`
  log(u,b), letter-cos cos) must equal the entry at random points, be in the
  command's form (TEXPAND: every angle one variable, `X/2` or a number;
  TCOLLECT: no product of two sin/cos; TOSIN: no `cos(u)` squared; TOEXP: no
  trig; TOLN: no `log(`/`logBASE(`), fit, and carry the Classic column byte.
- **Room for the app on this test ROM** (*measured* 2026-09-26). Its archive
  is nearly full of the ROM dumper's ROMData appvars, and `SYMCE1`/`SYMCE2`
  go in archived, as the build makes them, and stay there while the installer
  writes the app: the largest app that then installs is about 69.4 KB.
  69,098 bytes installs; 69,700 and every padded build tried up to 82,000
  (and the 82,484-byte build with TOLN .. TCOLLECT) end at "No blank flash
  below the last app. Nothing was changed.", hookptr 0, so every crash.py row
  fails `armed=False`. Sent to RAM instead (flag byte 0,
  `bigapp.tifile(flag=0)`), the 82 KB app installs, arms and answers; but a
  RAM clear then takes the appvars with it, so lifecycle's "installer
  replaces app" and "replaced three times" can't pass that way. A room figure
  that leaves out SymCE's own archived appvars does not apply here.
  So the suites now run on `tools/emu/_base.rom`, which `screen.py` makes from
  the stock ROM (`screen.STOCK`) by blanking every sector that holds a lone
  ROMData appvar (`0x14..0x1F`, the dumper's, not a user's): archive
  `0x0C..0x13`, ~830 KB free below the lowest app. `bigapp.py` still builds
  its flash states (full archive, GC, no room) from the stock ROM.
- On the ROM, `make -C symce emu`: `crash.py` (Y=, WINDOW, MODE, GRAPH, ERR, STAT,
  APPS, the ALPHA+DOWN menu all survive), `e2e.py` (answers, echo, recall,
  re-run, the menu in both modes), `lifecycle.py`
  (install, settings keys, RAM clear, re-arm), `bigapp.py` (the app padded to at least 132 KB: fresh,
  over an old SymCE, RAM clear and reinstall, a GC to make room, a missing or
  foreign appvar, no room keeps the old app), `engine_device.py`: links the exact
  `obj/engine/engine.s` into a CEdev test program (`tools/emu/engtest`), runs
  1500 entries on the real OS and requires the host build's bytes for each.
  That is where 24-bit `int` or a libcrt helper would differ.
- **Timing and profiling the engine** (2026-09-27; scratch tools, gitignored).
  - **Timer 1 as a cycle counter:** clear bits `0x207` of `0xF20030`
    (16-bit), zero `0xF20000`, set `0x201`. `0xF20000` then counts CPU
    cycles, and it reads fine from program code.
  - **Profiler:** a script rewrites `engine.s`, putting `call __pe` at every
    `@function` label and turning each `ret` into `jp __px`, and links it into
    a CEdev program. Leave hand-written asm routines alone: an entry hook
    corrupts their stack-relative argument reads.
  - **Read profiler numbers with care:**
    - Per-call self time carries roughly 1,100 cycles of hook overhead per
      call, so tiny functions look expensive. Trust wall timings (timer
      around `symce_engine`) and 50-iteration microbenchmarks.
    - Code runs from RAM there; the flash app on rev M+ runs faster (§10).
- **Fuzzing asm against the host** (2026-09-27).
  - A host program `#include`s `engine.c` and emits random inputs with the
    host's answers as a C header.
  - A CEdev program links the exact `engine.s` (plus `.globl` for the static
    functions under test), runs every case and reports mismatches in RAM.
  - A deliberately corrupted expected value must show up as one mismatch
    before a clean run counts.
  - For SymCE's float core, 16,400 cases (fmul, fdiv, fadd, fset, toint)
    matched.

---

## 11b. Graph screen (*measured* 2026-09-29, 5.8.4, branch `graph-probe` d30f1d1)

Probed with a regraph hook (`hookflags3` bit 6, slot `0xD025F0`) in `src/rhook.c`
on that branch; the drivers are the gitignored `tools/emu/_graph*.py`.

- **The OS graph area is fixed.** Full mode: x=27, y=46, 265x165 px. Horizontal
  split: 265x81. G-T: 185x145 at x=4. `0xD014FC` is x0, `0xD014FE` the height,
  `0xD014FF` the width (2 bytes); the names in `ti84pceg.inc` are misleading.
  Raising them moves the axes, but the OS clears a fixed white rectangle, so the
  graph does not get bigger.
- **OS draw times**, ZStandard, Full, 133 points (timed by holding the zoom key,
  then a CRC of VRAM rows 46..210 until it stops changing; precision about 4 ms;
  autotester delays track emulated time 1:1):

  | Y= | ms |
  |---|---|
  | `X` | 1864 |
  | `sin(X)` | 2804 |
  | `X^2` | 1624 |
  | `sin(X)/X+cos(3X)` | 5740 |
  | `sqrt(X)` | 1544 |
  | `sin(X)`, `cos(X)`, `X^2/5` together | 7480 |

- **Regraph hook events** (A on entry), CE: `00`, `01`, `02`, `09` once each;
  per point `03` (OP1 = X), `08` (OP1 = the equation name `03 5E 10`, OP4 =
  `00 58`), the parse, `04` (OP1 = x, OP2 = y), `08`, `05`, `06`; `07` once at
  the end. The hook could not feed y to skip the parser, so the OS stays at
  about 14-40 ms per point whatever the hook does.
- SymCE's scratch at `0xD0EE00` survives graphing.
- Consequence: SymCE draws its own graph (`src/graph.c`, opened by the GRAPH key
  through the raw key hook; settings row `4: GRAPH key`), straight into VRAM, full screen, in float32.

### TI's graph screen, what SymCE copies (*measured* 2026-09-30, 5.8.4)

Stock ROM, CEmu autotester, Full mode, ZStandard. Drivers `tools/emu/_ti*.py` (gitignored; `_ti.py` core, `_tilib.py`, `_tiu.py`, `_tigl.py` glyph decoder; batches `_ti_a*`, `_ti_b*`, `_ti_c*`, `_ti_d*`, `_ti_e*`). PNGs `tools/emu/_png/ti_*.png`. M = measured, I = inferred.

**A. Layout (M)** Status bar rows 0..29 (0x52AA, white small text, battery top right). Rows 30..45 and everything outside the plot = 0xE71C. Plot = x 27..291, y 46..210 (265x165, 264x164 intervals), white, no border. Axes 1 px black at x=159, y=128. Ticks black, 7 px (+-3). Tick x = round(27+(k*Xscl-Xmin)*264/(Xmax-Xmin)); y = 46+round((Ymax-v)*164/(Ymax-Ymin)), round half up. Top-left label (rows 32..41, small font, function colour): `Y1=sin(X)`; in CALC/zoom a mode title row 30..45 (`CALC ZERO`, `ZOOM BOX`, `ZOOM IN`; ZInteger none) and the `Y1=` label below it. Bottom text `X=` at x=28, `Y=` at x=162, glyph top y=226 (small variable font, 9-char number field, trailing zeros dropped, negative sign = negation glyph, colour = function colour in trace/CALC, black in free mode). Prompt (`Left Bound?`, `Guess?`, `Lower Limit?`, `First curve?`, `Second curve?`) in black at bottom-left one row above X=/Y=. Typed-number prompt: white box rows 216..234, 1 px black top and bottom, large font 12 px pitch, `X=2.5` at x=2, block cursor. PNGs: ti_plain, ti_menu_{fmt,zm0,zm1,calc,draw}, ti_window, ti_calc_*, ti_setf_a.

**B. Free cursor / TRACE (M)**
- RAM: curGStyle 0xD0146D, curGY 0xD0146E (px from bottom, 0..164; 165 = off screen), curGX 0xD0146F (2 B, 0..264), copies 0xD01476/77.
- Free cursor hidden until first arrow; starts at centre (132,82); tap = 1 px; hold repeats after ~0.5 s; stops at edge, NO panning; style 07. Trace style 0x51; ZBox 19/9; Zoom In 11; Zoom Out 13; ZInteger 21.
- Cursor 9x9, blinks 2 frames (~0.35-0.4 s each), black, interior cleared white. Free frame0 circle+plus, frame1 bold plus. Trace frames: see ti_tr_*.png.
- TRACE starts at centre pixel, X=(Xmin+Xmax)/2, lowest selected function; arrow = TraceStep = 2 px; Y exact. UP = previous function number with wrap (skips deselected), DOWN = next.
- Undefined Y: `Y=` blank, no cursor. Off-screen Y: text shown, no cursor, gy=165.
- Edge pan: stepping past gx=264 (or 0) shifts Xmin/Xmax by 16 px-columns of DeltaX (1.2121212 at ZStd); cursor lands at column 250 (left edge ~12). Only X pans.
- Digit opens `X=` prompt; ENTER moves exactly. Out-of-window -> ERROR: INVALID (1:Quit 2:Goto; see phase 1b below). 
- ENTER in trace = QuickZoom: same width/height, recentred on traced point (X and Y), redraw, trace continues at centre pixel. CLEAR / GRAPH / ENTER on free cursor -> plain graph, cursor position forgotten. 2nd QUIT -> home. CLEAR on plain graph -> home. 2nd TRACE in trace opens CALC.
- Side effects (M, corrected): free cursor and trace set X and Y (free cursor: X=cursor X, Y=cursor Y; trace: X and f(X)); **Ans is NOT changed** (earlier reading was contaminated by reading X first). Plain graph drawing itself leaves X=Xmax-ish (10) and Y=last function value (sin 10).
- ExprOff: flag only (traceFlags bit0); look on screen untested. CoordOff flag only.
- Phase 1b measurements: see §11c "Free cursor and TRACE".

**WINDOW (M)** DeltaX 0xD01FAE, DeltaY 0xD01FB7, TraceStep 0xD0203D, Xmin 0xD01E33, Xmax 0xD01E3C, Xscl 0xD01E45, Ymin 0xD01E4E, Ymax 0xD01E57, Yscl 0xD01E60. ZStd: DeltaX=20/264, DeltaY=20/164, TraceStep=2*DeltaX. Editing/storing TraceStep rewrites DeltaX=TS/2 and Xmax=Xmin+264*DeltaX; editing Xmax makes TS=2*DeltaX again. So in function mode TraceStep is ALWAYS 2*DeltaX; the spec's "honour OS TraceStep" branch is unreachable (parametric/polar untested).

**C. FORMAT / flags (M, RAM diff; iy=0xD00080)**
- grfDBFlags iy+4 (0xD00084): bit0 Dot(mode), bit1 Simul (0x02), bit2 Grid (GridDot 0x04; GridLine 0x84, bit7 = line), bit3 PolarGC (0x08), bit4 CoordOff (0x10), bit6 LabelOn (0x40). bit5 unused here (Axes is a colour, not on/off).
- traceFlags iy+0x18 (0xD00098) bit0 = ExprOff. asymptoteFlags iy+0x47 (0xD000C7) bit0 = Detect Asymptotes Off.
- Colours: GridColor 0xD024F1 (default 0x0D MEDGRAY), Axes colour 0xD024F2 (default 3 BLACK), each step +1 per RIGHT; BorderColor = graphBGColor 0xD02A98 (RGB565, default E71C, +1 -> BF59); Background choice 0xD024F3/F5/F6.. (+0xD000CF bit0, image-style, not decoded).
- Palette (M: ROM table 0x80344, confirmed in pixels for idx 4, 14, 13): 1 BLUE 001F, 2 RED F800, 3 BLACK 0000, 4 MAGENTA F81F, 5 GREEN 04E0, 6 ORANGE FC64, 7 BROWN B100, 8 NAVY 0010, 9 LTBLUE 049F, 10 YELLOW FFE0, 11 WHITE FFFF, 12 LTGRAY E71C, 13 MEDGRAY C618, 14 GRAY 8C51, 15 DARKGRAY 52AA. Default grid pixel = C618. Grid lines 1 px.
- y1LineType 0xD024BF (8 B, Yn then others), colour 0xD024D8. Y= popup (left*8 from end of expression, ENTER on the icon: Color + Line; OK) cycles values 1,2,3,4,5,6,7,0. Looks (graphs ti_lsg_* via _m6): 0 thin 1 px; 1 default normal line (~2 px); 2 shade above (vertical hatch, every other column, from top); 3 shade below; 4 path (curve + circle marker at end); 5 animate (only circle); 6 thick/dot-thick; 7 dotted thin. Y= default colours 1..7 blue red black magenta green orange brown (Y8 blue).
- MODE: grfModeFlags iy+2 (0xD00082): Func 0x10, Pol 0x20, Par 0x40, Seq 0x80 (mode diff: 12->22 Pol, 12->82 Seq). Dot mode: all LineType bytes set to 6, 0xD000D2=1 (so 6 = Dot). Simul = iy+4 bit1. Horiz/G-T split: sGrFlags 0xD00094 (Horiz 01, G-T 06) plus YOffset 0xD014FC etc. Degree = iy+0 bit2 (0xD00080 20->24). Stat plot bytes 0xD0228E.. (Plot1XYOnOff) not re-diffed.

**D. ZOOM windows from ZStd with Y1=sin(X) (M)** unit D=1/264 of width, E=1/164 of height.
| item | result |
| ZDecimal | X -6.6..6.6, Y -4.1..4.1 (DeltaX=DeltaY=0.05; Xmin=-132*0.05) |
| ZSquare | keeps the axis with the LARGER delta, expands other axis symmetrically about its centre until DeltaX=DeltaY. ZStd -> X +-16.0975610 (=10*264/164). Skew X -5..5,Y -1..1 -> Y +-3.10606 (Y expanded). X -30..-10,Y 0..100 -> X -100.4878..60.4878 (X expanded about -20) |
| ZStandard | +-10, Xscl=Yscl=1 |
| ZTrig rad | X +-132*pi/48 = +-8.6393798, Xscl pi/2, Y +-4, Yscl 1 |
| ZTrig deg | X +-495 (=132*3.75), Xscl 90, Y +-4 |
| ZInteger | ENTER: X -66..66, Y -41..41, scl 10, Delta 0.5. Cursor prompt (style 21): cursor moved (+10px,+5 up -> X .7576 Y .6098) -> centre rounds to (1,1): X -65..67, Y -40..42 |
| ZoomFit | Y only, X unchanged; Ymin/Ymax = min/max of 265 sampled values (sin: +-0.999881125; X^2/5: 0..20) |
| ZQuadrant1 | X 0..13.2, Y 0..13.2 (DeltaX .05, DeltaY 13.2/164) |
| ZFrac 1/2,1/3,1/4,1/5,1/8,1/10 | DeltaX=DeltaY=f/2, TraceStep=f; X=-132*Delta..+132*Delta, Y=-82*Delta..+82*Delta. 1/2: X +-33, Y +-20.5. 1/3: +-22, +-13.6667. 1/4: +-16.5, +-10.25. 1/5: +-13.2, +-8.2. 1/8: +-8.25, +-5.125. 1/10: +-6.6, +-4.1 |
| ZBox (style 19 then 9) | cursor at centre; ENTER corner1, move, ENTER corner2: window = exactly the two corner pixel coordinates (-0.6060606..1.1363636, Y -0.4878049..0.3658537 at px (124,78)-(147,85)); Xscl/Yscl unchanged |
| Zoom In (style 11) | XFact=YFact=4 default; ENTER at cursor X=0.3787879 -> width/4 centred on cursor (X -2.1212121..2.8787879, Y +-2.5 about cursor Y 0) |
| Zoom Out (13) | ENTER -> x4 about the cursor (+-40) |
| ZPrevious / ZoomSto / ZoomRcl | verified by windows: Prev returns the window before the last zoom (ZStd after ZDecimal); Sto+Rcl round-trips (ZDecimal) |
| SetFactors | screen `ZOOM FACTORS` (XFact= cursor, YFact=4) png ti_setf_a |
Menu order (M): ZOOM tab0 down-count n=0..16: ZBox ZoomIn ZoomOut ZDecimal ZSquare ZStandard ZTrig ZInteger ZoomStat ZoomFit ZQuadrant1 ZFrac1/2 ZFrac1/3 ZFrac1/4 ZFrac1/5 ZFrac1/8 ZFrac1/10 (keycodes 005F 005C 005D 0060 0062 0063 0064 0067 0068 005B FBC0..FBC6 in ROM; 7 FBC entries but 6 fracs + Q1 observed => FBC0 may be ZQuadrant1). Tab1: ZPrevious ZoomSto ZoomRcl SetFactors (005E 0066 0065 0051).

**E. CALC, Y1=X^2-2, Y2=X (M)** Stage title `CALC <NAME>` in rows 30..45; function label below. Cursor at CALC start = centre, moves 2 px per arrow (TraceStep). curGStyle: value 0x4F after result; zero 83/85/87 (0x53/55/57: left, right, guess); intersect 101/103/89 (0x65/67/59) for first, second, guess; min 105/107/109; max 119/121/123; dy/dx 99; integral 95/97. Bound markers: small triangle at top plus dotted vertical line. 
- value: prompt box `X=` (typed) -> result `X=1.5 Y=0.25` (style 0x4F, bold X marker).
- zero: Left Bound? Right Bound? Guess? -> `Zero X=-1.414214 Y=0` bottom-left label + coordinates; ERROR: NO SIGN CHANGE screen if root not bracketed (bounds -3.48 and -1.97 tried).
- minimum/maximum: same 3 prompts; result `Minimum X=0 Y=-2` (exact), `Maximum X=1.9696932 Y=1.8796915` (endpoint, not bracketed).
- intersect: First curve? Second curve? (cursor on Y2, first marker stays) Guess? -> `Intersection X=2 Y=2`.
- dy/dx: cursor moves freely, ENTER -> `dy/dx=6.0606061` line above X=/Y=. Title `CALC DERIVATIVE AT POINT`.
- integral: `Lower Limit?` `Upper Limit?` (I) -> vertical hatch shading in function colour between curve and x-axis, text `Jf(x)dx=6.4297706` + `[-3.030303,3.030303]`. Title `CALC INTEGRAL OVER INTERVAL`.
- Timings not measured (all finish inside 1.5-2.5 s wait).

**F. Menus (M)** menuCurrent: ZOOM 0x04 (tab 0 has 17 items, tab1 4), CALC 0x11 (7), DRAW 0x05 (tabs 11/7/4/2), FORMAT = screen (menuCurrent 0). cxCurApp = 0x44 with a menu open from the graph. OS key codes: Right 01, Left 02, Up 03, Down 04, Enter 05, Clear 09, Del 0A, Quit 40, Graph 44, Mode 45, Window 48, Y= 49, Format 57, Trace 5A. CALC keycodes value 0075 zero 0079 min 007D max 007E intersect 0076 dy/dx 0077 int 0078.

Not measured: stat plots, polar/parametric trace step, ExprOff/CoordOff visuals, ERR Goto, CALC timings, Background image option, DRAW items.


### SymCE's grapher on the calculator (*measured* 2026-09-29, `tools/emu/graph.py`)

- **Draw time**, `4` key to the last change of VRAM rows 0..219 (CRC every
  20 ms, so +-20 ms; 80 ms steps once a draw passes 1 s). First version, then
  after the fixes below, against the OS graph (§11b):

  | Y= | first | now | OS |
  |---|---|---|---|
  | none selected (fixed cost) | 340 | 20 | - |
  | `X` | 440 | 80 | 1864 |
  | `sin(X)` | 660 | 320 | 2804 |
  | `X^2` | 420 | 60 | 1624 |
  | `sin(X)/X+cos(3X)` | 2640 | 640 | 5740 |
  | `sqrt(X)` | 500 | 140 | 1544 |
  | `sin`,`cos`,`X^2/5` | 2160 | 800 | 7480 |

- **Profile.** Two things were wrong. (1) The full-screen clear, a C loop of
  76,800 stores with a 32-bit counter run from flash (10 cycles per instruction
  byte, §flash-is-slow), took about 320 ms of the 340 fixed; `memset` (libc's
  is an ldir) makes it under 20 ms. The asm glue's VAT and variable copying and
  the info bar (an ldir of 20 rows) are inside the remaining <20 ms. (2) The
  `GetKey` battery icon: it is drawn about 3.7 s after the draw, into rows
  0..219, so the "last change" of every function with a trig call read 2-3 s
  too long (`cos(3X)` 360 ms alone, `cos(3X)+X` 3760 ms: not the add, the
  icon). The rest is soft-float: `sinf`/`cosf` cost about 1 ms per call (300 ms
  per 320 columns); `X` alone is about 190 us per column (the mapping's floats).
  ponytail: the mapping was left as it is; `x += dx` would save one multiply.
- **Keys**: the viewer polls `GetCSC` (no status bar drawn: the battery icon is
  gone). 2nd then MODE = QUIT, CLEAR leaves. A held arrow repeats: after each
  draw the asm reads keypad group 7 (`0xF5001E`, bit set = down: Down 0, Left
  1, Right 2, Up 3) and pans again while it is down, so the pan rate is the draw
  time. Measured with `hold|right` for 700 ms: a tap moves the axis to column
  120, the hold pans it off the screen. No APD in the loop (like the menu's).
- `ChkFindSym` on `03 5E 1n`: the flag byte is at `HL - 1` (selected = bit 0),
  `DE` is the data with the size word first; works. `ChkInRam` keeps `DE`.
- The app's `iy` is `ti.flags`; C clobbers it, so it is reloaded after each call.
- `GetKey` on our own full-screen VRAM draws the OS battery icon top right
  (about 20x12 px, some seconds later); `GetCSC` does not. `VPutS` with
  `drawFGColor`/`drawBGColor` set by hand works for the bar.
- 160 KB of appvars no longer fit in RAM together: `bigapp.py`'s "no room keeps
  old SymCE" case sent them to RAM and failed with `SYMCE3 is missing`. It now
  sends them archived into a ROM with three sectors erased for them (the archive
  is full again after), and the "empty appvar" case has its own full ROM.

### What a grapher reads (*measured* 2026-09-29, `tools/emu/_gm2.py`: type in Y=, quit, dump RAM)

- **Y1..Y0 in the VAT.** Each is an equation entry, type `03` (`EquObj`), name
  `5E 10..19` (`tVarEqu`, then `tY1`..`tY9`, `tY0`; `20..2B` are X1T..Y6T, `40..45`
  r1..r6, `80..82` u v w). The 9-byte VAT entry, ascending in RAM (the table
  grows down from `0xD3FFFF`): `00 nn 5E ah am al 00 fl 03`: name reversed with
  a `00` terminator, the data pointer big-endian, a `00`, then the flag byte
  `fl` just under the type. **`fl` bit 0 is "selected" (the `=` sign
  highlighted): `01` selected, `00` not** (Y3 deselected with 2nd LEFT, LEFT,
  ENTER in Y=: `fl` 01 -> 00; LEFT x3 is the line-style icon and does not toggle
  it). An equation never typed has `fl = 00` and a size word of 0. So a Yn is
  plotted when `fl & 1` and its size is nonzero. Y1..Y0's data sits at
  `0xD2A8B4..`, each `size word (2) + tokens`, no terminator; `ChkFindSym` with
  OP1 = `03 5E 10` finds it (DE = data, size word first; the flag is the byte
  below the type byte, so it is at `HL - 1` if `HL` is the entry).
- **How the Y= editor stores MathPrint** (*measured*: 27 equations typed in
  MathPrint, mode word `0xD000C4 = 2E`, bit 5 = MathPrint). The stored tokens
  are the same flat ones the OS parses; nothing 2D survives:

  | typed | stored |
  |---|---|
  | `X^2` (box) | `58 0D` (the `²` token) |
  | `X^3` (math 3) | `58 0F` |
  | `X^4` | `58 F0 34` (`^` then the digit, no parentheses) |
  | `X^12` | `58 F0 31 32` |
  | `X^(-1)` | `58 0C` (the `⁻¹` token) |
  | `X^(1/2)` | `58 F0 10 10 31 EF 2E 32 11 11` |
  | n/d, 1 over X | `31 EF 2E 10 58 11`: the bar `EF 2E`, den parenthesised |
  | n/d, X over 1 | `10 58 11 EF 2E 31`: num parenthesised; a bare number is not |
  | n/d, (X+1) over (X-1) | `10 58 70 31 11 EF 2E 10 58 71 31 11` |
  | `√(X)` | `BC 58 11` (`√(` `X` `)`) |
  | `abs(X)` | `B2 58 11` |
  | `e^(X)` | `BF 58 11` |
  | `10^(X)` | `C1 58 11` |
  | `√(1/X)` | `BC 31 EF 2E 10 58 11 11` |
  | `X^2` then `1` after leaving the box | `58 0D 70 31` |
  | `1E3X` | `31 3B 33 58` (`EE` is `3B`) |
  | `-X+π+e+A+θ` | `B0 58 70 AC 70 BB 31 70 41 70 5B` (`π` `AC`, `e` `BB 31`, negation `B0`) |

  So the grapher needs no box parsing: the bar is a `/` (one level with `*`,
  left to right, as engine.c's `peek`), the `²`/`³`/`⁻¹` tokens are postfix, and
  the rest are ordinary tokens. The `(` after `√`/`abs`/`e^`/`10^` is part of
  the function token, its `)` is a `11` that may be missing at the end.
- **Window.** Nine-byte TI reals (byte 0 sign in bit 7, byte 1 = `0x80` + exponent,
  bytes 2..8 the 14 BCD digits `d.ddd`): `Xmin 0xD01E33`, `Xmax 0xD01E3C`,
  `Xscl 0xD01E45`, `Ymin 0xD01E4E`, `Ymax 0xD01E57`, `Yscl 0xD01E60`. After
  ZStandard: `80 81 10 00..` (-10), `00 81 10 ..` (10), `Xscl 00 80 10` (1);
  after WINDOW -5, 7, 1, -3, 4 they read `80 80 50`, `00 80 70`, `80 80 30`,
  `00 80 40`. **Degree flag: `trigFlags` (iy+0, `0xD00080`) bit 2**: 0x20 in
  Radian, 0x24 after MODE Degree.
- **Line colours.** `y1LineColor..y0LineColor` at `0xD024D8..0xD024E1`, one byte
  each, and `y1LineType..` at `0xD024BF..` (all 01 by default). Defaults
  `01 02 03 04 05 06 07 01 02 03`; the pixels those draw in the OS graph (VRAM
  RGB565): 1 blue `001F`, 2 red `F800`, 3 black `0000`, 4 magenta `F81F`,
  5 green `04E0`, 6 orange `FC64`, 7 brown `B100`. Indices 8 and up are unmeasured
  (Y= lets you pick more colours). The OS graph area is white `FFFF` on a
  `E71C` screen background.

---

## 11c. SymCE grapher as a TI clone (phase 1a, 2026-09-30)

- **Flags that send GRAPH to TI's own graph**: stat plot on = bit 0 of `0xD0228E` /
  `0xD022A0` / `0xD022B2` (plots 1-3); background image = nonzero `0xD024F3` (also
  `0xD000CF`); Xres real at `0xD01EF0`; `asymptoteFlags` `0xD000C7` bit 0 *set* = Detect
  Asymptotes OFF. TI's graph with a stat plot on and no list data ends in an error
  screen (cxCurApp `0x52`), which is what the fallback gives too.
- **Returning a key from the raw key hook**: after `NewContext0(kQuit)`, `ret` with `A` = key
  makes the OS act as if that key was pressed at home (this is how Y=, WINDOW, MODE, FORMAT,
  TABLE, TBLSET, TRACE and GRAPH reach the same cxCurApp as from TI's graph). `JForceCmd`
  with `A` = key does *not* switch context. Menu keys (ZOOM, MATH, STAT, VARS, APPS, 2nd CALC,
  DRAW, LIST ...) therefore open their menu over home (cx `0x40`), where TI opens it over the
  graph (`0x44`); phases 1c and 1d need the menu hook for that.
- **Calling the OS parser from C** (`_symce_osy`): `Mov9ToOP1` + `StoX`, OP1 = `03 5E 1n 00`
  (an Yn name), `push ix`, `push errSP`(`0xD02596`), `PushErrorHandler` `0x20798`, `FindSym`
  `0x20510`, `ParseInp` `0x20F00`, `PopErrorHandler` `0x2079C`. No `StoAns`, so `Ans` stays
  as it was. The error path must `pop de`, `FixTempCnt` `0x20E80`, `res 0,(iy+7)`. Y is stored
  with `StoY` `0x20F3C`. After a draw TI leaves X = Xmin + 264*DeltaX and Y = last value.
- **Layout**: plot 319x219 at (0,0), bar rows 219..239 and column 319 in the border colour.
  Line styles: thin 1 px; normal adds (x+1,y) and (x,y+1); dot-thick 3x3 blocks, unconnected;
  dotted a pixel on even columns. GridDot is a 3x3 block at each intersection, GridLine 1 px.
- **Compiler trap**: clang-ez80 dropped the NEG in a `neg ^= 1` toggle loop (`(-)X` plotted as
  `X`). Use recursion (`unary` -> `negpost`) and write bool-to-float as `c ? 1.0f : 0.0f`.
- **Test harness**: `vram.grab` (step=1) makes TI's own graph draw blank; take TI reference
  shots with `_ti.go` (`graph.tishot`). After leaving the SymCE settings app send
  `2nd mode .` once more. `screen.read` step=0 is fine for RAM/cx reads.
- **Y= popup keys** (Color and Line): `left*8 enter` opens it; right changes the colour
  (1 BLUE ... 15); `down` moves to Line, rights step the style (1, 2 ... 7, 0); `down enter`
  confirms. Keys named in the harness: x^-1 = `-1`, comma = `,`, negation = `(-)`, ANS = `2nd (-)`.

### Free cursor and TRACE (phase 1b, *measured* 2026-09-30, `tools/emu/trace.py`)

- **Compared with TI** (same keys, GRAPH key setting 4 off): X, Y, Ans, Xmin..Yscl, DeltaX/Y,
  TraceStep, ZPrevious, the X/Y variables, home-screen X/Y/Ans, bar glyphs. Cases free (R7 U4),
  trace, R5 L3, UP/DOWN, pan right and left, typed 2.5 (+ next arrow), out-of-window, QuickZoom
  (also after ZStandard, ZPrevious after it). All equal.
- **X/Y**: free cursor X = Xmin + rnd10(gx*DeltaX) (rnd10 = round half up to 10 digits; the sum is not
  rounded again), Y = Ymin + rnd10(gy*DeltaY). Trace X = x0 + rnd10(p*DeltaX), p = gx + 16*pans.
- **Pan**: lands at column 250 (right) or 14 (left). Xmin' = rnd10(Xmin +- rnd10(16*DeltaX)); TI sets bit
  0x40 in byte 0 of the new Xmin (flag byte, always: 0x40 / 0xC0); Xmax' = rnd10(Xmin' + rnd10(264*DeltaX)).
- **Typed X**: stored as typed (gx = round((X-Xmin)/DX)); the next arrow snaps t = floor((X-Xmin)/TraceStep
  + 0.5), gx = 2t, then +-2.
- **QuickZoom (ENTER)**: Xmin' = rnd10(X-hw), Xmax' = rnd10(X+hw), hw = (Xmax-Xmin)/2, Y alike; DeltaX, DeltaY
  = (max-min)/264, /164 and TraceStep = 2*DeltaX recomputed; the OLD window (54 B from 0xD01E33) is copied to
  ZPrevious 0xD01FFC. X, Y stay as traced; trace continues at gx 132.
- **X= box**: general expression editor (digits . (-) operators X pi EE sin( STO ...), arrows ignored, DEL
  deletes last, CLEAR clears (empty: plain graph), TRACE restarts trace, GRAPH plain, Y= leaves, 2nd QUIT
  home. ENTER on "2+" or "(" -> ERROR: SYNTAX (cx 0x52); outside [Xmin,Xmax] -> ERROR: INVALID (0x8F, has
  2:Goto). After Quit: cx 0x40. Goto on TI: cx 0x44, curG 51 52 84, trace at centre in TI's graph. SymCE
  deviation: Goto goes home (cx 0x40); a hand-built graph context (NewContext0 0x44 + cxErrorEP 0x6C96D)
  RAM-cleared the calculator, so not done. Box look: white rows 216..234, black lines at 216 and 234, large
  font 12 px pitch from x=2, glyph top 219, block cursor 10x14.
- **Look**: SymCE's bar is rows 219..239; the glyphs are the same as TI's, 2 rows lower (text top 228 vs 226).
  Trace cursor black with white interior; text colour = function colour in trace, black in free mode.
- **Evaluator without StoAns**: `_symce_eval` = CreatetEqu `0x2055C`, OP4ToOP1 `0x202C8`, ParseInp `0x20F00`
  under PushErrorHandler `0x20798` (like `_symce_osy`). Number text: wrapper `0x9968B` (A=9, OP1 in, HL=OP3
  out; `set 6,(iy+29)` first). Token text: `0x5E43B` (token in DE), `0xA2C17`; two-byte prefixes 5C 5D 5E 60 61
  62 63 7E AA BB EF. Error: `JError` `0x20790`, Invalid 0x8F, Syntax 0x88. pen: penCol `0xD008D2` (16 bit, write
  bytes), penRow `0xD008D5`, colours `0xD026AC` fg / `0xD026AA` bg, large font = bit 2 of (iy+0x32).
- **Time source**: timer 1 (control `0xF20030`, counter `0xF20000`) at 32768 Hz, started by `graphMain` (save and
  restore the control word, state in GW+0xC04), counting up. Blink 10500 ticks (320 ms), hold delay 16384 (0.5 s),
  repeat 3015 (92 ms, 5 px per free repeat). Held keys read from the keypad data register `0xF5001E`
  (Down 1, Left 2, Right 4, Up 8). The idle loop sends key 0xFF to the grapher as a tick.
- **GW additions**: struct gw after `buf` holds mode, cursor, label/bar save buffer (plotSScreen `0xD09466`),
  entry text. The opening key is kept in GW+0xC07 (GRAPH or TRACE).

### TRACE snap (SymCE only, function mode, 2026-10-08, `tools/emu/tracesnap.py`)
TI's TRACE just walks the 2-px grid. SymCE's stops on a **Zero, Y-intercept, Intersection, Minimum or Maximum** a step passes, shows the
CALC-style label ("Zero" ...) and X, Y exactly. Settings item 6 (default ON); the bit is `asm_Flag2` ($D000A2) bit 4, **set = OFF**, so a fresh RAM
is ON; the TI-parity tests switch it off (`graph.SNAP_OFF`). Not in Par/Pol/Seq, stat plots, a CALC prompt, Tangent(.
- Per step (one more Y evaluation per other selected function): Zero = Y changes sign or is 0; Intersection = Yfn - Yj changes sign; Min/Max = slope
  sign changes over the last three samples (X of the older two kept); Y-intercept = X=0 inside (x_old, x_new]. Refined with `symce_calc` kinds 1/4/2/3
  on [x_old, x_new] (min/max on [x_prev2, x_new]). The nearest to x_old wins, then Zero > Y-intercept > Intersection > Min/Max; a solver error drops it.
- **False roots**: a pole is a sign change too. `GRITSolver` happily converges onto tan/1/X poles; reject unless |Y(root)| <= DeltaY (zero), |Yfn-Yj| <=
  DeltaY (intersection). `DFMin` on a pole *runs off along it* (returned x outside the bracket, Y = -151359 on tan) and the three samples around a pole look like
  an extremum: reject a root outside the bracket, a Y outside the window or Y not at least as extreme as the samples. The solver leaves its guess in X: show again after.
- **Max/Min can be behind the cursor** (vertex between x_prev2 and x_old when the sign change shows only at the third sample): the cursor jumps back to it.
  The arrow after a POI goes to the grid point next to it in the direction pressed (the other way: the one on the other side), through samples already seen
  without detecting (else the old samples read as a new extremum); a POI on a grid point goes on to the next grid point. A reversal keeps the newest sample.
- Minimum / Maximum come back as 0.9999978 for X^3-3X (DFMin tolerance 1e-5, as TI's own CALC); Zero/Intersection to 1e-12.

### ZOOM on SymCE's graph (phase 1c, *measured* 2026-10-01, `tools/emu/trace.py` z* cases and `menus`)

- **Menu hook event 3 (a pick)**: B is the picked item's KEY CODE, whether ENTER or the item's digit was
  pressed. ZOOM tab 0: ZBox 5F, ZoomIn 5C, ZoomOut 5D, ZDecimal 60, ZSquare 62, ZStandard 63, ZTrig 64,
  ZInteger 67, ZoomStat 68, ZoomFit 5B; tab 1 (ZOOM MEMORY): ZPrevious 5E, ZoomSto 66, ZoomRcl 65,
  SetFactors 51. The letter items (ZQuadrant1, ZFrac 1/2 1/3 1/4 1/5 1/8 1/10) come as B = 0FBh with
  `keyExtend 0xD0058E` = C0..C6. Return Z = the OS goes on (`ld a,b`). The raw key hook sees keys pressed
  while a menu is up, but NOT the picked item's code.
- **Arming**: `symce_mhook` (main.asm) is armed when the GRAPH key setting is on OR the CAS is on. Event 3 ->
  `menuKey` (ZOOM interception, only if `gkeyIsOn` and menuCurrent 0xD00824 = 4); the other events go to the CAS
  shim (`symce_mhook_cas`, the 2nd MATH tab) only if `casIsOn`, else Z. Settings 1 (CAS off) clears the hook only
  when GRAPH is off too; 4 off only when CAS is off. The installer arms it as before.
- **Hook event 4 (a menu opens)** saves `curRow/curCol` (0xD00595) in `GW+0xC0B` (GWC): the menu moves the home
  cursor, and a pick that leaves into the grapher puts it back.
- **Pick -> grapher** (`menuKey .run`): GWA = 0FEh, `graphMain` = fallback probe (returns 0FFh when TI's graph must
  do it: Polar etc.); then `NewContext0(kQuit)`, GWA = action 1..20 (the grapher's first key is 0C0h+action),
  `graphMain` runs INSIDE the hook. Exit (a) home: restore the cursor, `set 5,(iy+17)`, `set 0,(iy+38)`,
  menuCurrent/Sub = 0 and menuSelected = 1 (the OS's home values; without them the OS keeps taking keys as menu
  keys: digits ignored, a later ENTER picks ZBox), `dec (0xD0265B)` (the hook depth the OS will never see
  return), `jp JForceCmdNoChar`. (b) an OS key (the user pressed Y=, ZOOM, MATH ... on the graph): same, then
  `ld sp,(0xD007FA)`, `res 1,(iy+40)`, `res 2,(iy+40)`, `jp 0x8C49F` with A = the key = the monitor's key
  dispatch just after the raw key hook returns. **`JForceCmd` takes a TOKEN (0x8C5BA), not a key**: a menu key
  through it does nothing.
- **CLEAR marker** GWM (GW+0xC0E, bytes 0A5h 5Ah; NOT +0xC09 as written earlier, which is free) = 0A5h 5Ah when the grapher hands a menu key (2Bh..3Ch) to the OS. The raw key
  hook clears it on any key but CLEAR and the arrows. **CLEAR in a menu is NOT a menu-hook event 3** (the OS
  closes the menu itself); the raw key hook sees it: CLEAR with the marker set is treated as the GRAPH key
  (`symce_khook` -> `.open`: leave the menu with NewContext0(kQuit), `graphMain`). 2nd QUIT in the menu goes
  home (the key is not CLEAR, the marker is cleared).
- **OS routines used** (all by `symce_rom`, iy = flags; `symce_roma` also sets A): `0x6EDDA` window -> ZPrevious
  (0xD01FFC) and the ZPrevious-valid bit (iy+74 bit 7); jump table ZmInt 0x20C08, ZmDecml 0x20C0C, ZmPrev
  0x20C10, ZmUsr (ZoomRcl) 0x20C14, SetUZM (ZoomSto) 0x20C18, ZmFit 0x20C1C (runs the graph, not callable),
  ZmSquare 0x20C20, ZmTrig 0x20C24; ZQuadrant1 0xBD0F0; ZFrac 0xBD2C6 with A = 2 3 4 5 8 10 (dispatch at
  0xBD332); ZoomIn/Out about X, Y: 0xB18E7 (carry = In, hl XFact/YFact); ZInteger's ENTER: 0x9AE22, 0x7FA0E,
  0x9AE13, ZmInt. XFact 0xD01F9B, YFact 0xD01FA4 (4 default), ZSto block 0xD01D61 (54 B, the window's layout).
- **What TI does after each zoom** (copied; every window variable byte equal): DeltaX = (Xmax-Xmin)/264, DeltaY =
  (Ymax-Ymin)/164, TraceStep = 2 DeltaX are recomputed when the OS routine did not (ZSquare, ZPrevious, ZoomRcl,
  ZStandard, ZoomFit; ZDecimal/ZTrig/ZQuadrant1/ZFrac set them). The OS's ZFrac puts the bits 0x18 in the first
  byte of the window variables and DeltaX/Y; TraceStep gets them too. ZFrac TraceStep = (Xmax-Xmin)/132 (2*DX off
  the rounded DX is 1 ulp high for 1/3); every other TS = 2 DeltaX.
  ZoomFit writes rnd10 of the extremes of the 265 columns. ZPrevious holds the window from before.
- **X after a draw** = Xmin + rnd10(264 DeltaX) (product first, so the flag bits ride along), not Xmax: ZTrig
  leaves 8.639379793, not 8.639379797. This fixes phase 1a/1b's `symce_setx`, gone now. After a ZFrac draw X
  keeps DeltaX's 0x18 bits (home shows 33.2 for ZFrac 1/4) unless X is a whole number (1/2, 1/3: 33, 22).
- After ZBox / Zoom In / Zoom Out the cursor is at the centre and X, Y are the centre's (Xmin+132 DX, Ymin+82 DY),
  as the free cursor stores them; ZInteger leaves X, Y as the draw left them.
- Interactive zooms (ZBox, Zoom In/Out, ZInteger) leave the free cursor at the centre (gx 132, gy 82) with TI's
  title row text (`ZOOM BOX`, `ZOOM IN`, `ZOOM OUT`). ZBox: first ENTER fixes a corner, the box (1 px outline, 7x7
  marker) follows the cursor, the second ENTER writes the two corners as the window.
- Fallback and passthrough: ZoomStat and SetFactors always go to the OS; if `graphMain`'s probe says TI's graph
  (Polar, split screen, stat plot, ...) the pick is left to the OS too.
- **Test**: `python3 trace.py <case>`; names z* (from the grapher), zh* (from home), `menus` (CLEAR, 2nd QUIT,
  Polar fallback, CAS/GRAPH combinations). Letters via ALPHA+key (A math, B apps, C prgm, E sin, F cos, G tan); D
  has no key name in the tester, so arrows + ENTER.

### 2nd CALC on SymCE's big graph (phase 1d, *measured* 2026-10-01, `trace.py calc`)
- The menu stays TI's (menuCurrent 0x11, 7 items). **A CALC pick does NOT reach the menu hook** (neither the
  open nor the pick); the raw key hook `symce_khook` sees digit 1..7 (key codes 0x8F..0x95) or ENTER (item =
  highlighted index at 0xD00826 + 1), only when menuCurrent==0x11 and the GWM marker is armed. `.cpick` asks
  `graphMain` (GWA=0FEh probe) first: 0FFh = fallback conditions, key goes on to TI's CALC.
- Stale hand-off RAM: `symce_khook` `.home` zeroes GWA; CLEAR-to-grapher needs both marker bytes AND a nonzero
  menuCurrent (test `trace.py stale`).
- OS routines (5.8.4): zero 0x6D3C4 (PushReal L,R; GEQNameA 0x91FA6; GRITSolver 0xA77AC; StoX), min/max
  0x6D453.. (DFMin 0x5D688, A=0x6D min / 0x7B max, (D005F9)=0x7B), intersect 0x6D56B.. (tEqu of the two
  functions, ITSolver 0xA7792; busy bit 2,(iy+7) set by 0xA81F5), dy/dx (RclVarSym 0x9AE26, ThreeExec 0x67EBE with
  A=0x25). Traced function number is 0xD01474 (1-based). SymCE calls the same solvers (`symce_calc`) so digits match.
- Effects (TI, copied): value leaves Ans, X typed, Y=f(X). zero/min/max/intersect: Ans=X=result, Y=f(X).
  dy/dx: Ans=derivative, X/Y unchanged. Integral: Ans=integral; **X = pixel (round to nearest of TI's DeltaX grid)
  of the LARGER limit, Y = f(second limit exactly as typed)**; reversed limits give a negative integral, no error.
  CLEAR mid-prompt: X/Y unchanged, Ans unchanged.
- Screens: prompts black at (28,214); results in the function colour; integral line 2 `[lo,hi]` replaces X=/Y=;
  shading = solid fill x-axis..curve; bound markers in function colour (dotted line + 9-row triangle widths
  1,2,3,5,5,5,3,2,1).
- Errors: `symce_throw` calls DrawStatusBar (0x21A3C; the big graph covers it) and, inside a CALC (`cthrow`), restores the
  home cursor (curRow from GWC; the OS text routines moved it) before JError. Harness: the home-screen cursor glyph
  0xE0 in the text shadow is a clock phase, `trace.py` blanks it. The cursor is saved at GRAPH from home (khook `.open`), since no menu hook runs for CALC.
- Known deviation: after a result cxCurApp is 0x40 on SymCE, 0x44 on TI (SymCE's grapher is not an OS context).
- Test: `python3 trace.py calc` (21 cases, TI vs SymCE; PNGs `_png/symce_calc_*.png`). Harness note: tools/emu loads
  its own copies of SYMCE.8xp / SYMCE*.8xv: copy fresh builds there.

### Grapher speed pass after phase 1a (*measured* 2026-09-30, `graph.py time`, +-20 ms)

  | Y= | before 1a | after 1a | now | OS |
  |---|---|---|---|---|
  | `X` | 80 | 460 | 140 | 1864 |
  | `sin(X)` | 320 | 720 | 400 | 2804 |
  | `X^2` | 60 | 260 | 120 | 1624 |
  | `sin(X)/X+cos(3X)` | 640 | 1120 | 720 | 5740 |
  | `sqrt(X)` | 140 | 529 | 230 | 1544 |
  | `sin`,`cos`,`X^2/5` | 800 | 1920 | 920 | 7480 |

  So 6.7-13.5x faster than the OS graph; "before 1a" is not reached: the pixel-identical
  `x0 + s*xspan/318` costs an `fdiv` per column (the old `x0 + s*dx` did not), and the
  3-pixel pen costs more than a vertical fill. Pixels are byte-identical to 9f0cb68 (39
  screens, plus X/Y/Ans). Per X column (cycles, timer 1): 26,700 after 1a, now ~16,900.
- Soft float, cycles each (a loop of 100, minus its load/store): `fadd` ~1300, `fmul` ~800,
  `fdiv` ~2300, int->float ~800; a trivial `run()` ~900.
- **clang folds byte reads of a float into shifts**: `memcpy(b,&r,4)` or a cast to bytes
  becomes `__lshru`/`__ishru`/`__ior`/`__land` calls (~3,000 cycles). A `volatile float`
  copy forces a store and each byte is one load: the row of a float went 3,400 -> 760.
  Same for "is it below 1e30": it turned a byte test into `fabsf` + `__lcmpu`.
- `floor(fl(r + 0.5))` for 0 <= r < 256 is the mantissa's top byte shifted, plus the bit
  below; the float add never rounds across an integer except r = 0x3EFFFFFF (-> 1). Checked
  against `rnd` for every float bit pattern in 1/32..512 and the negatives.
- A big struct offset (`w->y1` at +1280) costs ~6 instructions per float load; copying to
  locals did not help (the frame spills). `line` had 18 `__imulu` calls (clang turns
  `(y<<8)+(y<<6)` back into `y*320`); `step` writes the interior case by pointer.
- An integer `fl(fl(s*span)/318)` (exact on 2.7e9 host cases) was twice as slow as the
  soft-float path: 24-bit `__idivu` costs more than the ROM `fdiv`. Not used.
- Not measured: the 8 KB flash cache (§10) may add layout-dependent misses.
- Timing tool: timer 1 as a cycle counter (clear `0x207` of `0xF20030`, zero `0xF20000`, set
  `0x201`); read the sums from RAM with `screen.read`. Per-curve ms: draw 5 copies of the
  equation, subtract the empty draw, divide (4 ms resolution).

## 12. SymCE history (why things are the way they are)

| Build | What it did | What went wrong |
|---|---|---|
| v1-v4 | Hook body in archived appvar | Reset on Y=/MODE. Blamed on gates; really the validator |
| — | Painted answer with `_PutS` | Lost a repaint fight with the OS |
| — | Rewrote entry to `2X+2X:"4X"` | Echo showed the rewrite; string answer left-aligned |
| v15 | Body copied to RAM `0xD0F000`; answer via `0x91FC2` | **Echo fixed.** Still resets on Y=/MODE/ERR (RAM rejected) |
| `6531b25` | Hook inside the SymCE flash app | **Y=/MODE/ERR fixed** |
| `2fe4016` | Compiled C engine, answer computed at A=0 from `begPC` | MathPrint boxes, fractions, several variables answered |
| `e219b0b` | Refuses to arm on an OS that fails the fingerprints | A wrong `0x91FC2` would crash every ENTER |
| `2b9188f` | ENTER on an empty line (re-run) answered | Showed the OS's `0`: a `cmdVirgin` gate left from the edit-buffer design |
| `0b71f6a` | `Ans` is the last answer shown (`lastAns`) | `4X`, `*3` showed the OS's `0` |
| `divinto()` | Exact division by a polynomial | `(X²-1)/(X-1)` was refused |
| A=2 answer | Answered at A=2 from prog `#`; NZ skips the OS's evaluation | `X²/X` with X = 0 was `ERR:DIVIDE BY 0` |
| installer replaces | Deletes and packs an installed SymCE, then writes the new one | Refused ("already installed"): after a RAM clear users kept an old app with the hook off, `2Z+2X` -> `0` |
| fractions | Values are numerator/denominator; lowest terms proved by `reduce()`; hook passes `(` as glyph `0x28` | `1/X+1/X`, `X/(X+1)` were refused (OS's number, or ERR at X = 0); the first `(` answer drew `√(` |
| 2D answers | In MathPrint, type 0x18 and `/` as `F6` to `0x91FC2` | Answers drew flat: `X/2`, `X^4` with a caret |
| roots | `√(` of one term, rationalized denominators; Classic BC = columns | `√8` was the OS's 2.828427125 |
| commands | `EXPAND( FACTOR( SOLVE( DERIV(`, typed from the ALPHA+DOWN menu; an ERR screen the hook throws when one cannot finish | SymCE could only simplify |

Engine scope (`symce/src/engine.c`): polynomials in up to 6 variables (`A..Z`,
`θ`) with rational coefficients; `+ - * /`, `^`, `²`, `³`, negation,
parentheses, implied multiplication, the n/d bar, `Ans` (§4), negative powers,
decimals. A decimal is exact (`1.5` is 3/2, at most 6 digits in all); an entry
with a decimal point answers in decimals where a coefficient's denominator
divides 10^6 and fractions otherwise (`X/3+.5` -> `X/3+0.5`). An `Ans` real
does not count as decimal: MathPrint may have shown it as `1/4`. MODE `ANSWERS: DEC`
answers every entry as if it had a decimal point.
Every value is a fraction N/D of such polynomials, answered in lowest terms:
`1/X+1/X` is `2/X`, `(X²+5X+6)/(X²-4)` is `(X+3)/(X-2)`, `X^-2` is `1/X²`.
Lowest terms must be *proved* (`reduce()`): take out the common monomial;
then a one-term side shares nothing more; else if a side is in one variable V,
any common factor is in V alone and divides every V-part of the other side, so
Euclid over Q finds it (a side's own monomial factor is set aside first:
`B(X+4)` counts as in X); else if a side is linear in some variable with a
one-term coefficient or a one-term rest (`X+Y`, `XY+1`), it cannot be factored,
so it all cancels or none does; else one side dividing the other; else one
side p homogeneous in two variables U, V (`homgcd()`): every factor of p is
homogeneous in U, V, so the gcd divides each part of the other side of one
degree in U, V and one monomial in the rest; U = 1 gives one-variable
polynomials whose gcd, each term given back its U up to the full degree, is
the gcd (`(X²-Y²)/(X²-2XY+Y²)` -> `(X+Y)/(X-Y)`, `(X²-Y²+X-Y)/(X²-Y²)` ->
`(X+Y+1)/(X+Y)`; no power of U is lost, as p's own monomial is taken out
first). Otherwise the entry is refused, e.g. `(X²+X-Y²+Y)/(X²+2XY+Y²+X+Y)`. Each power under 8 (intermediates too),
numerators and denominators within 999999, entry <= 64 tokens, answer <= 26
tokens (MathPrint: 26 columns, see §7, so `(X+1)^6` fits), at least one
variable. Anything else falls through to the stock OS.
Commands (`command()`): `EXPAND(e)`; `DERIV(e[,V])`, quotient rule on N/D
with X·P′ per variable; `FACTOR(e)`: a number into primes (`2²*3`), else the
content, monomial factors, then per factor (ideas after KhiCAS/Giac, no code):
rational roots p/q screened by (q−p)|f(1) and (q+p)|f(−1); quadratic factors
by Kronecker from f(0), f(1), f(−1) (given up past 10^10); a quadratic left
with a positive non-square discriminant split over √ when the answer fits;
two variables homogeneous through Y=1; otherwise grouping by one variable's
content (`AX+AY+BX+BY` -> `(A+B)(X+Y)`). `SOLVE(A=B,V)`, the `=` and `,V` required (else `ERROR: SYNTAX`
with `SOLVE(A=B,X)` below): degree 1 in V
directly (`X+2Y=3` -> `X=3-2Y`), else FACTOR's factors of degree 1 and 2 in
V, roots sorted by value, `X=r or X=r`. For DERIV, V defaults to X, else
the first variable. Out of reach is an ERR screen, never the OS's number (`FACTOR(X)`
evaluated by the OS would be F·A·C·T·O·R·X).
Checked: 4000 fuzzed entries x4 sections, plus 2000 homogeneous fractions (some
with a factor that is not), each as in Classic and as in MathPrint (half the trees in DEC too), vs sympy,
0 wrong; 2000 commands on random products of factors vs sympy (same value,
every factor over Q found, every real root once in order, the derivative),
0 wrong; 2136 on the real ROM in all four modes, 124 of them error screens,
all equal to the host build. Slowest entry measured on the ROM,
`((X+1)/(X+2))^7`, well under half a second.

Calculus (`calc2()`, `csum()`, `cfmin()`, `carc()`, `clim()`, `cser()`):
functions are kernels (`sin(X)` a variable of its own, its argument kept at
the pool's top), so every command is polynomial code underneath. INTEGRAL:
term by term, by parts for polynomial × sin/cos/e^/ln of a linear argument,
partial fractions over rational roots (`pfrac()`: h = R/D′ at a simple root,
h⁽ᵗ⁾(r)/t! at a multiple one; the ln's first, then the rest, as sympy
writes it). LIMIT and DOMINANTTERM at a point take the first Taylor terms of
N and D there (`tfirst()`); at ±∞ (`1ᴇ99`, §6) the leading terms. SUM with
whole ends ≤ 100 apart goes term by term unless f is a polynomial in V; else
Newton's forward differences Σ Δⁱf(a)·C(n,i+1), plus e^'s as geometric sums;
Karr's convention (backwards is minus the range between) falls out of the
closed form and is copied by the loop. Lessons: a function's value at a point
(`evalat()`) must test the denominator **alone** first, or a hidden 0/0 slips
through (`DERIVAT(X²/tan(X),X,0)` gave 0, now `ERROR: DOMAIN`); the 24-bit `int` makes
C(n,i) overflow once Δⁱf is 0, so the loop stops there (`SUM(X,X,1,1000)`); a
`term_t` built by hand needs `rad = 1` and `mono = 0` (an unset `rad` printed
as `√(0)`, only at -O2). *Measured* (CEmu, 5.8.4): one term of SUM or PRODUCT
by value costs 20-35 ms (100 terms 2-3.5 s), and keys pressed while the
engine runs are lost: `SUM(X,X,1,100)` term by term, then `alpha down right
apps 2 1 / sin ...` 2 s after its ENTER, left the entry `Ans/sin(X),X,3)`
(the menu keys gone). Polynomial SUMs now take the closed form, which is why.

Geometry (`geo()`, DISTANCE .. DILATE, 21 commands, 9.1 KB of code): an
argument is a point `(x,y)` (tried first; on failure the parse backtracks,
`s->depth = 0` included, since `primary` leaves it raised), else an
expression, else `a=b` kept as a-b. Kinds pack 2 bits each into one word, so
a signature is one compare (`PE`, `PPNN`, `0xAAA & mask` for 3-6 points). A
line or circle is its coefficients of 1, X, Y, X², Y² (`gcoef()`: XY, a
higher power, X or Y under a root or in a denominator refuse; the
denominator is dropped). For DISTANCE .. REFLECT a bare f is Y=f. X and Y
are forced into the rank so they are variables, not letters to fold into
coefficients. Two lines meet by Cramer (`gsolve()`: 2 parallel, 3 the same
line); circumcenter and orthocenter are two such solves; the incenter
weights the vertices by the opposite sides, exact roots (`groot()`: an exact
root, or `√(r)` as the engine writes one). ROTATE is in degrees in either
angle mode, whole degrees whose remainder mod 90 is 0, 30, 45 or 60 (exact
cos/sin), else `SYMCE LIMIT`. A geo answer's width is counted in Classic
columns in MathPrint too: `width()` would stack a point's `(3/2,1/2)`.
AREA's shoelace needs the sign, so a symbolic area is `SYMCE LIMIT`.

Roots as kernels (2026-09-27). A term's `rad` holds one var/kernel bit, so
√ of anything but a monomial with one odd variable used to be left to the
OS (√(X+1)) or refused. Now `rootk()` makes the radicand a `K_ROOT` kernel:
U = P primitive and whole (numeric content and a square factor come out:
√(4X+4) = 2√(X+1)), and the term carries U's bit under the root. When P is
S², `kb` = S and it prints `abs(S)`. That is also how `abs(u)` is read, as
√(u²). A power of U becomes P again in every `mulinto()` (`unroot()`), so
√U·√U = P and 1/√U rationalises. A radicand with a root in it stays refused
(`√(√(2)-X)`). DERIV goes through calc() (U′ by the chain rule), since
command()'s polynomial DERIV would take √U for a constant.

INTEGRAL with √(aV+b): `lsub()` substitutes V := (u-b)/a, maps √U's bit to
√V's, integrates in u, and maps it back before V := aV+b. It maps back
because `subst()` refuses √V at a v that is not provably positive. The
constant that V := aV+b leaves (∫1/√(X+1)+X would have a -1/2) is dropped
when the bar is a number.

SOLVE with one root in V (√V, or √U / abs( of a V-kernel), `rsplit()`:
N = A + B√P becomes A² - B²P. Each root r of that is kept only if D(r) ≠ 0,
P(r) ≥ 0, and A(r), B(r) have opposite signs or one is 0 (`rok()`, `nsign()`
proving each sign; unproved means SYMCE LIMIT). A²-B²P = 0 identically
(√(X²)=X, true for X ≥ 0) is SYMCE LIMIT, and so are two roots in V.

X^X as a kernel (2026-09-28). On the plain home entry only (`ps_t.home`), a
power whose exponent holds a variable is a `K_POW` kernel `ka^kb`: X^X, 2^X,
(X+1)^(2X). It multiplies like any kernel (X^X·X^X = (X^X)², X^X/X^X = 1),
so a sum or product of them simplifies; 1^X is 1, 0^X and X^X^X are left to
the OS. `putk()` prints it with `putpar()`: parentheses only around a
base/exponent that is not one number or one letter, and `(X^X)²` when the
kernel itself is raised. Every command refuses it (SYMCE LIMIT): only the
polynomial code and the printer know the kind. MathPrint draws a flat
`X^10Y` or `X^XY^Y` answer with just the digits or the one letter in the
exponent box (*measured*, VRAM), and pasting it back re-runs to itself.

---

## 13. From the CE toolchain docs (read 2026-09-28)

Source: https://ce-programming.github.io/toolchain/. Everything below is
**documented** (from the toolchain's own pages), not measured, unless said
otherwise. The header docs almost never print numeric addresses or bit
positions for `#define`s, only symbolic names — so most of this is
canonical naming to check against our measured bytes, not new addresses.

### Memory map / VRAM color order
**Unresolved.** `ti/screen.h` calls `os_TextFGColor`/`os_TextBGColor`/etc.
"565 BGR color", and `lcddrvce.h` gives `LCD_MADCTL_DEFAULT = LCD_BGR`
("BGR order (swap red and blue)") as the TI-OS panel default; §2 and §11's
`vram.py` call the VRAM bytes RGB565. The docs may name the panel's wiring
rather than the byte layout, and neither they nor our SDK includes list
the OS color values that would settle it. Harmless for SymCE: `vram.py`
only separates dark from light pixels, which an R/B swap doesn't change.
Check before doing real color math on VRAM bytes.

### C ABI
- Args: pushed right-to-left, **always 3 bytes/slot** even for smaller
  types (upper byte(s) are garbage for `char`/`short`). `sp+[0,2]` is the
  return address, args start at `sp+3`. Sizes: `char`=1, `short`=2,
  `int`/pointer=3, `long`/`float`/`double`=4 (`sp+[3,6]`),
  `(u)int48_t`=6, `long long`/`long double`=8 (`sp+[3,10]`).
- Returns by type: `char`->`A`, `short`->`HL`, `int`/pointer->`UHL`,
  `long`/`float`/`double`->`E:UHL`, `long long`/`long double`->
  `BC:UDE:UHL`.
- `IX` and `SP` are the only callee-preserved registers; everything else
  is free to clobber (matches this project's asm, which saves no more).

### OS flags: hook-enable byte member names
`0xD000B4..B6` (§3) are each documented as one 8-member `GROUP`, listed in
bit order (bit 0 = first member); no numeric bit values are ever printed.
- **hookflags4** (`0xD000B6`): trace, **parser**, appChange, catalog1,
  help, cxRedisp, **menu**, silentLink — this order is now **confirmed**:
  it puts parser at bit 1 and menu at bit 6, exactly matching the two
  independently measured bits at line 75.
- **hookflags3** (`0xD000B5`): token, localize, window, graph, yEqu, font,
  regraph, drawing (full 8; line 74 names only 4, unordered).
- **hookflags2** (`0xD000B4`): getCSC, library, homescreen, rawKey,
  catalog2, cursor — only 6 of 8 slots named. Naive positional order
  would put homescreen at bit 2, but line 73 measures
  `homescreenHookActive` at bit 4, so positional inference does **not**
  hold for a partial (non-8/8) group; don't trust it here.
- **hookflags5**: single member `USBActivity` (bit unknown).
- Two whole flag-group bytes are officially reserved for us:
  `OS_FLAGS_ASM1`/`OS_FLAGS_ASM2`, "intentionally unused by TI, available
  for programs" (byte address not given, would need measuring).

### Error handling
`os_ThrowError(uint8_t error)` (noreturn) raises an OS error from the
`OS_E_*` enum (~52 symbolic codes; `OS_E_APPERR1`/`OS_E_APPERR2` are free
for app use). `os_PushErrorHandler(void)` returns twice like `setjmp`: 0
on the first return, the error number on the second (an error thrown
before `os_PopErrorHandler`). No C++ destructor unwinding.

### Timers
(SymCE's grapher does use Timer 1 as a 32 kHz blink/repeat clock: see §11c "Free cursor and TRACE"; it saves and restores the control word.)

Reservations: **Timer 1** = `clock()`/`sleep()`, **Timer 2** = usbdrvce,
**Timer 3** = "used by the TI-OS USB stack and shouldn't be touched in
almost every case... may cause the above functions and/or libraries to
not work correctly." **Tension**: §11's tooling uses Timer 1 as a cycle
counter (line 1098) and Timer 3 as a 32 kHz stopwatch (line 942) directly
— fine for host-side/CEmu scratch tooling that never runs alongside the
OS's own USB stack, but not safe to reuse inside anything that ships.
`timer_Get()` is unsafe when a timer runs at `TIMER_CPU` rate (no atomic
32-bit read on this hardware); `timer_GetSafe(n, dir)` is the safe form.

### Debugging aids (`debug.h`, gated by `make debug`)
`dbg_printf`/`dbg_sprintf` (CEmu console output), `dbg_Debugger()` (drops
into CEmu's debugger), `dbg_WatchpointSet(addr, size, mask)` /
`dbg_WatchpointRemoveAll()`, `dbg_ClearConsole()`. CEmu-only, no effect on
real hardware.

### OS/system info
`os_GetSystemInfo()` fills a `system_info_t`: `hardwareVersion` (7 on
CE/83PCE, 8 on 82AEP), `hardwareType`/`hardwareType2`,
`osMajorVersion`/`osMinorVersion`/`osRevisionVersion`/`osBuildVersion`
(e.g. 5/4/0/34 for "OS 5.4.0.0034"), matching `boot*Version` fields,
`calcid[8]` (from the device certificate), `language` (`EN=0x109`,
`FR=0x10C`, ...). **Bears on open question 3** (whether other 5.8.x
builds keep `0x91FC2`/`0x97C7F` unchanged): this is the documented way to
read the exact OS build directly, instead of only inferring it from
whether the two routine fingerprints still match.

### APD (automatic power-down)
`os_EnableAPD()`/`os_DisableAPD()` toggle it; `os_ApdFlags`, `os_ApdTimer`,
`os_ApdSubTimer` expose its state (no bit/offset numbers given). **Bears
on open questions 5 and 6** (paste-pending window, ALPHA+DOWN menu loop):
`os_DisableAPD()` around either window would remove the "untested whether
APD fires" uncertainty outright; `os_ApdFlags` could also just be read to
check APD's current arm state instead of live-testing on hardware.

### real_t / exact-value types
- `real_t`: `int8_t sign; int8_t exp; uint8_t mant[7]` (9 bytes, BCD
  mantissa, one decimal digit per nibble, 14 digits). Exponent is an
  8-bit field the OS limits to +/-99 — matches this project's own
  `preal()` (`engine.c:923`), which never calls the OS's `os_Real*` API
  and reimplements this format from scratch, portably.
- Native OS "exact answer" VAT types are named `OS_TYPE_REALFRAC`,
  `OS_TYPE_EXACTREALRAD`/`EXACTCPLXRAD`,
  `OS_TYPE_EXACTREALPI(+FRAC)`/`EXACTCPLXPI(+FRAC)`. No numeric type-byte
  values given, so this doesn't confirm §7's measured type `0x18` (the
  OS's own `1/3►Frac` type) — just names to check against if it's ever
  measured which one that byte is.

### Toolchain / build
- `symce/makefile` sets none of `LTO`/`PREFER_OS_LIBC`/`PREFER_OS_CRT`/
  `HAS_MATH_ERRNO`, so the build uses documented defaults: `LTO=YES`,
  `PREFER_OS_LIBC=YES`, `PREFER_OS_CRT=NO`, `HAS_MATH_ERRNO=NO` (`errno`
  not maintained on math functions — irrelevant here, engine.c never
  checks `errno`).
- `printf`/`sprintf` cost ~8 KiB of program size; engine.c/hook.c use
  neither (grep-confirmed), so this is a non-issue, not a fix to make.
- The toolchain assembles with GAS (GNU as); fasmg "is no longer
  supported" for assembly linked with C (the getting-started page still
  has fasmg install steps). Our hand-built flash-app sources
  (`installer.asm`, AsmHook2) are fasmg syntax, fetched by
  `tools/get-fasmg.sh` (§8). The docs also call inline asm "highly
  discouraged" (compiler changes may break it); `symce/src/hook.c` is
  file-scope `__asm__` passed through clang to GAS. Porting it to a plain
  `.s` was weighed 2026-09-28 and skipped (same bytes, token cost > gain);
  do it if a CEdev/clang update ever breaks the hook build.
- Flash is documented read-only at runtime: "the calculator's hardware
  prevents you from writing to flash, and in fact, attempting to do so
  will cause a crash." **Soft tension** with §8 lines 677-678, where the
  installer writes flash successfully via a port-unlock sequence
  (`portUnlock` / `out0 ($24),$8C` / bit 2 of port `$06`) that the
  toolchain docs never mention — not a real contradiction, just
  confirmation that the port trick is undocumented/unofficial.

---

## Open questions

- Exact `lowest_app_addr` on the user's calc (about `0x18403C` in the test ROM).
- Whether `portSetup`'s pattern matches the user's boot code (AsmHook2 works
  there, so probably).
- Whether other 5.8.x builds keep `0x91FC2`/`0x97C7F` unchanged. SymCE refuses
  any OS that fails the fingerprints, maybe more often than it has to.
- Setting the OS's own `Ans` to the answer's numeric value after SymCE
  answers. It would take evaluating the answer, which is what A=2 skips.
- A paste-from-history ENTER leaves `pending = 0xFF` until the next key. Nothing
  seen calls A=2 or A=0 in that window, but APD in it is untested: CEmu's
  autotester never powers down (340 s idle after the paste, then ENTER still
  evaluates; *measured* 2026-09-26), so it needs a real calculator or a way to
  run the APD timer down.
- The ALPHA+DOWN menu's `GetCSC` loop does no APD: a menu left open keeps the
  calculator on until a key. Untested on hardware how much that matters.
- Where MODE `ANSWERS: FRAC-APPROX` lives. MODE-screen probes changed
  `0xD0009A` bit 6 or `0xD0009B` bit 5 depending on the run, and `.25` still
  filed as `0.25`, type 0, in history. SymCE ignores FRAC-APPROX (it only
  changes how the OS shows its own decimals).

**Implementation (SymCE, evo-font branch).** `src/fhook.c` (73 B, slot
`0xD025ED`) and `src/lhook.c` (67 B, slot `0xD02611`) answer from one shared
256-byte `font_map` (0 = not ours, else 1-based record number) plus per-font
record tables (`font_large` 28 B/glyph, `font_small` 25 B/glyph) generated by
`tools/mkfont.py build` from the hand-edited `symce/font/{large,small}.txt`
into `obj/font.inc`. 101 covered display indices: `0x21..0x7E` (`0x5B` is
theta), `0x12` squared, `0xD5` cubed, `0x17` `<=`, `0x18` `!=`, `0x19` `>=`,
`0x1C` arrow, `0xC4` pi. Everything else falls through to TI's glyph. Small
glyphs keep TI's advance width exactly, so no layout moves. The hooks are
position independent: the slot holds the body's base (the OS is calling
through it), and the two `dl` words placed just past the body (map,
records less one record) are relocated by `app_create`, so the bytes run
unmodified in `tools/ez80sim.py`. A hit copies the record to `0xD005A5`
(+ width `0x0C` at `0xD005A4`) / `0xD005C5` (width first) and returns Z with
`HL = 0xD005A1` / `0xD005C5`; a miss returns NZ with everything but A
restored. `ldir`, `mlt de` and `ld hl,(hl)` are used. Toggle: `hookflags3`
enable bits and our pointers only ("on" = our pointer with the bit set);
`prgmSYMCE` and APPS>SymCE arm both, key 1 on the ON screen flips them,
APPS>SymCE OFF clears each only if it points into SymCE. The app grew by
about 6 KB (138,023 to about 144 KB). Licence: glyphs derive from Roboto
(SIL OFL 1.1, `symce/font/OFL.txt`).

## Par/Pol 2nd CALC (SymCE graph, 2026-10-04)

- CALC menus: menuCurrent (0xD00824) 0x11 Func, 0x3A Par, 0x3B Pol. The raw key hook must accept all three; item counts 7 / 4 / 3.
- Handler 0x6D2E9: GEQNameA (0x91FA6), then mode flags (0xD00082 bit 4 Func, 5 Pol, 6 Par); stage code at (0xD0146D) picks the item.
- Derivatives: 0x5E227 d/dX (Func dy/dx), 0x5E231 d/dtheta (Pol dr/dtheta), 0x5E23B d/dT (Par dy/dt, dx/dt), 0x5E25A then 0x7CB69 Pol dy/dx, 0x5E2D6 Par dy/dx (after 0x80177, PushRealO1 0x82A11, res 0,(hl)). All finish with CkOP1Real 0x7F86D, result in OP1.
- Var-name setters: 0x8005F theta, 0x80063 R, 0x80077 X, 0x8007B Y, 0x8007F T. T or theta must be stored before the derivative runs.
- After a CALC result, TI's free cursor leaves T/theta (and R) at the traced value; a redraw steps them to Tmax, so a clone must restore them (graph.c replot).

## Seq graph mode (SymCE graph, 2026-10-05)

- Sequence terms: `0xD01D0F + 18*i` (u,v,w). First 9 bytes = current term, next 9 = previous. Type byte `&0x1F` must be 0 (real) or the term is bad.
- Window: nMin `0xD01EC3`, nMax `0xD01EA8`, PlotStart `0xD01E9F`, PlotStep `0xD01EE7`. seqFlags `0xD0008F`: bit0 Web, bit2 uv, bit3 vw, bit4 uw. Line style bytes `0xD024D5+i` (6 = dot). Colour bytes `0xD024EE..F0`.
- `0xD02032` = u/v/w selected bits. `0xD02033` = recursion depth (0 explicit, 1 u(n-1), 2 u(n-2)). n variable at `0xD02034`.
- OS calls: flags `0x285AA` then SeqSet `0x7D633`; SeqSolve `0x7D683` advances to n. Restart (SeqSet) for any lower n. Returns OS error code.
- Equation tokens: u( = `5E 80`, v = `5E 81`, w = `5E 82`, "(" separate `10`, n = `62 21`. `u(n-1)` = `5E 80 10 62 21 71 31 11`. Key "2nd 7" inserts only the `u` token.
- y= editor rows per sequence on CE: u(n), u(1), u(2), v(n), v(1), v(2), w(n)...
- Plotted n = nMin + PlotStart - 1 + k*PlotStep up to nMax. Trace grid identical. After draw: X = last n, Y = its term (last selected sequence). After trace + CLEAR, X/Y keep trace values.
- Trace bar: "n=" over "X=n  Y=term"; label "u=" etc. Typed n: ERR:INVALID (0x8F) first when outside [start, nMax], then DOMAIN (0x84) for non-integer (compare with ~1e-5 tolerance).
- ZStandard and Q1 reset nMin,nMax,PlotStart,PlotStep to 1,10,1,1. Other zooms keep them. ZoomFit stays with TI.
- Seq 2nd CALC menu id `0x3C`, one item (value). After value, up/down keep tracing (swap sequence); left/right give a free cursor.
- Term error (e.g. DIVIDE BY 0): earlier points drawn, X/Y stored, then OS error screen.
- Hang: SymCE calling SeqSolve for u(n-2) hung at n=3 (cause unknown, call pattern differs from TI's). SymCE only takes exact `u(n-1)`; u(n-2), Web, uv/vw/uw, non-dot line styles fall to TI.

## Stat plots (SymCE grapher, graph_stat.c)

- Plot records: 3 x 18 bytes at 0xD0227E (plot n at +18n). Byte 0: low nibble type (0 Scatter, 1 xyLine, 2 ModBox, 3 Hist, 4 Box, 5 NormProb), high nibble mark (0 box, 1 cross, 2 dot). Bytes 1..5 X list name, 6..10 Y list name, 11..15 Freq list name (L1..L6 = 00 00 00 00 00 .. 05; a literal Freq 1 is '1'). Byte 16 bit 0 = plot on. Byte 17 = colour index as Y1's.
- List lookup: OP1 (0xD005F8) = 01 (ListObj), 5D, then the 5 name bytes at OP1+2; call ChkFindSym 0x2050C; carry = not found, DE = data (finds archived lists too). Data = count word, then 9-byte reals.
- ZoomStat = zoom menu key 0x68, SymCE action 9; call OS ZmStats 0x20B28 so the window bytes match TI.
- TI maps a point to grid gx = round((x-Xmin)/DX), gy = round((y-Ymin)/DY) and draws at screen pixel (27+gx, 210-gy); only 0<=gx<=264, 0<=gy<=164 get a mark. Marks are 7x7: box = 2 px border, hollow centre; cross = 3x3 block with arms of 3; dot = 3x3. Plots draw before functions, plot 1 first.
- Plot colour is pen colour; xyLine joins points with the thick pen in the plot colour, marks on top.
- TRACE with a plot on is left to TI (graph_fall); mismatched list counts, undefined lists, complex lists and ZoomStat on a list of equal values (ERR:WINDOW RANGE) also fall to TI.
- Histogram: bins of Xscl from Xmin, [left, right), weighted by Freq. A non-empty bin is a hollow rectangle drawn with the 2 px pen (pen extends right and down from the line) from the baseline y=0 to its count; the full-width baseline at y=0 is drawn in the plot colour. Empty bins draw nothing; everything is clipped at the grid.
- Box (type 4) and ModBox (type 2): fixed rows whatever the window or plot number: box top rows 58-59, bottom 96-97 (grid y 152 and 114), whisker line rows 77-78 (grid y 133), caps rows 75-80, all 2 px pen at x = 27+gx..+1; median is a full-height vertical. Quartiles are 1-Var Stats': Q1/Q3 = medians of the lower/upper halves, the middle value left out when the count is odd (n=1: all five equal); Freq repeats a value. Box whiskers run to min and max; ModBox to the last values within Q1-1.5*IQR .. Q3+1.5*IQR (a value exactly on the fence is still inside); the others get the plot's mark (box/cross/dot) centred on grid y 133. Lines are clipped at the grid; a vertical at gx = -1 draws nothing (no pen reaches column 0). A list of equal values or a 1-value list gives ZoomStat a zero width window: ERR:WINDOW RANGE.
- NormProb (type 5): sorted values against z = invNorm((i - 0.5) / n), i = 1..n (not the manual's (i-.375)/(n+.25)); byte 16 bit 4 = Data Axis Y (values on Y, z on X), else values on X. Marked like a scatter. ZoomStat (the OS) gives X = data range +-10%, Y = about +-1.34 * z(n) (e.g. +-2.265432983 for n = 11). TRACE shows X= and Y=, with Y to 7 digits (the OS invNorm is good to about 1e-7, e.g. Y=-1.690622).
- TRACE over plots (SymCE does Scatter/xyLine): the label is the small font at the top left in the plot's colour ("Plot1:L1,L2", "Plot2:L1", "Plot3:L1"), the readout at the bottom ("X=3" left and "Y=2" from x ~163; Box shows "Med=4" etc.); up/down cycles through the plots then the functions.
  Measured on TI (Scatter, ZoomStat, 5-point lists), for graph.c: TRACE starts at the lowest plot that is on, point 1. LEFT/RIGHT walk and clamp at the ends (no wrap), and the window never pans (an off-grid point shows X=/Y= and no cursor). UP/DOWN ring = plots that are on, then the selected functions, with wrap (UP from Plot1 goes to the last item). Plot to plot keeps the point index, clamped to the shorter list, and the clamped index stays; the index also survives a visit to a function (a function is entered at the centre column). ENTER = QuickZoom: window recentred on the point, ZPrevious saved, trace restarts at point 1 with the cursor at its place in the new window. A digit leaves to HOME and is typed there. CLEAR gives the plain graph (a following arrow gives a free cursor at an odd place, X=1.0181818 Y=1.9868293, not understood). 2nd CALC opens a garbage CALCULATE menu (7 items '3root('). TI quirk: DOWN/UP into Plot3, or RIGHT past the end then UP/DOWN, shows garbage ('Plot3:L4,L2' with stale X/Y); SymCE does not copy it and the tests avoid it. TI also leaves black outlines on visited points. Wait about 700 ms after TRACE before the first arrow or TI loses it.
- HOME history after a ZOOM-menu pick (SymCE, 2026-10): GRAPH then CLEAR / 2nd QUIT / a digit from plot trace keeps TI's HOME history (0 px differ vs TI). But a ZOOM pick from HOME (e.g. ZoomStat) followed by CLEAR or 2nd QUIT leaves HOME with blank history above the entry line (4417 px differ). Pre-existing fault of the ZOOM-pick path in main.asm, not plot trace; cause not found (skipping the NewContext0(kQuit) there did not fix it).
- Editor driving: Hist has X list + Freq; ModBox X list + Freq + mark; Box X list + Freq; NormProb List + Data Axis + Mark. Each plot can be configured once per boot with the harness.

## 2nd DRAW on the SymCE graph (2026-10-04)

- DRAW menu: menuCurrent (0xD00824) = 5; tab at 0xD00825 (0 DRAW, 1 POINTS, 2 STO), highlighted item 0-based at 0xD00826. Digits 1..9,0 (item 10) are keys k1..k9,k0 (0x8F..0x97, 0x8E). The menu reopens at the DRAW tab. From home every pick pastes; only the grapher can run them, so the raw key hook must intercept a pick when the grapher armed its menu mark.
- The OS draws on a 265x165 logical grid in plotSScreen (0xD09466): 165 rows x 133 bytes, 4 bpp, high nibble = even x, row 0 = top (row = 164 - gy). The buffer holds the whole graph picture (functions too); TI's erase writes nibble 0. The low nibble of byte 132 per row is unused padding: SymCE keeps its signature there.
- Mark shapes (cursor centre c): Pt-On / Pt-Off = a 7x7 ring 2 px thick (inner 3x3 untouched); Pt-Change = 3x3 block, each pixel flipped (on if off). Line/Horizontal/Vertical are ILine (ROM 0x7B2F5) and thick IPoint (0x7B501): Bresenham with e = 2*minor - major, the major axis steps every point, the point plus neighbours relative to the previous one.
- Pt/Line cursor: the free cursor restarts at the last position of the previous DRAW session; a ZOOM resets it. Line( goes back to stage 0 after the second point.
- Persistence: ring survives CLEAR, 2nd QUIT and GRAPH again, and a Y= edit that adds a function. Cleared by ClrDraw, any ZOOM except ZoomSto, a window change.
- grfModeFlags (0xD00082) low bits change on a quit to home (0x12 -> 0x10): hash only the 0xF0 mode bits when deciding whether the graph is the same.
- Emulator pitfalls: TI redraws a Horizontal/Vertical preview slowly and drops keys pressed meanwhile (pause every 8 cursor moves); a SymCE render with a pixel-by-pixel layer overlay of 70k pixels takes seconds in flash, so fill by logical cell and skip zero bytes.

### TI drawings vs SymCE's layer (2026-10-05)
- Dirty flag 0xD00083 bit 0: set = graph needs replot. TI's replot (home Line(, Circle(, Pt-On( ...) clears it and wipes the pad nibbles of plotSScreen (our signature).
- SymCE marks plotSScreen magic A5C4 + window hash when it sees TI's drawing (dirty clear, no ours-signature): graph_fall hands the plain GRAPH key to TI until the window changes. Sticky until window change or ZOOM.
- Known gap: a Y= edit between a home draw and GRAPH sets dirty, so the drawing is treated as fresh and lost (TI would replot too).

### Y= edits and the drawing (*measured* on TI, 2026-10-05)
- After a plot with a ring drawn: adding an equation to an empty slot and a Y= visit with no edit keep the drawing; changing, appending to, clearing, deselecting an equation TI had plotted drop it (a toggle off then on still drops it). Same after a home Line(: Y= change drops it, Y= add keeps it and plots the new function over it.
- The OS's record: `0xD00097` (iy+0x17) bit 0 = an equation was added, bit 1 = one was changed/deleted/toggled; TI's plot clears both. Right after typing in Y1 before any plot it is 2. It works only for equations TI's own plot drew: after a SymCE render the same edits set bit 0 (also with the dirty flag clear and with 0xD024BE, 0xD01D49.. copied from TI's state), so SymCE cannot read it and keeps its own 8 bit signature per equation slot (pad nibbles 19..38 of the layer).
- Site of the logic in the ROM: 0x7F1DE (selection toggle: on sets bit 0 unless bit 1, off sets bit 1 unless the type byte's bit 3 is set), 0x60CFD reads the equation VAT pointer at 0xD0244E (a VAT entry address: `dec hl` = flag byte, selected = bit 0).
- Y= opens on the equation row it was left on by TI's graph, but at Y1 after SymCE's.

### DRAW menu measurements, not shipped (2026-10-05)
Measured on TI (ZStandard, layer diff via tools/emu drawm scenarios): Circle( from the graph is a 2-px ring, 22 px across for a 10-step radius (center 132/133, 81/82), not symmetric (right side one column wider), so it is a polyline of thick IPoint/ILine segments (ROM 0xAFC60..0xAFD96 area calls ILine 0x7B2F5 and thick IPoint 0x7B501 in a loop), not a midpoint circle; exact step count not recovered. Tangent( at x=0 on X^2 draws a 260-px 1-px line limited to the visible window. Pen draws a 1-px line along the cursor moves. Circle(, Tangent(, Pen are SymCE's now; Text( and the STO tab stay with the OS. Home draw then Y= edit then GRAPH, and home ClrDraw then GRAPH, give SymCE's plain graph (trace.py homedraw hclr/hedit/hadd).
The e2e `5->X stored -> SOLVE` FAIL predates the graph work: it fails identically on a 669c761 build (second SOLVE types `Y` then `►Frac` for the `2nd math 1` pick).


## + on a menu item (SymCE CATALOG HELP, 2026-10-06)
- Menu hook event 3 is NOT delivered for + or arrows; it is called (0x86098, in 0x86089) only when an item is picked, with the item's key code (0x21 for SymCE items).
- OS + in a menu: 0x85758 -> 0x51B74 (accepts kAdd 0x80 or 0xCB) -> 0x4EA70 (reads menuSelected 0xD00826, calls 0x86797) -> 0x51BC3, which asks localize event C9 (BC = 0x0021 for SymCE items, hence TI's wrong help).
- The raw key hook (0xD025DB, hookflags2 iy+0x34 bit 5) is called once per key in the monitor loop 0x8C482 including while menus are open; kAdd = 0x80 is what it sees for +. `set 1,(iy+40)` swallows the key. SymCE uses it (khook, or khook_cas when GRAPH key is TI's and only the +).
- Menu RAM: menuCurrent 0xD00824 = 9 for TEST/2nd MATH, [1] = tab (3 Alg, 4 Calc, 5 Geo), [2] = selected 0-based. cxCurApp 0xD007E0 = 0x40 at home.
- GetCSC: ENTER 09, CLEAR 0F, ALPHA 30, 2nd 36, MODE 37 (2nd QUIT), ADD 0A. Font: '[' is glyph 0xC1 (0x5B is theta), ']' 0x5D.

## SymCE error screens (2026-10-06)
- Error 0x2B (appErr1 = title, then the lines): the OS draws `ERROR: <appErr1>` and `1:Quit`, then the description lines from the text after the title's NUL, at rows 4..9 (`0x62210`, loop `0x622FD`, copied to 0xD00842 and printed with 0xA1E5B, which wraps at 26 columns: a 26-column line gets a blank row after it). **If the first description byte is 0 the OS skips the lines and the 0xD3 localize call**, so an empty detail has to be a space (hook.c .Lerr does that).
- Localize event 0xD3 at `0x622F7`: NZ with HL = another list of NUL-terminated lines (ended by a second NUL) makes the OS draw those. lhook.c answers with the lines menu_err (menu.c) builds at 0xD0EF00 when errNo & 0x7F == 0x2B.
- `2:Goto` is the high bit of the error number: 0xAB shows the same screen with `2:Goto`; Goto puts the entry back on the home screen with the cursor at `errOffset` (below).
- **Goto's cursor spot = `errOffset` (0xD008E3, 3 bytes), a token offset from the start of the entry** (*measured*, 2026-10-06). The OS's own `1+*2` gives ERR:SYNTAX (errNo 0x88, `*` is token 2), `errOffset` = 2, and Goto leaves the cursor at `editTop + 2` (`editCursor - editTop`); in MathPrint the flat entry is the same, only `editTop` moves. `_JError` does not touch `errOffset`, so SymCE writes it before the throw (hook.c `.Lgoto`). Goto starts in overwrite: the OS clears `textFlags` bit 4 (iy+5) during Goto, so setting it before the throw is lost; the home hook is not called again until a key, so the cursor draws as a block until the first key. hook.c sets `gotoFix` (0xD0EE07 = 0xA5) at the throw and the next A=1 on the home screen takes a new baseline (editTop) as for a new line, so insert mode (SymCE's INSERT cursor) is back on that first key.
- Inside a MathPrint box (fraction, `^`, root) the buffer holds 2D markers (`EF 20 1C 00 EF 2D ..`), so a flat token offset does not map. The OS's own errors there put the cursor in the first box whatever `errOffset` is (`(1+*2)/3` gives 1, `2+(1+*2)/3` gives 4, same spot). SymCE's `SOLVE(1/X)` (offset 16) lands at buffer byte 12, inside the template; `SOLVE(X^2-4)` in MathPrint (X^2 is a box) reports 0 and lands at the start. Flat MathPrint entries are exact.
- The engine reports the offset at `out[MAXOUT]` (= `ansBuf + 64`): where the parse stopped (`ps_t.i`, which at `goto syntax` is the failing token) plus the command-name prefix, or 0 (the command token) when the parse had consumed the whole argument list. Measured: `SOLVE(X-4)` 9 (the `)`), `POLYGCD(X)` 9, Classic `SOLVE(X^2-4)` 11. `FACTOR(,X)` is not SYNTAX/ARGUMENT in SymCE (error 0x2B, no Goto).
- With the Evo font off the localize hook is not armed: hook.c arms it (pointer and hookflags3 bit 1) and sets tempArm (0xD0EE06); lhook's 0xD3 clears both and tempArm. While tempArm is set lhook leaves the glyph events alone.

## + in the ALPHA+DOWN popup (2026-10-06)
- The popup reads keys with GetCSC itself (menu.c `popup`), so the raw key hook is not involved: `+` is GetCSC 0x0A there, handled in the popup's key loop. On a leaf item with an `H` entry it leaves the loop, restores the popup's saved pixels, runs `help()` (which saves/restores the same SAVE buffer, so the popup must be restored first and saved again afterwards), then CLEAR/ALPHA re-saves and redraws the popup on the same `sel`; ENTER returns the item as a pick; 2nd QUIT closes. Items that open a submenu, or have no entry, ignore `+`.
- `help()` and `popup()` both use `SAVE+12` for their 2-bit pixel codes, so they cannot be open at once.


## SOLVE inequalities and PARTFRAC (2026-10-07)
- Test-menu glyphs in textShadow (*measured*, Classic home screen): `<` 0x6B and `>` 0x6C draw as their ASCII characters; `≤` reads back 0x17, `≠` 0x18 (and `≥` 0x19). 2nd MATH 1..6 types `=` `≠` `>` `≥` `<` `≤`. The tokens 0x6A..0x6F pass through the OS result routine unchanged, like the other operators.
- The same large-font codes hold for `_VPutS` with fracDrawLFont (SymCE help/popup text): write ≤ ≥ ≠ as `\x17 \x19 \x18`. `\xC9 \xCA \xCB` there draw φ Ω x̄ (the solve( help bug, fixed 2026-11-03).
- engine.c: SOLVE reads the test token after the left side (`op`), CSOLVE and the rest need `=`. `ineq()` factors numerator and denominator (fl.split=2), sorts the linear roots with approx(), and walks the atoms (region, root, region...) with the sign at +infinity; a factor of degree >= 2 must have no real root (noroot()), else SYMCE LIMIT. Chained `lo<X<hi` form chosen (Nspire style, fits the 26 columns better than `lo<X and X<hi`). width(): 0x6A..0x6F split segments in MathPrint like `=`.
- PARTFRAC does not exist in EXPAND (`EXPAND(1/(X²-1))` stays as is). `apart()`: whole(p) division, factor D with fac(), unknown numerators solved by fraction-free (Bareiss) Gauss-Jordan over int64 (`pfsolve`, up to 8 degrees, entries below 2^31, else SYMCE LIMIT). Terms in FACTOR's order, lower powers first. Classic answers wider than 26 columns give TOO WIDE (`1/((X-1)²(X+2))` needs MathPrint).
- Emulator (tools/emu/ineq.py): the 11 Classic entries above read back exactly; MathPrint answers are drawn to VRAM (only "drawn, no error" is checked). In MathPrint `X^2` opens an exponent box: leave it with RIGHT before typing `=`, a test or `,`, or the entry is a syntax error (err 43 with cx 82).
- Inequalities with abs: `absfix()` rewrites `A+B*abs(S)` (A, B numbers, one abs kernel, K_ROOT with kb set) to `B(U-c^2)` over D, with U = S^2 the kernel's argument and c = -A/B, or to a constant `B` when c < 0, then ineq() runs unchanged. The term's `rad` field carries the kernel bit together with the numeric part's 1 (0x1000001), so test `rad & kernelbit`, not equality. tools/engine_device.py needed PART 25000 (not 40000): the engine plus its vectors no longer fit in RAM as one program.

## SOLVE systems (2026-10-07)
- `SOLVE({eq,..},{var,..})` / `CSOLVE`: the entry starts with `{` (0x08); the engine's `systems()` handles it. Answer tokens: ` and ` is one token 0x40 (`T_AND`, 5 columns in Classic, a 5-wide separator in MathPrint like ` or `), ` or ` 0x3C, output comma 0x2C. The answer is the history record text (0xD008F2): `SOLVE({X+Y=3,X-Y=1},{X,Y})` records `58 6A 32 40 59 6A 31` (`X=2 and Y=1`), Classic screen shows it right-aligned.
- Entry limit: a system may have 128 tokens (`2*MAXLEN`); other commands stay at `MAXLEN` 64. The hook reads the entry from the temp program `#`, so only the engine limits it.
- Stored values: `ps_t.sv` became the bitmask `sm`, every letter in the `{vars}` list is an unknown, the others use `look()` as before.
- Linear: Gauss-Jordan on `term_t` rows in one pool slot; 2..6 unknowns; free variables print as `X=(3-Y)/2`. Nonlinear: 2 equations, 2 unknowns, one linear in an unknown: solve it (`root`), `subst` into the other, `fac`; the pairs are sorted by the first unknown. An answer wider than 26 columns with ` and ` is redone as `{X=..,Y=..},{..}`; still wider is TOO WIDE. Anything else beyond linear is SYMCE LIMIT.
- Measured with systems in: app 252,156 B image (254,514 B with appvars), engine 110,950 B; engine_device stack peak 1361 B; engine_device needed `PART = 30000` (40000 vectors + the bigger engine no longer started: report magic 0).
- RAM of the 128-token system entry (checked 2026-10-07, host): there is no separate entry buffer. `symce_engine(const uint8_t *in, ...)` is handed `data+2` of prog `#` (the OS's temp program in user RAM, found with ChkFindSym) and only reads it, so the 128 tokens live in the OS's own variable, which already holds any length the user typed. The raised limit is a compare in engine.c, not memory. New engine state is C locals on the stack (rat_t pointers, one `mat_fits`-asserted matrix inside one pool slot); the pool is still `NPOOL * sizeof(rat_t)` in engineWork `0xD0EF00..` (asserted `<= 0x4F00`, ends before `0xD13E00`), unchanged. Nothing new outside SymCE's `0xD0EE00..0xD13E00` scratch.

## DESOLVE (SymCE ODE solver, 2026-10-07)
- Tokens (*measured*): prime `'` is 0xAE (2nd ANGLE 2); ` and ` is 0x40 (2nd MATH LOGIC 1). With the SymCE tabs the 2nd MATH menu opens on TEST, so `2nd math right 1` is `and`; a key typed straight after a menu pick can be dropped (the emulator test waits 400 ms, `w400`, after each pick).
- Design: engine.c `desolve()` is a token-string driver on top of `engine()`: it builds expressions (`q()`, printf-like) and runs `engine()` on them under hook mode bit 0x40 `MODE_EVAL` (0x01 DEC, 0x04 DEG, 0x20 MP are the others). EVAL lets plain arithmetic through instead of returning 0 ("leave it to the OS"), allows 250 input tokens, and sets `fn = C_TOLN` for plain expressions. Driver strings live on the stack (`SYMCE_WORK` is about 17 KB and what follows it is not free).
- Engine facts found: the variable letters plus the kernels (sin, cos, tan, e^, ln, ln|u|, e, root, pow) take one nibble each of the packed monomial, so letters + kernels must be 6 or fewer (a solution with X, Y, C, D, e^(aX), e^(bX) is 6; more fails, so `dscomb` assembles `C g1 + D g2 + yp` by hand, only with no initial conditions). The engine never merges `e^(-X)e^(X)`. INTEGRAL refuses `e^(X²)`-type integrals and accepts negative linear rates (unary minus is T_NEG 0xB0, not 0x71: the engine returns LIMIT for a binary minus with nothing before it). SOLVE cannot do `Y²=X²+4` symbolically, so the separable path tries the quadratic root itself. A depth scan over tokens must skip two-byte T_BB tokens: BF in `cos(` (BB BF) looks like T_EXP.
- 2nd MATH tab table (`tabs[]` in menu.c): the 2nd item count byte is per tab (TEST 6, LOGIC 4, CONDITIONS 16, Alg 10, Calc 15, Geo 13) and every item has its own key code pair `n, 0x21`; adding a menu item means raising the count and adding its pair (the ALPHA+DOWN popup reads M and needs nothing else). Missing this left the item out of 2nd MATH only.
- Classic home screen shows e as glyph 0xDB, the raised 2 as 0x12 and the root as `10 28` in textShadow; `Y=` is dropped when the answer is past 26 columns.

## Implicit equations and inequalities (SymCE grapher, 2026-10-07)

- *Measured* on TI (OS 5.8.4, `tools/emu/implicit.py` probes): the Y= editor takes any of them (`Y1=X^2+Y^2=25`, `Y1=Y`, `Y1=X+Y`, `Y1=Y>X^2`, with ALPHA 1 = Y), but TI's graph says ERR:INVALID (1:Quit 2:Goto) for any Yn that contains the letter Y. So SymCE replaces no TI picture.
- SymCE: in Function mode a Yn with a Y that is one relation (`=` `<` `>` `<=` `>=`) or such relations joined by `and` / `or` is drawn implicitly (`imptype`, `idraw`, `icell` in graph.c). `!=`, `xor`, `not(` or an `=` beside `and`/`or` are not (they plot the 0/1 value with Y as stored, as before). Recompiled with `w->yop`: Y is op O_Y (value `w->oy`), each relation is left-right (right-left for < <=) so f >= 0 where it holds, and/or are O_MIN/O_MAX.
- f runs at the nodes of a 40 x 28 grid (cells about 8 px; 100 ms of float32 per 1000 nodes is not available: 4455 nodes of an 80 x 55 grid took 1.55 s of evaluation and 1.25 s of cell work, 2.8 s in all, measured with timer 1; 8 px cells brought a circle to 0.6 s). Node classes (NaN, <0, >=0) are taken off the float's bytes. Marching squares draws f = 0 from the linear crossing on each cell edge (shared edges give the same point: continuous, one 2 px / 1 px pen like other curves; saddles by the sign of the centre). Hyperbolas and X*Y=1 give every branch. Features under about 8 px (a circle of radius 3 px) are lost; at the kink where an `and` / `or` joins two curves a cell cuts the corner by up to 4 px.
- Inequalities tint the region: 1/4 of the pen colour added to 3/4 of what is there (`d - ((d >> 2) & 0x39E7) + q`), so overlaps, the axes and other curves show through; the cell's boundary pixels by the bilinear f, a row at a time. `<=` `>=`: solid boundary; any `<` `>`: dashed (5 x 4 pixel checkerboard by segment midpoint). The implicit equations draw first, under the normal curves.
- TRACE and CALC skip them (`lenok`/`have` use `w->imp`, a bitmask set by render); an implicit equation does not set the last-function Y either. ON stops a slow draw (row granularity).
- The row buffers (648 B) share the union with `sv` (the pixels under the cursor, dead while render runs): `gw` stays under 0xC00.
- Timing (`implicit.py time`, +-80 ms, GRAPH to fully drawn): circle 620 ms, X^2+Y^2<=9 820, Y>X^2 660, sin plus the disk plus a parabola region 3040. Tests: `tools/emu/implicit.py` (circle hyper parab disk mixed trace time, PNGs `imp_*.png`) and the host `implicit_cli` cases in `graph_check.py` (in `make check`).



### DRAW Circle( and Tangent( on TI, measured (2026-10-07); Circle( is now SymCE's
- Circle(cx,cy,r) from home: 60 chords, k=0..60, angle 2*pi*k/60 counter-clockwise from 0, each drawn with ILine (2 px thick, nibble 1). Vertex = round_half_up(((cx+r*cos+10)*13.2, (cy+r*sin+10)*8.2)) on the 265x165 grid, exact decimal ties go up. Clipping is Liang-Barsky in floats of the unrounded polyline against [0,264]x[0,164], then the clipped endpoints are rounded; chords wholly outside are dropped. ZSquare/ZDecimal fit with their own scales.
- Circle( from the DRAW menu: first ENTER takes the cursor as centre (it keeps a stamp of cursor frame 1, VRAM only, until the last ENTER), moving the cursor shows nothing (no rubber band), second ENTER draws with r = hypot(dgx,dgy) in grid pixels (round circle), vertex ties round half-down, same 60 chords and float clip. r = 0 gives a thick dot. Cursor clamps at 0..264, 0..164.
- SymCE: dcirc() in graph_draw.c (integer cos table, 1e9 scale); 6 cases in tools/emu/_draw.json (circA..circBot) equal TI's layer.
- Tangent( is now SymCE's (dtang/dtent in graph_draw.c; Function mode, no stat plot, a function on; else TI's). Measured rule, pixel exact (0 mismatches on X^2 at X = 0 1 -3 0.5 4 -0.1 7 -7 0.1 2.5, 9 plain lines, and sin at 1.5): TI plots the line y = m X + b like a function, in 14 digit BCD, only at the even columns k = 0, 2 .. 264 (133 points): X_k = Xmin + (k/2) (2 DeltaX), y_k = m X_k + b, g_k = (y_k - Ymin) (164/(Ymax - Ymin)), row = round half away from zero of g_k (a BCD tie matters: round14(DeltaX k) was wrong at k = 110); consecutive points are joined by the thick ILine (the same pen as dline), one segment each; a segment with an end row outside 0..164 is cut as unrounded reals by Liang-Barsky against [0,264]x[0,164] and the cut ends rounded (a tie either way), wholly outside dropped. m is nDeriv at X (the OS routine, symce_calc kind 5), b = Y - m X. Layer nibble = the function's colour index (Y1 1, Y2 2). Other windows measured (ZDecimal, ZSquare, ZTrig, custom Xmin -3.3 Xmax 7.1 Ymin -2.7 Ymax 5.9, where 164/8.6 does not terminate): equal with K = 164/(Ymax-Ymin) by BCD division, so that is TI's rule. Untested: whether TI uses the exact m and b or the digits of the readout.
- Tangent( UI on TI: DRAW 5 opens a TRACE-like screen (header DRAW TANGENT, label Yn=expr top left in the function colour, X= Y= in that colour and a MENU box at the bottom right), starts at X=0 on the lowest function; arrows trace (2 px), UP/DOWN change function, a digit opens the X= box and its ENTER draws at once (no second ENTER). ENTER draws (about 4 s) in the function colour and shows two lines bottom left: X=2 and y=4X+-4 (slope, X+, intercept with the raised minus); the cursor stays on the point. An undefined Y (1/X at 0): ENTER does nothing. sqrt-like slope error (X^.5 at 0): ERROR: NONREAL ANSWERS from nDeriv, as CALC dy/dx. After the draw an arrow gives the free cursor, ENTER or CLEAR hides it (plain graph), and the next arrow continues from the free cursor's last position (SymCE matches: nibble 40 of the layer signature remembers the session, dr_home resumes the kept spot, DRAW TANGENT header shown by ttit()). Edge: the Tangent( trace NEVER pans; RIGHT keeps stepping past Xmax (72 presses from 0: X=10.909091 Y=119.00826, 150: X=22.727273), no limit, the cursor just disappears off the picture; SymCE matches (tmove skips tpan, tshow hides the cursor outside gx 0..264).
- Tests: tools/emu/_draw.json tanX0 tanX1 tanXm3 tanX05 tanX7 tanSin tanXm01 tanCube0 tanCube2 (SymCE's pixels under TI's own curve are ignored: TI's plot is in the layer, SymCE's is not), and a Y2 check (nibble 2).

## Matrices and vectors (SymCE matrix(), 2026-10-07, measured on 5.8.4)
- Keys: `[` is 2nd `×` and `]` is 2nd `-` (CEmu key names `2nd *`, `2nd -`); 2nd `(` `)` are `{` `}`. Tokens `[` = 0x06, `]` = 0x07 (with the input comma 0x2B between entries). In textShadow (Classic) `[` reads back as glyph 0xC1 and `]` as 0x5D; the answer's comma is the 0x2C glyph (T_LCOMMA) and a minus sign the 0x1A glyph as for any answer.
- Prog `#` holds the typed matrix flat, `[[1,2][3,4]]` is 06 06 31 2B 32 07 06 33 2B 34 07 07, in Classic and MathPrint alike: the OS makes no template box for `[` `]` (unlike a fraction or `^`), so the engine reads it like any other entry. Letters, `+ - * /` and powers are ordinary tokens inside.
- MathPrint draws the entry AND an answer made of `[[..][..]]` tokens as a grid with big brackets, and a `/` between digits inside a cell as a stacked fraction; the factored `1/(A²-2)*[[..]]` is a stacked fraction, `*`, then the grid. A vector answer `[a,b,c]` (one bracket pair) is drawn flat, as text, in both modes. No change was needed in 0x91FC2's glyph pass: `[` `]` pass through unchanged.
- Measured grid widths (large font columns of 12 px): 2x2 `[[-2,1][3/2,-1/2]]` about 74 px; 3x3 of stacked thirds about 100 px (8 columns) where the same text is 38 columns in Classic. engine.c mpwidth() models it as 2 for the brackets plus, per column, the widest cell (width() in MathPrint) plus 1, never fewer than drawn; a prefix before `[[` is width() of it. A 4x4 inverse with a 3-digit determinant is still TOO WIDE (the engine's out buffer, not the screen).
- Pool: cells live in the `work` pool one slot (about 722 B) each, 20 at most (MAXM: 4x4, 3x5, 4x5), so a 4x4 determinant needs 16 cells + scratch + one `tmp` per cofactor level; mdet() allocates `tmp` only while rows remain (the first version ran out of slots at 4x4).
- Errors: `SINGULAR MAT`, `INVALID DIM`, `DIM MISMATCH` are error 0x2B screens with menu.c `S` sentences; the singular case shows errNo 0xAB in the emulator (a Goto offered), the others 0x2B.
- Menu: `Matrix >` is Algebra's 11th item (submenu id 13), 2nd MATH key prefix 0x0A, tabs[] count 10 to 11; the popup's LEFT/CLEAR parent lookup needs `m == 13` as Algebra (ids are not contiguous by tab). App size with the Matrix menu, 7 commands and 7 help entries: 251,208 B (was 241,515 B).
- Tests: engine_check.py section 11 (exact cases and random numeric/symbolic matrices against sympy), tools/emu/matrix.py (Classic text, MathPrint no-error and TOO WIDE, popup and 2nd MATH picks), help.py (+ on the 7 items).

## SymCE TABLE (2nd GRAPH, graph_table.c, 2026-10-07)
- The raw key hook now also takes kTable (0x4A): it opens the grapher with GWA 0x20 (first key = 0xC0 + GWA = 0xE0), `graph_fall()` answers NZ (OS draws its own table) in Par/Pol/Seq (`pmode()`), `tblFlags` (0xD00093, iy+0x13) bit 4 = Indpnt Ask or bit 5 = Depend Ask, or sGrFlags&7 (split screen). bit 6 = reTable.
- TblStart = 0xD02267, DeltaTbl = 0xD02270 (9-byte reals). TI writes TblStart when the table scrolls and keeps it (and the selected cell) across leaving and reopening; SymCE does the same, keeping its selection at 0xD0EED0 (magic 0xA5, col, row, header flag, first visible col). 0xD0EE00..0xD13E00 survives the OS; the gw struct lives at 0xD0EF00 and is zeroed on each grapher entry.
- Cells: the OS evaluates Yn at X (`symce_osy`, no StoAns) and the OS formats the number: routine 0x98F2B with A = width 6 gives the table cell string at 0xD0060E (NUL ended), so numbers are TI's, not an emulation. Errors show `ERROR` in the cell. Row X = TblStart + r*DeltaTbl by multiplication.
- Geometry (measured on TI, 320x240): gray header band, grid lines; X column x 0..63, each Y column 60 px wide from x 64; 4 Y columns visible; header text large font at y 31 (pen x-2, y+1); cells small font, 6x10 digits, pen y = row top, text x+8..x+12; band line 2 at pen (2,14); bottom line = full value of the selected cell, large font at pen (2,219); a selected header cell shows a drawn "=" block 10x14 with two 6x2 white slots (not an inverted '=' glyph); deselected Y headers need a 17-row white fill or row 46 stays black. Band strings are at ROM 0x55EE3.
- Navigation measured on TI: up in the X column at row 0 scrolls TblStart by -DeltaTbl; up in a Y column at row 0 selects the header, down from it goes to row 0; right past the 4th visible Y column scrolls the columns by one; ENTER on a cell, 2nd+DOWN, DEL and CLEAR do nothing; ENTER on the header, `+` in the X column and digits start TI's in-table editing (SymCE hands those to TI's table, key 0x4A).
- Scroll = memmove of 139 VRAM rows plus one new row evaluated; the cache of evaluated cells sits in the SB cache at 0x0D10300 (free while the table is up).
- Bottom line of the table = the OS number field `0x9968B` (A = width) with width 15, +1 if negative, +2 if |x| < 1 (exponent byte < 0x80): gives TI's 14 digits (`-909.29742682568`, `-0.63828473850423`, `0.27272727272728`). `0x98F2B` caps the width, it is only for cells.
- TI's own table selection RAM: column `0xD02048` (copy at `0xD0204A`; 0 = X), row+1 `0xD02049` (0 = header), visible Yn numbers `0xD0225A..0xD0225D` (1-based, used at ROM `0xB4491`). Writing them from the hook before returning kTable (0x4A) did NOT make TI reopen on that cell (measured, removed).
- After TI scrolls its table, TblStart's type byte has bit 6 set (0x40); and with 1/3 steps its last mantissa digits differ from SymCE's repeated FPAdd (TI `-0.6666666666667`0 vs ours `...77`), so X values show equal at 4-5 digits but not in the bottom line.
- Table timings (emulator, ms from key to last pixel change, 20 ms resolution): TI first draw 80/200/400 for 1/2/4 columns, SymCE first draw 4 columns 420 after LDIR row fill (was 520); stages of the 4-column open: calc 156 ms (OS eval, ~3 ms/cell), frame 60 (was 128; per-pixel fill loop replaced by LDIR `symce_fill`), cells text 122, bottom line 14; NewContext0 is not the cost (warm opens equally slow), nor DrawStatusBar; one-row scroll TI 80, SymCE 20; column scroll 20 both.
- 2nd QUIT from the SymCE table leaves the home screen exactly as it was before opening it (checked by pixels); TI's own home after the same Y= setup differs only by the setup, not the table.
- e2e.py: the app is ~269 KB now, so the flash install in the prologue needs 2 more seconds (`L . . . .`); with the shorter wait 9 later entries typed too early.

### ZoomFit in Par and Pol (measured on TI, 2026-10-07; now SymCE's)
- Sets Xmin, Xmax, Ymin and Ymax to the min and max of X and Y over the points T = Tmin + k*Tstep (k = 0 .. floor((Tmax-Tmin)/Tstep)) of every selected curve; Xscl, Yscl and the T window stay. Par example: X1=5cos T, Y1=5sin T, X2=T, Y2=T^2/4 over 0..2pi gives Xmin -5, Xmax 6.283185307, Ymin -5, Ymax 9.869604401; over T 1..3 step 0.5 Xmin -4.949962483 (5cos 3), Ymax 4.987474933 (5sin 1.5).
- Pol: X = R*cos(theta), Y = R*sin(theta) in the OS's own arithmetic (Xmax 6.283185305 for R = theta/2 at 4pi, one ulp-ish below the Par style value). Same DeltaX/DeltaY/TraceStep refresh as the other zooms. zfitp() in graph.c; trace.py cases pz_fit, pz_fitw, oz_fit, oz_fitd equal TI's window, deltas and curves.
- Text( (from DRAW with the graph up; now SymCE's, `dr_text()` in graph_draw.c, DRAW menu item 10 = pick D_TEXT): the title is `DRAW TEXT`. Each typed key is placed at the cursor at once; ENTER and CLEAR leave to the plain graph with the text kept (ENTER first, with nothing typed, does the same and later keys go to the home screen), DEL does nothing, menu keys act as on the plain graph. A key typed before moving is part of the text. ALPHA is one-shot, 2nd ALPHA locks.
- Each character writes a white (nibble 11) cell, advance wide and 14 rows tall (cursor gy-1 down to gy-14), with the glyph in nibble 1: "12" at cursor (132,82) fills x 132..147, y 68..81. The glyph is the OS's small-font record (`Load_Sfont` 0xA55D3, A = char, HL = char*25, so the Evo font hook applies): byte 0 = advance, then 14 one-byte rows (width <= 8) or 12 two-byte rows, row 0 at the top of the cell, MSB at the cursor's gx. A token is typed as its GetTokString characters, one cell each (`sin(` is 4). Keys: digits, `.`, `(-)`, + - * / ^, ( ), comma, X (T, theta in Par, Pol), sin cos tan log ln, x^2, x^-1, STO (an arrow), 2nd: pi, sqrt(, 10^(, e^(, the inverse trig, u v w, L1..L6, Ans, i, { }, [ ], EE, e; ALPHA: A..Z, theta, space, colon, ?, quote.
- Edges: the cursor is clamped on entry to gx <= 256, gy >= 12 (the whole cell is on the grid) and by the arrows to gx <= 263, gy >= 16. A character that would end past column 264 (gx + advance > 264) is not drawn and the cursor goes to 263. Rows below gy 0 are clipped (a cell at gy 12 shows rows 11..0).
- tools/emu/trace.py draw cases txtDig, txtLet, txt2nd, txtTok, txtAns, txtI, txtU, txtSq, txtInv, txtEE, txtX, txtRight, txtEdge, txtBot, txtEnter (a 7th item in _draw.json compares the white cells too; the hold-right to the edge needs a 20 s hold on SymCE, whose repeat is slower than TI's). x^2 (key `^2`, token 0x0D) and x^-1 (key `-1`, token 0x0C) equal TI's cells (txtSq, txtInv); EE is 2nd `,` (txtEE). Tokens: tRecip = 0x0C, tSqr = 0x0D (0x0A radian mark, 0x0B degree).

### DRAW Pen (measured on TI, 2026-10-07; now SymCE's)
- Item A (alpha math) of the DRAW tab, title `DRAW PEN`. ENTER lowers the pen at the cursor and draws one point (nibble 1); while it is down every cursor move draws a 1-px Bresenham line (ILine's e = 2*minor - major rule, points only, none of the thick neighbours) from the last spot to the new one; ENTER lifts it (moves draw nothing), ENTER lowers it again, CLEAR leaves. Down and up with no move leaves the dot. dlinet() in graph_draw.c; _draw.json cases penA..penE equal TI's layer (TI drops cursor keys sent without a wait at the grid corner: give a 300 ms pause between move groups in tests).

### Seq axes uv, vw, uw (measured on TI, 2026-10-07; the graph is now SymCE's)
- FORMAT row 0 in Seq mode: Time, Web, uv, vw, uw (2, 3, 4 rights from Time); seqFlags 0xD0008F bit 0 Web, 2 uv, 3 vw, 4 uw. Each term n (PlotStart, PlotStep as in Time) is one 3x3 dot at (first term, second term) in the colour of the second sequence (uv: v's red; vw and uw: w's black). Both sequences must be defined (SymCE hands the graph to TI otherwise). After the draw X = the last first-term value, Y = the last second-term value (Time leaves X = n). TRACE, the zoom picks and CALC in these formats go to TI (graph_fall: seqFlags and the handed-menu-key mark GW+0xC07 = 0x5A). trace.py cases uv_pd, vw_pd, uw_pd, uv_w, uv_std, uv_tr, vw_zin equal TI's.

### Seq Web format (measured on TI, 2026-10-07; GRAPH is now SymCE's, TRACE below)
- FORMAT row 0 Web = seqFlags 0xD0008F bit 0. GRAPH draws no cobweb: for each defined sequence its formula as a function curve of X (the sequence's own `u(n-1)` read as X; u(1), u(2), nMin, nMax, PlotStart and PlotStep play no part), in the sequence's colour, thin lines whatever the line style, and after each curve Y = X in black (drawn over it: u(n) = u(n-1) leaves only the black line; where u and v cross Y = X, v shows). A formula with no u(n-1) (u(n) = 3) is a horizontal line. A pole (1/u(n-1)) breaks the line like a function's. Nothing else of the three sequences may be in a formula: n, u(n-2), v(n-1) in u(n) give ERR:INVALID (cxCurApp 0x52) at the first column: X = Xmin stored, Y untouched.
- After the draw: X = Xmin + 263 DeltaX (the column before the last: 9.92424242 for -10..10), Y = the last drawn sequence's formula at that X (v's when u and v are both there).
- SymCE: wdraw() (graph.c) uses the Function compiler on the formula with u(n-1) replaced by the X token (websub()). The INVALID cases, a formula the compiler cannot take, a non-dot line style and any handed menu key (zooms) go to TI (graph_fall). trace.py cases wb_pd, wb2_pd, wbv_pd, wbp_pd, wbd_pd, wbc_pd, wbi_pd (curves and the black line within a pixel of TI's, X Y Ans window equal) and wbe_n, wbe_u2, wbe_v (TI's error screen).

Web TRACE on TI (measured; SymCE does one sequence natively, two or more go to TI):
- One sequence: step 0 is n=nMin, X=u(nMin), Y=0. Odd step 2k-1: n=k+1, X=u(k), Y=u(k+1) (cursor on the curve). Even step 2k: n=k+1, X=Y=u(k+1) (cursor on y=x). Top label `u=...` in the sequence colour, bottom bar `n=` over `X= Y=`.
- Cobweb: 1 px lines in the sequence colour, over ticks and axes (step 1 vertical (u1,0)-(u1,u2), step 2 horizontal to y=x, ...). They persist: LEFT/RIGHT/UP only move the cursor. CLEAR keeps the cobweb, drops cursor, label and bar. No nMax limit.
- Typed n jumps to step 2(n-nMin)-1 (n>nMin) or step 0 (n=nMin); prompt `n=`, cobweb drawn up to that step.
- ENTER (QuickZoom) recentres on (X,Y), redraws without the cobweb, restarts at step 0 with X=1, Y=0.
- After a typed n (step s0), RIGHT goes to step 2*s0 and LEFT to s0-1; that RIGHT draws only a vertical from the x axis at the X of step k-1, then the segment to step k. Typed n above nMax is accepted by TI (terms past nMax are computed). SymCE's ERR:DOMAIN was its own integer check: real() is a float (8 reads 8.00001), so the check needs a relative tolerance; it hit every typed n of 8 or more in Seq. Home X/Y after leaving are the trace's last X,Y, CLEAR or not.
- Solver (SymCE side): the OS parser (symce_eval of a typed n) and every other OS call between keys leave the OS Seq solver stale, so u(n-1) is wrong on an incremental term; restart it (sn = -1) on each key, then go up one term at a time.
- Diverging u: cursor off screen, bar shows X=Y=15, lines clipped at the window.
- Two sequences: UP/DOWN draws a stray vertical (v(2),0)-(v(2),v(3)) in the new colour. 2nd CALC value: after the pick the prompt is `n=` and y=x turns BLUE. Too quirky to copy, so TRACE/CALC stay with TI.

## STO to a SymCE answer (*measured* 2026-10-07, tools/emu/ans.py)

- The OS's parser refuses a store in an equation-mode ParseInp (`"x"->Y1` as the body of a CreatetEqu equation: Syntax 0x88; in a temp program (type 5, 0x8364B with A=5) the run died). So engine.c's `symce_osrun` (modes 1..3) does the work itself.
- Mode 1: ParseInp of the answer's real tokens (display glyphs mapped back: 0x2C -> 0x2B comma, 0xD7 -> 0x2C i, the 7-byte cos letters -> 0xC4) under PushErrorHandler, then StoAns 0x20F30 when OP1 is real/complex: the OS Ans. POLYDEGREE(X^3), then `Ans*2` = 6.
- Mode 3 (Yn, Strn): ChkFindSym 0x2050C with OP1 = `03 5E 1n 00` / `04 AA 0n 00`; if found, `DelVarArc` 0x21434 **straight after ChkFindSym** (it reads HL/DE; DelVar 0x20588 called after re-setting OP1 raised 0x8D); then CreateEqu 0x20560 / CreateStrng 0x20554 with HL = size, DE = the size word, data at DE+2 (tokens only, no leading zeros); then ChkFindSym again and `dec hl; set 0,(hl)` (flag byte below the entry type: "selected", so Yn plots). Y= then shows `Y1=(X-1)(X+1)`.
- Mode 2 (letter): the value from mode 1's ParseInp is copied to OP3 0xD0060C, OP1 = `00 letter 00 00`, ChkFindSym, CreateReal 0x20534 if absent, 9 bytes into DE. StoOther 0x20F4C with OP2 = 0xD00603 gave 0x84 (Data Type) for a real: not used.
- errNo 0xD008DF keeps the code of an error that was thrown and caught: parsing `(5/2,9/2)` left 0x88 there, which failed e2e's `run.err == 0`. Lists (comma) are not numeric.
- engine_check.py `numeric()`: an entry with a numeric OS-held Ans that the engine cannot do passes to the OS (not an error screen).
- 2:Goto cursor (*measured* 2026-10-07): after Goto the OS is in overwrite mode until the first key, so Load_LFont asks for the block cursor 0xE0..0xE3. fhook.c adds 4 to B while gotoFix (0xD0EE07) is 0xA5, so the insert bar 0xE4..0xE7 is drawn at once. tools/emu/cursor.py checks VRAM: after `SOLVE(X-4)` + ENTER + 2, the only full-height columns are the stems of L and E and a 2 px pair at x 105..106.

## Graph Ans (*measured* 2026-10-07)
- The OS creates empty equations (size word 0) for the Y variables when the grapher or Y= context runs, so "Yn does not exist" is not the test for a free slot: ChkFindSym `03 5E 1n 00` finds it, with DE at the size word; free = not found or size 0. graphAns (main.asm) picks the first such n.
- symce_engine mode bit 0x80: no entry is parsed, lastAns is the answer (0xFF real: the Ans token) and a trailing `04 5E 1n` stores it (finish, mode 3). main.asm builds that 3-byte entry in OP5 and calls symce_engine with the C stack convention (7 arguments of 3 bytes: a `ld h,0` after `ld hl,addr` leaves the upper byte, so len was 0xD00003 until `ld hl,3`).
- The ALPHA+DOWN pick travels as menu_key's return value (item typed text 0x7F): hook.c `.Lmback` jumps through the word at `_symce_hook_end + 10` (graphAns) when it is nonzero, abandoning the hook frame like menuKey does (counter at 0xD0265B decremented, then JForceCmdNoChar or the monitor's key dispatch 0x8C49F).
- tools/emu copies of SYMCE*.8xv are not rebuilt by `make`: copy bin/SYMCE.8xp and bin/SYMCE*.8xv there (or `make emu`) before running emulator tests, or they test the old app.

### Held arrow repeat (measured 2026-10-07, free cursor and Text( on TI; Line on SymCE)
- TI: the first move at the press is 1 px, then from about 0.5 s a 5 px step every 100 ms (tools/emu/_hold.py: hold right 400/700/900/1100/1300/1600 ms of script time gave gx 143/158/168/178/188/203 from 132; the plain graph's cursor shows the same numbers in 0xD0146D+2).
- SymCE moved 1 px per ~100 ms instead, because ti.GetCSC itself repeats a held key: every repeat reached graph_key, which restarted the hold timer (w->th) so tick()'s 5 px repeats never began. graph_key now ignores a press of the arrow already held (w->hk); tick() does the repeats (HOLD 0.5 s, REPT 3277 ticks = 100 ms) and SymCE gives TI's numbers exactly.

### Menu / mode keys inside DRAW Text( (measured 2026-10-07, TI)
- MATH, VARS, 2nd TEST, ALPHA y= (F1..F5), CATALOG, 2nd MATRIX: the OS menu opens over the graph; picking an item writes its text at the Text( cursor and Text( stays active.
- y=, window, zoom, mode, 2nd graph: leave to the Y= editor, WINDOW editor, ZOOM menu, MODE screen, TABLE. graph: STYLE color dialog (Color: BLUE, OK/CLEAR). trace, 2nd mode: plain graph.
- SymCE: y=, window, zoom, mode, 2nd graph, 2nd mode match. MATH/VARS/TEST/CATALOG/MATRIX menus leave the grapher and Text( mode is lost after the pick (text is not inserted): known difference, would need the OS menu opened over the SymCE graph plus harvesting the pick.
- SymCE (2026-10-07): MATH, 2nd TEST and 2nd MATRIX opened from Text( leave the grapher for TI's menu with the marker GWX (GW+0xC12 = 0xA5, set by graph_key when it hands the OS the key). The menu hook (event 3, `menuKey`) sees the pick as B = key code: `ConvKeyToTok` (0x20E40, A = key code, `keyExtend` 0xD0058E = the low byte of an FE/FC key) returns DE = token (D = prefix or 0). The token goes to GWX+1,+2, the menu is left with NewContext0(kQuit) and the grapher reopens with first key 0xFC, which writes it at the saved cursor (signature nibbles 13..17) and stays in Text(. A key pressed within about a second of the pick is lost (the grapher replots first); tests wait 2.5 s.
- TI inside DRAW Text( (measured 2026-10-07): MATH, 2nd TEST, 2nd MATRIX, 2nd LIST (stat), 2nd ANGLE (apps), 2nd DISTR (vars) and ALPHA y= .. open their menu over the graph and a pick writes its text at the cursor, Text( stays (DISTR items insert `normalpdf(` bare, no wizard; SymCE's DISTR pick opens the OS wizard because it never reaches menu hook event 3). CLEAR in the menu: back in Text( at the same spot; 2nd QUIT: blank home. VARS (VARS Y-VARS COLOR), CATALOG (alpha lock on), APPS, PRGM, STAT and 2nd + (MEMORY), 2nd TRACE (CALCULATE), 2nd WINDOW (TBL SETUP), 2nd Y= (STAT PLOTS), 2nd ZOOM (FORMAT), 2nd GRAPH (TABLE) are the OS's own full screens. TRACE leaves Text( to the plain graph (no cursor, no title, text kept). GRAPH opens the STYLE dialog (`Color: BLUE <>`, OK, CLEAR) for the text colour; ENTER on OK keeps the dialog's picture until confirmed.
- SymCE: CLEAR out of a Text( menu returns to Text( (first key 0xFC with an empty token); TRACE is the plain graph (cleave). ALPHA y= .. graph raise key 0x3D in the raw hook (menuCurrent 0x48), the Y= editor opens instead; DISTR picks do not reach menu hook event 3 (its wizard is the OS's, a known difference).
- SymCE VARS (OS key 0x35) is one more menu with event-3 picks: the Xmin pick writes at the cursor. CATALOG is not a menu: the raw key hook (GWY marker, set when 2nd 0 is pressed in Text() takes ENTER, reads `catalogCurrent` (3-byte pointer to a 2-byte key-code entry: prefix, byte), sets keyExtend = byte, `ConvKeyToTok` with A = prefix, and re-enters with first key 0xFC. The catalog is alpha-locked, so its ENTER reaches the hook as kAlphaEnter (6), not 5; CLEAR is 9. TI drops the key typed right after a VARS or ANGLE pick (tests end at the pick).
- CATALOG up = `menuCurrent` (0xD00824) 0x46, measured with the catalog open vs. the home screen (cxCurApp stays 0x40, `catalogCurrent` keeps its old value on the home screen, so neither tells). The raw hook sees the opening key 0x3E while menuCurrent is still 0. The Text( markers are two bytes `A5 5A` (GWX 0xD0FB12, GWY 0xD0FB16; picked token GWP 0xD0FB14): GW is the engine's work pool, so a single byte can be stale from any CAS answer. `tools/emu/stale.py` pokes them from a patched copy of prgmSYMCE (first instruction jumps to a stub that pokes when the hook is armed; APPS > SymCE > 5 runs it) and checks CLEAR, ENTER and a MATH pick on the home screen. `Asm(` and a bare asm program do nothing on OS 5.8.4.
- Catalog paging in Text(: the raw hook sees ALPHA up/down as codes 7/8 (letters and arrows pass through); the catalog marker (GWY) is cleared only when menuCurrent != 0x46 (catalog gone) or on ENTER (5/6)/CLEAR (9), never by other keys, so paging then ENTER still pastes. A token whose letters run past the right edge is drawn per character: those that fit are drawn, the rest dropped (DiagnosticOff from 30 px left of the edge matches TI). Token strings are up to 15 chars (DiagnosticOff 13).
- Harness (draw suite): one long TI session drops keys (the baseline bc7b608 full `trace.py draw` derailed from circBot on too; same cases pass in small batches). drawcmp now runs 10 cases per session and pauses 150 ms every 4 cursor moves (8 left penE flaky); full suite 45/45.

## Commands inside commands (nested(), 2026-10-07)

- `SOLVE(EXPAND((X+1)^2)=0,X)`, `FACTOR(DERIV(..))`, `2*EXPAND(..)`, to any depth the buffers allow. `nested()` in engine.c wraps `engine()` (called by `symce_engine`): it finds the LAST command name + `(` in the entry (so nothing is inside it), runs `engine()` on that slice (name to its matching `)`, mode `MODE_INNER` = 0x08: answer may be any width), and rewrites the entry as `prefix ( answer ) suffix`, the answer through `totok()` (display glyphs back to OS tokens). Repeat; then `engine()` on what is left with `MODE_NEST` = 0x10 (answers numbers too, never returns 0 to the OS, since the OS would read the original entry as a product of letters). Loop, not recursion: stack peak unchanged.
- One place recognises a command name: `cmdat(in, len, &n)` (number, tokens through the `(`). A name inside a longer one is the longer one (`CFACTOR(` is not C times `FACTOR(`).
- Buffers: two of `NSB` = 192 bytes at `engineWork + SYMCE_WORK` (0xD0EF00 + about 0x3F00; `nest_fits` asserts the end <= 0xD0EF00 + 0x4F00). Host/test `work` arrays must have the extra room (engine_cli.c, engtest main.c). Entries over 192 tokens, or a rewrite over 192, are the SYMCE LIMIT (NESTED); names are two tokens each now; `MODE_EVAL` (DESOLVE's own calls) never nests.
- An inner answer must be one value: no `=`, `or`, `and`, list, matrix, comma (point). Otherwise ARGUMENT / NOT ONE VALUE, Goto at the inner command; an inner error is that error, Goto at the inner command (its offset is the same in the original, rewrites only happen to its right). An error from the final run is clamped to the leftmost rewrite.
- Inner matrix answers are not substituted into matrix commands (not done). A command name is its lowercase tokens (below); a product of capital variables that spells one is just a product.

## Commands are lowercase tokens, drawn blue (2026-10-07, branch cmd-lower, measured on 5.8.4)

- **Names.** The menus type a command as its lowercase letter tokens, 2 bytes each: `BB xx` with `xx = 0xB0 + (c - 'A') + (c >= 'K')` (a..j = `BB B0..B9`, `BB BA` is unused, k..z = `BB BB..`; t = `BB C4`). Typed through `keyExtend` (`0xD0058E`) = `0xE2 + n` and `cxMain` with A = `0xFC` (`kExtendEcho2`), which inserts the token like the lowercase ALPHA key. `engine.c cmdat()` recognises only that spelling, then `(` (0x10 is the root glyph; `(` is 0x28). Capital letters (ALPHA letters, `41..5A`) are variables: `SOLVE(X)` is S*O*L*V*E*(X). DESOLVE's own internal formats emit `BB` pairs too (`vfm`, a run of two or more capitals).
- **Home test for the colour** (all three must hold): `cxCurApp 0xD007E0 == 0x40`, `menuCurrent 0xD00824 == 0` (TI menus over home keep cxCurApp 0x40 but set it, MATH = 8), `textInverse (0xD00085 bit 3)` clear. SymCE's own popup leaves menuCurrent 0, so it is told apart by who draws: its text goes through `_VPutS` (VPutMap `0xA2594` called from `0xA28D4`/`0xA28AE`), the MathPrint home renderer calls VPutMap from `0x8Fxxx` (`0x8F20F`, `0x8F96E`). SymCE's graph text also uses `_VPutS`, so it stays black.
- **Colour sources.** Classic: PutMap's cell routine `0xA1948` calls `Load_LFont` at `0xA1970` (return `0xA1974`); each row tests `bit 4,(iy+74)` (`putMapUseColor`, `0xD000CA`) at `0xA1AC8`/`0xA1B80`: clear writes the constants (white `FFFF` / black `0000`), set writes `textFG 0xD02688` for ink and `textBG 0xD0268A` for the rest (`0xA1BEA`). MathPrint: VPutMap `0xA2594` draws every ink pixel with `drawFG 0xD026AC` (and `drawBG 0xD026AA`). Blue is `0x001F` (TI's Y1 blue). The hook sees only A=1 and B = glyph (lowercase `0x61..0x7A`) before the pixels are drawn, so it cannot restore the colour itself.
- **Restoring.** At hook entry the stack is `[ret 0x257D7][saved ix][ret 0x7BFFB][Load_LFont's caller]`: the caller's return is at SP+9. Classic (`0xA1974`): PutMap pushed af, bc, de, hl, ix, so ITS return is 15 + 3 bytes above. MathPrint (`0xA2584`, the glyph fetch inside VPutMap, with `0xA25AF` above it; 19 other ROM routines call `0xA256F` just for a width): VPutMap pushed 6 words, so its return is 24 bytes above. The hook saves that return in `0xD0EE09`, writes the address of its own trampoline in its place, saves the colour (`0xD0EE0C..`, putMapUseColor in `0xD0EE10`, mode in `0xD0EE08`: 1 Classic, 2 MathPrint) and sets blue. The trampoline puts everything back and returns on. Any later A=1 call finding the mode set undoes it first (a draw that never came back).
- **Measured** (`tools/emu/colour.py`, 0x001F pixel counts): a menu-typed `solve(` is 252 blue pixels in Classic and MathPrint, Evo font on and off, and nothing else on the screen changes colour; `sin(` 138; capitals 0; a TI menu (MATH, NUM) 0; SymCE's ALPHA+DOWN popup 0; the status bar 0. TI's own `Done` is lowercase and turns blue too (166 px), as do `Ans`, `abs(`, `round(` on the home screen.
- **Limit.** When a TI menu (or MODE) closes, the OS replays the 1-bit pixel shadows (`0xD031F6`) and draws no glyph through the font hook (the hook sees only the cursor), so the old text turns black (MathPrint redraws the entry on the next key, Classic only what changes); what is typed or redrawn afterwards is blue. SymCE's own popup saves and restores the VRAM, so it keeps the colour.
- **Quit from an error screen replays black (found 2026-10-11).** Home cxRedisp `0x58353` copies cmdShadow to textShadow, calls `0xA2164` (copies shadow bank `0xD031F6` <-> `0xD07396`), then in Classic calls `0xA2A03`, which blits the 1-bit shadow to VRAM `0xD44B00` as black/white only (text variant `0xA2AF6` when bit 5 of `(iy+76)` = `0xD000CC` is set); MathPrint calls `0x9F0CF`/`0x9E7B0`. The font hook is never called for the history glyphs, so after any error Quit (OS `(1)/(0)` too) the history and entry lose rainbow/blue until UP highlights them. No cursor-glyph calls happen while the error screen is up; they resume right after Quit. SymCE's own errors fix it: hook.c `.Lerr` calls blob +24 `err_save` (menu.c: scans VRAM, stores address + colour for each non-black/white px at `0xD0F004..0xD13700`, end pointer `0xD0F000`, flag `0xD0EE1E` = 0xA5), and fhook.c `.Lcpass` (the cursor glyph, last call of a redraw) calls blob +28 `err_put` (= paren_pass pointer at font end + 7, +8), which repaints still-black px and clears the flag. OS errors cannot be fixed this way (no hook point).
- **Assembly programs stay black.** An asm program run from the home screen (the installer, via AsmHook2) draws its lowercase text with the same cell routine on cxCurApp 0x40 / menuCurrent 0, so it came out blue (2528 px) and differed from the same screen run from the settings app. The hook now also skips the colour while `asm_prgm_size (0xD0118C)` is nonzero; installer screens are pixel identical again (lifecycle.py "5 shows installer screen").
- **Runs that begin with a capital stay black** (*measured* 2026-10-08): TI's own mixed words (`Done`, `Ans`, `Xmin`) are not half blue; a lowercase glyph is blue only when its run of letters did not begin with a capital (`Xsolve(` goes black, accepted). Mechanism, all in the font hook (fhook.c), updated only for glyphs that pass the home and caller-return tests: `colRun` (`0xD0EE11`: 0 no run, 1 run began with a capital, 2 began lowercase), `colLX/colLR` (`0xD0EE12/15`: the last glyph's x and row; a glyph that is on another row or not to the right of it starts a new run, so the right-aligned `Done` cannot leak into the next line), `colCF/colCP` (`0xD0EE16/17`: a cursor glyph was just drawn, and where). Position = Classic `curRow/curCol` (`0xD00595/6`); MathPrint `penRow >> 3`, `penCol >> 1` (`0xD008D5`, `0xD008D2`; curRow/curCol are stale there). **The blink**: each blink draws the cursor glyph (0xE4..) and then puts back that one letter alone (MathPrint: VPutMap called from `0x05CADF`, not from `0x8Fxxx` like the line renderer; Classic: the normal cell routine). The put-back is recognised as the glyph right after a cursor glyph at the same position and takes the colour recorded for that position in `colTab` (`0xD13D60`, 160 bytes at the end of engineWork, entry = row << 2 | 1 blue / 2 black; `nest_fits` and the popup pixel-save literal limit (menu.c) now stop at 0xD13D60; the table is only read after a row check and written only for x < 160, so a stale entry costs one wrongly coloured letter on a blink). **Classic also looks left**: after `left` onto `Ans` the OS redraws the token right to left (`s`, `n`, `A`, measured with a hook-side ring log), so a lowercase glyph in Classic scans TI's textShadow (`0xD006C0`, 26 per row) leftwards over letters and stays black if the leftmost one is a capital. MathPrint draws every glyph twice per sweep (a width query from `0x8F901`, then the draw whose VPutMap return is `0x8F96E`); only the draw passes the caller test. A MathPrint line longer than the screen scrolls sideways (4 `solve(` shown), it does not wrap.
- **Harness notes.** `help.py` compares the text shadow case-insensitively (the shadow holds `norm(` lowercase); `errs.py` Goto offsets are bytes (2 per command letter: SOLVE(X-4) 14, POLYGCD(X) 16); the error-screen cursor test sees 4 full-height columns (the stem of the l, and the bar). `cmdkeys.pick` reaches items past the 14th digit key with DOWN x n, ENTER. `lifecycle.py` "app moved up by delete" fails the same way on the base build (first key lost after the delete), not caused by this branch.


## Emulator "deaf window" (2026-10-08, harness)

- Taps (key presses) can land in a window under 100 ms, tied to the time since boot, and are lost before any SymCE code runs; it reproduces from Y= without the grapher, so it is the OS or emulator, not our hooks. `apdFlags2` (`0xD0009B`) differs between a deaf and a hearing run. Likely cause: the OS battery refresh is deaf to the keypad while it runs; unproven.
- Harness fix: baac258 waits 3 s after the 2nd CALC before the next key. A test that loses its first key after a long idle is this, not a product bug (cf. the lifecycle "app moved up by delete" first-key loss).

- **A 1 ms tap is lost to MathPrint's paren pass: desolve.py `Y''-3Y'+2Y=0 and Y(0)=1 and Y'(0)=0` ended in errNo 171 (2026-10-09, mpfix, measured).** 0xAB is SymCE's own `0x2B | E_EDIT` error screen (`ERROR: SYNTAX`, "Check the arguments: DESOLVE(EQ,X,Y)"), thrown by engine.c `desolve` because the entry it got had a token missing. Dumped the temp program `#` after the error: `... Y' 30 11 ...` where it should read `Y' 10 30 11` (the `(` of `Y'(0)` gone); another run lost one `,`. Not the engine, not RAM: the engine pool ends at 0xD132B0, the steps( log (0xD13800..0xD13BB0, written only under steps() and paren_pass's state (0xD13BB0..BC) are past it and never overlap; the edit buffer was intact apart from the missing key. A counter in the key hook (A=1) read 40 calls for 41 keys in the failing run and 41 in the passing ones, so the OS never delivered the key. Cause: every `vram.grab` / `e2e.mathprint` run uses `delay_after_step 1`, so `key|k` is a 1 ms tap; the OS needs 5 keypad polls (one per halt wake-up, see "key-deaf window" above) to accept a key, and 5f1dabc's paren pass (every MathPrint cursor glyph, so every blink) adds ~5 ms of busy time that can swallow a wake-up and leave the tap one poll short. Bisect: 3752e99 passes, 3752e99 + 5f1dabc fails, c448399 fails, c448399 with `.Lcpass` skipped (`jr .Lgx`) passes; the same keys pass with a 20, 100 or 200 ms hold or a `w13` before the entry and fail with 0 or 40 ms (phase-dependent, deterministic). 24fe0b0 has the same pass and the same exposure: it passed by phase. Fix is in the harness: `screen.read` holds each key 120 ms when `delay_after_step` is not 0 (`hold|k`, `delay|200`, `release|k`). edittop.py (its DEMO.8xp harness was gone: KeyError 'top') now reads through screen.read and runs. Cost of the pass, timer 3 (30.5 us/tick) around a 66-key MathPrint entry, last key: key hook to first cursor glyph 48.8 ms (48.95 ms with `.Lcpass` skipped, i.e. the pre-1a88d67 path of 24fe0b0/3752e99), the pass itself 4.36 ms (~209k cycles at 48 MHz, all of it cpir over 35 rows x 640 B of VRAM), so ~9% on a ~49 ms redraw, imperceptible; Classic has no pass (cursor glyph 1.1 ms). Not made cheaper: unchanged-row skipping needs a safe notion of 'rows the OS did not repaint' and a wrong guess leaves black parens. Rule: a MathPrint VRAM test that "loses" a typed token is this; compare the typed text (mathprint(keys without enter)) before blaming the engine. A human tap is 80-150 ms, so no real-calculator risk is shown.

## Interrupts and the stack: never keep live bytes below SP (2026-10-08, branch nroots-hang, measured)

- The OS's interrupts stay enabled while an asm program or the app runs, and an interrupt pushes its return address (3-4 bytes) at SP-1 downward. Any byte stored below SP, or any transient SP (`push hl` / `inc sp` / `pop hl` / `dec sp`), can be overwritten at an instruction boundary. `symce_umul` shifted HL >> 8 that way and lost its zero byte when an interrupt came after the `pop`, so a product got a stray byte (a hang in NROOTS on ENGTEST). Fixed by storing HL at (iy-6) and reading (iy-5), with the zero at iy-3 kept above SP.
- Tell-tale: a run that hangs or differs only for some launch phases of the same image (`engine_device.py only=764-927 ph=0..3` hung, ph>=5 passed), and passes with `di` at the start of main. The harness `ph=MS` waits MS ms before launching to move the phase.

## Rainbow parentheses (2026-10-08, branch rainbow-paren, measured on 5.8.4)

- **Settings item 7 "Paren colors" (branch rainbow-build, default ON).** The bit is `asm_Flag2` ($D000A2) bit 5 (`RB_OFF` 0x20 in main.asm), **set = OFF**, so a RAM clear means ON; bit 4 of the same byte is item 6 (Trace snap). Free: no other use of $D000A2 in `symce/` (only graph.c `snapon` bit 4 and main.asm), none in this notebook, and the TOS include lists asm_Flag1-3 as user ASM flags (Flag3 "no longer available"). `fhook.c .Lisp` tests it before `.Lpar` and falls to `.Lnop` (black, exactly the pre-rainbow draw). `parentoggle.py` covers ON, OFF, ON again; the screen is now 10 rows with the blank line before the tail dropped.

- **Classic: by the font hook; MathPrint: by a VRAM pass (see the last bullet below).** The font hook (`fhook.c`) colours a `(` or `)` glyph (0x28/0x29, only through the home renderer `0xA1974`, same gates as the blue commands) by depth: red `D800`, green `0460`, magenta `B016`, orange `E360`, teal `04B2`, olive `8B60` (RGB565; `.Lpal` after the body), `depth mod 6`. Both parens of a pair share the colour; a `)` with nothing open stays black. The `(` of `sin(`, `solve(` counts (it is a plain `0x28` glyph after the lowercase letters).
- **Depth = the parens to the left, read from TI's text shadow (`0xD006C0`, 26 x 10).** The OS redraws only from the cursor on (insert, delete, the blink's one-cell put-back, `left` onto a token), so no running counter is right. The hook scans the shadow from the entry's first cell to the cell being drawn: `promptRow/promptCol` (`0xD00800/1`) are **0,0 on the home screen, not maintained** (measured at three entry positions), so the start is found by walking up while the row above is *full* (first and last cell non-blank; answers are right-aligned, so their first cell is blank; a history entry is always followed by an answer row). Unmatched `)` while scanning does not go below 0. Cost: at most 260 cell reads per paren drawn.
- **MathPrint parentheses are NOT drawn through the font hook, so they are recoloured in VRAM (2026-10-09, branch mp-paren).** Logging every `A=1` call (glyph, caller return, penCol) over `( 1 + 2 ) * ( 3 )`: the digits, operators and cursor arrive, the parens never do (also the `(` of `sin(`: its call log is six B=0 calls). The pixels on screen are exactly the ROM large glyph `0x28`/`0x29`, but they are drawn as vectors (disassembly: MathPrint layout loop `0x8F1D0..0x8F215`, `(` `0x8F251` -> `0x8F2D9` -> `0x8E19E`, `)` `0x8E115`, segment runs `0x91636..0x91714` / `0x91784..0x91821`, colour from drawFG `0xD026AC`); nothing between two parens, so drawFG cannot be set per paren. The first conclusion, "they stay black", was wrong for the user: MathPrint is the default mode, so on the calculator the rainbow never showed ("didn't work in any font version").
  - **Shapes (VRAM, measured).** Black on white, a 6 x 14 px box (cell x + 4..9), identical with the Evo font on or off. `(` rows (first column in the box, run): (3,3) (2,2) (1,3) (1,2) (0,3) x6 (1,2) (1,3) (2,2) (3,3); `)` is the mirror (0,3) (2,2) (2,3) (3,2) (3,3) x6 (3,2) (2,3) (2,2) (0,3). The stem, rows 4..9, is a run of exactly 3 black pixels. Evo's own paren record (2 px strokes, `large.txt` 0x28) never appears in MathPrint.
  - **Trigger order (logged, hook-call ring in RAM).** After a key the OS redraws the whole entry (spaces, then the glyphs, parens silently as vectors) and the cursor glyph call (`E4`, twice, or `E0..E3` in overwrite) is the LAST font-hook call of the redraw, also when the cursor is mid-entry (insert after `left`). The blink alternates `E4` with the put-back of the cell's glyph. So fhook.c `.Lcur0` runs the pass for a cursor glyph when MathPrint (mpFlags `0xD000C4` bit 5 set; clear = Classic), rainbow ON (`0xD000A2` bit 5 clear) and the `.Lgo` gates hold (home `0xD007E0 == 0x40`, no menu `0xD00824`, not inverted `0xD00085` bit 3, no asm program `0xD0118C/D`). `.Lcpass` pushes BC, a return address (`.Lppr`) and the pass entry (the word at `_symce_font_end + 7`, `dl symce_menu + 20`), loads HL = `.Lpal`, and `ret`s into it; the pass keeps IX and IY. The pass runs before the cursor is drawn, which is fine (the bar is no paren).
  - **The pass (menu.c `paren_pass`, hand asm, state at 0xD13BB0..0xD13BBC).** Every 6th VRAM row from y = 30 to 234 (35 rows, `cpir` for zero bytes: a hit with the high byte also 0 is a black pixel). Because the stem is rows 4..9 (6 rows) of the shape, exactly one scanned row crosses each paren's stem, so each paren is found once: the hit must start a run of exactly 3 black pixels (white either side), go up 0..5 more rows of the same run (a longer run is no paren), which puts the shape's row 0 at the stem top - 4; both shapes are then matched in full (all 14 rows black, white either side of each run), `(` 3 px right of the stem, `)` 3 px left. A tall (stretched) MathPrint paren, a small-font paren, or a letter fails the match and stays black. Depth: the scanned row's address is the band (parens whose stems cross the same scanned row, so one line, and an exponent's raised parens too), reset to 0 on a new band, `(` takes colour `depth mod 6` then depth++, `)` does depth-- then colours (a `)` at depth 0 stays black). The palette is fhook.c `.Lpal`, passed in HL. It recolours the glyph's black pixels in place; history rows and answers are done by the same pass.
  - **Cost (timer 1 as a cycle counter, debug build only).** Scanning all 197 rows and testing every black pixel was 1.35 M cycles (28 ms); every pixel with only the scan, 0.85 M. With 6-row sampling: 201,000 cycles (4.2 ms) for a one-line entry, 238,000 (5.0 ms) for a full screen of 9 lines and 24 parens; 57 / 185 passes were run in those screens (two cursor calls per key, one per blink on). No dirty flag needed. Rev M+ flash cache assumed (10 cycles a code byte on older hardware would be a few times slower; the loop is a few hundred code bytes, the `cpir` is one instruction).
  - **Limits.** (1) The OS blinks the cursor off with the cell's vector black and does not call the hook for a paren, so a paren under the cursor is black for the cursor-off half of the blink and the bar for the other half (measured: it never takes another depth's colour); every other paren keeps its colour (the pass runs at each cursor-on). (2) An entry wider than the screen scrolls in MathPrint and the parens scrolled off are not counted, so the visible `)` of a hidden `(` is black (no way to know what is left of the `<` marker). (3) The depth is per scanned row, not per expression: `X^(2)` shares the line's count only when the stems cross the same scanned row. (4) Tall/stretched and small-font parens stay black. (5) Popup/HELP save budget: a MathPrint paren is 38 coloured pixels (Classic about 20); a popup opened over 36 rainbow parens in the test; the limit is `0xD13D5E` as before.
  - **Harness.** `tools/emu/mpparen.py` finds the shapes in a `vram.grab` and checks each colour against depth; colour.py `mprb` cases (Evo and TI font, nested, `sin(`, unmatched, six deep, history, recall, insert, delete, answer, blink, popup) and parentoggle.py (ON, OFF, ON again, Classic and MathPrint, Evo and TI) use it.
- **Registers/RAM.** `colVal` `0xD0EE1C..1D` (the colour `.Larm` puts in textFG/drawFG: blue `001F` for a command's letters, else the paren's). The body only reads colVal; nothing else new. `colTab` and the others unchanged.
- **Lossless again for rainbow screens (2026-10-10, parenfix).** `save_px` literals were 2 bytes per extra-colour pixel (white, black, blue took the three code slots, so every rainbow paren pixel was a literal): ~1150 fit, and 16 of 63 parens came back black after `steps(`/popup/HELP closed. Now a literal is a 1-byte index into a table at `lit` (+0 count, +1 16 colours, +33 next index address, indices from +36): 2270 pixels fit, 80 parens restore exactly (`parenspill.py` now demands zero differing pixels). A 17th colour or index past 0xD13D5E is still black. Below is the older lossy design.
- **Lossy screen saves (2026-10-09, mp-paren).** `save_px` stored its literals from `0xD130AC` up to `0xD13D5E`, straight through the steps( log (`0xD13800..0xD13BB0`) that `steps_show` reads AFTER saving, and failed (returned 0) past 1625 literals. Measured on the emulator (MathPrint, rainbow ON, rows of `((((((((((1))))))))))` then `steps(solve(2X+3=7,X))`): 1 row, steps shows; 2-3 rows (40-60 parens): steps never appears (save failed, `steps_show` returned silently); 4 rows: `ERROR: SYMCE LIMIT` (log overwritten). Now `steps_show` copies the 0x3B0-byte log to `0xD130AC` (just after the codes), points `stwalk` at the copy and starts the literals after it (room: 1153 literals); and `save_px` never fails: a pixel with no room is stored as code 3 without a literal, and `restore_px` gives black (0x0000) for a code 3 whose literal pointer is past `0xD13D5E`. So steps, the popup and HELP always open; the colours that did not fit come back on the next redraw (MathPrint: the paren pass recolours exactly-black parens, but it counts only those, so a band that is half black and half still coloured gets shifted colours; Classic: black until redrawn). Tests: `tools/emu/parenspill.py` (80 parens: steps screen identical to the plain-history one, popup and HELP restored), `tools/hook_sim_test.py` codec cases "running out" and "all overflow".
- **Popup/HELP cost.** SymCE's popup and HELP save the screen at 2 bits a pixel; every pixel past the first three colours it meets costs a 2-byte literal, up to `0xD13D5E` (menu.c; 160 bytes lower since the colour fix). So extra colours cost: a full-screen HELP save has `0xD13D5E - 0xD130AC = 3250` bytes = 1625 coloured pixels (was 1785); a rainbow paren is about 20 pixels. Measured (`Classic`, n `(` typed, ALPHA+DOWN): the popup opens with 52 and 78 parens on screen and is refused (no menu, entry untouched) with 104; the limit is somewhere between, about 4 parens lower than before.
- **Harness.** `colour.py` (rainbow cases: per-cell ink colours in VRAM; row y band `20 r + 39 .. 20 r + 53`, 12 px cells), `shots_rb.py` (colour PNGs), `vram.png` is RGB now (scale argument). A key typed right after `enter . .` can be lost (the deaf window); the scrolled case waits `w1500` first.
## Test fixtures that outgrew the app (2026-10-09, bugs-oct9)
- `lifecycle.py` "app moved up by delete" was a harness deaf window, not the app: after `DELETE_ABOVE ... clear .` the first two typed keys (`2 xton`) are lost and the screen shows `Ans+2X`; with `w1000`, `w3000` or `w8000` before TYPE the same build answers `2X+2X`/`4X`, hook moved and armed. The state() retry (`w100` prefix at the start) never helped. The test now waits `w1000`.
- `bigapp.py` fixtures are derived from the build: `make emu` pads to the shipped parts' size + 8 KB (`BIG` in symce/makefile); the "gc" ROM's erased run starts at `slot//64K - nsec + 2` (slot = LOWEST - (image+3); the installer's `room` needs the slot above the archive's last used sector + 64 KB; measured: 6 archived parts occupy 5 sectors 0x16..0x1A, the small last part packs into slack) and "tight" erases nsec+1 top sectors. The GC case really collects (ROMDataD's sector rewritten); the installer was fine.

## txtCatClr: what 2026-10-09 (bugs-oct9) added

- The key-deaf window is a polling-rate problem, not a lost key: a CLEAR held >= 500 ms (hold, 200 ms per_key, w300, release) passes at the same spot where a 200 ms tap fails, and so do 700 and 1200 ms. The OS GetKey loop polls the keypad once per `halt` wake-up (debounce 0x3FA2C: 0xD00589 last key, 0xD0058B counts 5 polls), so in the window the wake-ups are sparse and a short tap never gets its 5 polls. A human tap (~100 ms) on a real calculator would be lost the same way: treat as a real bug, not only a harness one.
- Timer block read after the first Text( catalog visit (probe: trace.py `tigo.go` with `Snap(ram=[(0xF20000,0x40)])` at 100/400/1100/2600/5100 ms): TI idle = timer 1 counter 0, control 0xF20030 = 0x40, status 0xF20034 = 0xC0. SymCE after graphMain1 = timer 1 counter 0x1125F (stopped), control 0x40, status 0xC3 (stale bits 0 and 1). Clearing both at graphMain1's exit (counter 0, status 0x1FF written to 0xF20034) did NOT fix txtCatClr: reverted. Interrupt controller 0xF00004/08 identical on both sides (enable 0x19), interrupt mask 0xF20038 is 0 on both.
- Still open: what thins the halt wake-ups for ~1.7 s. Next to try: a PC/stack sample of the `halt` at 0x40DD8 across the window, and which interrupt source ends it (read 0xF00000 status at 50 ms steps).
- Tick counter 0xD00590 runs at ~100 Hz in the txtCatClr window on SymCE and TI alike (about 10 per 100 ms), so the OS tick is not stopped; kbd bytes 0xD00587-8E and keypad regs identical too. penE (2026-10-09): 6 solo runs and 2 full `trace.py draw` runs (74 cases each, 10 per session) all passed penE; only txtCatClr failed. Not reproducible after the pacing change (150 ms every 4 moves), no root cause found, test left as is.

### txtCatClr round 2 (2026-10-09): harness position effect, not a SymCE-only window

- Same keys, different outcome by position in the case list: `txtCatClr` alone passes; `txtCat txtCatClr` fails; a copy of the same keys named txtCatW1500 run after txtCat passes; waits 1000..2000 ms (100 ms steps) after `2nd 0` all pass; waits 100..2500 alone all pass. TI side (grapher off) accepts the 200 ms tap in every run.
- So no wait offset is lost systematically and nothing SymCE leaves differs (timers, int mask, kbd bytes, scan rate checked, see above). It matches the known emulator deaf window (keys lost shortly after a boot/session start). Treated as an emulator/harness trait. Real-calculator risk not shown: a 200 ms tap passes at every tested offset.
- OS idle/APD counter D00590 (byte) + D00591..93 (reload via 0x40DA9); kbd scan 0x3CC2 runs ~410 Hz from the ISR (routine 0x1652) on both sides.
## steps( : worked solutions (2026-10-08, branch steps)

- `steps(` + a command + `)` is the whole entry (menu: 2nd MATH Algebra tab, last item, and CATALOG HELP; typed S T E P S stay variables). `symce_engine` strips it, runs the command with `MODE_STEP` (0x02, the one free mode bit) | `MODE_NEST` (never left to the OS), and the answer is exactly the command's. Anywhere else `steps(` is `ERR:SYMCE LIMIT / STEPS`. `nested()` clears `MODE_STEP` for inner commands, so only the outermost command is explained (no "First:" lines yet).
- The step log lives in the engine's scratch at `engineWork + 0x4540` = **0xD13440**, 0x3B0 bytes (the grapher's undo-log area, dead whenever the engine runs: the pool already covers its start; the shadow park at 0xD13800 is not touched). Layout in engine.c (magic 0xA5 at +0 once finished, count, flags, entry, used, then `[rule][plen][param][elen][expr]`, then `[alen][answer]`; all tokens as STO would store them, `totok`). `symce_engine` zeroes the magic on every call. The pixel saves of menu.c (help, popup) run at 0xD0EF00 and must stay below it: `save_px`'s literal limit is now **0x0D13440** (was 0x0D13DFE); a 210-row save is 16,812 bytes, so it still fits.
- Display: hook.c's `.Lshow` (A=0, after `dispResult` drew the answer) calls menu.c `steps_show` (menu blob entry +16, after menu_key/tab/help/err) which returns at once unless the magic is set. It saves the 210 rows under the status bar like help(), draws STEPS, the entry, each step (rule text with the parameter, the expression in blue), "Answer: ..." and a footer, scrolls with UP/DOWN (25 columns per line, wrapped at spaces), and CLEAR/ENTER/ALPHA/2nd put the rows back (`restore_px`+`sync_px`). Measured on the real OS (tools/emu/steps.py): the home screen is whole again in MathPrint and Classic, ALPHA and CLEAR leave the same way, the next entry is answered. Tokens are turned into text with GetTokString (0x5E43B then 0xA2C17), as graph.c does.
- Rules are explained at algorithm boundaries with the engine's own `answer()` printing every expression; a step that cannot be printed sets flag 2 and the log is dropped (the answer is never touched). `tools/steps_check.py` (hand-checked transcripts, `make check`) and `tools/emu/steps.py` (device) test it; with `T` in front of an entry `engine_cli` wraps it in steps( and prints the log as `S<hex>`.
- Trap: tools/ez80sim.py's hook loader looked for hook.bin in SYMCE1.8xv only; once the app grew, the body moved to SYMCE2 (it now looks in every part). The sim has to stub the menu blob's entry +16 (`steps_calls`).

### steps( phase 2: polynomial SOLVE and FACTOR (2026-10-09, branch steps)
- New rules SR_MOVE..SR_PDIV (engine.c enum, menu.c R[] in the same order): move to the left, discriminant, factored form, "set factor to 0", common factor, root found, prime division. `psteps` (SOLVE, degree 2+, runs before the roots are put out so `out` is scratch) and `fsteps`/`nsteps` (FACTOR) read the factor list `fl`; `stept` writes a step from token strings.
- fac() divides the monomial out of p->n, so SOLVE copies p->n into `o` BEFORE fac (vdeg of p->n after fac is the degree minus the X power).
- An error answer (SOLVE(X^2+1=0,X) = NO SOLUTION) gets no log: the magic is only set when the answer is not SYMCE_ERR.
- ENGTEST (engine_device.py) is the engine plus its vectors in one RAM program: at 152 KB it did not start. PART is now 2500 vector bytes per program (24 parts, 8.5 min); stack peak with steps on 1613 B.

### steps( phase 3: INTEGRAL, LIMIT, nested commands (2026-10-09, branch steps)
- New rules SR_FIRST..SR_LLEAD (28 in all; engine.c enum = menu.c R[] order): First (an inner command done first), Integrate / Top limit / Bottom limit / Top minus bottom, Dominant term (limit at infinity), Put in (limit by substitution), Leading terms (0/0).
- INTEGRAL steps (`isteps`, only a sum with V in the numerator only): each term through integ() alone, then the sum, then F(b), F(a), F(b)-F(a); kernels made on the way are let go (s->nk, s->top, s->lim restored) so the real run is unchanged.
- Nested commands: nested() writes a "First: inner" step when it replaces an inner command by its answer; the log keeps a base (st[64] count, st[65..66] used offset) so streset() rewinds to the steps before the outer command, not to zero.
- TRAP (host only): `sizeof(rat_t)` is 776 on the host and 722 on the eZ80, so a hard-coded log offset (0x4540) sat inside the host's pool and apply() of a kernel (cos(2) in a definite integral) zeroed the log. STOFF is now `SYMCE_WORK + 0x190` (0x4540 on the calculator, asserted under __ez80__); tools/engine_cli.c's work buffer is 0x5000 bytes, enough for both.

### steps( phase 3b/4: spelled-out rules, display (2026-10-09, branch steps)
- Rules 28..35 (SR_PF, SR_PD, SR_PC, SR_QF, SR_CHIN, SR_EXP, SR_SWAP, SR_NTH; menu.c R[] in the same order): product rule as F=/G=, F'=,G'=, F'G+FG'; quotient rule F=/G=; derivative of the inside for a chain; "multiply out" when the entry has a bracket after an operator/number; swap sides when V is only on the right; DERIV of order n shows each derivative. Product split: G = the last V-dependent kernel's nibbles, F = the rest of the term (`pstep`, only two V-holding things, rad 1). Token scratch for these is a pool slot cast to uint8_t*.
- SOLVE of a quadratic shows the discriminant only when it is not a perfect square (`sqok`) or negative: a factoring one goes Move -> Factor -> Set to 0.
- menu.c detok(): `^2`/`^3` (F0 32/33, not followed by a digit or '.') are drawn with the answer's glyphs (GetTokString of 0x0D/0x0F); the glyph char 0x0D is also usable in R[] strings ("b\x0D-4ac"). stwalk() redraws the command name (up to the first '(') of the entry line in 0x001F; PNGs from vram.png are grey, so steps.py counts 0x001F pixels instead.
- ENGTEST limit: the program loads at 0xD1A87F and must end before 0xD40000 (about 157 KB incl. stack/BSS); at 157,072 B it never started ("magic 0000 after 0 vectors" within 8 s). engine_device.py now compiles with `-DNODESOLVE -DNOSTEPSX` (engine.c drops the spelled-out step helpers: pstep/pqf/chstep/dnth and the Expand/Swap block); steps.py covers those on the device. PART = 1200.

### steps( round 2 (2026-10-09, branch steps)
- Installer (app/main.asm, installer.asm): PART_SIZE 65000, PARTS computed from the image, `assert PARTS <= 9`; findPart/the copy loop are generic. 7 parts would install (flash needs ~2x the app free). Image now ~383 KB of the 390,000 B six parts hold.
- SOLVE with no real roots under steps(): psteps logs Discriminant < 0 + "Negative: no real roots" and sets st[67]; symce_engine then closes the log with an EMPTY answer even though the result is the NO SOLUTION error (menu.c prints "No real solution" for an empty answer). hook.c .Lerr calls steps_show (blob+16) after copying the message to appErr, so the steps screen comes first and CLEAR/ENTER lets the error screen follow. Non-steps behaviour is untouched (st[67] is only set when MODE_STEP logs).
- Rule texts: char 0x01 in R[] stands for the answer's ² (detok of token 0x0D); a raw 0x0D draws as a different glyph.
- **Harness phase flukes (2026-10-09).** Four trace.py cases failed on rainbow-build (zhbox, CALC seq_val_wlo, DRAW txt2nd, DRAW txtCatLong) and passed on every earlier tree and alone. Not code: the failing case moves with image size, batch mix and a few ms of wait. Evidence: `seq_val_wlo` passes with the same sources up to a 612-byte pad before `tailStr` and fails from 613 on (nothing else changed); any `w37` in the settings prefix cures it; zhbox passes with `enter w200/w400/w500/w700` after the box corner and fails with `w300` (TI's own box zoom then runs full width); txt2nd/txtCatLong fail inside their 10-/4-case DRAW batch and pass alone, while txtCatPg or txtCatClr fail on other trees. Fixes are all in the harness: zhbox waits `w700`; the CALC slow retry also shifts the phase (`w37` before the NOCAS settings); `drawcmp` retries a failed batch case alone before calling it a difference. Rule: a case that passes alone on 9376eff and on the failing tree is not a regression; vary a wait before touching code.

### steps( round 3: linear systems
`linsys` (SOLVE/CSOLVE of `{..},{..}`) logs one step per Gauss-Jordan row move via `rowstep()`: SR_RPIV (row swapped to put the pivot), SR_RDIV (row scaled when pivot != 1), SR_RELIM (unknown cleared from another row). Each step prints the new row as an equation; param = row number. `tools/ineqlib.py` now encodes `{` `}` (0x08/0x09). The last step is a row, not the whole set.

### steps( round 3: radical SOLVE
`rsplit` logs SR_RISO (root alone on the left) and SR_RSQ (A^2 = B^2 P, then lsteps for the linear case); the squared polynomial then goes through psteps (the `!w` guard on `o` is gone, MOVE is forced when w). Roots `rok` rejects are logged after the solve as SR_RBAD ("Root % fails the check"), deferred with a `rej` bitmask so they come last. Test entry for sqrt in steps_check: `√X+1)` (0xBC includes the paren).

### steps( round 3: INTEGRAL by parts, u-sub, partial fractions
By parts: `pstepsp` logs SR_PARTU/SR_PARTV (u = ln or the polynomial, dv = the rest) before the term's Integrate step. u-sub: `usteps` after lsub() logs SR_USUB (u = the root's argument), SR_UINT (the integrand in u, the variable letter swapped to U via `s->var`) and its terms through isteps; calc2 then logs SR_UBACK. Partial fractions: `pfsteps` prints apart()'s split (SR_PFR) before integ(), `pfdone` the antiderivative (SR_PFI). `ilim` = the shared definite-limit steps, restores nk/lim. TRAP: running apart() on a scratch slot changed a later answer's term order (ln(3)/2-ln(4)/2+ln(2)/2 vs ln(3)/2+...): some code reads a fresh pool slot's n/d counts without setting them, so pfsteps zeroes `n.n`/`d.n` of the slots it used. Root cause not found.

### steps( round 3: DESOLVE
desolve() logs through `dsl()` (a vfm-formatted expression, no engine call) into `d->st` = work+STOFF when MODE_STEP is on (the inner engine() calls run without it). First order: SR_DFORM (y'=F), SR_DMU (the homogeneous C*e^(..) part, linear case) or SR_DSEP (H=K+C) with SR_DIC (C=value). Second order: SR_DCHAR (R^2+pR+q=0), SR_DROOT per root or SR_DCPX (real part param, imaginary part expression), SR_DHOM, SR_DPART. vfm maps `'` to T_PRIME. The final answer is the log's last entry as for every command.

### steps( round 3: nonlinear 2x2 systems
`nonlin` logs SR_NSOL (equation o+1 solved for one unknown) and SR_NSUB (that put in the other equation, `= 0`) via `ssteps`, then psteps on the substituted polynomial when it is a quadratic or more; no-root systems set st[67] like SOLVE.

- **Installer at 8-9 parts** (*measured* 2026-10-09, `instcheck`): steps2 = 489,596 B (8 parts); with stepscalc (+32,480) and mp-paren (+826) about 523 KB = 9 parts, the most `SYMCE1..9` can name (`assert PARTS <= 9`, 585,000 B). The installer itself has no other limit (part count only in `PARTS`, copy loop generic, 8 KB chunks). It needs the appvars archived (1 sector each) plus the app (~8 sectors) free at once, ~1.1 MB. The test ROM's old blank run (0x14..0x1F, 13 sectors) could not hold that, so lifecycle/crash/e2e failed at 8 parts: `screen.roomy` now also blanks the dumper's 0x0C..0x13, `bigapp.py` uses `WIDE` and a GC fixture of "a fully deleted sector just under the erased run, one sector per appvar above it" (valid to ~575 KB; at 584 KB prgmSYMCE's own archive entry takes the freed sector), and `make emu` pads to at least 530,000 B (9 appvars). `SYMCE_OLD=dir` runs the upgrade case over a real previous release's parts.

## calcgap item 1: DERIV nested chain, inverse trig, general powers (2026-10-10, branch calcgap)
- Tokens: sin⁻¹ 0xC3, cos⁻¹ 0xC5, tan⁻¹ 0xC7 (paren-openers like sin( 0xC2), π 0xAC. Engine names `T_ASIN/T_ACOS/T_ATAN/T_PI`; kernels `K_ASIN,K_ACOS,K_ATAN,K_PI` appended after `K_POW`. TRAP: every "is this token a function opener" range test (`T_LN..T_TAN` in nested(), the comma finder, opens(), width()) had to grow to `T_ATAN`; 0xC7 missing in nested()'s paren counter gave "SYMCE LIMIT NESTED". width(): the three inverse tokens print 6 Classic columns.
- `ps_t.kn` flags (set in calc(), DERIV only, 0 elsewhere): `KX_NEST` kernels inside kernel arguments, `KX_INV` inverse trig and π kernels, `KX_POW` power kernels away from home (`2^X`, `X^X`, `X^π`, `X^(1/3)`). INTEGRAL and home never see these kernels: arithmetic on the home screen is unchanged (`(~2)^X` still works).
- `K_POW`: kernel arg `ka` = base, `kb` = exponent; `kdep(s,k,j)` asks "does the kernel hold V in ka or kb". Derivative `dpow`: constant exponent b gives `b a' a^(b-1)` (denominator kernel `a^(1-b)` when b-1<0); general `a^b (b' ln a + b a'/a)`. DERIVAT at a point with K_POW is refused (evalat returns 0).
- Inverse derivatives: `oinv()` makes 1/√(1-u²), -1/√(1-u²), 1/(1+u²); answers are rationalized by the existing root machinery, so sin⁻¹' prints `-√(1-X²)/(X²-1)`, not 1/√(1-X²). `gsplit()` regroups a combined numerator by kernel part so `X·tan⁻¹(X)` prints `X/(X²+1)+tan⁻¹(X)`.
- Pool pressure: the pool is fixed at NPOOL=24 rat_t slots, so deep chains in steps run out (get() fails with top==lim). Mitigations: `dvar()` returns 1 for "the variable itself" with no slot, dord frees its a/b slots before dsteps, dsteps scratch z[] is 6. sin(X)^X is TOO WIDE with no steps.
- Steps: six rules appended at the end of SR_/R[]/RULES (306 total): K asin, K acos, K atan, K powc, K powb, K powl.

## calcgap item 2: INTEGRAL by general u-substitution, 2^X, X^(1/3) (2026-10-10, branch calcgap)
- INTEGRAL now sets `s->kn = KX_NEST|KX_POW` (item 1 had DERIV only); `INV` comes with item 3. New `ps_t.i0` = where calc() began reading f, so calc2 can re-read f (`s->i = s->i0; expr; reduce`) after integ() has eaten p. TRAP: do NOT keep a saved copy of p in a pool slot: one slot less is enough to push `INTEGRAL(3sin(X)^2,X,1,1/2)` into SYMCE LIMIT (NPOOL=24 is the RAM limit, work_fits/nest_fits asserts). Re-reading costs no RAM.
- calc2 fallback `us:` label, reached from the K_ROOT loop (lsub fails: nonlinear root argument or a second root) and when `integ()` returns 0 with no lsub: `top = top0; streset(st); re-read f; ipow() else usub()`, then `ic()`/`ilim2()` for the steps, `defint()` for limits, `result()`.
- `usub(s,out,p,j,lo)`: candidates for u in turn: (1) K_ROOT argument / nonlinear kernel argument (`rate()` is the linear test: nonzero = linear, skipped, integ handles those), (2) the whole denominator polynomial, then each irreducible-or-not factor of degree >= 2 from `fac()`, (3) a kernel of V itself (`u = sin(X)`, `ln(X)`, `e^X`; w is then the monomial of that kernel's nibble, `kp` = its nibble).
- `utry(w,kp)`: q = p / w' (`rmulinto inv`), `uconv()` rewrites q in a new variable that TAKES V's NIBBLE: V-parts in w's powers (`bexp()`: repeated `vrem` by w, digits become the exponent of the nibble), kernels of V made over with args in the new variable (`kern()` with converted arg, `mp[k]` map), K_ROOT with the same argument becomes the rad bit on the new variable (√u). For kernel-u (`kp`) the kernel nibble is moved to j (`mvnib`). Then ordinary `integ()` and `ubak()` (kernels' args back via `subst`, V := w). Right by construction, no derivative check; sympy fuzz (fz.py, 650 random f(g(X))g'(X)) found 0 wrong.
- `ps_t.pl` (u known positive, e.g. X²+1 or a root): pfrac's last term uses K_LOG instead of ln|u| there (`ln(X²+1)/2` not `ln(abs(X²+1))/2`). Definite integrals via usub are accepted only when the integrand has no pole possible (`lo` check) .
- `ipow`: INTEGRAL of c·a^b alone: number^(aV+b) -> a^b/(a'ln a) (2^X, 10^X, 3*2^(2X)); (aV+b)^e with e a number -> (..)^(e+1)/(a'(e+1)) (X^(1/3), X^(-1/2) gives 2√X). e+1 is kept as an unsigned kernel exponent with the `neg` flag exactly as dpow does. X^π needs KX_INV (item 3). Definite POW integrals are still LIMIT.
- vrem crashes on f with no term of V's degree (`d == 0`): `e^(√X)/√X` segfaulted until `bexp` guarded `vdeg(w,j)` in 1..254 and vrem got `!d`. Run an ASAN host build (`cc -g -fsanitize=address,undefined tools/engine_cli.c`) over fuzz after touching these.
- Steps (no new rules): `I usub` (u, slope) / `I uint` (integrand in U) / `I any` (integrate) / `I uback` / `I c` (or the limit steps). Pool-reusing rules from the lsub path.
- Known limits: X³√(X²+1) (X³ is no polynomial in X²+1), definite usub with a possible pole, sin(X)^X, X/(X²+4X+5) before item 3 (completing the square).

### ENGTEST two-build split (calcgap)
The ENGTEST program loads at 0xD1A87F and must end before 0xD40000 (~153 KB); a bigger one fails with "calculator did not finish: magic 0000 after 0 vectors". The engine outgrew it, so engine_device.py builds two images: default = `-DNODESOLVE -DNOSTEPSX -DNOSIDE` (no Geometry/Matrix/PARTFRAC; host reference `engtest/engine_cli_noside`), and `engine_device.py side` = `-DNOCALC` (no Calculus menu; runs only the SIDE commands, minus NORMALLINE which needs DERIV). Run both after any engine math change.

### calcgap item 3: arctan, arcsin, irreducible quadratics
INTEGRAL ps_t.kn now has KX_INV (sin⁻¹( cos⁻¹( tan⁻¹( π kernels parse). pfrac() calls qpart() for an irreducible quadratic factor to the first power, only on calc2's second try (ps_t.q = 1, set when ipow/usub fail), so X²/(X³+1) keeps its short usub answer. qpart(): (uV+w)/fq with (uV+w)·(D/fq) ≡ R mod fq (2x2 system, term_t fractions via fra/frm/frd), then u/(2F2)·ln(fq) + (w-uF1/(2F2))·2/√Δ·tan⁻¹((2F2V+F1)/√Δ), √Δ as s·√r with sqfree (fsq). iasin(): c·√U/(λU), U = -AV²+bV+c0 → c/(λ√A)·sin⁻¹((2AV-b)/√(b²+4Ac0)); a numeric content √q of U's argument arrives in the rad low bits. Steps: SR_IATAN (the arctan piece, shown after the split) and SR_IASIN. Size trap: 64-bit fraction helpers inlined 16 KB into qpart and broke the ENGTEST RAM limit; term_t fractions with NOINLINE ops cost ~2 KB. Whole item: app +9.6 KB (551,384 B).

## calcgap item 4: definite integrals with π and e limits (engine.c)

- `exact(s,out,h,a)` (called from `apply()` for sin/cos/tan and sin⁻¹/tan⁻¹, and from `mklog()` for ln) turns a kernel of an exact argument into a number: sin cos tan of k·π/12 where sin is 0, 1/2, √2/2, √3/2, 1 (table `sn_ sd_ sr_`, the 15° multiples stay kernels); sin⁻¹ of 1/2, √2/2, √3/2, 1 and tan⁻¹ of 1, √3, √3/3 as a π fraction (apply() already took the sign out); ln(e^k) = k. Only INTEGRAL/DERIV see π and e as kernels (KX_INV), so nothing else changes.
- `nsignk()` is `nsign()` for numbers with π and e in them (fixed point 2^-20, 3294199 and 2850325), used only by `cmpr()`: pfrac's "pole between the limits" test for `INTEGRAL(1/X,X,1,e)` needed the sign of e-1. `kdepx()`: a kernel other than π and e is in r; calc2's `lk` argument (the sin(X)cos(X) pre-pass) is now 1 when the limits hold only π/e.
- `lin()` (TCOLLECT/trigprod pre-pass in `integ`) used to reset `nk = 0` and rebuild sin/cos kernels, which scrambled the limits' π/e nibbles (wrong answers like `sin(2sin(2X))/4`). Now `ps_t.la/lb` (the limits, set in calc2 just before `integ`) are renumbered by `nlim()` after the rebuild; the old `kf > K_COS return 0` guard lets K_PI/K_E through only when `s->la` is set.
- `isteps()` (the step pre-pass) called `integ`, so `lin()` renumbered the kernel table behind calc2's back: **steps( INTEGRAL(sin(X)^2,X) answered X/2+sin(4X)/8 (wrong, pre-existing)**. Now isteps saves and restores kf/ka/kb, gives lin a dummy limit (`z`) so π/e survive, and when the table changed (trig products/powers) logs flag 2 (no steps) instead of garbled words. Plain answers were always right.
- Trap: the steps_check text syntax has no `e` constant: `e` (not before `^(`) is now token BB 31, shown as `e`; `e^X` is the K_POW (e)^X.
- Known limits left: `INTEGRAL(tan(X),X,0,π/4)` (ln|cos| kernel inside a kernel at a limit), `INTEGRAL(e^X,X,0,1)`, negative limits with π/e (`INTEGRAL(1/X,X,~1,~e)`), 15° multiples (sin(π/12)), sin⁻¹(1/2) etc. are exact only for positive arguments in apply()'s sign-out form, steps for trig powers/products (flag 2).

## gaps-de: DESOLVE linear with variable P, explicit Y, complex roots with a root (engine.c)

- Linear `y' = a y + b` with a non-constant `a`: `dsint()` writes tan(u) as sin(u)/cos(u) before INTEGRAL (INTEGRAL(-tan) alone gives -ln(tan²+1)/2), `eexp(d,r,A,m)` builds e^(mA) by hand: each `c ln(abs(u))` term becomes u^(m c) (so e^(ln|X|) is X, and 2ln|X| gives X²) and the other terms go under one `e^(` that is spelled out token by token, because the engine **cannot hold `e^(cos(X))`** (it answers PASS, which `q` counts as failure; the same for `ln(abs(cos(X)))` times anything). `emul` does mu·W term by term and drops a factor equal to e^(-A) (written out) so `e^(-X²)·e^(X²)/2` is `1/2`, which the engine does not do. dsfirst logs SR_DLP, DLM, DLX, DLI and then SR_DIV; it calls `dscomb` through a copy of ds_t with `q=1` so dscomb does not log its own SR_DYA.
- Trap: `q(d,r,...)` zeroes `r[0]` before it runs the engine, so a failed `q` whose `r` is also an argument destroys that argument. `dsmul` (r may be a or b) works in a local buffer for this reason.
- Trap: an answer that is built by hand must spell cos as the letters `BB B2 BB BF BB C3 (` (vfm char `@`), not the key token 0xC4 that `k` emits: strings that go through engine() are re-spelled by it, strings that skip it (dssecond's root-of-3 branch) are not, and engine_check's `readable()` then dies with `token c4`.
- Separable: `dssep` returns 255 when it could neither SOLVE nor take a square root; dsfirst then tries `dsexpl` and falls back to `dsimp` (the old implicit form). dsexpl re-samples at (0,0): there H is exactly tan⁻¹(y) for y'=f(x)(1+y²) (Y=tan(K+C)) or -1/e^(y) for f(x)e^(y) (Y=-ln(C-K)); otherwise H must be a sum of c ln|u|, then ∏u^(c/g) = C e^(K/g), SOLVEd with a letter D standing for the constant and D := C e^(K/g) put back (SOLVE cannot take function kernels). A letter, not the kernel, goes to SOLVE; the right side is then tidied by one engine call (`1/(1-Ce^(-X))` becomes `-1/(Ce^(-X)-1)`; a first answer that starts with a minus is solved again with D := -C e^(K/g), since C absorbs the sign: the logistic equation gives `1/(Ce^(-X)+1)`). Initial conditions: C = acc(y0)/E(x0); tan⁻¹ ICs stay unsupported because tan⁻¹(1) is only exact inside INTEGRAL/DERIV (`Y'=1+Y² and Y(0)=1` is SYMCE LIMIT, as before), and Y(0) with cos(0) likewise.
- dssecond: complex roots whose imaginary part is not a plain rational (`Y''+Y'+Y=0`) are written out as `e^(-X/2)(Ccos(√(3)X/2)+Dsin(√(3)X/2))`, only for the homogeneous equation without conditions.
- New rules appended after SR_IASIN: SR_DLP, DLM, DLX, DLI, DSX, DSI ('D lp' 'D factor' 'D multiply' 'D int' 'D exp' 'D inv' in steps_check.py).

## Answers wider than the screen (2026-10-11, branch wideans2, measured on 5.8.4)

- The OS cannot scroll a SymCE answer. The stock list answer is history record type 0x01 and scrolls by RIGHT **reading Ans**, not the record; SymCE answers are type 0 (Classic) or 0x18 (MathPrint) records made by dispResult 0x91FC2, which have no scroll state.
- Classic type 0: dispResult wraps an answer of 27..52 columns over two rows (right-aligned REAL, text in the answer buffer) but leaves the cursor on the **second** row (curRow 0xD00595). The OS counts a record as one row, so the next prompt is typed over the second row. The OS's own redraw of a history record (UP, UP) clips it to one row of 26 columns with the ellipsis glyph **0xCE** in the last column and draws entry and answer from the prompt row upward, so with a two-row answer on screen UP, UP overdraws the entry row with a mix ("solve(...) X=(.(5)-1").
- Fix in menu.c steps_show (flag 0x80 in the step log): after the view closes, in Classic, copy textShadow (0xD006C0, 26 bytes a row) of the first answer row (curRow-1), put 0xCE in column 25, PutS it back at column 0 (26 characters wrap the cursor to the next row), EraseEOL the second row, cursor at its column 0. Home then shows what UP's redraw would show, and UP, UP, 2nd ENTRY and a following entry all stay clean. The history record keeps the **whole** answer (type 0, length 24 tokens for SOLVE(X²+X=1,X)); only the screen is clipped.
- MathPrint type 0x18 draws one line cut off at x=320; scroll-back is clean (UP highlights the whole record band, as for any answer), so nothing is repaired there; the view shows it whole.
- Step log (STOFF 0xD13800) flag byte 2: 1 full, 2 unreliable, **0x80 = answer view** (no steps, entry kept up to 60 bytes, answer tokens after the steps; steps_show title "ANSWER", no "No worked steps" line). engine.c's symce_engine arms it when the answer is wider than the screen (`wfit`: Classic width(), MathPrint width() or mpwidth() by command) and the call was not MODE_EVAL/MODE_INNER or a steps( run.
- The view (242 px linear text, UP/DOWN scroll) shows any answer the engine builds, whatever its width; the Classic home row keeps its first 25 characters. TOO WIDE stays only where the engine itself cannot make an answer: NROOTS' list at fewer than 4 digits (engine.c putlist loop) and a matrix whose own emit refuses (MWIDE).

### Wide answers: MathPrint home after the view, keys in the view (2026-10-11, wideans2)
- MathPrint type 0x18 draws a wide answer as one line cut off at x=320 with no mark; UP redraws the same record highlighted, also unmarked. SymCE's `s_cut` (menu.c) finds the last dashed separator (two rows, 2 px on / 4 off, dark at x = 6k+2, 6k+3), walks up to the answer's rows (ink in x 296..319, gaps up to 4 rows), whites out x 306..319 and draws three 2x2 dots. The next OS redraw (UP, scrolling) repaints without the dots.
- The answer view (step-log flag 0x80) closes on any key but UP/DOWN, and the key is consumed (the OS never sees it). steps( keeps its narrower set (CLEAR, ENTER, 2ND, 0x30). ON does not come through os_getcsc, so it is not tested separately from the 2nd that follows it in wide.py.

## 14. The parser hook inside TI-BASIC programs (2026-10-11, branch basic-p0, measured on 5.8.4)

Spike: a log-only parser hook inside the SymCE flash app (tools/emu/basic_p0.py, main.asm symce_phook, armed with key 8 on the settings screen).
- Slot 0xD025F9, enabled by hookflags4 (0xD000B6) bit 1; the OS calls it through 0x023A6A -> 0x0257C1 (first byte 0x83) -> 0x0257CB (depth counter 0xD0265B, `jp (ix)`).
- A=0 fires: at a program's start (basic_prog 05:name, begPC..endPC = the whole body, endPC inclusive); at EVERY expression the program evaluates (each Disp argument, each While/If condition, each argument of a command); for a nested prgm's body (05:name); for the inner string of `expr("1+2")` (basic_prog 03:$, begPC = the string body, end-beg = len-1) AFTER the outer expression got its own A=0; for a Y1 equation (03:^) once per graph point (A=0, source 58 70 31 = X+1).
- A=2 (B=10 or 5D) fires too, when the OS saves or restores state around a nested program call; AsmHook2 only wants A=2 with B=5D.
- Stack: SP - stackBot is about 4036 at a program start, about 3982 at the deepest nesting, inside the hook with its own pushes. Plenty.
- Substitution works: at A=0 set begPC=curPC=buffer, endPC=last byte (inclusive). `Disp expr("99")+1` with "99" replaced by "5" shows 6. A buffer holding `"AB` is accepted as a string result of expr( (displays AB, also stored via ->Str1). The buffer may live anywhere in RAM.
- saveSScreen (0xD0EA1F, 21945 B) is NOT written by the OS during Menu(, Input, Pause, DispGraph in a program: a pre-filled pool stayed intact (a 0x3E1-byte part at its start and 0x4600 bytes after 0xD0F800). Safe as a scratch pool at those times.
- Swapping the program: at the program-start A=0 of A, copy the OP1 name of A2 into basic_prog (9 bytes: 05 + name) and point begPC/curPC/endPC at A2's body (ChkFindSym; data = size word + body). A2 runs, and after a nested prgmB returns the OS resumes in A2 (the saved pointers are A2's): output 1, 7, 3, 1 for A2 = Disp 1/prgmB/Disp 3/Disp expr("1"). So a hook can rewrite a whole program and nested calls keep it.
- Break: ON during `While 1:End` with the hook armed gives errNo 0x86 (BREAK), cx 0x52, the usual error screen; the hook returning Z every condition test does not block it.
- Errors thrown from the hook (appErr1 = "SYMCE" + 0, `jp JError`): at the program start or at a Disp-argument A=0 they show ERROR: SYMCE with 1:Quit 2:Goto (errOffset 7, OP1 and basic_prog restored to the program). Thrown at the INNER expr( A=0 (any of 0x2B, 0xAB, 0x82) the calculator resets (RAM cleared). 2:Goto after the hook-thrown error does NOT open the editor at the line: the first throw (code 0x2B) does nothing, the second (0xAB) wipes RAM ("RAM Cleared") on this build; a TI syntax error opens cx 0x46 at the right offset instead. So: throw at the outer argument level, and prefer Quit-only 0x2B-style errors until Goto is solved; never throw while inside the inner expr( parse.
- Coexistence: with symce_phook owning the slot, an EF 7B program and a `#` home entry still run through the chain to AsmHook2 (marker byte written, A=2 B=5D chained too), and lifecycle.py passes (ALL PASS).
- Build trap: tools/bin is a tracked self-referencing symlink (broken in the main checkout and worktrees), so fasmg is missing; copy it from another worktree and pass FASMG=... to make.

### 14.1 Safe error design for hook-thrown errors (2026-10-11, basic-p0, measured)
- A=1 is a parser-hook event the OS raises INSIDE the expr( command, after its argument is read and before the inner parse (SP 9 bytes deeper than the A=0 events, B=C=8E, begPC = the outer source `expr("...")` or `expr(Str1)`, cur-beg = end of it, basic_prog still the caller). Nothing fires after the inner parse returns, so there is no later "deferred" event inside expr(.
- The OUTER A=0 (the Disp argument) sees only the expression itself: `Disp expr("1")` gives begPC..endPC = `BB 2A 2A 31 2A 11` (end-beg 5), so a literal command string is readable there; an `expr(Str1)` shows `BB 2A AA 00 11` and the string is read through the variable.
- Throwing (JError, code in A) is SAFE at: the program-start A=0, a plain argument A=0 (`Disp 94`), and A=1 inside expr(. It RESETS the calculator (RAM cleared, hookptr 0) at any A=0 whose source contains expr( (outer `expr("84")`) and at the inner string's A=0, for 0x2B, 0x82, 0x88, 0x8D and 0xAB alike. A one-shot flag (no re-throw on hook re-entry) did not change it, so it is not a re-entry loop. A TI syntax error inside expr("1+") is normal (errNo 88, errOffset 2), so the cause is in how the OS handles a throw out of those two A=0 calls; PC at the reset was not traced.
- Quit-only code: 0x2B (bit 7 clear). The ERR screen shows it, 2 does nothing, 1 returns home with the hook still armed. Real TI codes (0x88 syntax) and the custom 0xAB (with appErr1 = "SYMCE",0 text) show Quit and Goto.
- Goto after a throw from A=1 WORKS for 0x88 and 0xAB: cx 0x46, the program editor, cursor at errOffset (10 = just after `expr("74")`). Goto after a throw from a plain-argument A=0 does not: 0x88 returns home (cx 40, OP1 garbage), 0xAB gave errNo 0E and a RAM wipe (E6). So throw from A=1, not from the argument A=0.
- errOffset after the OS reports the error = (curPC - begPC) & 0xFFFF at the throw, and basic_prog names the program Goto opens. In a swapped program (A replaced by A2 at A's start): a plain throw from A=1 shows A2 on the ERR screen and Goto opens the copy A2 at offset 13. To open the ORIGINAL: before JError write 05 'A' 00 into basic_prog, and set begPC = A's body (ChkFindSym on A, +2 for the size word) and curPC = begPC + offset in A. Measured: errOffset 1, Goto opens A (editTop D1A90D) with the cursor at 1. Quit goes home either way (A is never modified, A2 stays in RAM and must be deleted by the hook's own cleanup).

## SymCE commands in TI-BASIC programs: P1 (2026-10-11, branch basic-p1, measured on 5.8.4)
- `expr("FACTOR(X^2-1)")` in a program works: `symce_phook` (main.asm, armed by the installer and by APPS > SymCE, hands the slot back on CAS off) evaluates at A=1 (B=8Eh, inside expr(, OP1 = the string variable: `04 24 xx` temp or `04 AA 00` Str1; ChkFindSym on it gives DE = data, whose first two bytes are the length). `expr(Str1)` works the same. Plain `expr("1+2")` is untouched.
- ChkFindSym returns a full 24-bit DE: load the length with `ld de,0` first, or UDE (0xD0) makes it a huge length and the engine hangs the program silently (found the hard way).
- Engine side: MODE_PRGM = 0xC0 (both high bits) at symce_engine only; nothing outside string literals that is a command, or more than 250 tokens: returns 0 and the OS parses the string itself. Symbolic answers come back as `"`+tokens (a string), numbers as plain tokens. steps( is skipped. Commands are lowercase tokens (`BB B0..`, see cn() in engine_check.py), not typed letters.
- The answer waits at 0xD0EE80 (ansBuf, idle during a program) with PH_ARMED 0xD0EE62 / PH_LEN / PH_SLEN / PH_SUM 0xD0EE65; the next A=0 whose source has that length and byte sum gets begPC/curPC/endPC (endPC inclusive) pointed at it; every other A=0 is untouched, A=0 `#` entries and EF 7B programs and A=2 B=5Dh are chained to AsmHook2's parser hook (found with FindAppStart + the `83 B7 28` signature).
- Errors are thrown at A=1 only (an A=0 throw out of anything with expr( resets): the 26-byte message goes to appErr1, SYNTAX/ARGUMENT use 0xAB (Quit + Goto; Goto opens the editor, cx 0x46), the rest 0x2B (Quit only). ON Break still works (errNo 0x86, cx 0x52); a Y1 equation set by String>Equ( still graphs; an Asm program runs through the chain.
- Stack: at A=1 SP is 0xD1A821 (4003 B above stackBot). The engine's worst measured use is 3396 B, so the hook throws ERR:MEMORY when SP - stackBot < 3950 (nested deeper than a plain program).
- tools/emu/basic.py (T1-T7) is the device test.
- The parser slot is shared: the installer and APPS > SymCE put `symce_phook` there (CAS off gives it back to AsmHook2: 0x2241DA, measured; on again: ours). Deleting SymCE clears the slot, AsmHook2's registration included, so ASM programs run nothing until APPS > AsmHook2 is opened again (as after a RAM clear); lifecycle.py's "reinstall after delete" opens AsmHook2 first.
- RAM map 0xD0EE00..0xD0EF00 (checked against hook.c, fhook.c, main.asm): EE00 baseTop (3), EE03 baseOk, EE04 pending, EE05 inKey, EE06 tempArm, EE07 gotoFix (hook.c); EE08..EE1D fhook.c colour state; EE20 lastAns (length byte + at most MAXOUT = 64 tokens, ends EE60); EE62 PH_ARMED, EE63 PH_LEN, EE64 PH_SLEN, EE65..67 PH_SUM (phook); EE80 ansBuf = PH_OUT (the engine writes out[0..MAXOUT+1], 66 B at most: ends EEC1); EEF0 graph.c's _errno (graph only); EF00 the engine pool. In MODE_PRGM the answer is at most 65 B with the leading quote; MWIDE (0xFE) or anything over MAXOUT is turned into SYMCE LIMIT / WIDE (Quit only), so nothing reaches the pool.
- ChkFindSym's B is not the archive flag (it was nonzero for a RAM temp string): use `call ti.ChkInRam` (Z = in RAM) on DE, as the installer does; an archived Str1 is left to the OS.
- The installer hands the parser slot back to AsmHook2 (same as CAS off) before DeleteApp of an old SymCE; a plain MEM-menu delete clears the slot, so ASM programs need APPS > AsmHook2 again (lifecycle.py reopens it).
- Stack threshold now stackBot + 3950 (3396 worst measured by stack.py + 512 + 27 B call frame; worst-case left 618); top-level and 1-3 nested prgm levels still answer (basic.py T10).
- **Hand-typed programs (measured, tools/emu/basic.py H1-H5):** a program typed with ALPHA spells `FACTOR(` in six capital-letter tokens (46 41 43 54 4F 52), not the lowercase BB B0.. tokens only SymCE's menus type. Before the fix `Disp expr("FACTOR(X^2-1)")` showed `0` (the OS multiplies the letters, all 0) and `expr("SOLVE(X^2-4=0,X)")->Str1` errored, so P1 never ran for a user program. Now `symce_engine` in MODE_PRGM copies the entry into `work + SYMCE_WORK + 2*NSB` (free behind nested()'s buffers, below STOFF) with every exact command name from `cmds` followed by `(`, outside string literals, turned into the lowercase tokens; the rest of the engine is untouched. A name inside a string, a near miss (`FACTO(2)`) and the home screen stay the OS's. Stack.py worst after the change 3386 B used, 628 left.
- **The program editor has none of SymCE's menus (measured, edit buffer + screen):** reached with a Goto from an error (cx 0x46). `2nd MATH` is the stock MATH/TEST menu (`2nd MATH 1` inserts `=` 6A, inside quotes too); `ALPHA` then `DOWN` is just ALPHA + cursor-down (the cursor moves a line, a following `1` types a digit). No SymCE command can be inserted in the editor by SymCE's own keys; only capital letters typed by hand (now accepted in expr( strings).

## Bare SymCE commands in TI-BASIC programs: P2 (2026-11-02, branch p2-bare, measured on 5.8.4)
Lines typed with ALPHA letters, `FACTOR(X²-1)→Str1` or `Disp EXPAND((X+1)²)`, give the same answer `expr("...")` did in P1. The user's program is never written: it runs as a hidden, protected, OS-made copy and is left byte-identical (N1..N7 check the original's VAT data pointer and body bytes, and that no copy is left after the next home entry).
- **Hook events (measured):** A=0 fires at program start (basic_prog `05/06:name`, begPC=curPC=body, endPC inclusive) and for every statement expression (a temp string `03:$`). A=1 fires inside `expr(` with B=8E; basic_prog and the PCs there can be the copy's or a temp string's. A=2 with B=10h fires when a nested `prgm` returns or a sub-evaluation resumes. There is no event at program end. `ti.JError` works at A=1 only.
- **Design (symce/app/main.asm `phStart`):** at the program-start A=0 (type 5/6, name[0] != 21h, begPC == curPC): fast reject (a `(` after 3 capital letters, length 5..8191, stack and level checks), `phFindOrig` (RAM program whose body == begPC), engine count pass (`MODE_WRAP = 0x80`, out = NULL), `EnoughMem(len+400)`, `CreateProtProg` of the copy (name `06 21 '1'..'3'`, one per nesting level, so recursion works), fill pass, then `basic_prog := copy`, `begPC=curPC=copy body`, `endPC = body+len-1`. The original's basic_prog is saved per level. Because the OS now believes it runs the copy, its own nesting save/restore resumes the copy after a `prgm` returns. Anything oversize, too little RAM, a 4th simultaneous level or any failure falls back to plain OS behaviour; nothing is ever written to the archive.
- **Copy placement (measured):** the OS puts the new program right after the original's data (higher address), so the original's VAT pointer and data stay put (VAT entry ascending: name, len, ptr hi..lo, 00 00, type; e.g. `43 01 D2 A8 A9 00 00 05` for C, the size word at D2A8A9).
- **Engine (symce/src/engine.c `wrap_bare`, tools/engine_cli.c):** `wrapinfo {uint16 len, tgt, orig}` at PH_WI. Count pass (out NULL, tgt 0xFFFF) sizes the copy, fill pass writes it, map pass (tgt = copy offset) returns `orig`, the source offset of a copy offset. Lengths are 16-bit; `WRAPMAX` 8000. The `WPUT` macro must evaluate its byte unconditionally, because the source read has side effects (a count pass that skipped it hung). engine_check.py has the map queries (copy offsets 0, 9, n-1 map; n does not), a 4510-byte wrap and a >WRAPMAX PASS.
- **Cleanup:** copies are deleted at the next `#` home entry, at `phookArm`, on a SymCE error, and when a level is reused (`phCleanAll`, by name, then LVN=0). Deliberately not at "next program start": deleting there would shift memory under a running program. A level is released when execution resumes into a copy (A=2, B=10h); resuming into an unwrapped program does not pop it, so such a leftover waits for the next `#`.
- **Errors (`phErrFix`):** SymCE error at A=1 in the copy: A=1 can have a temp string as basic_prog, so the level comes from LVN and, if begPC is not inside the copy, the temp string is located as a substring of the copy (cpi loop). The engine map pass converts the copy offset to the original's, `basic_prog` gets the original name back, the copies are deleted, begPC/curPC/endPC point into the original, and the existing 2:Goto machinery (0xAB) opens the ORIGINAL at the exact byte. Measured: B6/N4 offset 7 for `Disp POLYGCD(X)` (the call's first byte; the old design reported 14), editor text is the unwrapped source (`PROGRAM:C`). Failure of the lookup falls back to Quit only (0x2B).
- **OS errors in a copy (measured):** the copy is protected, so the OS offers 1:Quit only. 1/0: the menu ignores 2 (cx stays 0x52, errNo 82). ON: errNo 86, 2 goes home (cx 40), never to an editor. The user can never edit the copy.
- **Programs without bare calls, near misses and strings:** the cheap reject leaves them alone (`I+1→A` costs a few µs); `"FACTOR(X)"→Str1`, `A(B+1)`, `2X(X+1)` stay the OS's. basic.py B11 loop timing unchanged.
- **Tests (tools/emu/basic.py):** T1..T10, H1..H5, B1..B11 unchanged (B6 offset 14 -> 7), plus N1 normal end, N2 ON + 1:Quit / ON + 2, N3 1/0 + 1:Quit / 2, N4 SymCE error + 2:Goto (then 2nd MODE out of the editor), N5 387-byte program (all lines print), N6 nested A/B both with bare calls, N7 a program calling itself once. N-tests read the original's VAT pointer and body before/after and check no `!1..!3` copy is left in the VAT after the next home entry. A freed copy's bytes stay in RAM, so only the VAT says whether one exists.
- **Limits and risks:** one copy per nesting level, 3 levels; programs over 8000 bytes of wrapped output or with under len+400 B free RAM run unwrapped (bare commands then fail like in plain OS). A copy can survive a program that was left some other way until the next `#`.
- **Speed/stack/size:** loop benchmark unchanged (B11); stack.py worst 3396 B used, 618 B left (LEFT_MIN 512, desolve_quad_forced Classic, no BAD entry); app image 656,442 B, 14 parts, 299,108 B of appvars.
- **UP after a program loses its Disp lines (measured 2026-11-02, build 6):** the home history keeps only the entry (`prgmC`) and its answer; UP redraws from history, so Disp lines go. A plain program with no SymCE command does the same, so this is stock OS behavior.
- **A program whose last line is a bare SymCE command or store (measured 2026-11-02, build 6, basic.py L1-L5):** the OS shows the program's last value in place of `Done`. In P2 named copies, `phWrap` wraps the bare call into `expr("CMD(...)")` in the copy `!1`. When the copy finishes, the OS evaluates the expression or store and stores its string result in `Ans` and the history record (`0xD008EE`: type `0x04`, length byte + tokens). The result appears on the last row right after `prgmC` and Disp lines. On pressing UP, the highlighted history answer is that result, and subsequent HOME `#` cleans all copies (untouched ok). Nested programs returning to a caller ending in Disp show `Done` (L3). The earlier report of `0` was an artifact of testing with stale binaries without P2 active (where ALPHA letters evaluate as multiplied variables).

## The grapher on the full 320x240 screen (2026-11-03, branch graph-full, measured on 5.8.4)

- **Geometry:** the plot is columns 0..318 (column 319 is the border colour) and rows 0..239 (`SH`). Scale: column = round(gx*318/264), row = round((164-gy)*`PH`/164) with `PH` = 239 (`PW` = 319 and `PH` = 239 are bounds minus one: 318 and 239 are the scales). The x axis lands on row 120 (was 109), a circle of radius 5 tops out at row 60 (was 55).
- **Text over the plot:** TI's bar (grey, below the plot) is gone; the prompt/result line (small font, 12-row cell at y=214, `PY`) and the X=/Y= line (y=226, `BARY`) now sit over the plot. Their cells paint white, so the y axis (column 159) shows between glyphs outside the cell: tests that read the bar text ignore black column 159.
- **PBUF (save buffer under the prompt line):** 0xD0FB20, a byte per pixel (palette index 0..15, or 16+i for that colour's shade over white, 11 = white), 12 rows x 168 = 2016 B, exactly the room below SB (0xD10300) (`_Static_assert`). A pixel that is neither (two shades overlapping) comes back white. Build the 32-entry colour table once per pass: calling `pal()`/`pshade()` per pixel and per index cost ~150 ms per bar (TRACE took 400 ms to show its bar, table.py and trace.py timings broke).
- **ENTER in the X=/T= box redraws the plot** (the box, 19 rows at y=220, covers the plot's foot): the label strip is saved and drawn again (`labsave`, `tlabel`). Cost: one full draw (Par, 3 curves ~1 s; recursive Seq several s). TI redraws too.
- **Timing in tests:** keys typed during a draw are read after it, so a wait after `enter` on a typed number must cover the redraw (`trace.py` `WR`, w1500; recursive Seq w5000).
