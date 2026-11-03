# Using SymCE

SymCE is a **flash app**: OS 5.8.4 resets the calculator whenever a hook
points anywhere but inside a flash app, so the hook lives in the app.
Everything learned about the calculator is in `docs/TI84CE-KNOWLEDGE.md`.

Build: `make -C symce` gives `symce/bin/SYMCE.8xp` (the installer) and
`symce/bin/SYMCE1.8xv`, `SYMCE2.8xv`, ... (the app, compressed, in pieces).

---

## Install (once)

1. **Send `symce/bin/SYMCE.8xp` and every `symce/bin/SYMCE#.8xv`** (as many
   as the build made; **send them to the Archive**, at about 300 KB the
   appvars no longer fit in RAM together), then run `prgmSYMCE` through
   AsmHook (OS 5.8.4 blocks `Asm(`; AsmHook is one way back in). No reset needed first; the installer replaces the old
   hook and deletes v15's leftover appvars (`SYMCEHK`, `SYMCEPV`).
2. It should say **"SymCE app installed. SymCE is ON"**. It checks everything
   before it deletes or writes anything, so every other message leaves the
   calculator as it was, old SymCE included:
   - *"Appvar SYMCE2 is missing."* Send every `SYMCE#.8xv` from the same build.
   - *"Appvar SYMCE2 is from another SymCE build."* The pieces come from two
     builds; send them all again.
   - *"Appvar SYMCE2 is not a packed part"*, *"A SYMCE appvar is damaged"*, *"The
     SYMCE appvars do not add up to one whole app"*: the files are from an
     old build, were damaged in transfer, or one is missing. Delete every
     `SYMCE#` appvar, send the whole set again and run `prgmSYMCE`.
   - *"Not enough RAM: SymCE unpacks its app in free RAM"* (it shows Free and
     Need in hex bytes). The appvars are compressed and the installer unpacks
     one part at a time in free RAM, up to about 66 KB. Delete or archive
     variables (2nd mem 2), then run it again.
   - *"No blank flash below the last app. ... Delete an app or archived
     files."* The installer already garbage collected the archive for you
     (that is the pause); there is still no room for the app. Free some
     archive (2nd mem 2) and run it again. The appvars are compressed to
     about 46 % of the app, so installing needs about the app, plus 46 % of
     it for the appvars, plus 128 KB spare: roughly 1.1 MB free archive for
     today's 660 KB app (it used to be about 1.3 MB: the appvars took as
     much as the app).
   - *"SymCE was built for OS 5.8.4 and does not know this one. Nothing was
     written."* Check the OS version (`2nd mem 1`). SymCE calls two
     undocumented OS routines and refuses to arm on any OS it has not checked.
3. **APPS -> SymCE** opens a settings screen (opening it changes nothing;
   any other key, such as `CLEAR`, leaves it). Press:
   - `1` to turn CAS (the SymCE hook and its 2nd MATH menu) on or off. After a
     RAM clear the hook is gone (the app is not); open SymCE from APPS and
     press `1` to turn it back on.
   - `2` to switch the cursor between INSERT (SymCE keeps the entry line in
     insert mode) and TI (the OS's own cursor, untouched). Only while CAS is
     on; ALPHA+DOWN opens the menu either way.
   - `3` to switch the Evo-style font (a Roboto-derived replacement for the
     calculator's text) between Evo and TI. It is independent of CAS.
   - `4` switches the **GRAPH key** between SYMCE (the default the installer
     sets) and TI. With SYMCE, GRAPH opens SymCE's grapher from the home
     screen, Y=, WINDOW and the other screens that would go to the OS graph
     (an equation still being typed in Y= is filed first). It looks like TI's
     graph, only bigger: a 319x240 plot (the whole screen) with the axes, tick marks, grid
     (GridDot or GridLine), axis labels, the 15 Y= colours and the line
     styles (thin, normal, dot-thick, dotted) from FORMAT and Y=, Xres, and
     Detect Asymptotes. It does Function, Parametric, Polar and Seq (Time
     format) graphs, and the stat plots Scatter, xyLine, Histogram, Box,
     ModBox and NormProb. It draws in float32 (0.2-1.1 s; the OS graph
     1.5-7.5 s: X 0.3 s against 1.9, sin(X) 0.56 against 2.8, three functions
     1.1 against 7.5). Anything it cannot compute (a function it does not
     know, a list, `Ans`, an archived Yn) is handed to the OS parser for that
     point, so the graph is still right, only slower; ON stops a slow draw. X and
     Y (T, theta, R) are set as TI sets them, and `Ans` is untouched.
     It hands the GRAPH key to TI's own graph when the screen is split, a
     background image is on, a selected Yn has the line style
     above/below/path/animate (in Par and Pol also the dot styles, in Seq
     anything but dot), or TI has drawn on the graph (Text(,
     Tangent(, DrawF, Shade(, Pxl; it stays TI's until the window or an
     equation changes). Also: in Seq mode the Web format, TRACE, ZOOM and CALC in the uv, vw and uw formats (the graph itself is SymCE's), a
     term that uses u, v or w other than as u(n-1), and any stat plot; any
     stat plot SymCE cannot draw (a list that is not L1..L6, or one the OS
     would raise an error for); ZoomFit in Seq; and ZoomStat
     with no plot on or with all the data equal.
     Implicit equations: a Yn with the letter Y (ALPHA 1) that is a relation, for
     instance `X^2+Y^2=25`, `Y>X^2`, `X^2+Y^2<=9`, or relations joined with and / or
     (`Y>=X and Y<=4`), is drawn as a curve (`=`) or a tinted region with its boundary
     (solid for <= and >=, dashed for < and >), under the other curves. TI's own graph
     says ERR:INVALID for those. TRACE and CALC skip them. About 0.6-1 s each.
     Keys on the graph: an arrow shows TI's free cursor (X=, Y= in the bottom
     bar); TRACE (also from home, Y= ...) traces the lowest selected function,
     left/right step TraceStep and pan at the edge, up/down change function, a
     digit opens the `X=` box, ENTER recentres (QuickZoom, ZPrevious works). With a
     Scatter or xyLine plot on, TRACE walks its points; with a Histogram, Box,
     ModBox or NormProb on, TRACE is TI's.
     CLEAR and 2nd QUIT go home; Y=, WINDOW, MODE, FORMAT, TABLE, TBLSET and
     GRAPH do what they do on TI. ZOOM and ZOOM MEMORY are TI's own menus;
     every item but SetFactors (and ZoomStat in the cases above) zooms on SymCE's big graph with TI's
     numbers: ZBox (ENTER, move, ENTER), Zoom In / Zoom Out (cursor, ENTER; XFact,
     YFact), ZDecimal, ZSquare, ZStandard, ZTrig, ZInteger (cursor, ENTER),
     ZoomFit, ZQuadrant1, ZFrac1/2 to 1/10, ZPrevious, ZoomSto, ZoomRcl. They work
     from home too (while GRAPH is SYMCE) and when TI's graph is the fallback they
     simply run TI's. 2nd CALC is TI's menu; its 7 items (value, zero, minimum,
     maximum, intersect, dy/dx, integral) run on SymCE's big graph with TI's prompts and
     numbers (errors like NO SIGN CHANGE are TI's); in Par and Pol the dy/dx, dy/dt,
     dx/dt and dr/dtheta items too. 2nd DRAW from the grapher: ClrDraw, Line(,
     Horizontal, Vertical, Circle( (centre, then a point on it; 60 chords, as TI), Pen (ENTER lowers or lifts the pen, every cursor move draws a 1-px line) and Pt-On, Pt-Off, Pt-Change are SymCE's; Tangent(,
     DrawF, Shade(, DrawInv, Text(, Pxl- and STO are TI's. MATH, STAT, VARS, APPS and the other menu keys
     open their menu over the home screen; CLEAR in such a menu comes back to the
     graph, 2nd QUIT goes home. In an
     out-of-window X= error, 2:Goto goes home (TI re-enters its graph). A RAM clear turns the key back to TI; press `4` to re-arm it.
     **TABLE (2nd GRAPH)** also opens on SymCE's own table while the GRAPH key
     is SYMCE: the look, colours, header, TblStart / ΔTbl, number format and
     the full value of the selected cell on the bottom line are TI's. Cells are
     worked out by the OS evaluator and formatted by the OS routine, so they
     equal TI's; it only draws faster. Arrows scroll rows and columns (TI's
     rules: up at the first row of the X column scrolls TblStart, up in a Y
     column selects the header), the selection and TblStart are kept between
     visits like TI. 2nd QUIT goes home, GRAPH opens SymCE's graph, TRACE its
     trace, Y= / WINDOW / MODE and the rest do what they do on TI. TI's own
     table takes over (same look, TI's speed) in Par, Pol and Seq mode, with
     Indpnt or Depend set to Ask, with a split screen, and for editing: ENTER
     on the header, `+` in the X column, or typing a digit. The key that
     opens TI's table is not replayed, so press it once more there (TI opens
     on the cell it last had, not necessarily the one you were on). Speed
     against TI (emulator, 4 Y columns): first draw about 420 ms against
     400 ms (OS evaluation of the cells is the bulk, ~3 ms per cell), one-row scroll
     20 ms against 80 ms, column scroll equal. The last digits of TblStart
     after scrolling with a repeating step like 1/3 can differ from TI's in
     the 13th digit.
   - `5` runs `prgmSYMCE` (the installer) from the app itself, so it works
     with no AsmHook2, even right after a RAM clear (the program is archived as
     sent, so it survives one). It needs free RAM about the size of the program
     (a few KB) and says so if there is not enough, or if `prgmSYMCE` is
     missing or is not the installer. Any key returns to the settings.
   The screen is redrawn after every press, in the font just chosen.
4. **Upgrading** = send the new files and run the new `prgmSYMCE`. It deletes
   the SymCE app that is there and writes the new one. A RAM clear does *not*
   remove the app (it lives in flash), which is why older installers said
   "already installed" and left you on the old build with SymCE off
   (`2Z+2X` -> `0`). Once SymCE is installed, run the new `prgmSYMCE` from
   **APPS -> SymCE -> 5**: no AsmHook2 needed, and it works after a RAM clear
   too as long as `prgmSYMCE` is still archived (it is, as sent). The *first*
   install still needs AsmHook2, and typing `prgmSYMCE` at the home screen after
   a RAM clear gives `ERROR: INVALID` until AsmHook2 is opened from APPS.

`prgmSYMCE` and the `SYMCE#` appvars can be deleted after installing, to free
archive; the app is all that's needed. Keep them if you'll want to reinstall.
Deleting other apps is fine too: the calculator shifts SymCE in flash and
moves its hook along with it. (Keep `prgmSYMCE` if you'll want to upgrade: it
runs from APPS -> SymCE -> 5.)

---

## Try it

Classic or MathPrint, either works. These should simplify:

```
2X+2X            -> 4X
(X+1)^2          -> X²+2X+1
2X+3Y+X          -> 3X+3Y
(X+Y)(X-Y)       -> X²-Y²
X/2+X/3          -> 5X/6
6X/3             -> 2X
X^3 with the MathPrint exponent box, then + X   -> X³+X
X n/d 2 (alpha y= 1)                            -> X/2
(X²+2X)/X, even while X is 0                    -> X+2
1/X+1/X, even while X is 0                      -> 2/X
(X²+5X+6)/(X²-4)                                -> (X+3)/(X-2)
X/(X+1)                                         -> X/(X+1)
X^-2  (the (-) key)                             -> 1/X²
X⁻¹ (the x⁻¹ key), (X+1)⁻¹                     -> 1/X, 1/(X+1)
(X²-Y²)/(X+Y)                                   -> X-Y
(X²-Y²)/(X²-2XY+Y²)                             -> (X+Y)/(X-Y)
(X²-Y²+X-Y)/(X²-Y²)                             -> (X+Y+1)/(X+Y)
(X+1)^6 in MathPrint                            -> X⁶+6X⁵+15X⁴+20X³+15X²+6X+1
.5X+.5X, 1.5X+X, .1X+.2X                        -> X, 2.5X, 0.3X
X/3+.5                                          -> X/3+0.5
X/4 with MODE ANSWERS: DEC                      -> 0.25X
ENTER on the empty line right after 2X+2X       -> 4X again
2nd ENTER        -> brings back 2X+2X, exactly as typed
X^10+X^10                                       -> 2X^10
X^X, X^X*X^X, 2^X*3                             -> X^X, (X^X)², 3*2^X
```

Square roots of one term:

```
√(8)             -> 2√(2)           √(12X)+√(27X)  -> 5√(3X)
1/(1+√(2))       -> √(2)-1          √(X³)          -> X√(X)
```

**Commands.** On the home screen press **ALPHA then DOWN**: an **Algebra**
menu pops up, laid out like the TI-Nspire CX II CAS one:

```
1:solve(   2:factor(   3:expand(   4:deriv(
5:Polynomial Tools ►   Find Roots of Poly, Real Roots of Poly, Complex Roots
                       of Poly, Remainder of Poly, Quotient of Poly, Greatest
                       Common Divisor, Coefficients of Poly, Degree of Poly
6:comDenom(
7:Convert Expression ► ►ln ►logbase ►exp ►sin ►cos
8:Trigonometry ►       tExpand( tCollect(
9:Complex ►            cSolve( cFactor( cZeros(
0:Extract ►            left( right(
```

Pick with the number, or the arrows and ENTER. ENTER or RIGHT on a `►` item
opens its submenu; LEFT or CLEAR goes back up, and CLEAR at the top closes the
menu. A pick types the command at the cursor in lowercase and the home screen draws
it **blue** (`polydegree(`, `toexp(`, `czeros(`...). Only the menus type a command:
spelling `SOLVE(` with ALPHA letters is the variables S, O, L, V and E, a product like
any other, so you always see which words are commands. (TI's own lowercase names, such as
`sin(` or `abs(`, and `Done`, turn blue too. Blue is drawn only on the home screen, not in
a menu, and the OS draws old text black again when a TI menu or MODE closes over it; what
you type next is blue.)
The same ten items are an **Algebra tab in 2nd MATH**, after TEST, LOGIC and
CONDITIONS (LEFT three times from TEST; its title there is `Alg`, since the full
word does not fit): a command types, a `►` item opens the popup at that
submenu. The tab works inside MathPrint boxes too.

**+ shows help.** With a SymCE tab open in 2nd MATH, pressing `+` on a command
opens a SymCE CATALOG HELP screen: the command, its syntax (optional arguments
in brackets), a one-line description and an example. ENTER pastes the command
as if you had picked it; CLEAR or ALPHA goes back to the menu on the same item;
2nd QUIT goes home. On a `►` submenu item `+` does nothing. TI's own menus keep
TI's `+` help. The same `+` works in the ALPHA+DOWN popup, on every command, in its
submenus too: ENTER pastes, CLEAR or ALPHA returns to the popup on the same item,
2nd QUIT closes it.
Then type the rest and ENTER:

```
FACTOR(X²-4)         -> (X-2)(X+2)
FACTOR(X^4-1)        -> (X-1)(X+1)(X²+1)
FACTOR(6X²-X-2)      -> (3X-2)(2X+1)
FACTOR(X²-Y²)        -> (X-Y)(X+Y)
FACTOR(AX+AY+BX+BY)  -> (A+B)(X+Y)
FACTOR(X²-2)         -> (X-√(2))(X+√(2))
FACTOR(12)           -> 2²*3
EXPAND((X+1)³)       -> X³+3X²+3X+1
SOLVE(X+2=4,X)       -> X=2
SOLVE(Y+4=8,Y)       -> Y=4
SOLVE(X²=4,X)        -> X=-2 or X=2
SOLVE(X²=2,X)        -> X=-√(2) or X=√(2)
SOLVE(X³-X=0,X)      -> X=-1 or X=0 or X=1
SOLVE(X+2Y=3,Y)      -> Y=(3-X)/2
SOLVE(√(X+1)=X,X)    -> X=(1+√(5))/2       (1-√(5))/2 fails the check, so it is dropped
SOLVE(abs(X-1)=2X,X) -> X=1/3
SOLVE(√(X)=-3,X)     -> ERROR: NO SOLUTION
```

SOLVE also takes an inequality, with `<` `>` `≤` `≥` `≠` (2nd MATH 2..6) in place of the
`=`. It works on a polynomial or a rational function of the one letter, by a sign chart
over its exact real roots (radicals included); poles are left out, and a root is kept only
when the test has an equals sign. An interval is written in the chained form
`-1≤X≤3` (Nspire style); two rays are joined by `or`:

```
SOLVE(X²-4>0,X)         -> X<-2 or X>2
SOLVE(X²-4<0,X)         -> -2<X<2
SOLVE(X²-2X-3≤0,X)      -> -1≤X≤3
SOLVE(X²-2X-3≥0,X)      -> X≤-1 or X≥3
SOLVE((X-1)²>0,X)       -> X≠1
SOLVE((X-1)²≤0,X)       -> X=1
SOLVE((X+1)/(X-2)≥0,X)  -> X≤-1 or X>2
SOLVE(1/X<1,X)          -> X<0 or X>1
SOLVE(X³-X<0,X)         -> X<-1 or 0<X<1
SOLVE(X²>2,X)           -> X<-√(2) or X>√(2)
SOLVE(X²+1>0,X)         -> ERROR: ALWAYS TRUE
SOLVE(X²+1<0,X)         -> ERROR: NO SOLUTION
```

One `abs(` is allowed when the rest is a number: `A+B*abs(S)` over a polynomial, by
squaring (`abs(S)<c` becomes `S²<c²`, and a negative `c` is all reals or nothing):

```
SOLVE(abs(X-1)<3,X)     -> -2<X<4
SOLVE(abs(2X+1)≥5,X)    -> X≤-3 or X≥2
SOLVE(abs(X-1)≠3,X)     -> X<-2 or -2<X<4 or X>4
SOLVE(abs(X)≤0,X)       -> X=0
SOLVE(abs(X-1)>-1,X)    -> ERROR: ALWAYS TRUE
```

Limits: `abs(` times X or against another X-expression (`abs(X-1)<X`), square roots
or other kernels inside an inequality, and a polynomial with non-real factors of degree 3 or more, give `ERROR: SYMCE LIMIT`. A factor that cancels
between top and bottom (`(X-1)X/(X-1)`) is cancelled before the chart, so its hole is not
reported. CSOLVE takes `=` only.

```DERIV(X³)            -> 3X²
DERIV(X²Y,Y)         -> X²
DERIV(1/X)           -> -1/X²
```

SOLVE and CSOLVE use the values you have stored, as the calculator's own
solve( does: every letter but the one you solve for is replaced by what it
holds, and nothing stored is changed. Letters with nothing stored (after
`DelVar X`) stay letters:

```
5->X, then SOLVE(X+2Y=3,Y)     -> Y=-1           (X still holds 5)
2.5->X, then SOLVE(X+2Y=3,Y)   -> Y=1/4
5->X, then SOLVE(X²=4,X)       -> X=-2 or X=2    (the letter solved for is ignored)
DelVar X, then SOLVE(X+2Y=3,Y) -> Y=(3-X)/2
```

A stored value must be a short decimal (5, -3, 2.5). Anything else, such as
`1/3->X` (stored as 0.33333333333333) or a complex number, gives
`ERROR: SYMCE LIMIT`; store a simpler value, or `DelVar` it, to solve. The
other commands, and plain entries like `2X+2X`, never look at stored values.

SOLVE and CSOLVE also take a system: a list of equations (2nd `(` and 2nd `)`
make the braces) and then a list of the letters to solve for, up to 6 of each.
The answer names the letters in the order you gave them:

```
SOLVE({X+Y=3,X-Y=1},{X,Y})             -> X=2 and Y=1
SOLVE({X+Y=3,X-Y=1},{Y,X})             -> Y=1 and X=2
SOLVE({2X+3Y=1,5X-2Y=7},{X,Y})         -> X=23/19 and Y=-9/19
SOLVE({X+Y+Z=6,X-Y=0,Y+Z=5},{X,Y,Z})  -> X=1 and Y=1 and Z=4
SOLVE({X+Y=3},{X,Y})                   -> X=3-Y                 more unknowns than equations:
SOLVE({2X+3Y=1,4X+6Y=2},{X,Y})         -> X=(1-3Y)/2             the others stay free
SOLVE({X+Y=3,X+Y=4},{X,Y})             -> ERROR: NO SOLUTION
SOLVE({A*X+Y=1,X-Y=2},{X,Y})           -> X=3/(A+1) and Y=(1-2A)/(A+1)   (MathPrint; wider than Classic)
SOLVE({X²+Y²=25,X-Y=1},{X,Y})          -> {X=-3,Y=-4},{X=4,Y=3}
SOLVE({X*Y=6,X+Y=5},{X,Y})             -> X=2 and Y=3 or X=3 and Y=2
CSOLVE({X²+Y=0,Y=1},{X,Y})             -> {X=-i,Y=1},{X=i,Y=1}
```

Linear systems are exact for any number of equations in 1 to 6 unknowns (fractions
over 999999 are `SYMCE LIMIT`). Letters that are not in the second list are their
stored values, as above, or stay letters. Beyond linear, SymCE handles two
equations in two unknowns when one of them is linear in one of the unknowns
(the circle and the line above): that one is solved and put into the other, and
the answer's sets are in order of the first unknown. An answer wider than 26
columns with `and` is written `{X=..,Y=..}`, one brace pair per solution, and
if that is too wide too, `ERROR: TOO WIDE` (a circle and a line meeting at
irrational points, say, in Classic; try MathPrint). Three or more equations
with letters in the coefficients, or nonlinear systems of other shapes, are
`ERROR: SYMCE LIMIT`. A list that is not `{A=B,..},{letters}` is `ERROR: SYNTAX`,
a letter twice or a non-letter in the second list `ERROR: ARGUMENT`.

Roots and `abs(` of anything stay exact (`abs(` is 2nd CATALOG or MATH NUM 1):

```
√(X²)                -> abs(X)            √(8X²)       -> 2√(2)abs(X)
√(4X+4)              -> 2√(X+1)           √(X²+2X+1)   -> abs(X+1)
1/√(X+1)             -> √(X+1)/(X+1)      √(X+1)^3     -> X√(X+1)+√(X+1)
DERIV(√(X+1),X)      -> √(X+1)/(2X+2)     DERIV(abs(X),X) -> abs(X)/X
INTEGRAL(√(X+1),X)   -> 2X√(X+1)/3+2√(X+1)/3
INTEGRAL(1/√(X),X,0,1) -> 2
```

The TI-Nspire's Algebra commands, under the same names (pick them from the
Algebra menu). A list answer is `{…}`, `i` is
2nd `.`'s i, and V is optional (X, else the first variable):

```
POLYROOTS(X²-2)               -> {-√(2),√(2)}        exact real roots
POLYROOTS(X²+1)               -> {}
CPOLYROOTS(X^4+4)             -> {-1-i,-1+i,1-i,1+i}
NROOTS(X²-2)                  -> {-1.414213562,1.414213562}
NROOTS(X²+X+1)                -> {-.5-.86603i,-.5+.86603i}
POLYREMAINDER(X³+2X+1,X²+1)   -> X+1
POLYQUOTIENT(X²,2X+1)         -> X/2-1/4
POLYGCD(4X+4,6X+6)            -> 2X+2
POLYCOEFFS(AX²+BX+C)          -> {A,B,C}
POLYDEGREE(XY²,Y)             -> 2
COMDENOM(1/(X+1)+1/(X-1))     -> 2X/(X²-1)
PARTFRAC(1/(X²-1),X)          -> 1/(2(X-1))-1/(2(X+1))
PARTFRAC((X+3)/(X³-X),X)      -> -3/X+2/(X-1)+1/(X+1)
PARTFRAC(X³/(X²-1),X)         -> X+1/(2(X-1))+1/(2(X+1))
PARTFRAC(X/((X-1)(X²+1)),X)   -> 1/(2(X-1))-(X-1)/(2(X²+1))
PARTFRAC(1/(X²(X+1)),X)       -> -1/X+1/X²+1/(X+1)
CSOLVE(X²+2X+5=0,X)           -> X=-1-2i or X=-1+2i
CFACTOR(X²+1)                 -> (X-i)(X+i)
CZEROS(X²+2X+5)               -> {-1-2i,-1+2i}
LEFT(X²+1=3X)                 -> X²+1               RIGHT(X²+1=3X) -> 3X
```

**Convert Expression** (7) and **Trigonometry** (8) work on `sin(` `cos(`
`tan(` `ln(` `log(` `logBASE(` (MATH A) `e^(` and `e`, typed with their own
keys. They are exact rewrites, like the Nspire's `▶ln`, `▶logbase`, `▶exp`,
`▶sin`, `▶cos`, `tExpand` and `tCollect`:

```
TOLN(logBASE(X,3))            -> ln(X)/ln(3)                    ▶ln
TOLOGBASE(ln(X),5)            -> logBASE(X,5)/logBASE(e,5)      ▶logbase(5), MathPrint: log₅(X)/log₅(e)
TOEXP(cos(X))                 -> (e^(Xi)+e^(-Xi))/2             ▶exp
TOSIN(cos(X)²)                -> 1-sin(X)²                      ▶sin
TOCOS(sin(X)²)                -> 1-cos(X)²                      ▶cos
TEXPAND(sin(2X))              -> 2sin(X)cos(X)                  also cos(A+B), sin(3θ), tan(2X)
TCOLLECT(sin(X)cos(X))        -> sin(2X)/2                      also cos(X)², sin(A)cos(B)
```

In MathPrint `TOLOGBASE(logBASE(10,3)-logBASE(5,5),5)` gives the Nspire's
`log₅(10)/log₅(3)-1` (in Classic it is 28 columns: `ERROR: TOO WIDE`).
`TOSIN`/`TOCOS` only trade squares, as the Nspire does: `TOSIN(cos(X)³)` is
`cos(X)-cos(X)sin(X)²`. Limits: `sin(X)` and friends on their own are still
the OS's (only these commands take them); up to six variables and different
function calls together; `TEXPAND` takes multiples up to 7 and two-term
angles (`sin(8X)`, `cos(A+B+C)`: `ERROR: SYMCE LIMIT`) while the answer fits
one row (`cos(4X)` does, `sin(4X)` is `ERROR: TOO WIDE`); `TOEXP` needs
RADIAN (`ERROR: MODE` / `USE RADIAN` in DEGREE; the rest are the same in
both); `TOLOGBASE` without its base is `ERROR: ARGUMENT`, with base 1, 0 or
a negative `ERROR: DOMAIN`; `log(` of 0 or a negative number, e^ mixed with trig in
`TCOLLECT`, and `TOEXP(e^(X))` are `ERROR: SYMCE LIMIT`. An answer with
functions is `Ans` only inside these commands: `TCOLLECT(Ans)` after
`TEXPAND(sin(2X))` gives `sin(2X)`; `Ans*2` is `ERROR: SYMCE LIMIT` / `ANS`.

**Calculus.** In the ALPHA+DOWN popup press **RIGHT**: the **Calculus**
tab, next to Algebra at the bottom, in the Nspire's order. In 2nd MATH it
is the fifth tab, `Calc` (LEFT twice from TEST). 14 items, so the
list scrolls (`↑`/`↓` next to the number); after `0` the items are `A`-`D`,
picked in the popup with MATH, APPS, PRGM and x⁻¹ (no ALPHA), or with the
arrows:

```
1:Derivative              DERIV(
2:Derivative at a Point   DERIVAT(
3:Integral                INTEGRAL(
4:Limit                   LIMIT(
5:Sum                     SUM(
6:Product                 PRODUCT(
7:Function Minimum        FMIN(
8:Function Maximum        FMAX(
9:Tangent Line            TANGENTLINE(
0:Normal Line             NORMALLINE(
A:Arc Length              ARCLEN(
B:Series ►                Taylor Polynomial TAYLOR(, Generalized Series SERIES( and
                          Dominant Term DOMINANTTERM(
C:Implicit Derivative     IMPDIF(
D:Numeric Calculations ►  Numerical Derivative nDeriv(, Central Diff Quotient CENTRALDIFF(,
                          Numerical Integral fnInt(, Numerical Function Min fMin(,
                          Numerical Function Max fMax(
E:Differential Equation   DESOLVE(
```

Exact answers, with the same functions as Convert Expression (`sin(` `cos(`
`tan(` `ln(` `log(` `logBASE(` `e^(` `e`, `√(`), and the chain, product and
quotient rules:

```
DERIV(sin(X))                -> cos(X)            DERIV(expr[,V[,order]]); V: X, else the first variable
DERIV(X³,X,2)                -> 6X                also DERIV(tan(X)) -> tan(X)²+1, DERIV(e^(2X)) -> 2e^(2X)
DERIVAT(X³,X,2)              -> 12                DERIVAT(expr,V,point[,order]); DERIVAT(sin(X),X,1) -> cos(1)
TANGENTLINE(X²,X,1)          -> 2X-1              TANGENTLINE(expr,V,point)
NORMALLINE(X²,X,1)           -> 3/2-X/2           NORMALLINE(expr,V,point)
TAYLOR(e^(X),X,3)            -> X³/6+X²/2+X+1     TAYLOR(expr,V,order[,point]), point 0 if left out
TAYLOR(ln(X),X,3,1)          -> X³/3-3X²/2+3X-11/6
IMPDIF(X²+Y²=1,X,Y)          -> -X/Y              IMPDIF(eq,V,W[,order]): dW/dV; order 2 -> (-X²-Y²)/Y³
CENTRALDIFF(X³,X,H)          -> H²+3X²            CENTRALDIFF(expr,V,step): (f(V+step)-f(V-step))/(2step)
nDeriv(X³,X,2)               -> 12                the OS's own, as MATH 8; D 1 types it
fnInt(X,X,0,1)               -> 0.5               the OS's own, as MATH 9; D 3 (fMin( fMax( too: D 4, D 5)
INTEGRAL(Xe^(X),X)           -> Xe^(X)-e^(X)      INTEGRAL(expr,V[,lo,hi]); no +C
INTEGRAL(1/(X(X+1)),X)       -> ln(abs(X))-ln(abs(X+1))
INTEGRAL(X²,X,0,3)           -> 9                 also INTEGRAL(1/X,X,1,2) -> ln(2), INTEGRAL(sin(X)²,X) -> X/2-sin(2X)/4
LIMIT(sin(X)/X,X,0)          -> 1                 LIMIT(expr,V,point[,dir]): dir 1 from the right, -1 from the left
LIMIT((2X²+1)/(X²-3),X,1E99) -> 2                 1E99 is +∞ and -1E99 is -∞ (E is 2nd ,); LIMIT(1/X,X,0,1) -> ERROR: +INFINITY
SUM(X²,X,1,N)                -> N³/3+N²/2+N/6     SUM(expr,V,lo,hi); SUM(X,X,1,100) -> 5050
SUM(1/(X(X+1)),X,1,99)       -> 99/100            also SUM(e^(X),X,1,N) -> (e^(N+1)-e)/(e-1)
PRODUCT((X+1)/X,X,1,99)      -> 100               PRODUCT(expr,V,lo,hi), whole ends only
FMIN(X^4-2X²,X)              -> X=-1 or X=1       FMIN/FMAX(expr,V[,lo,hi]): where, as the Nspire
FMAX(X³-3X,X,-3,3)           -> X=3               FMIN(X³,X) -> ERROR: AT -INFINITY
ARCLEN(X²,X,0,1)             -> ln(2+√(5))/4+√(5)/2   ARCLEN(expr,V,a,b); ARCLEN(2X√(X)/3,X,0,3) -> 14/3
SERIES(1/sin(X),X,3)         -> 7X³/360+X/6+1/X   SERIES(expr,V,order[,point]): negative powers too
DOMINANTTERM(sin(X)-X,X)     -> -X³/6             DOMINANTTERM(expr,V[,point]); at 1E99:
DOMINANTTERM((2X³+X)/(X-1),X,1E99) -> 2X²
```

Integral, Limit, Sum, Product, Function Min/Max, Arc Length, Series and
Dominant Term are exact too, from these rules (ideas after the Nspire's
reference, sympy and KhiCAS, no code copied):

- **INTEGRAL**: polynomials; `X^n`, `1/X` (as `ln(abs(X))`); fractions of
  polynomials in X by partial fractions over their rational roots (`ln(abs(`
  terms, then the rest); sin, cos and e^ of `aX+b`, times a polynomial (by
  parts), products of sin and cos; `ln(` of a linear argument times a
  polynomial; `√(X)` times a polynomial over a power of X
  (`INTEGRAL(√(X),X)` -> `2X√(X)/3`, `INTEGRAL(1/√(X),X)` -> `2√(X)`); `√(aX+b)`
  the same way, by u = aX+b (`INTEGRAL(X√(X+1),X)`).
  A definite integral is F(hi)-F(lo); across a pole it is `ERROR: DOMAIN`
  (`INTEGRAL(1/X,X,-1,1)`), and from a point where ln is infinite it is
  `SYMCE LIMIT` (`INTEGRAL(ln(X),X,0,1)`: improper). Not done: arctan
  (`1/(X²+1)` and any quadratic with no rational root), `tan(`, a root of
  more than aX+b (`√(X²+1)`), `√(X+1)/X`, a function inside another (`e^(X²)`).
- **LIMIT**: the value, when the function is defined there; else by the first
  Taylor terms of top and bottom (so `(sin(X)-X)/X³` -> `-1/6`); at 1E99 or
  -1E99 fractions of polynomials only. A pole is `ERROR: +INFINITY`,
  `-INFINITY`, or `UNDEFINED` when the two sides differ (give dir). Not done:
  `ln(X)` at 0, `√(X)` from the left of 0, e^ at infinity.
- **SUM**: whole ends up to 100 apart, term by term (any function); else, or
  with letters as ends, a polynomial times anything free of V plus e^ terms
  (geometric), in closed form. Backwards ranges follow Karr's convention, as
  the Nspire and sympy: `SUM(X,X,3,1)` -> `-2`. Not done: `1/X` with a letter
  end (the harmonic numbers), `2^X`. **PRODUCT**: whole ends up to 100 apart
  only; past 999999 on the way it is `SYMCE LIMIT` (`PRODUCT(X,X,1,20)`).
- **FMIN, FMAX**: for fractions of polynomials in V with no pole in the range:
  where the function is least/greatest, from the rational roots of f′ (and
  the ends, with lo and hi), all the places that tie joined with `or`. With no
  minimum it is `ERROR: AT -INFINITY` (`AT +INFINITY`, or `AT INFINITY` when
  both sides run off: `FMAX(X²,X)`); a constant is `ALWAYS TRUE`. Not done:
  f′ with an irrational root (`X^4-X`), sin, e^, ln (`FMIN(X-ln(X),X)`).
- **ARCLEN**: ∫√(1+f′²) when that integral is one of the above: a line, a
  parabola (the answer above), `2X√(X)/3`; else `SYMCE LIMIT` (`X³`,
  `ln(X)`).
- **SERIES**, **DOMINANTTERM**: like TAYLOR, but the function may have a pole
  at the point: negative powers. Answers are written highest power first, like
  TAYLOR's. DOMINANTTERM at 1E99 is for fractions of polynomials.

SUM and PRODUCT term by term take 2 to 3.5 s for 100 terms on the calculator
(measured in CEmu); a key pressed meanwhile is lost.

The Numerical items type the calculator's own `nDeriv(`, `fnInt(`, `fMin(`
and `fMax(` (in MathPrint, the same templates as MATH 8 and 9), which the
calculator works out as ever. Central Diff Quotient is SymCE's, exact, with
any step: a number or a letter.

**DESOLVE** solves a differential equation the Nspire's way: `DESOLVE(eq,X,Y)`, with
`'` (prime, 2nd ANGLE 2) for the derivative and `and` (2nd MATH LOGIC 1) before each
initial condition. Constants are the first free letters from C (C, then D): a letter
the entry uses is skipped.

```
DESOLVE(Y'=2Y,X,Y)                        -> Y=Ce^(2X)
DESOLVE(Y'=2Y and Y(0)=3,X,Y)             -> Y=3e^(2X)
DESOLVE(Y'+2Y=e^(X),X,Y)                  first order, linear (integrating factor), or separable
DESOLVE(Y'=X/Y and Y(0)=2,X,Y)            -> Y=√(X²+4)   (the branch through the point)
DESOLVE(Y''-3Y'+2Y=0,X,Y)                 second order, constant coefficients: real, repeated, complex roots
DESOLVE(Y''+Y=cos(2X),X,Y)                right side: polynomial, e^(kX), sin(kX), cos(kX), sums of them
DESOLVE(Y''=2 and Y(0)=1 and Y'(0)=0,X,Y) -> Y=X²+1
```

One initial condition per constant (one for first order, two for second order). Limits
(`ERROR: SYMCE LIMIT`): irrational characteristic roots, coefficients that are not
constants in a second order equation, a right side that mixes more than the engine's 6
letters-and-functions, and an answer that does not fit 26 columns (`Y=` is dropped
first, then `ERROR: TOO WIDE`; in Classic try MathPrint).

Where a Calculus command can't give a true answer it says so:
`ERROR: DOMAIN` at a point outside the function (`DERIVAT(1/X,X,0)`,
`TANGENTLINE(1/X,X,0)`, `TAYLOR(sin(X)/X,X,3)`), `ERROR: DIVIDE BY 0` for a
flat tangent's normal (`NORMALLINE(X²,X,0)`) or an equation without the
second variable (`IMPDIF(X²=1,X,Y)`), `ERROR: ARGUMENT` for arguments in the
wrong place (`DERIVAT(X²,X,X)`: the point must be a number). In **DEGREE**,
derivatives, lines and Taylor polynomials of sin/cos/tan of the variable are
`ERROR: MODE` / `USE RADIAN` (the Nspire would multiply by π/180; SymCE
refuses rather than show a radian answer); e^, ln and polynomials give the
same in both, and CENTRALDIFF (only substitution) works in both. Limits
(`ERROR: SYMCE LIMIT`): a function inside another (`ln(sin(X))`,
`sin(sin(X))`, `X^X`), ln or √ of a negative point (`DERIVAT(ln(X),X,-1)`), a point that is not a number (`DERIVAT(e^(X),X,ln(2))`),
powers of 16 and up on the way (`DERIV(X^20,X)`, `DERIV(tan(X),X,15)`),
answers past 64 tokens (`TAYLOR(sin(X),X,3,1)`); answers too wide for the row
are `TOO WIDE` (`CENTRALDIFF(sin(X),X,H)` in Classic; it fits in MathPrint).

In DEGREE the same holds for INTEGRAL, LIMIT, SUM, FMIN/FMAX, ARCLEN, SERIES
and DOMINANTTERM of sin/cos/tan of the variable (`INTEGRAL(sin(X),X)` is
`ERROR: MODE`); `INTEGRAL(e^(X),X,0,1)` is `e-1` in both. There is no ∞ key:
type `1E99` (`E` is 2nd `,`), or `-1E99`, as LIMIT's and DOMINANTTERM's point.
A decimal as a point or an end (`LIMIT(X²,X,2.5)`) is `SYMCE LIMIT`: use
`5/2`. Note: `SUM(`, `LIMIT(` and the rest spelled with ALPHA letters used to
be a product of letters; they are SymCE's now.

**Geometry.** In the ALPHA+DOWN popup press **LEFT** from Algebra (or RIGHT
from Calculus): the **Geometry** tab. In 2nd MATH it is the last tab, `Geo`
(LEFT once from TEST). A point is `(x,y)`; a line is an equation (`Y=2X+3`,
`2X+3Y=6`, `X=3`) or, for the line commands, just `2X+3` for `Y=2X+3`.
13 items; `A`-`C` are MATH, APPS and PRGM in the popup:

```
1:Distance                DISTANCE(
2:Midpoint                MIDPOINT(
3:Slope                   SLOPE(
4:Line Through 2 Points   LINE(
5:Line: Point, Slope      LINE(
6:Parallel Line           PARALLEL(
7:Perpendicular Line      PERPENDICULAR(
8:Perpendicular Bisector  PERPBISECTOR(
9:Intersection of Lines   INTERSECT(
0:Partition Segment       PARTITION(
A:Triangle ►              Area/Perimeter of Polygon AREA( PERIMETER(, CENTROID(, CIRCUMCENTER(,
                          ORTHOCENTER(, INCENTER(, Hypotenuse HYPOT(, Missing Leg LEG(
B:Circle ►                CIRCLE( from center and radius, center and a point, 3 points,
                          or the center and radius of an equation
C:Transformations ►       Reflect Over Line REFLECT(, Rotate (Degrees) ROTATE(, DILATE(
```

All exact:

```
DISTANCE((1,1),(4,5))           -> 5                  DISTANCE(point,point) or (point,line)
DISTANCE((1,2),Y=2X+3)          -> 3√(5)/5
MIDPOINT((1,2),(4,7))           -> (5/2,9/2)          letters too: MIDPOINT((A,B),(C,D))
SLOPE((1,2),(3,8))              -> 3                  or of a line: SLOPE(2X+3Y=6) -> -2/3
LINE((0,1),(2,5))               -> Y=2X+1             point and slope: LINE((1,2),3) -> Y=3X-1
PARALLEL((1,2),Y=3X+1)          -> Y=3X-1             PARALLEL(point,line), through the point
PERPENDICULAR((1,2),Y=3X+1)     -> Y=7/3-X/3
PERPBISECTOR((0,0),(4,2))       -> Y=5-2X
INTERSECT(2X+3Y=7,X-Y=1)        -> (2,1)              parallel: ERROR: NO SOLUTION; the same line: ALWAYS TRUE
PARTITION((0,0),(10,5),2,3)     -> (4,2)              the point dividing the segment 2:3
AREA((0,0),(4,0),(0,3))         -> 6                  3 to 6 vertices, in order around
PERIMETER((0,0),(1,0),(0,1))    -> 2+√(2)
CENTROID((0,0),(1,0),(0,1))     -> (1/3,1/3)
CIRCUMCENTER((0,0),(4,0),(0,6)) -> (2,3)              in a line: ERROR: NO SOLUTION
ORTHOCENTER((0,0),(4,0),(1,3))  -> (1,1)
INCENTER((0,0),(1,0),(0,1))     -> (1-√(2)/2,1-√(2)/2)
HYPOT(3,4)                      -> 5                  LEG(hypotenuse,leg): LEG(5,3) -> 4
CIRCLE((1,-2),3)                -> (X-1)²+(Y+2)²=9    center, radius
CIRCLE((0,0),(3,4))             -> X²+Y²=25           center, a point on it
CIRCLE((0,0),(2,0),(0,2))       -> (X-1)²+(Y-1)²=2    through 3 points
CIRCLE(X²+Y²-2X+4Y=4)           -> C=(1,-2),R=3       center and radius of an equation
REFLECT((1,2),Y=X)              -> (2,1)              over a line
ROTATE((1,0),45)                -> (√(2)/2,√(2)/2)    ROTATE(point,degrees[,center]), counterclockwise
DILATE((2,3),1/2,(1,1))         -> (3/2,2)            DILATE(point,factor[,center]); center (0,0) if left out
```

Limits: ROTATE takes whole degrees at multiples of 30 or 45 (any other angle
is `SYMCE LIMIT`), in degrees whatever the angle mode; an AREA with letters
is `SYMCE LIMIT` (it needs the sign); lines and circles are at most X² and Y²
with no XY (`INTERSECT(Y=X²,Y=1)` is `ERROR: ARGUMENT`: use SOLVE); `Ans`
holding a point cannot be used as one.

**Matrix.** The last item of the Algebra menu, `Matrix ►` (`A` in the popup,
the 11th item in 2nd MATH). Type a matrix with the calculator's own brackets,
**2nd ×** for `[` and **2nd −** for `]`: `[[A,1][2,A]]`, and a vector as
`[1,2,X]`. Each entry is a number or an expression in letters. Up to 20
entries in all (4x4, 3x5, 4x5); vectors up to 20 long, cross product 3. In
MathPrint the OS draws a matrix as a grid.

```
1:Determinant           MDET(      MDET([[A,1][2,A]])        -> A²-2
2:Inverse               MINV(      MINV([[1,2][3,4]])        -> [[-2,1][3/2,-1/2]]
3:Row Reduced Echelon   MRREF(     MRREF([[1,2,3][4,5,6]])   -> [[1,0,-1][0,1,2]]
4:Transpose             MTRANS(    MTRANS([[1,2][3,4]])      -> [[1,3][2,4]]
5:Dot Product           DOTP(      DOTP([1,2,3],[4,5,6])     -> 32
6:Cross Product         CROSSP(    CROSSP([1,0,0],[0,1,0])   -> [0,0,1]
7:Norm                  NORM(      NORM([3,4])               -> 5
```

A letter in a matrix stays a letter: `MINV([[A,1][2,A]])` is
`1/(A²-2)*[[A,-1][-2,A]]` (the determinant is not cancelled against the
entries), `MRREF` treats letters as any value but the one that zeros a pivot.
The determinant is exact by cofactors. An inverse of a determinant of 0 is
`ERROR: SINGULAR MAT`; a matrix that isn't square (MDET, MINV), a
vector that isn't one (DOTP, CROSSP, NORM) or a cross product that isn't of
two 3-vectors is `ERROR: INVALID DIM`; `DOTP` of two lengths is
`ERROR: DIM MISMATCH`. An entry with a letter below a fraction bar or a root
is `SYMCE LIMIT`. An answer wider than the screen is `ERROR: TOO WIDE`: in
Classic the whole `[[..][..]]` text must fit 26 columns (a 3x3 inverse of
fractions rarely does), in MathPrint the grid does (each column its widest
entry plus one, plus the brackets). `NORM` of a matrix is not done.

A command SymCE can't finish shows its own error screen (`1:Quit` goes back):
`ERROR: NO SOLUTION` (`SOLVE(X²+1=0,X)`), `ERROR: ALWAYS TRUE` (`SOLVE(X=X,X)`),
`ERROR: TOO WIDE` (the answer doesn't fit; in Classic it says `TRY MATHPRINT`,
where `SOLVE(X²+X=1,X)` does fit), `ERROR: SYMCE LIMIT` (past what it can do),
`ERROR: SYNTAX` with `SOLVE(A=B,X)` under it (SOLVE always needs the `=`, from
`2nd MATH 1`, and a comma then the variable to solve for; LEFT and RIGHT need
the `=` too), `ERROR: DATA TYPE` (`POLYDEGREE(1/X)`: not a polynomial in V),
`ERROR: ARGUMENT` (`POLYGCD(X)`: it takes two), `ERROR: DIVIDE BY 0`
(`POLYQUOTIENT(X²,0)`).

These keep TI's error screen (the title, `1:Quit`) and SymCE fills in what is
under it: a sentence saying what went wrong, then the command's syntax and an
example (the same table `+` help uses). `SYNTAX` and `ARGUMENT` also get
`2:Goto`, which puts the entry back with the cursor at its start. The sentences:

| Error | What it says |
| --- | --- |
| `SYNTAX` | Check the arguments: |
| `ARGUMENT` | An argument is wrong: |
| `DOMAIN` | Not defined for these values: |
| `DATA TYPE` | An argument is not a polynomial in V: |
| `NO SOLUTION` | No real solution. For complex ones use CSOLVE(. (`PARALLEL`: The lines are parallel; `COLLINEAR`: The points are on one line.) |
| `ALWAYS TRUE` | True for every value. (`SAME LINE`: The lines are the same.) |
| `TOO WIDE` | The answer is too wide. Try MathPrint (MODE). (already MathPrint: ... for the screen.) |
| `SYMCE LIMIT` | SymCE can't do this one exactly. (`ANS`: Ans can't be a list or equation here.) |
| `SINGULAR MAT` | The matrix has no inverse: its determinant is 0. |
| `INVALID DIM` | Needs a square matrix, up to 4x4 (vectors up to 3 long). |
| `DIM MISMATCH` | The two vectors need the same length. |
| `MODE` | This needs RADIAN mode. Press MODE and pick RADIAN. |
| `DIVIDE BY 0`, `UNDEFINED`, `+INFINITY`, `-INFINITY`, `AT INFINITY` | one sentence each |

They show in Classic and MathPrint, with the Evo font on or off (with it off the
localize hook is switched on for the one screen and off again).

`Ans` after a SymCE answer is that answer, even a plain number:
`POLYDEGREE(X³)` then `+1` gives 4. A list or an equation can't be used that
way: `ERROR: SYMCE LIMIT` / `ANS`.

These must be left to the stock OS, unchanged:

```
2+2         -> 4                √(4), 1E3X, X^16    -> the OS's number
prgmANYTHING -> runs it         X/0                 -> ERR: DIVIDE BY 0
DelVar X, 5->X                  anything with no variable in it
```

And these used to reboot the calculator. Now they must not, and 2X+2X must
still give 4X afterwards:

```
Y=   WINDOW   MODE   GRAPH   STAT   APPS   1/0 ENTER (ERR screen)
```

The home-screen cursor is an **insert** cursor on the entry line.

---

## TI-BASIC programs

- Write SymCE commands straight into a program: `FACTOR(X²-1)→Str1`, then
  `Disp Str1`, or `Disp FACTOR(X²-1)`. In the program editor, type the command
  name in capitals with ALPHA, then `(`. Programs only: on the home screen use
  the SymCE menus.
- The answer is a string: store it in a string such as `Str1`, or show it
  with `Disp`. Very long answers may not fit in a program.
- Your program is never changed. SymCE runs a hidden copy of it and deletes
  the copy when you are back on the home screen.
- A program whose last line is a SymCE command (no `Disp`) shows that answer in
  place of `Done`, and UP keeps it in the history. `Disp` lines are never kept
  in the history: that is the OS.
- If a SymCE command fails, the error screen offers `2:Goto`, which opens your
  own program at that command. After ON (break) or a math error such as 1/0,
  only Quit is offered.
- `expr("FACTOR(X²-1)")` works everywhere in programs, too.
- Limits: a program over about 8000 bytes, an archived program, or very little
  free RAM runs without this feature (bare commands then give an error, as on a
  plain calculator). Up to 3 programs that call each other can use bare
  commands at once.

## Changelog (since v15, the last RAM-hook build)

- **Full-screen graph (build 6.1).** SymCE's grapher draws on all 320×240
  pixels; the white strip at the bottom is gone. TRACE, CALC, ZOOM and DRAW text
  sits over the foot of the graph, as TI's text box does, and the graph comes
  back when it goes. ENTER in the X= box redraws the graph.
- **SymCE commands in TI-BASIC programs (build 6).** See TI-BASIC programs above.
- **STO and Ans with SymCE answers.** `FACTOR(X^2-1,X)` then STO and `Y1` (or
  `Str1`) stores the symbolic answer: Y1 is selected, so it graphs, and Y= shows
  its tokens. `STO A` (a letter) works when the answer is a number; a symbolic
  answer gives DATA TYPE / NOT A NUMBER. A numeric SymCE answer (a fraction, a
  decimal, POLYDEGREE) is also the OS's Ans, so `Ans*2` works. Answers that are
  lists, equations or "or" lists cannot be stored (DATA TYPE).
- **Graph Ans.** The 11th item of the ALPHA+DOWN Algebra popup (press UP at the
  top to wrap to it): the last answer, symbolic or numeric, goes into the first
  empty Y= slot (Y1 when all are in use) and the SymCE grapher opens on it. A
  slot with no tokens counts as empty. A list or equation answer cannot be
  graphed and nothing happens.
- **Y= / MODE / ERR reboot: fixed.** The hook lives inside the SymCE app.
- **Speed, and why there is no overclock.** The CE's CPU already runs at its
  48 MHz maximum. Every known CE "overclock" program only shortens the flash
  wait states. SymCE does the same, with three safeguards:
  - only while it calculates;
  - only to 3, the value every C program uses;
  - putting the OS's value back right after.

  On a CE from before about 2019, calculations run ~9% faster with the same
  answers. On newer models (boot code 5.6.1 and later):
  - its flash ignores the wait-state setting, so no program can speed it up
    that way;
  - its cached flash already runs SymCE faster than RAM would.
- **The engine does the same arithmetic with less work.** Every answer is
  byte for byte what it was, and nothing is rounded differently. Checked on
  about 270,000 host inputs and 16,400 emulator cases. Measured in CEmu, before
  → after:

  | Command | Before | After |
  |---|---|---|
  | `SUM(1/(X(X+1)),X,1,99)` | 9.0 s | 0.73 s |
  | `PRODUCT((X+1)/X,X,1,99)` | 7.8 s | 0.58 s |
  | `NROOTS(X^5-X-1)` | 2.4 s | 0.63 s |
  | `SERIES(tan(X),X,5)` | 1.0 s | 0.17 s |
  | `TAYLOR` | 0.68 s | 0.12 s |
  | `LIMIT` | 0.58 s | 0.10 s |

  FACTOR is about 2× faster. Three changes made up the gain:
  - Fractions skip needless divisions.
  - A plain number times or plus a plain number skips the polynomial
    machinery.
  - NROOTS's arithmetic is hand-written for the CPU's byte multiplier.
- **Echo: still fixed.** The entry is never touched; the answer goes through the
  OS's own result routine, right-aligned like any number.
- **MathPrint boxes work.** The answer is computed from the OS's own parsed copy
  of the entry, where boxes are already flattened, instead of the edit buffer.
- **New engine:** several variables, fractions, expanding products and powers,
  and dividing by a polynomial when it divides exactly: `(X²-1)/(X-1)` gives
  `X+1`, `(6X²+3X)/(3X)` gives `2X+1`.
- **ENTER on an empty line** (re-run the last entry) answers again instead of
  showing 0.
- **Fractions.** Any answer can be a fraction of polynomials, in lowest terms:
  `1/X+1/X` gives `2/X`, `(X²+5X+6)/(X²-4)` gives `(X+3)/(X-2)`, `X^-2` gives
  `1/X²`, `(X²-Y²)/(X+Y)` gives `X-Y`, `1/(X+Y)+1/(X-Y)` gives
  `2X/(X²-Y²)`. SymCE only answers when it can prove nothing more cancels;
  when it cannot (e.g. `(X²+X-Y²+Y)/(X²+2XY+Y²+X+Y)`) the calculator answers
  as usual.
- **Two-variable fractions where every term has the same degree** cancel too:
  `(X²-Y²)/(X²-2XY+Y²)` gives `(X+Y)/(X-Y)`, `(X³-Y³)/(X²-Y²)` gives
  `(X²+XY+Y²)/(X+Y)`. Only one side needs it: `(X²-Y²+X-Y)/(X²-Y²)` gives
  `(X+Y+1)/(X+Y)`, `(X²-Y²)/(XZ+YZ+X+Y)` gives `(X-Y)/(Z+1)`.
- **MathPrint answers are 2D.** Fractions are stacked and every power is
  raised, like the calculator's own fraction answers. You can arrow up to an
  answer, press ENTER to paste it, and keep editing.
- **`(` in an answer drew as a square root.** The OS's display routine reads
  the `(` token as the root sign; SymCE now passes it the plain `(` character.
- **Dividing by X works while X is 0.** SymCE answers before the calculator
  works the entry out as a number, so `(X²+2X)/X` is `X+2`, not
  `ERR:DIVIDE BY 0`. `1/0` still is that error.
- **`Ans` carries on from the last answer.** `2X+2X` gives `4X`; then press
  `×` `3` (the calculator types `Ans*3`) and get `12X`. It works after a plain
  number too: `2+2`, then `×` `X`, gives `4X`.
- **The x⁻¹ key works**: `X⁻¹` gives `1/X`, `(X+1)⁻¹` gives `1/(X+1)`. It used
  to be left to the OS.
- **Decimals work**: `.1X+.2X` gives `0.3X`, `1.5X+X` gives `2.5X`. A decimal
  is kept exact (`1.5` is 3/2); the answer is in decimals where a coefficient
  ends within 6 places and a fraction where it does not (`X/3+.5` gives
  `X/3+0.5`). Only an entry with a decimal point in it answers in decimals.
  They used to be left to the OS.
- **MODE ANSWERS: DEC is followed**: `X/4` gives `0.25X` then, as if a
  decimal point were typed; `X/3` stays `X/3`, since it has no exact decimal.
- **Longer answers in MathPrint**: raised powers and stacked fractions take
  less room, so `(X+1)^6` and `((X+1)/(X+2))^3` are answered there. Classic
  still stops at 26 characters.
- **Refuses to arm on an OS other than 5.8.4**, instead of crashing on ENTER.
- **Square roots**: `√(8)` gives `2√(2)`; roots are kept out of denominators.
- **FACTOR, EXPAND, SOLVE, DERIV** commands, and the Nspire-style **Algebra
  menu** (ALPHA+DOWN, with submenus) and **Algebra tab in 2nd MATH** that type
  them and the rest of the Algebra commands. The factoring ideas come from KhiCAS (the open-source CAS for
  TI/Casio/NumWorks), reimplemented small, no code copied.
- **Convert Expression and Trigonometry**: `TOLN`, `TOLOGBASE`, `TOEXP`,
  `TOSIN`, `TOCOS`, `TEXPAND`, `TCOLLECT` rewrite logarithms and trig exactly,
  from the identities (the Nspire's reference for what they give), no code
  copied.
- **Calculus menu**: a second tab in the ALPHA+DOWN popup and in 2nd MATH.
  `DERIV` now takes functions and an order, and `DERIVAT`, `TANGENTLINE`,
  `NORMALLINE`, `TAYLOR`, `IMPDIF`, `CENTRALDIFF`, `INTEGRAL`, `LIMIT`, `SUM`,
  `PRODUCT`, `FMIN`, `FMAX`, `ARCLEN`, `SERIES`, `DOMINANTTERM` are new, all
  exact; the numerical items type the calculator's own `nDeriv(` `fnInt(`
  `fMin(` `fMax(`. A 0/0 hidden in a function at a point is now caught:
  `DERIVAT(X²/tan(X),X,0)` is `ERROR: DOMAIN`; it used to be a wrong 0.
- **Geometry menu**: a third tab in the ALPHA+DOWN popup and in 2nd MATH.
  `DISTANCE`, `MIDPOINT`, `SLOPE`, `LINE`, `PARALLEL`, `PERPENDICULAR`,
  `PERPBISECTOR`, `INTERSECT`, `PARTITION`, `AREA`, `PERIMETER`, `CENTROID`,
  `CIRCUMCENTER`, `ORTHOCENTER`, `INCENTER`, `HYPOT`, `LEG`, `CIRCLE`,
  `REFLECT`, `ROTATE`, `DILATE`, all exact, no code copied. In 2nd MATH the SymCE tabs are now
  `Alg`, `Calc`, `Geo`: LEFT three times, twice, once from TEST.
- **Matrix menu**: `Matrix ►` in the Algebra menu (popup and 2nd MATH).
  `MDET`, `MINV`, `MRREF`, `MTRANS`, `DOTP`, `CROSSP`, `NORM` on `[[..][..]]`
  matrices and `[..]` vectors with numbers or letters in them, exact.
- **Powers up to 15.** `X^8` and past used to overflow the engine and answer
  wrong; now `X^10+X^10` gives `2X^10`, and a power of 16 or more is left to
  the OS (or `SYMCE LIMIT` in a command).
- **X^X and 2^X on the home screen** stay symbolic: `X^X*X^X` gives
  `(X^X)²`, `2^X*3` gives `3*2^X`, `X^X/X^X` gives 1. The OS used to work
  them out as numbers. Commands on them (`DERIV(X^X,X)`) are `SYMCE LIMIT`.
- **The cursor stays the insert cursor** after CLEAR, DEL and the arrow keys
  (2nd LEFT/RIGHT too). The calculator switched those back to the block
  (overwrite) cursor, although the next key still inserted. After ENTER it is
  the block until you type, as before. 2nd INS still switches to overwrite,
  until the next key.

## Known limits

- In Classic mode answers are flat: `X/2`, `(X+3)/(X-2)`, `X^4` with its caret
  (Classic has no stacked fractions for anything).
- Inside SymCE, `Ans` means the last answer shown. Anywhere else (a program,
  Y=, a graph, or an entry SymCE does not answer, like `Ans→Y`) `Ans` is the
  calculator's last number, from before SymCE's answer: the calculator never
  works out an entry SymCE answers. A number that is not an exact short
  decimal (`1/3` = 0.3333333333) is not carried on; the calculator answers as
  usual.
- Limits: entry up to 64 tokens, answer up to 26 characters wide (in
  MathPrint a raised power or stacked fraction counts narrower), up to 6 different
  variables, each power under 16, on the way too (`X^8*X^8/X` is out), numbers
  up to 999999. A fraction whose top and bottom both hold two or more
  variables, and neither is of the first power in some variable (`X+Y` is,
  `X²+Y²` is not), is refused unless one divides the other or one of them is
  in two variables with every term of one degree (`X²-2XY+Y²`). Past
  any of them, the OS answers as usual.
- OS 5.8.4 only (see above).
- Commands: FACTOR handles one variable, and two when every term has the
  same degree (`X²-Y²`) or the terms group (`AX+AY+BX+BY`); `X²-2X+1-Y²` is
  `SYMCE LIMIT`. SOLVE answers linear and quadratic factors only: `X³=2`
  (an irreducible cubic) is `SYMCE LIMIT`. Big coefficients can hit the
  limit too (`FACTOR(720X^6-1)`). A quadratic is split over `√` only when
  the answer fits the screen, so Classic may show `(X²-3)(X²-2)` where
  MathPrint shows four factors.
- The root commands find the roots FACTOR can: an irreducible cubic or
  quartic (`POLYROOTS(X³-2)`) is `SYMCE LIMIT`, never a short list. NROOTS
  takes degree 7 at most, shows 10 digits down to 4 and is `TOO WIDE` past
  that (often, with three or more complex roots), refuses roots too close to
  tell apart, and takes up to ~3.5 s at degree 7.
- The ALPHA+DOWN menu is on the home screen's entry line only (not inside a
  MathPrint box or with a history line selected); the 2nd MATH tab works
  anywhere on the home screen. On a screen of more than a few colours (not
  seen on the home screen) the popup does not open: it has no room to keep
  the pixels under it. The tab is home screen only: in the program
  editor 2nd MATH is stock. The menu always reopens on TEST.
- Roots: one root of X per term (`√(X)√(X+1)` and a root under a root,
  `√(√(2)-X)`, are left to the OS). SOLVE takes one root or `abs(` of X
  (`√(X)+√(X+1)=3` is `SYMCE LIMIT`), and an equation true for a whole range
  (`√(X²)=X`) is `SYMCE LIMIT` too. `√(XY²)` stays `√(XY²)`, not `abs(Y)√(X)`.
- Calculus: see its paragraph above. DERIV of a plain polynomial keeps its
  old answer; DERIV( without a variable uses X, else the first variable
  (`DERIV(Y)` is 1).

---

## Verification for this build

```
make -C symce check
  engine_check.py 4000 vs sympy        0 WRONG      (18000 entries, 2000 of them two-variable fractions;
                                                     2000 FACTOR/SOLVE/DERIV/EXPAND on random products;
                                                     1000 Calculus commands on random functions, by value
                                                     and form, the Nspire's examples, the error cases;
                                                     250 more for INTEGRAL..DOMINANTTERM vs sympy/mpmath:
                                                     derivative of the antiderivative, quadrature, limits,
                                                     sums, series, each also in DEGREE;
                                                     56 Geometry examples and errors, 500 random
                                                     MIDPOINT/CENTROID/AREA/INTERSECT/ROTATE/DILATE
                                                     against exact fractions; powers to 15 and X^X)
  hook_sim_test.py                     all hook cases pass, against the assembled bytes
                                                    (the cursor keys through a cxMain stub included)
make -C symce emu   (your ROM, CEmu)
  crash.py                             ALL SURVIVE  (25 screens, the Geometry popup and tab too: Y=, WINDOW, MODE, GRAPH, ERR, STAT, APPS,
                                                     TABLE, TBLSET, FORMAT, ZOOM, TRACE, CATALOG, MEM, LIST,
                                                     DRAW, VARS, MATH, the list editor, the program editor,
                                                     power off/on, ON, GarbageCollect, Horiz split screen)
  e2e.py                               ALL PASS     (36 answers, X^12Y and X^X*X^X among them,
                                                     the insert cursor after CLEAR, DEL, LEFT and 2nd LEFT
                                                     in both modes, FACTOR/SOLVE/DERIV/EXPAND from the menu,
                                                     ERROR: NO SOLUTION and ERROR: SYNTAX then 1:Quit, the menu in both modes, MODE ANSWERS: DEC, fractions, decimals, echo, 2nd ENTER recall, re-run, Ans, /X at X=0,
                                                     MathPrint: stacked answer pasted back from history and edited,
                                                     a decimal next to a fraction likewise,
                                                     (X+1)^6, 29 tokens, likewise, and six stacked fractions, 30 tokens,
                                                     Input in a program stays numeric;
                                                     Calculus: DERIV, TANGENTLINE and nDeriv( from the popup,
                                                     INTEGRAL, LIMIT, SUM and B 2 SERIES answered,
                                                     scrolling to A-D, Series and back, the Calc tab with
                                                     fnInt(, and D 1 drawing MATH 8's template;
                                                     Geometry: MIDPOINT from the popup, DISTANCE from
                                                     the Geo tab, B 3 circle through 3 points, and the
                                                     Circle submenu closed leaving every pixel as it was)
  lifecycle.py                         ALL PASS     (install, settings keys 1 2 3, 4: GRAPH key SYMCE/TI (TI's graph when off) and 5: Run prgmSYMCE with no AsmHook2, RAM clear, re-arm, another app deleted, re-install)
  graph_check.py (in make check)      ALL PASS     (the grapher's float32 evaluator against Python math: 60 named
                                                     cases, degree mode, unsupported/syntax/long/deep, 300 random trees)
  graph.py                             ALL PASS     (the GRAPH key on the real ROM: sin(X) and X^2/5 within +-1 row of
                                                     round((Ymax-f)/(Ymax-Ymin)*219) at 10 columns each; GRAPH from
                                                     Y= mid-edit and from WINDOW plots; CLEAR and 2nd MODE go home
                                                     with CAS answering; draws 5-7x faster than the OS graph: X 300 ms against 1864, sin 560 against 2804, X^2 280 against 1624, sin(X)/X+cos(3X) 880 against 5740, three functions 1120 against 7480)
  engine_device.py                     ALL MATCH    (2131 vectors, 55,490 bytes in 2 programs, Calculus,
                                                     Geometry and DEGREE included: the calculator's
                                                     engine gives the host's bytes)
  bigapp.py                            ALL PASS     (the 138,023-byte app, 3 appvars: fresh, over an old
                                                     SymCE, RAM clear + reinstall, a GC to make room, 2X+2X -> 4X
                                                     each time; a missing and a foreign appvar refused, and no room
                                                     keeps the old SymCE, all with flash untouched)
MathPrint, read off the screen pixels: X^3 (box) + X -> X³+X, right-aligned;
  (X^2 (box) + 2X)/X at X=0 -> X+2, and 2nd ENTER recalls the box entry;
  2/X² draws as 2 over X² (not (2/X)²); 34 answers in a row, history intact
```
