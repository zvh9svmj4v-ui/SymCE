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

1. **A=1, any key:** `pending = 0`. On the home screen, set `textInsMode` if
   `editTop == baseTop` (cursor on the entry, not in a box or menu). There,
   RIGHT LEFT UP CLEAR DEL 2nd-LEFT 2nd-RIGHT (`01 02 03 09 0A 0E 0F`) go
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
   engineWork, lastAns, mode)` (mode: `mpFlags & 0x20 | (iy+0x1A) & 1 | trigFlags & 4`) through the relocated pointer stored right after the
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
bit 4 is cleared by the OS's handling of RIGHT, LEFT, UP, CLEAR, DEL,
2nd-LEFT/RIGHT (`0x0E`/`0x0F`) and ENTER, Classic and MathPrint alike (DEL:
`res 4,(iy+5)` at `0x58A8A`; CLEAR and ENTER through the new-line routine,
`set 5,(iy+12)` / `res 4,(iy+5)` at `0x58D0D`/`0x58D11`). DOWN and typing
leave it; 2nd INS (`0x0B`) toggles it. The cursor draw (`0x5C80A`) picks
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
  failed entry not filed, `cxCurApp` `0x40`, `0xD0265B` back to 0, and
  `2X+2X` answers `4X`. No reset. Classic keeps the failed entry's line on
  screen with no answer (the OS does the same for its own errors).
- `2nd ENTRY` after Quit recalls the failed entry (`90X`).
- `2:Goto` (with `0xAB`) goes home with the entry in the edit buffer, cursor
  at its start (`begPC`/`curPC` are stale at A=2). Harmless, but `0x2B` avoids
  offering it.

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
- **SymCE app.** Appvars `SYMCE1`, `SYMCE2`, ... carry the app image (65,000
  bytes each after a 4-byte build id); `prgmSYMCE` (a few KB) writes it below
  the last app and arms `symce_hook` inside it. Opening SymCE from APPS toggles
  the hook. A RAM clear zeroes the hook pointer but keeps the app (flash);
  opening the app re-arms it.
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
  variable holds about 64 KB, so the image rides in appvars `SYMCE1..9`
  (65,000 bytes each, up to 585 KB by format) that `prgmSYMCE` reads where
  they lie, archive or RAM, and writes through `pixelShadow` 8 KB at a time
  (flash cannot be written from flash). Every part is checked (there, right
  size, same FNV-1a build id) before anything is deleted, and each is found
  again right before it is copied, since the GC moves them. A 132,000-byte
  app (`make PAD=132000`, three appvars) installed at `0x1EEBC1`, across
  sectors `0x1E..0x20`, with a CRC equal to the image relocated for there,
  and answers `2X+2X` -> `4X`, also after a RAM clear and reinstall.
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
  (install, toggle, RAM clear, re-arm), `bigapp.py` (the app padded to at least 132 KB: fresh,
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
