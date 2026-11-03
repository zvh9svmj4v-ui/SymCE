;----------------------------------------
;
; SymCE as a flash app.
;
; OS 5.8.x resets the calculator whenever an armed hook pointer lies outside a
; flash app (ROM 0x8CAE6: accept only ptr <= 0x0BD869, or lowest_app < ptr <=
; 0x3AFFFF, lowest_app re-walked from 0x3B0000 on every check). Any RAM or
; appvar address fails, so the hook body is shipped INSIDE this app and the
; homescreen hook slot points at it.
;
; One fasmg run builds three kinds of file:
;
;   obj/SYMCE.img   the app image, as it goes into flash (before relocation)
;   SYMCE.8xp       prgmSYMCE, the installer, a few KB
;   SYMCE1.8xv ..   (SYMCE1..9, then SYMCEA..Z) made from the image by tools/zpack.py, not here:
;                   the image cut into slices of at most MAX_SLICE bytes, each ZX0-packed into an
;                   archived appvar after a 15-byte header (build id, format marker, size, sums)
;
; A TI variable holds under 64 KB, so the image rides in appvars and the
; installer reads them where they lie, archive or RAM, and unpacks each into free
; RAM. Before it touches anything it unpacks every part once and checks it is
; there, packed, from this build and sums right; if not it names the part and
; writes nothing. Then it checks there
; is room -- mirroring the OS's own rule for placing an app, and garbage
; collecting the archive once if that would make room -- before it deletes any
; SymCE already installed, so a SymCE that does not fit leaves the old one
; installed ("No blank flash"). It then writes the new app below the lowest
; app, relocates it and turns the hook on; rerunning it is how to upgrade.
; After that, opening SymCE from APPS shows its settings: 1 turns CAS on/off
; (also how to turn it back on after a RAM clear, which keeps the app and the
; appvars), 2 picks the insert or TI cursor, 3 the Evo or TI font, 4 switches the GRAPH
; key between SymCE's grapher and TI's, 5 runs prgmSYMCE, so an upgrade needs no AsmHook2.
;
; make PAD=n pads the app to n bytes with dead ones, to test all this over 64 KB.
;
; Installer and flash-app packaging adapted from AsmHook2 by RoccoLox Programs
; and jacobly (installer.asm, ports.asm); app.inc by MateoConLechuga; ez80.inc
; by jacobly.
;
;----------------------------------------

include 'include/ez80.inc'
format binary as 'img'

; The finished image is this output, byte for byte: hash it. tools/zpack.py cuts it into
; compressed parts (SYMCE1.8xv ..) and checks the installer carries this hash. Declared before app.inc, as fasmg runs postponed blocks last declared
; first: this one runs after app.inc's, which end the image with its
; signature and size trailer.
postpone
    IMAGE_SIZE := $%
    if defined PAD
        image.tail := IMAGE_SIZE - image.padEnd
    end if
    image.id = 2166136261               ; FNV-1a
    repeat IMAGE_SIZE
        load image.byte: byte from : % - 1
        image.id = ((image.id xor image.byte) * 16777619) and $FFFFFFFF
    end repeat
    BUILD_ID := image.id
end postpone

include 'include/app.inc'
include 'include/ti84pceg.inc'

; Most image bytes per part: the installer's RAM buffer for the unpacked slice (the parts
; themselves pack to far less; tools/zpack.py CAP).
MAX_SLICE := 65000

; A TI variable file as TI Connect CE sends it: archived variable `name` of
; `type`, its data the `length` bytes of `area` from `start`, after a `head`
; dword (the build id) if `headed`.
macro tifile? ext*, type*, name*, area, start*, length*, headed:0, head:0
    local entry, bytes, sum, n
    n = length + 4 * headed
    virtual at 0 as ext
        db '**TI83F*', 26, 10, 0
        db 42 dup 0
        dw n + 19
        entry = $
        dw 13, n + 2
        db type
        dq name
        db 0, $80                       ; version, archived
        dw n + 2, n
        if headed
            dd head
        end if
        load bytes: length from area: start
        emit length: bytes
        sum = 0
        repeat $ - entry
            load bytes: byte from entry + % - 1
            sum = sum + bytes
        end repeat
        dw sum and $FFFF
    end virtual
end macro

; The hook's scratch in saveSScreen; see src/hook.c. Zeroed at arm time so
; leftover RAM passes for neither a baseline, an ENTER waiting for its A=0,
; nor a last answer for Ans.
HOOK_OK      := $D0EE03
HOOK_PENDING := $D0EE04
HOOK_ANS     := $D0EE20
RB_OFF       := 20h                  ; asm_Flag2 bit 5: rainbow parens off (src/fhook.c .Lisp)
SNAP_OFF     := 10h                  ; asm_Flag2 ($D000A2) bit 4: TRACE snap off (graph.c snapon)

; The hook calls 0x91FC2 and copies its flag prologue from 0x97C7F, both
; undocumented and read out of OS 5.8.4. Their first bytes hold absolute calls
; into the rest of the OS, which move between builds, so any other OS fails this
; and SymCE is never armed on it: a wrong 0x91FC2 would crash every ENTER.
; Emits `name`, which returns Z when both match. Used by the installer and the
; app, which do not share code.
macro os_check? name
    local cmp, sig1, sig2
name:
    ld hl, $091FC2
    ld de, sig1
    ld b, 16
    call cmp
    ret nz
    ld hl, $097C7F
    ld de, sig2
    ld b, 16
cmp:
    ld a, (de)
    cp a, (hl)
    ret nz
    inc hl
    inc de
    djnz cmp
    ret
sig1:
    db $FD,$CB,$45,$8E,$FE,$15,$20,$25,$21,$4D,$22,$09,$3E,$65,$CD,$F4
sig2:
    db $FD,$CB,$53,$BE,$FD,$CB,$1F,$86,$CD,$93,$88,$09,$CD,$66,$7C,$09
end macro

; prgmSYMCE: an eZ80 program, which the OS runs at userMem ($D1A881).
virtual at ti.userMem - 2
installerProgram::
    db ti.tExtTok, ti.tAsm84CeCmp
    include 'installer.asm'
    installerLength := $ - $$
end virtual
tifile '8xp', ti.ProtProgObj, 'SYMCE', installerProgram, ti.userMem - 2, installerLength

app_start 'SymCE', '(C) 2026 SymCE. Installer from AsmHook2 by RoccoLox and jacobly', 0

appMain:
    call osOk
    ld hl, osStr
    jq nz, .show
    call casIsOn
    call z, phookArm                    ; opening the app puts the parser hook back (AsmHook2 may have taken the slot)
.screen:
    call ti.ClrScrn
    call ti.HomeUp
    ld hl, headStr
    call appLines
    call casIsOn
    ld de, onVal
    jq z, .casV
    ld de, offVal
.casV:
    ld hl, casLbl
    call row
    call casIsOn
    jq nz, .curTi                       ; CAS off: TI's own
    call curIsTi
    jq nz, .curIns
.curTi:
    ld de, tiVal
    jq .curV
.curIns:
    ld de, insVal
.curV:
    ld hl, curLbl
    call row
    call fontIsOn
    ld de, evoVal
    jq z, .fontV
    ld de, tiVal
.fontV:
    ld hl, fontLbl
    call row
    call gkeyIsOn
    ld de, symceVal
    jq z, .gkeyV
    ld de, tiVal
.gkeyV:
    ld hl, gkeyLbl
    call row
    ld de, nullVal
    ld hl, runLbl
    call row
    ld a, (iy + ti.asm_Flag2)           ; 6: TRACE snap, ON unless the bit is set (a fresh RAM is ON)
    and a, SNAP_OFF
    ld de, onVal
    jq z, .snapV
    ld de, offVal
.snapV:
    ld hl, snapLbl
    call row
    ld a, (iy + ti.asm_Flag2)           ; 7: rainbow parentheses, ON unless the bit is set
    and a, RB_OFF
    ld de, onVal
    jq z, .rbV
    ld de, offVal
.rbV:
    ld hl, rbLbl
    call row
    ld hl, tailStr
    call appLines
    call ti.GetKey
    cp a, ti.k1
    jq z, .cas
    cp a, ti.k2
    jq z, .cursor
    cp a, ti.k3
    jq z, .font
    cp a, ti.k5
    jq z, .run
    cp a, ti.k6
    jq z, .snap
    cp a, ti.k7
    jq z, .rb
    cp a, ti.k4
    jq nz, .exit
    call gkeyIsOn
    jq nz, .gkeySet
    call gkeyClr
    call casIsOn
    call nz, mhookClr                   ; the menu hook stays while the CAS needs it
    call khookSync                      ; and so does a raw key hook, for +
    jq .screen
.gkeySet:
    ld hl, symce_khook
    call ti.SetGetKeyHook               ; arms the RAW KEY hook despite its name
    ld hl, symce_mhook                  ; ZOOM picks are the menu hook's
    call ti.SetMenuHook
    jq .screen
.run:
    call runPrgm                        ; returns only on failure, HL = why
    push hl
    call ti.ClrScrn
    call ti.HomeUp
    pop hl
    call appLines
    call ti.GetKey
    jq .screen
.snap:
    ld a, (iy + ti.asm_Flag2)
    xor a, SNAP_OFF
    ld (iy + ti.asm_Flag2), a
    jq .screen
.rb:
    ld a, (iy + ti.asm_Flag2)
    xor a, RB_OFF
    ld (iy + ti.asm_Flag2), a
    jq .screen
.font:
    call fontFlip                       ; redrawn in the font it just chose
    jq .screen
.cas:
    call casIsOn
    jq z, .casOff
    xor a, a
    ld (HOOK_OK), a
    ld (HOOK_PENDING), a
    ld (HOOK_ANS), a
    ld hl, symce_hook
    call ti.SetHomescreenHook
    ld hl, symce_mhook
    call ti.SetMenuHook
    call phookArm
    call khookSync
    call fontIsOn
    call nz, fontCur
    jq .screen
.casOff:
    call fcurClr
    ld hl, (ti.homescreenHookPtr)
    ld de, symce_hook
    or a, a
    sbc hl, de
    jq z, .clrHs
    add hl, de
    ld de, symce_hook_ti
    or a, a
    sbc hl, de
    jq nz, .noHs
.clrHs:
    call ti.ClrHomescreenHook
.noHs:
    call phookClr                       ; the parser hook, and the slot back to AsmHook2
    call gkeyIsOn
    call nz, mhookClr                   ; the menu hook stays while the GRAPH key needs it
    call khookSync
    jq .screen
.cursor:
    call casIsOn
    jq nz, .screen                      ; CAS off: nothing to switch
    call curIsTi
    ld hl, symce_hook_ti
    jq nz, .setCur
    ld hl, symce_hook
.setCur:
    call ti.SetHomescreenHook
    jq .screen
.show:
    push hl
    call ti.ClrScrn
    call ti.HomeUp
    pop hl
    call appLines
    call ti.GetKey
.exit:
    ld a, ti.kClear
    jp ti.JForceCmd

;
; Run prgmSYMCE from the app. On OS 5.8.4 only the parser refuses an asm program
; (unless AsmHook2's hook is armed), so copying it to userMem ourselves works
; with no hook, even after a RAM clear. It returns only on failure, HL = lines.
; The installer deletes and rewrites this very app, so it must not return into
; app code: it returns into a stub copied after it in the same inserted block
; (DelMem of the installer's own RAM frees both, then JForceCmdNoChar).
runPrgm:
    call findPrgm
    ret c
    ld hl, stubLen
    add hl, bc                          ; BC = code length
    push hl
    call ti.EnoughMem                   ; NC if HL bytes fit; never throws
    pop de
    ld hl, memStr
    ret c
    ld (ti.asm_prgm_size), de
    ex de, hl                           ; InsertMem: HL = bytes, DE = where
    ld de, ti.userMem
    call ti.InsertMem
    call findPrgm                       ; InsertMem moved RAM programs up
    ld de, ti.userMem
    ldir
    push de                             ; the stub is where the code ends
    ld hl, stub
    ld bc, stubLen
    ldir
    jp ti.userMem

; Runs from RAM: absolute OS addresses only, no app labels.
stub:
    ld de, (ti.asm_prgm_size)
    or a, a
    sbc hl, hl
    ld (ti.asm_prgm_size), hl
    ld hl, ti.JForceCmdNoChar
    push hl
    ld hl, ti.userMem
    jp ti.DelMem                        ; DE bytes at HL, then on to the home screen
stubLen := $ - stub

; NC, HL = prgmSYMCE's code after its EF 7B header, BC = its length; or C and
; HL = lines. Archived or in RAM, as the installer's findPart reads a part.
findPrgm:
    ld hl, prgmName
    call ti.Mov9ToOP1
    call ti.ChkFindSym
    ld hl, noPrgmStr
    ret c
    call ti.ChkInRam
    ex de, hl
    jq z, .inRam
    ld de, 9                            ; skip the archive entry's header
    add hl, de
    ld e, (hl)
    add hl, de
    inc hl
.inRam:
    ld bc, 0
    ld c, (hl)
    inc hl
    ld b, (hl)
    inc hl
    ld a, (hl)
    cp a, ti.tExtTok
    jq nz, .notAsm
    inc hl
    ld a, (hl)
    cp a, ti.tAsm84CeCmp
    jq nz, .notAsm
    inc hl
    dec bc
    dec bc
    or a, a
    ret
.notAsm:
    ld hl, notAsmStr
    scf
    ret

; One settings line: label at HL, value at DE (each fixed width, so it lines up).
row:
    push de
    call ti.PutS
    pop hl
    call ti.PutS
    jp ti.NewLine

mhookClr:                               ; clear the menu hook if it is ours
    ld hl, (ti.menuHookPtr)
    ld de, symce_mhook
    or a, a
    sbc hl, de
    ret nz
    jp ti.ClrMenuHook

; CAS is on when the homescreen hook is either body of ours and enabled.
casIsOn:                                ; Z if on
    ld hl, (ti.homescreenHookPtr)
    ld de, symce_hook
    or a, a
    sbc hl, de
    jq z, .ours
    add hl, de
    ld de, symce_hook_ti
    or a, a
    sbc hl, de
    ret nz
.ours:
    ld a, (iy + ti.hookflags2)
    cpl
    and a, 1 shl ti.homescreenHookActive
    ret

curIsTi:                                ; Z if the armed body is the TI-cursor one
    ld hl, (ti.homescreenHookPtr)
    ld de, symce_hook_ti
    or a, a
    sbc hl, de
    ret

; The Evo font is the font hook and the localize hook (TI's language apps use
; that one too, so SymCE takes it over while the font is on). "On" is our
; pointer in the font slot with its enable bit set; off clears each hook only
; if it is still ours, as the menu hook is above.
fontIsOn:                               ; Z if on
    ld hl, (ti.fontHookPtr)
    ld de, symce_fhook
    or a, a
    sbc hl, de
    ret nz
    ld a, (iy + ti.hookflags3)
    cpl
    and a, 1 shl ti.fontHookActive
    ret

fontFlip:
    call fontIsOn
    jq nz, fontSet
fontClr:
    xor a, a
    ld (0D0EE06h), a                    ; tempArm (hook.c): not armed for an error screen
    ld hl, (ti.fontHookPtr)
    ld de, symce_fhook
    or a, a
    sbc hl, de
    call z, ti.ClrFontHook
    ld hl, (ti.localizeHookPtr)
    ld de, symce_lhook
    or a, a
    sbc hl, de
    call z, ti.ClrLocalizeHook
fontCur:                                ; CAS on: the font hook stays, for the cursor bar
    call casIsOn
    ret nz
    ld hl, symce_fhook_c
    jp ti.SetFontHook

fcurClr:                                ; CAS off: the cursor-only font hook goes
    ld hl, (ti.fontHookPtr)
    ld de, symce_fhook_c
    or a, a
    sbc hl, de
    ret nz
    jp ti.ClrFontHook

fontSet:
    xor a, a
    ld (0D0EE06h), a
    ld hl, symce_fhook
    call ti.SetFontHook
    ld hl, symce_lhook
    jp ti.SetLocalizeHook

; The GRAPH key is the raw key hook: the OS's main loop hands it every key it
; reads (ROM 0x8C482), before a context key like GRAPH switches screens. "On" is
; our pointer in that slot with its enable bit set.
gkeyIsOn:                               ; Z if on
    ld hl, (ti.rawKeyHookPtr)
    ld de, symce_khook
    or a, a
    sbc hl, de
    ret nz
    ld a, (iy + ti.hookflags2)
    cpl
    and a, 1 shl ti.rawKeyHookActive
    ret

gkeyClr:
    ld hl, (ti.rawKeyHookPtr)
    ld de, symce_khook
    or a, a
    sbc hl, de
    ret nz
    jp ti.ClrRawKeyHook

; The raw key hook follows the settings: khook while the GRAPH key is SymCE's, else khook_cas
; (only + in 2nd MATH) while the CAS is on, else none. Clears only a body of ours.
khookSync:
    call gkeyIsOn
    ret z
    call casIsOn
    jq z, .cas
    call gkeyClr
    ld hl, (ti.rawKeyHookPtr)
    ld de, symce_khook_cas
    or a, a
    sbc hl, de
    ret nz
    jp ti.ClrRawKeyHook
.cas:
    ld hl, symce_khook_cas
    jp ti.SetGetKeyHook

; The grapher (src/graph.c). Its work struct lives in the engine's RAM (see the
; layout at `struct gw`); this fills the inputs from the OS, then calls
; symce_graph(w, key) with key 0 and after each key until it returns 1. Keys come from
; GetCSC (GetKey would draw the battery icon over the plot); a held arrow repeats.
GW := 0D0EF00h
GWN := GW + 0C00h                       ; setup scratch, past the struct (GW is cleared up to here)
GWD := GW + 0C01h                       ; 3 bytes
GWT := GW + 0C04h                       ; 3 bytes: timer 1's control word, put back on the way out
GWS := GW + 0C07h                       ; the key that opened the grapher (GRAPH or TRACE)
GWA := GW + 0C08h                       ; menu pick for the grapher's first key: a zoom action 1..21, 0 plain, 0FEh = only check
GWK := GW + 0C0Ah                       ; GWA, taken by graphMain0
GWC := GW + 0C0Bh                       ; 3 bytes: the home cursor (curRow, curCol), put back when a menu pick leaves
GWB := GW + 0C10h                       ; the equations' first name byte (5E 10 Y1, 20 X1T, 40 r1) and how many slots (10, 12, 6)
GWQ := GW + 0C11h
GWX := GW + 0C12h                       ; 0A5h 5Ah: the grapher left Text( for a menu (MATH, TEST ...). Two bytes: this RAM is the
                                        ; engine's work pool, a stale single byte must not read as set
GWP := GW + 0C14h                       ; 2 bytes: the picked item's token, written at the Text( cursor
GWY := GW + 0C16h                       ; 0A5h 5Ah: the grapher left Text( for the CATALOG (no menu: its pick is read in the raw key hook)
GWM := GW + 0C0Eh                       ; 0A5h 5Ah: a menu opened from the grapher is up (CLEAR there comes back). This RAM is
                                        ; shared scratch (the CAS menu, the engine, programs): a stale byte must not read as set

; The cursor's blink and a held arrow's repeats are timed with timer 1 at 32768 Hz
; (the OS's own tick is timer 3; timer 1 is the toolchain's clock()). Its control
; word is saved here and put back whichever way the grapher is left.
; The grapher stores X, Y (T; theta, R) as it draws. CreateReal under the live home screen, with an entry
; of 2..6 tokens and no history, left the OS state the next Y=/WINDOW/MODE put-away trips on (reset). TI creates
; them from its own graph context, home put away: do that, only when one is missing.
graphMain:
    ld a, (GWA)
    cp a, 0FEh                          ; only asks graph_fall: nothing to make, home stays as it is
    jq z, graphMain1
    cp a, 0FDh                          ; the same, for a DRAW pick
    jq z, graphMain1
    call graphNeed
    jq c, .put
    jq graphMain1
.put:
    ld hl, (GWC)                        ; the home cursor as it was before a menu (CALC, ZOOM) moved it: the put-away files this one
    ld (ti.curRow), hl
    ld a, ti.kMode
    call ti.NewContext0
    ld iy, ti.flags
    call graphMain1
    push af
    ld a, ti.kQuit
    call ti.NewContext0
    ld iy, ti.flags
    pop af
    ret

graphNeed:                              ; C: a variable the grapher may store does not exist yet
    ld a, 58h
    call .chk
    ret c
    ld a, 59h
    call .chk
    ret c
    ld a, (0D00082h)                    ; grfModeFlags: Par 40h, Pol 20h
    bit 6, a
    jq z, .nopar
    ld a, 54h
    call .chk
    ret c
.nopar:
    ld a, (0D00082h)
    bit 5, a
    jq nz, .rt
    ld a, (0D00084h)                    ; grfDBFlags bit 3: PolarGC stores R and theta too
    bit 3, a
    jq z, .no
.rt:
    ld a, 5Bh
    call .chk
    ret c
    ld a, 52h
.chk:
    ld hl, ti.OP1
    ld (hl), 0
    inc hl
    ld (hl), a
    inc hl
    ld (hl), 0
    inc hl
    ld (hl), 0
    jp ti.ChkFindSym                    ; C: not found
.no:
    or a, a
    ret

graphMain1:
    ld hl, (0F20030h)
    ld (GWT), hl
    ld a, l
    and a, 0F8h                         ; stop it: enable, clock and interrupt bits off ...
    ld l, a
    ld a, h
    and a, 0FDh                         ; ... and count down
    ld h, a
    ld (0F20030h), hl
    ld hl, 0
    ld (0F20000h), hl                   ; from zero
    xor a, a
    ld (0F20003h), a
    ld hl, (0F20030h)
    set 0, l                            ; enable
    set 1, l                            ; the 32768 Hz clock
    set 1, h                            ; count up
    ld (0F20030h), hl
    call graphMain0
    push af
    ld hl, (GWT)
    ld (0F20030h), hl
    pop af
    ret

graphMain0:
    ld a, (GWA)
    ld (GWK), a
    xor a, a
    ld (GWA), a
    ld hl, GW
    ld (hl), 0
    ld de, GW + 1
    ld bc, 0BFFh
    ldir
    ld hl, ti.Xmin                      ; Xmin, Xmax, then Ymin, Ymax: 9-byte reals
    ld de, GW
    ld bc, 18
    ldir
    ld hl, 0D01E4Eh
    ld bc, 18
    ldir
    bit ti.trigDeg, (iy + ti.trigFlags)
    jq z, .rad
    ld a, 1
    ld (GW + 36), a
.rad:
    ld hl, ti.y1LineColor               ; Func: Y1..Y0, 10 colours
    ld bc, 10
    ld a, 10h
    ld (GWB), a
    ld a, 10
    ld (GWQ), a
    ld a, (0D00082h)                    ; grfModeFlags: Par 40h, Pol 20h
    bit 6, a
    jq z, .notPar
    ld hl, ti.para1LineColor            ; Par: X1T Y1T .. X6T Y6T (12 slots), a colour per pair
    ld bc, 6
    ld a, 20h
    ld (GWB), a
    ld a, 12
    ld (GWQ), a
    jq .colset
.notPar:
    bit 5, a
    jq z, .notPol
    ld hl, ti.polar1LineColor           ; Pol: r1..r6
    ld bc, 6
    ld a, 40h
    ld (GWB), a
    ld a, 6
    ld (GWQ), a
    jq .colset
.notPol:
    bit 7, a
    jq z, .colset
    ld hl, ti.secULineColor             ; Seq: u v w (5E 80..82), 3 colours
    ld bc, 3
    ld a, 80h
    ld (GWB), a
    ld a, 3
    ld (GWQ), a
.colset:
    ld de, GW + 37
    ldir
    xor a, a
    ld (GWN), a
    ld (GWX), a
    ld (GWX + 1), a
    ld (GWY), a
    ld (GWY + 1), a
    ld hl, GW + 304
    ld (GWD), hl
.eq:                                    ; Y1..Y0 are 5E 10..19, X1T..Y6T 5E 20..2B, r1..r6 5E 40..45
    ld a, (GWB)
    ld hl, GWN
    add a, (hl)
    ld hl, ti.OP1
    ld (hl), ti.EquObj
    inc hl
    ld (hl), 5Eh
    inc hl
    ld (hl), a
    inc hl
    ld (hl), 0
    call ti.ChkFindSym
    jq c, .eqNext
    dec hl
    ld a, (hl)                          ; the flag byte: bit 0 is "selected"
    and a, 1
    jq z, .eqNext
    call ti.ChkInRam
    jq nz, .eqNext                      ; ponytail: an archived Yn is not plotted
    ex de, hl
    ld c, (hl)
    inc hl
    ld b, (hl)
    inc hl
    ld a, b
    or a, c
    jq z, .eqNext
    ld a, b
    or a, a
    ld a, c
    jq z, .lenOk
    ld a, 255
.lenOk:
    push hl
    push af
    ld a, (GWN)
    ld de, 0
    ld e, a
    ld hl, GW + 49
    add hl, de
    pop af
    ld (hl), a                          ; len[i]; over 96 graph.c calls too long
    cp a, 97
    jq c, .cnt
    ld a, 96
.cnt:
    ld bc, 0
    ld c, a
    ld de, (GWD)
    pop hl
    ldir
.eqNext:
    ld hl, (GWD)
    ld de, 96
    add hl, de
    ld (GWD), hl
    ld a, (GWN)
    inc a
    ld (GWN), a
    ld hl, GWQ
    cp a, (hl)
    jq nz, .eq
    xor a, a
    ld (GWN), a
    ld hl, GW + 61
    ld (GWD), hl
.var:                                   ; A..Z, theta: real vars 00 41..5B
    ld a, (GWN)
    add a, 41h
    ld hl, ti.OP1
    ld (hl), ti.RealObj
    inc hl
    ld (hl), a
    inc hl
    ld (hl), 0
    call ti.ChkFindSym
    jq c, .varNext
    call ti.ChkInRam
    jq nz, .varNext                     ; ponytail: an archived letter reads as 0
    ex de, hl
    ld de, (GWD)
    ld bc, 9
    ldir
.varNext:
    ld hl, (GWD)
    ld de, 9
    add hl, de
    ld (GWD), hl
    ld a, (GWN)
    inc a
    ld (GWN), a
    cp a, 27
    jq nz, .var
    ld hl, GW
    push hl
    call symce_graph + 4                ; NZ: the OS draws this graph (a mode or stat plot it cannot)
    pop hl
    ld iy, ti.flags
    or a, a
    jq z, .mine
    ld a, 0FFh
    ret
.mine:
    ld a, (GWK)
    cp a, 0FEh
    jq z, .chk
    cp a, 0D0h                          ; 0D0h + action = the same check, for a ZOOM pick
    jq c, .go
.chk:
    xor a, a                            ; only the fallback check was asked for
    ret
.go:
    res ti.onInterrupt, (iy + ti.onFlags)   ; a stale ON must not stop the first draw
    or a, a
    jq z, .loop
    add a, 0C0h                         ; a zoom: the grapher's first key (draws first when it has to)
.loop:                                  ; A = 0 first draw, else a GetCSC scan code
    ld hl, 0
    ld l, a
    push hl
    ld hl, GW
    push hl
    call symce_graph
    pop hl
    pop hl
    ld iy, ti.flags                     ; C clobbers IY
    or a, a
    jq z, .poll                         ; stay
    push af
    call 021A3Ch                        ; DrawStatusBar: the big graph covered it; every exit below (home, Y=, a menu ...) must start from a whole bar
    ld iy, ti.flags
    pop af
    dec a
    ret z                               ; 1: home (A = 0)
    inc a
    ret                                 ; an OS key for the caller to send
.poll:                                  ; ponytail: no APD in here, like the menu's loop
    ld a, (GWS)                         ; TRACE opened the grapher: start tracing once it is drawn
    cp a, ti.kTrace
    jq nz, .key
    xor a, a
    ld (GWS), a
    ld a, 0FEh
    jq .loop
.key:
    call ti.GetCSC
    or a, a
    jq nz, .loop
    ld a, 0FFh                          ; no key: the blink and the repeats
    jq .loop

; Print zero-terminated lines until an empty one.
appLines:
    ld a, (hl)
    or a, a
    ret z
    call ti.PutS
    call ti.NewLine
    jq appLines

os_check osOk

osStr:
    db "SymCE is OFF", 0
    db " ", 0
    db "It was built for OS 5.8.4", 0
    db "and does not recognise", 0
    db "this one, so it stays off.", 0
    db 0
headStr:
    db "SymCE settings", 0
    db " ", 0
    db 0
tailStr:
    db "Press 1-7, CLEAR exits.", 0
    db 0
casLbl:
    db "1: CAS         ", 0
curLbl:
    db "2: Cursor      ", 0
fontLbl:
    db "3: Font        ", 0
gkeyLbl:
    db "4: GRAPH key   ", 0
runLbl:
    db "5: Run prgmSYMCE", 0
snapLbl:
    db "6: Trace snap  ", 0
rbLbl:
    db "7: Paren colors", 0
nullVal:
    db 0
prgmName:
    db ti.ProtProgObj, "SYMCE", 0
noPrgmStr:
    db "prgmSYMCE is missing.", 0
    db "Send SYMCE.8xp, then", 0
    db "press 5 again.", 0
    db 0
notAsmStr:
    db "prgmSYMCE is not the", 0
    db "SymCE installer.", 0
    db 0
memStr:
    db "Not enough free RAM to", 0
    db "run prgmSYMCE.", 0
    db 0
onVal:
    db "        ON", 0
offVal:
    db "       OFF", 0
insVal:
    db "    INSERT", 0
tiVal:
    db "        TI", 0
evoVal:
    db "       EVO", 0
symceVal:
    db "     SYMCE", 0

; Built from src/hook.c by the makefile. Position independent: it finds itself
; through homescreenHookPtr, so it needs no relocation entries.
symce_hook:
    file '../obj/hook/hook.bin'
; The hook reads these words, just past its own last byte, to find the engine
; and the ALPHA+DOWN menu. They are relative to app.base, so app_create
; relocates them. The byte after them is the cursor: 0 inserts, 1 is TI's own.
    dl symce_engine
    dl symce_menu
    db 0
    dl symce_lhook                      ; .Lerr arms it for one error screen
    dl graphAns                         ; the ALPHA+DOWN menu's Graph Ans
; The same body again for the TI cursor; the settings screen arms one or the other.
symce_hook_ti:
    file '../obj/hook/hook.bin'
    dl symce_engine
    dl symce_menu
    db 1
    dl symce_lhook
    dl graphAns
; The menu hook: ZOOM picks go to the grapher while the GRAPH key is SymCE's, and
; everything else to the CAS's SymCE tab in 2nd MATH while the CAS is on. Armed
; whenever either is.
symce_mhook:
    db 83h
    cp a, 3
    jq z, menuKey
    cp a, 4
    jq nz, .nc
    push hl
    ld hl, (ti.curRow)                  ; a menu opens: the home cursor, before the menu moves it
    ld (GWC), hl
    pop hl
.nc:
    push hl
    push de
    push af
    call casIsOn
    jq nz, .off
    pop af
    pop de
    pop hl
    ld ix, symce_mhook_cas + 1          ; the shim finds menu_tab through ix, as the OS sets it
    jp symce_mhook_cas + 1
.off:
    pop af
    pop de
    pop hl
    cp a, a
    ret

; A = a key code (ti.keyExtend for an FE/FC one): its token, D 0 for one byte, to GWP
textTok:
    call ti.ConvKeyToTok
    ld hl, GWP
    ld a, d
    or a, a
    jq nz, .t2
    ld d, e
    ld e, a
.t2:
    ld (hl), d
    inc hl
    ld (hl), e
    ret

; A = 3, B = the key. CLEAR in a menu opened from the grapher goes back to it; a pick in the ZOOM
; menu (ENTER, a digit, a letter) is done on the grapher unless TI's graph has to
; draw (mode, stat plot ...), the item is ZoomStat or SetFactors, or the GRAPH key is TI's.
; Never returns NZ: the grapher ends in JForceCmd, the rest is the OS's (Z).
menuKey:
    push hl
    push de
    push bc
    push af
    ld a, (GWX)
    cp a, 0A5h
    jq nz, .nottext
    ld a, (GWX + 1)
    cp a, 5Ah
    jq nz, .nottext
    ld a, b                             ; Text(: the item's key code is a token; the grapher writes it at the cursor
    call textTok
    xor a, a
    ld (GWX), a
    ld (GWX + 1), a
    ld a, 3Ch                           ; the grapher's first key 0FCh
    push af
    jq .go
.nottext:
    ld a, b
.pick:
    call gkeyIsOn
    jq nz, .no
    ld a, (ti.menuCurrent)
    cp a, 4
    jq nz, .no
    ld a, b
    cp a, 0FBh
    jq nz, .tab
    ld a, (ti.keyExtend)                ; ZQuadrant1 and the ZFracs are FB C0..C6
    sub a, 0C0h
    cp a, 7
    jq nc, .no
    add a, 11
    jq .run
.tab:
    ld hl, zkeys                        ; the item's key code, the same whether ENTER or a number picked it
.zl:
    ld a, (hl)
    or a, a
    jq z, .no                           ; ZoomStat, SetFactors, anything else: the OS's
    inc hl
    cp a, b
    ld a, (hl)
    inc hl
    jq nz, .zl
.run:
    push af
    add a, 0D0h
    ld (GWA), a
    call graphMain                      ; only asks graph_fall
    ld iy, ti.flags
    cp a, 0FFh
    jq nz, .go
    pop af
    jq .no
.go:
    ld a, ti.kQuit
    call ti.NewContext0                 ; leave the menu, Y=, WINDOW ... first (khook's cx test is for no menu)
    ld iy, ti.flags
    res ti.indicRun, (iy + ti.indicFlags)
    pop af
    ld (GWA), a
    xor a, a
    ld (GWS), a                         ; a TRACE press the OS answered must not start one here
    call graphMain
    ld iy, ti.flags
    ld hl, 0D0265Bh                     ; the OS counts hook calls in here and will never see this one return
    dec (hl)
    set 5, (iy + 17)                    ; the OS's menu-closed flags (measured at home); else it keeps treating keys as menu keys
    set 0, (iy + 38)
    ld hl, (GWC)
    ld (ti.curRow), hl
    ld hl, 010000h
    ld (0D00824h), hl                   ; menuCurrent/Sub off
    or a, a
    jp z, ti.JForceCmdNoChar            ; home
    cp a, 0FFh
    jq nz, .key
    xor a, a
    jp ti.JForceCmdNoChar
.key:
    call setMark
    ld sp, (0D007FAh)                   ; the monitor's stack, as JForceCmd does
    res 1, (iy + 40)
    res 2, (iy + 40)
    jp 08C49Fh                          ; the monitor's key dispatch, A = the key: as if the raw key hook returned it (JForceCmd takes a token, not a key)
.no:
    pop af
    pop bc
    pop de
    pop hl
    cp a, a
    ret

; ALPHA+DOWN > Graph Ans, from the home hook (.Lgans): Ans into the first empty Yn (Y1 when none is), as a
; store the engine does (symce_engine, mode 80h: no entry, Ans is the answer), then the grapher, left the way
; menuKey leaves it. The hook's frame is gone; on no store it returns to the hook's caller, key swallowed.
graphAns:
    push hl
    push de
    push bc
    ld iy, ti.flags
    ld c, 10h
.fy:
    ld hl, ti.OP1
    ld (hl), 3
    inc hl
    ld (hl), 5Eh
    inc hl
    ld (hl), c
    inc hl
    ld (hl), 0
    push bc
    call ti.ChkFindSym
    pop bc
    jq c, .got                          ; no such equation: empty
    ex de, hl                           ; or one with no tokens (the OS makes them empty): its size word
    ld e, (hl)
    inc hl
    ld d, (hl)
    ld a, d
    or a, e
    jq z, .got
    inc c
    ld a, c
    cp a, 1Ah
    jq c, .fy
    ld c, 10h
.got:
    ld hl, ti.OP5                       ; the "entry": ->Yn
    ld (hl), 4
    inc hl
    ld (hl), 5Eh
    inc hl
    ld (hl), c
    ld hl, 0
    push hl                             ; look
    ld l, 80h
    push hl                             ; mode
    ld hl, 0D0EE20h
    push hl                             ; lastAns
    ld hl, 0D0EF00h
    push hl                             ; engineWork
    ld hl, 0D0EE80h
    push hl                             ; ansBuf
    ld hl, 3
    push hl                             ; len
    ld hl, ti.OP5
    push hl
    call symce_engine
    ld hl, 21
    add hl, sp
    ld sp, hl
    ld iy, ti.flags
    or a, a
    jq z, .no
    cp a, 0FFh
    jq z, .no
    ld hl, (ti.curRow)
    ld (GWC), hl
    ld a, ti.kGraph
    ld (GWS), a
    xor a, a
    ld (GWA), a
    res ti.indicRun, (iy + ti.indicFlags)
    call graphMain
    ld iy, ti.flags
    ld hl, 0D0265Bh                     ; as menuKey: the OS will never see this hook call return
    dec (hl)
    set 5, (iy + 17)
    set 0, (iy + 38)
    ld hl, (GWC)
    ld (ti.curRow), hl
    ld hl, 010000h
    ld (0D00824h), hl
    or a, a
    jp z, ti.JForceCmdNoChar
    cp a, 0FFh
    jq nz, .key
    ld a, ti.kGraph                     ; TI's graph has to draw: it does, from the key
.key:
    call setMark
    ld sp, (0D007FAh)
    res 1, (iy + 40)
    res 2, (iy + 40)
    jp 08C49Fh
.no:
    pop bc
    pop de
    pop hl
    xor a, a
    inc a
    ret

; DRAW menu item -> the grapher's pick (D_* in graph_draw.h), 0 = not on the grapher: DRAW tab, POINTS tab
dtab0:
    db 1, 2, 3, 4, 5, 0, 0, 0, 6, 7, 8
dtab1:
    db 9, 10, 11, 0, 0, 0, 0, 0, 0, 0, 0


; key code, action: ZBox ZoomIn ZoomOut ZDecimal ZSquare ZStandard ZTrig ZInteger ZoomStat ZoomFit | ZPrevious ZoomSto ZoomRcl
zkeys:
    db 5Fh, 1, 5Ch, 2, 5Dh, 3, 60h, 4, 62h, 5, 63h, 6, 64h, 7, 67h, 8, 68h, 9, 5Bh, 10, 5Eh, 18, 66h, 19, 65h, 20, 0

; A = a key the grapher hands to the OS: a menu key (2Bh..3Ch) arms the CLEAR marker.
setMark:
    push af
    sub a, 2Bh
    cp a, 12h
    jq nc, .n
    ld a, 0A5h
    ld (GWM), a
    ld a, 5Ah
    ld (GWM + 1), a
.n:
    pop af
    ret

; The CAS's tab, src/mhook.c: a shim into menu.c's menu_tab, whose address is the word after it.
symce_mhook_cas:
    file '../obj/hook/mhook.bin'
    dl symce_menu + 4

; The font hook and the localize hook (src/fhook.c, lhook.c), and the font they
; answer from (obj/font.inc, made by tools/mkfont.py). Each body reads the two
; words after it, relocated like the ones above: the glyph map, and its own
; records less one record.
symce_fhook:
    file '../obj/hook/fhook.bin'
    dl font_map
    dl font_large - 28
    db 1                                ; the Evo glyphs too
    dl symce_menu + 20                  ; paren_pass (menu.c): MathPrint's rainbow parentheses
symce_fhook_c:                          ; the insert-cursor bar only: Evo off, CAS on
    file '../obj/hook/fhook.bin'
    dl font_map
    dl font_large - 28
    db 0
    dl symce_menu + 20
symce_lhook:
    file '../obj/hook/lhook.bin'
    dl font_map
    dl font_small - 25
    dl symce_menu + 12                  ; menu_err: the error screens' lines
include '../obj/font.inc'

; + in 2nd MATH on a SymCE tab's item (src/menu.c, menu_help): the help screen. Z: not ours, the
; OS's + goes on. Else A = the key for the OS: kEnter or kQuit as the screen was left, or the
; key eaten (the monitor is told to wait for the next one).
mathHelp:
    push hl
    push de
    push bc
    res ti.indicRun, (iy + ti.indicFlags)   ; the monitor set the busy dots; they'd spin over the screen
    call symce_menu + 8
    ld iy, ti.flags                     ; C clobbers IY
    ld a, l
    pop bc
    pop de
    pop hl
    cp a, 1
    jq c, .no
    jq nz, .key
    set 1, (iy + 40)                    ; eaten (8C48C: the monitor goes back for a key)
.key:
    or a, a                             ; NZ
    ret
.no:
    xor a, a
    ret

; The raw key hook while the GRAPH key is TI's: only the CAS's + (mathHelp). One slot, so this
; is khook with less (khookSync arms one or the other).
symce_khook_cas:
    db 83h
    push bc
    ld b, a
    ld a, (ti.menuCurrent)
    cp a, 9
    jq nz, .pass
    ld a, b
    cp a, ti.kAdd
    jq nz, .pass
    call mathHelp
    jq z, .pass
    pop bc
    ret
.pass:
    ld a, b
    pop bc
    or a, a
    ret

; The raw key hook body. Like the others it lives in the app (the OS resets the
; calculator at a context switch if an armed hook points outside flash). The OS
; jumps to the byte after the 83h with A = the key and takes A back as the key
; to process. GRAPH opens SymCE's grapher in place of TI's; the rest goes on.
symce_khook:
    db 83h
    push bc
    ld b, a                             ; the key
    ld a, (GWY)
    cp a, 0A5h
    jq nz, .nocat
    ld a, (GWY + 1)
    cp a, 5Ah
    jq z, .cat
.nocat:
    ld a, (ti.menuCurrent)
    cp a, 11h                           ; 2nd CALC (from the grapher, home, Y= ...; the menu hook never sees its picks)
    jq z, .calcmenu
    cp a, 3Ah                           ; its Par (3Ah) and Pol (3Bh) menus
    jq z, .calcmenu
    cp a, 3Bh
    jq z, .calcmenu
    cp a, 3Ch                           ; and Seq's (3Ch, one item: value)
    jq z, .calcmenu
    cp a, 5                             ; 2nd DRAW, opened from the grapher (from home it pastes: the OS's)
    jq z, .drawmenu
    cp a, 9                             ; 2nd MATH: + on a SymCE tab's item
    jq z, .mathmenu
    or a, a
    jq nz, .nomenu
    ld (GWX), a                         ; no menu up: a Text( menu marker is stale
    ld (GWX + 1), a
    ld a, b
    cp a, ti.kCalc                      ; the menu is about to open: the home cursor, before it moves it (the menu hook's event 4 does this for ZOOM)
    jq nz, .nomenu
    push hl
    ld hl, (ti.curRow)
    ld (GWC), hl
    pop hl
    jq .nomenu
.mathmenu:
    ld a, b
    cp a, ti.kAdd
    jq nz, .nomenu
    call mathHelp
    jq z, .nomenu
    pop bc
    ret
.cat:                                   ; the CATALOG opened from Text( is up, if it is (menuCurrent 46h)
    ld a, b
    cp a, 3Eh
    jq z, .nocat                        ; the key that opened it: the catalog is not up yet
    ld a, (ti.menuCurrent)
    cp a, 46h
    jq nz, .catclr                      ; not up: the marker is stale, or the catalog was left
    ld a, b
    cp a, ti.kEnter
    jq z, .catenter
    cp a, 6
    jq z, .catenter                     ; the catalog is alpha-locked: ENTER is kAlphaEnter
    cp a, ti.kClear
    jq z, .catback
    jq .nocat                           ; paging (ALPHA up/down arrive as 7, 8), letters ...: the catalog stays up; menuCurrent says when it is gone
.catclr:
    xor a, a
    ld (GWY + 1), a
    ld (GWY), a                         ; any other key: it is gone
    jq .nocat
.catback:
    xor a, a
    ld (GWP), a                         ; CLEAR: back in Text(, nothing to write
    ld (GWY + 1), a
    ld (GWY), a
    jq .catgo
.catenter:
    xor a, a
    ld (GWY + 1), a
    ld (GWY), a
    ld hl, (ti.catalogCurrent)          ; the highlighted item: a key code, prefix first
    ld d, (hl)
    inc hl
    ld a, (hl)
    ld (ti.keyExtend), a
    ld a, d
    call textTok
.catgo:
    ld a, 3Ch                           ; the grapher's first key 0FCh
    push af
    jq .cgo
.calcmenu:
    ld a, b
    cp a, ti.kEnter
    jq nz, .dig
    ld a, (0D00826h)                    ; the highlighted item, 0 based
    inc a
    jq .cpick
.dig:
    sub a, ti.k1
    jq c, .nomenu
    ld c, 7                             ; the menu's items: 7, Par 4, Pol 3
    push af
    ld a, (0D00082h)
    bit 6, a
    jq z, .np
    ld c, 4
.np:
    bit 5, a
    jq z, .nq
    ld c, 3
.nq:
    bit 7, a
    jq z, .ns
    ld c, 1
.ns:
    pop af
    cp a, c
    jq nc, .nomenu
    inc a
.cpick:
    add a, 20h                          ; the grapher's first key is 0C0h + this: 0E1h..0E7h
    push af
    ld a, 0FEh
.probe:
    ld (GWA), a
    call graphMain                      ; only asks graph_fall
    ld iy, ti.flags
    cp a, 0FFh
    jq nz, .cgo
    pop af                              ; TI's own CALC
    ld a, b
    pop bc
    ret
.cgo:
    ld a, ti.kQuit
    call ti.NewContext0                 ; leave the menu first, as the ZOOM picks do
    ld iy, ti.flags
    res ti.indicRun, (iy + ti.indicFlags)
    pop af
    ld (GWA), a
    xor a, a
    ld (GWS), a
    call graphMain
    ld iy, ti.flags
    set 5, (iy + 17)                    ; the OS's menu-closed flags
    set 0, (iy + 38)
    ld hl, (GWC)
    ld (ti.curRow), hl
    ld hl, 010000h
    ld (0D00824h), hl                   ; menuCurrent/Sub off
    or a, a
    jp z, ti.JForceCmdNoChar            ; home
    cp a, 0FFh
    jq nz, .ckey
    xor a, a
    jp ti.JForceCmdNoChar
.ckey:
    call setMark
    ld sp, (0D007FAh)
    res 1, (iy + 40)
    res 2, (iy + 40)
    jp 08C49Fh
.drawmenu:
    ld a, (GWM)
    cp a, 0A5h
    jq nz, .nomenu
    ld a, (GWM + 1)
    cp a, 5Ah
    jq nz, .nomenu
    ld a, b
    cp a, ti.kEnter
    jq nz, .ddig
    ld a, (0D00826h)                    ; the highlighted item, 0 based
    inc a
    jq .dtab
.ddig:
    sub a, ti.k0                        ; 0..9 (k0..k9 are consecutive); 0 is item 10
    jq c, .nomenu
    cp a, 10
    jq nc, .nomenu
    or a, a
    jq nz, .dtab
    ld a, 10
.dtab:                                  ; A = the item, 1 based; the tab (0 DRAW, 1 POINTS, 2 STO) is at 0D00825h
    dec a
    cp a, 11
    jq nc, .nomenu
    ld hl, dtab0
    ld de, 0
    ld e, a
    ld a, (0D00825h)
    or a, a
    jq z, .dlook
    dec a
    jq nz, .nomenu                      ; STO: the OS's
    ld hl, dtab1
.dlook:
    add hl, de
    ld a, (hl)                          ; the grapher's pick, 0 = the OS does it (DrawF, Shade(, Pxl-On( ... paste to home)
    or a, a
    jq z, .nomenu
    add a, 30h                          ; the grapher's first key is 0C0h + this: 0F1h..0FBh
    push af
    ld a, 0FDh
    jq .probe
.nomenu:
    ld a, b
    pop bc
    cp a, ti.kClear
    jq nz, .nc
    ld a, (GWM)
    cp a, 0A5h
    jq nz, .clr
    ld a, (GWM + 1)
    cp a, 5Ah
    jq nz, .clr
    ld a, (ti.menuCurrent)              ; and an OS menu really is up
    or a, a
    jq z, .clr
    xor a, a
    ld (GWM), a
    ld a, ti.kGraph                     ; CLEAR out of a menu the grapher opened: back to the grapher
    jq .open
.clr:
    ld a, ti.kClear
    jq .mk                              ; CLEAR keeps the marker (the arrows too): it is the menu's own
.nc:
    dec a
    cp a, 4
    inc a
    jq c, .mk
    push af
    xor a, a
    ld (GWM), a                         ; any other key: whatever menu was up is not "from the grapher" any more
    pop af
.mk:
    cp a, ti.kGraph
    jq z, .open
    cp a, ti.kTable                     ; 2nd GRAPH: SymCE's table (graph_fall hands the OS the ones it cannot do)
    jq z, .open
    cp a, ti.kTrace
    ret nz
.open:
    ld (GWS), a                         ; GW's setup bytes are free outside the grapher
    ld a, (ti.menuCurrent)
    or a, a
    ld a, (GWS)
    jq nz, .nosave
    ld hl, (ti.curRow)                  ; GRAPH from home: the cursor 2nd CALC's errors and ENTER give back (no menu hook runs for it)
    ld (GWC), hl
.nosave:
    ld a, (ti.cxCurApp)
    cp a, ti.kExtApps                   ; flash apps and up: theirs
    jq nc, .os
    cp a, ti.kPrgmInput                 ; a program's Input: the OS keeps it
    jq z, .os
    cp a, ti.kError                     ; the ERR screen ignores GRAPH
    jq z, .os
    cp a, ti.kQuit
    jq z, .home
    ld a, ti.kQuit                      ; leave Y=, WINDOW, ... first: its putaway
    call ti.NewContext0                 ; files an equation still being edited
.home:
    ld a, (GWS)
    cp a, ti.kTable
    ld a, 20h                           ; the table's first key is 0C0h + this
    jq z, .gwa
    xor a, a
.gwa:
    or a, a
    jq nz, .gwb
    ld a, (GWX)
    cp a, 0A5h
    ld a, 0
    jq nz, .gwb
    ld a, (GWX + 1)
    cp a, 5Ah
    ld a, 0
    jq nz, .gwb
    ld (GWP), a                     ; CLEAR out of a menu opened from Text(: back in Text(, nothing to write
    ld a, 3Ch
.gwb:
    ld (GWA), a                         ; graphMain0 takes this as the first key: RAM nobody wrote to is not a zoom
    res ti.indicRun, (iy + ti.indicFlags)   ; the monitor set the busy dots; they'd spin over the plot
    call graphMain                      ; A = 0 home, 0FFh the OS graphs this, else an OS key to send
    or a, a
    jp z, ti.JForceCmdNoChar            ; home, redrawn, as CLEAR from TI's graph
    cp a, 0FFh
    jq z, .fall
    jp setMark                          ; the key the user pressed on the graph (Y=, WINDOW ...): the OS does it from home
.fall:
    ld a, (GWS)                         ; from home: TI's graph, or TI's TRACE
    ret
.os:
    ld a, (GWS)
    ret


; ---- The parser hook: SymCE commands inside TI-BASIC programs, expr("factor(X^2-1)") (docs/TI84CE-KNOWLEDGE.md section 14). ----
; A=1 is raised INSIDE expr(, with B = 8Eh, once the OS has made its argument a string (a temp variable, or Str1) and
; left its name in OP1. Read it, and if a SymCE command stands outside the string literals in it, run the engine
; (MODE_PRGM). An answer is kept at PH_OUT, ready to parse; an error is thrown from here, at A=1, which is safe
; (an A=0 throw out of anything with expr( in it resets the calculator, and a Goto from one is dead). Then the
; inner parse of that string calls A=0: the one whose source is the string just read (length and byte sum) is pointed
; at the answer. Every other A=0 and A=2 is AsmHook2's or nobody's: a `#` entry, an EF 7B program and B = 5Dh are
; chained to its parser hook; everything else returns Z untouched.
PH_ARMED      := 0D0EE62h                    ; 0A5h: the answer waits at PH_OUT for the next A=0, which disarms it whether it matches or not
PH_LEN        := 0D0EE63h                    ; its length
PH_SLEN       := 0D0EE64h                    ; the source string's length (1..250)
PH_SUM        := 0D0EE65h                    ; and the sum of its bytes (3 bytes, the high one 0)
PH_OUT        := 0D0EE80h                    ; hook.c's ansBuf, idle while a program runs
PH_WORK       := 0D0EF00h                    ; the engine's pool
PH_NLEV       := 3                           ; wrapped copies of programs alive at once (nesting levels)
PH_MARK       := 21h                         ; a copy is the hidden, protected program PH_MARK, '1'..'3' (the level)
PH_LVN        := 0D0EEC2h                    ; copies alive: levels 1..PH_LVN are in use
PH_LVNAME     := 0D0EEC3h                    ; PH_NLEV x 9: the original's basic_prog (type, name) of each level
PH_WI         := 0D0EEDEh                    ; engine wrapinfo: len, tgt, orig (3 words)
PH_LV         := 0D0EEEFh                    ; the level of the copy an error is in
PH_OPSAVE     := 0D0EEE4h                    ; OP1 on entry to a hook routine that calls the OS (11 B, ends EEEE)

phookArm:
    xor a, a
    ld (PH_ARMED), a
    call phCleanAll
    ld hl, symce_phook
    jp ti.SetParserHook

; Clear the parser hook if it is ours, and give the slot back to AsmHook2 when it is installed.
phookClr:
    ld hl, (ti.parserHookPtr)
    ld de, symce_phook
    or a, a
    sbc hl, de
    ret nz
    call ti.ClrParserHook
    call phA2
    ret nz
    dec hl
    jp ti.SetParserHook

; AsmHook2's parser hook: Z and HL = the byte after its 83h, NZ when the app is not installed.
; FindAppStart gives its app; the hook is the 83h followed by or a,a : jr z (AsmHook2's `parser`).
phA2:
    ld hl, a2name
    push hl
    call 021100h                        ; FindAppStart: HL = the app, Z if not there
    pop de
    jq z, .no
    ld bc, 800h
.f:
    ld a, 83h
    cpir
    jq nz, .no
    ld a, (hl)
    cp a, 0B7h
    jq nz, .f
    inc hl
    ld a, (hl)
    cp a, 28h
    jq nz, .f
    dec hl
    cp a, a
    ret
.no:
    or a, 1
    ret

; B bytes at HL: DE = their sum. Clobbers A, HL, B.
phSum:
    ld de, 0
.l:
    ld a, (hl)
    inc hl
    add a, e
    ld e, a
    jq nc, .n
    inc d
.n:
    djnz .l
    ret

; A = a level: OP1 = its copy's name
phCopyName:
    ld hl, ti.OP1
    ld (hl), 6                          ; protected program
    inc hl
    ld (hl), PH_MARK
    inc hl
    add a, '0'
    ld (hl), a
    inc hl
    xor a, a
    ld b, 7
.z:
    ld (hl), a
    inc hl
    djnz .z
    ret

; A = a level: delete its copy if there is one
phDelCopy:
    call phCopyName
    call ti.ChkFindSym
    ret c
    jp ti.DelVarArc

; A = 0..: HL = that slot of PH_LVNAME
phSlot:
    ld hl, PH_LVNAME
    or a, a
    ret z
    ld de, 9
.l:
    add hl, de
    dec a
    jq nz, .l
    ret

phSaveOP1:
    ld hl, ti.OP1
    ld de, PH_OPSAVE
    ld bc, 11
    ldir
    ret

phLoadOP1:
    ld hl, PH_OPSAVE
    ld de, ti.OP1
    ld bc, 11
    ldir
    ret

; every copy goes (whatever the state RAM says: the first call after a reset has none)
phCleanAll:
    call phSaveOP1
    ld a, PH_NLEV
.l:
    push af
    call phDelCopy
    pop af
    dec a
    jq nz, .l
    ld (PH_LVN), a
    jp phLoadOP1

; the running program of basic_prog, in RAM: NC, HL = its body, BC = its length; C if it is not there or archived
phFindOrig:
    ld hl, ti.basic_prog
    ld de, ti.OP1
    ld bc, 9
    ldir
    xor a, a
    ld (de), a
    call ti.ChkFindSym
    ret c
    call ti.ChkInRam
    jq z, .ram
    scf
    ret
.ram:
    ex de, hl
    ld bc, 0
    ld c, (hl)
    inc hl
    ld b, (hl)
    inc hl
    or a, a
    ret

phWILen:                                ; HL = wrapinfo.len
    ld hl, 0
    ld a, (PH_WI)
    ld l, a
    ld a, (PH_WI + 1)
    ld h, a
    ret

; HL = tokens, BC = their length, DE = where to write the wrapped copy (0: only measure), tgt set in PH_WI:
; the engine's MODE_WRAP. A = bare commands found.
phWrap:
    push hl
    pop iy
    ld hl, 0
    push hl                             ; look
    ld l, 80h
    push hl                             ; mode
    ld hl, PH_WI
    push hl                             ; ans = the wrapinfo
    ld hl, PH_WORK
    push hl
    push de                             ; out
    push bc                             ; len
    push iy                             ; in
    call symce_engine
    ld hl, 21
    add hl, sp
    ld sp, hl
    ld iy, ti.flags
    or a, a
    ret

; a program of RAM has just started (A=0, begPC=curPC): if it has bare SymCE commands, run a copy of it with each one wrapped
; in expr(; the original is never written. The copy is a hidden protected program made by the OS, one per nesting level;
; basic_prog and the PCs are moved to it, so the OS' own save and restore resume the copy after a prgm call returns.
phStart:
    ld hl, 0
    add hl, sp
    ld bc, ti.stackBot + 3950
    or a, a
    sbc hl, bc
    ret c                               ; stack too low
    ld a, (PH_LVN)
    cp a, PH_NLEV
    ret nc                              ; all levels in use
    ld hl, (ti.endPC)
    ld de, (ti.begPC)
    or a, a
    sbc hl, de
    inc hl                              ; HL = length
    ld a, h
    cp a, 20h
    ret nc                              ; 8192 and over (the engine's WRAPMAX is the exact limit)
    or a, a
    jq nz, .scan
    ld a, l
    cp a, 5
    ret c
.scan:                                  ; cheap reject: a '(' after 3 capital letters
    push hl
    pop bc
    ld hl, (ti.begPC)
.sp:
    ld a, 10h
    cpir
    ret nz
    push hl
    push bc
    ld de, (ti.begPC)
    dec hl
    dec hl
    dec hl
    dec hl
    or a, a
    sbc hl, de
    jq c, .tn
    add hl, de
    ld a, (hl)
    cp a, 'A'
    jq c, .tn
    cp a, 'Z' + 1
    jq nc, .tn
    inc hl
    ld a, (hl)
    cp a, 'A'
    jq c, .tn
    cp a, 'Z' + 1
    jq nc, .tn
    inc hl
    ld a, (hl)
    cp a, 'A'
    jq c, .tn
    cp a, 'Z' + 1
    jq nc, .tn
    pop bc
    pop hl
    jq .found
.tn:
    pop bc
    pop hl
    ld a, b
    or a, c
    jq nz, .sp
    ret
.found:
    call phSaveOP1
    call phFindOrig
    jq c, .done                         ; archived or gone: never touched
    ex de, hl
    ld hl, (ti.begPC)
    or a, a
    sbc hl, de
    jq nz, .done                        ; not the body the VAT has at this address
    ex de, hl                           ; HL = body
    ld a, 0FFh
    ld (PH_WI + 2), a
    ld (PH_WI + 3), a                   ; tgt: none
    ld de, 0
    call phWrap
    or a, a
    jq z, .done                         ; nothing to wrap, or too long
    call phWILen
    ld bc, 400
    add hl, bc                          ; and some room left for the program's own variables
    call ti.EnoughMem
    jq c, .done
    ld a, (PH_LVN)
    inc a
    call phDelCopy                      ; this level's leftover, if any
    ld a, (PH_LVN)
    inc a
    call phCopyName
    call phWILen
    call ti.CreateProtProg              ; OP1 = the name, HL = the length
    ld a, (PH_LVN)
    inc a
    call phCopyName
    call ti.ChkFindSym                  ; where the OS put it (and the original may have moved)
    jq c, .lost
    push de                             ; the copy's data
    call phFindOrig
    pop de
    jq c, .bail
    ex de, hl                           ; HL = copy data, DE = original body
    ld a, (PH_WI)
    ld (hl), a
    inc hl
    ld a, (PH_WI + 1)
    ld (hl), a
    inc hl                              ; HL = copy body
    push hl
    ex de, hl                           ; HL = original body, DE = copy body
    call phWrap
    pop de
    jq z, .bail
    push de
    ld a, (PH_LVN)
    call phSlot
    ex de, hl
    ld hl, ti.basic_prog
    ld bc, 9
    ldir                                ; the original's name, for an error's Goto
    ld hl, PH_LVN
    inc (hl)
    ld a, (hl)
    call phCopyName
    ld hl, ti.OP1
    ld de, ti.basic_prog
    ld bc, 9
    ldir                                ; the copy runs
    call phWILen
    pop de
    ld (ti.begPC), de
    ld (ti.curPC), de
    add hl, de
    dec hl
    ld (ti.endPC), hl
    jq .done
.bail:
    ld a, (PH_LVN)
    inc a
    call phDelCopy
.lost:
    call phFindOrig
    jq c, .done
    ld (ti.begPC), hl
    ld (ti.curPC), hl
    add hl, bc
    dec hl
    ld (ti.endPC), hl
.done:
    jp phLoadOP1

; A SymCE error while a copy runs: put the original under basic_prog and begPC/curPC, at the call that made the error, so that
; 2:Goto opens the original. All copies go. Z: done (or nothing of ours); NZ: could not, Quit only.
; At A=1 the PCs are the copy's own (a statement) or a temp string's (a Disp argument, If/While condition): then the string
; is looked up in the copy (its first match).
phErrFix:
    ld a, (PH_LVN)
    or a, a
    ret z
    ld c, a
    ld a, (ti.basic_prog + 1)
    cp a, PH_MARK
    jq nz, .lv
    ld a, (ti.basic_prog + 2)
    sub a, '0'
    jq z, .bad
    ld c, a
.lv:
    ld a, c
    cp a, PH_NLEV + 1
    jq nc, .bad
    ld (PH_LV), a
    call phCopyName
    call ti.ChkFindSym
    jq c, .bad
    ex de, hl
    ld bc, 0
    ld c, (hl)
    inc hl
    ld b, (hl)
    inc hl                              ; HL = the copy's body, BC = its length
    ex de, hl                           ; DE = body
    ld hl, (ti.begPC)
    or a, a
    sbc hl, de
    jq c, .temp
    or a, a
    sbc hl, bc
    jq nc, .temp
    ld hl, (ti.curPC)                   ; the PCs are the copy's
    or a, a
    sbc hl, de
    jq .have
.temp:                                  ; DE = body, BC = length: look for [begPC..endPC] in it
    ld (PH_OPSAVE), de
    ld hl, (ti.endPC)
    ld de, (ti.begPC)
    or a, a
    sbc hl, de
    inc hl
    ld (PH_OPSAVE + 3), hl              ; TL
    or a, a
    sbc hl, bc
    jq z, .fits
    jq nc, .bad                         ; longer than the copy
.fits:
    ld hl, (PH_OPSAVE)
    add hl, bc
    ld de, (PH_OPSAVE + 3)
    or a, a
    sbc hl, de
    ld (PH_OPSAVE + 6), hl              ; the last place it can start
    ld de, (PH_OPSAVE)
.next:
    push de
    ld hl, (ti.begPC)
    ld bc, (PH_OPSAVE + 3)
.cmp:
    ld a, (de)
    cpi
    jq nz, .miss
    inc de
    jp pe, .cmp
    pop de
    ld hl, (PH_OPSAVE)
    ex de, hl
    or a, a
    sbc hl, de                          ; where it starts in the copy
    ld de, (ti.curPC)
    add hl, de
    ld de, (ti.begPC)
    or a, a
    sbc hl, de                          ; plus where the error is in it
    jq .have
.miss:
    pop de
    inc de
    ld hl, (PH_OPSAVE + 6)
    or a, a
    sbc hl, de
    jq nc, .next
    jq .bad
.have:
    ld (PH_WI + 2), hl                  ; tgt: where in the copy (the 3rd byte lands on orig, which the engine sets)
    ld a, (PH_LV)
    dec a
    call phSlot
    ld de, ti.basic_prog
    ld bc, 9
    ldir                                ; the original is the program now
    call phFindOrig
    jq c, .bad
    ld de, 0
    call phWrap                         ; orig = where in the original
    ld a, (PH_WI + 4)
    ld b, a
    ld a, (PH_WI + 5)
    and a, b
    inc a
    jq z, .bad                          ; 0FFFFh: no call ends after tgt
    call phCleanAll
    call phFindOrig
    jq c, .bad
    ld (ti.begPC), hl
    ld de, 0
    ld a, (PH_WI + 4)
    ld e, a
    ld a, (PH_WI + 5)
    ld d, a
    push hl
    add hl, de
    ld (ti.curPC), hl
    pop hl
    add hl, bc
    dec hl
    ld (ti.endPC), hl
    xor a, a
    ret
.bad:
    call phCleanAll
    or a, 1
    ret

; a return from a prgm call, or a sub-evaluation (A=2, B=10h): if the program now running is a copy, the levels above it are free
phResume:
    ld a, (PH_LVN)
    or a, a
    ret z
    ld a, (ti.basic_prog + 1)
    cp a, PH_MARK
    ret nz
    ld a, (ti.basic_prog + 2)
    sub a, '0'
    ret z
    ld c, a
    ld a, (PH_LVN)
    cp a, c
    ret c
    ld a, c
    ld (PH_LVN), a
    ret

symce_phook:
    db 83h
    push ix
    push iy
    push af
    push bc
    push de
    push hl
    ld ix, 0
    add ix, sp                          ; L +0, DE +3, C +6, B +7, F +9, A +10, IY +12, IX +15
    ld iy, ti.flags
    ld a, (ix + 10)
    or a, a
    jq z, .a0
    dec a
    jq z, .a1
    dec a
    jq nz, .out
    ld a, (ix + 7)                      ; A=2: B = 10h is ours (a copy resumed); AsmHook2's only with B = 5Dh
    cp a, 10h
    jq nz, .a2b
    call phResume
    jq .out
.a2b:
    cp a, 5Dh
    jq z, .chain
.out:
    pop hl
    pop de
    pop bc
    pop af
    pop iy
    pop ix
    cp a, a
    ret
.a0:
    ld a, (PH_ARMED)
    cp a, 0A5h
    jq nz, .a0b
    xor a, a
    ld (PH_ARMED), a                    ; once, matched or not
    ld hl, (ti.endPC)
    ld de, (ti.begPC)
    or a, a
    sbc hl, de
    inc hl                              ; the source's length
    ld a, h
    or a, a
    jq nz, .a0b
    ld a, (PH_SLEN)
    cp a, l
    jq nz, .a0b
    ld b, l
    ld hl, (ti.begPC)
    call phSum
    ld hl, (PH_SUM)
    or a, a
    sbc hl, de
    jq nz, .a0b
    ld hl, PH_OUT                       ; the answer is what the OS parses
    ld (ti.begPC), hl
    ld (ti.curPC), hl
    ld a, (PH_LEN)
    ld bc, 0
    ld c, a
    add hl, bc
    dec hl
    ld (ti.endPC), hl
    jq .out
.a0b:
    ld a, (ti.basic_prog + 1)           ; a home entry (#): leftover copies go; it is AsmHook2's
    cp a, '#'
    jq nz, .nothash
    call phCleanAll
    jq .chain
.nothash:
    ld hl, (ti.begPC)
    ld a, (hl)
    cp a, 0EFh
    jq nz, .not_asm
    inc hl
    ld a, (hl)
    cp a, 7Bh
    jq z, .chain                        ; an EF 7B program: AsmHook2's
.not_asm:
    ld a, (ti.basic_prog)               ; a RAM program (5/6) that is not one of our copies, at its first byte
    cp a, 5
    jq z, .isprg
    cp a, 6
    jq nz, .out
.isprg:
    ld a, (ti.basic_prog + 1)
    cp a, PH_MARK
    jq z, .out
    ld hl, (ti.begPC)
    ld de, (ti.curPC)
    or a, a
    sbc hl, de
    jq nz, .out
    call phStart
    jq .out
.chain:
    call phA2
    jq nz, .out
    ld (ix + 15), hl
    pop hl
    pop de
    pop bc
    pop af
    pop iy
    pop ix
    jp (ix)
.a1:
    ld a, (ix + 7)
    cp a, 8Eh                           ; expr(
    jq nz, .out
    ld hl, ti.OP1                       ; its string argument
    ld a, (hl)
    and a, 1Fh
    cp a, 4
    jq nz, .out
    ld hl, (ti.OP1 + 6)                 ; OP1 saved: ChkFindSym and the engine's own look() use it
    push hl
    ld hl, (ti.OP1 + 3)
    push hl
    ld hl, (ti.OP1)
    push hl
    call ti.ChkFindSym
    jq c, .a1x
    call ti.ChkInRam                    ; Z: in RAM
    jq nz, .a1x                         ; archived: DE is the flash copy's header, not the data (the OS parses the string as it is)
    ex de, hl
    ld de, 0
    ld e, (hl)
    inc hl
    ld d, (hl)
    inc hl                              ; DE = the string's length, HL = its tokens
    ld a, d
    or a, a
    jq nz, .a1x
    ld a, e
    or a, a
    jq z, .a1x
    cp a, 251
    jq nc, .a1x
    ld (PH_SLEN), a
    push hl
    push de
    ld b, a
    call phSum
    ld (PH_SUM), de
    pop de
    pop hl
    push hl
    ld hl, 0
    add hl, sp
    ld bc, ti.stackBot + 3950           ; 3396 B (stack.py's worst) + 512 B left + the 27 B of the call below; a deeper program says ERR:MEMORY
    or a, a
    sbc hl, bc
    pop hl
    ld a, 8Eh
    jp c, ti.JError                     ; ERR:MEMORY
    push hl
    pop iy                              ; the tokens
    ld hl, phLook
    push hl                             ; look
    ld a, (0D0009Ah)                    ; mode: ANSWERS DEC, DEGREE (as hook.c), and MODE_PRGM
    and a, 1
    ld c, a
    ld a, (0D00080h)
    and a, 4
    or a, c
    or a, 0C0h
    ld hl, 0
    ld l, a
    push hl
    ld hl, 0
    push hl                             ; ans: none
    ld hl, PH_WORK
    push hl
    ld hl, PH_OUT
    push hl
    push de                             ; len
    push iy                             ; tokens
    call symce_engine
    ld hl, 21
    add hl, sp
    ld sp, hl
    ld iy, ti.flags
    or a, a
    jq z, .a1x                          ; nothing of ours: the OS parses the string as it is
    cp a, 0FFh
    jq z, .a1err
    ld (PH_LEN), a
    ld a, 0A5h
    ld (PH_ARMED), a
.a1x:
    pop hl
    ld (ti.OP1), hl
    pop hl
    ld (ti.OP1 + 3), hl
    pop hl
    ld (ti.OP1 + 6), hl
    jq .out
.a1err:                                 ; "TITLE",0,"LINE",0,0 at PH_OUT: appErr1, appErr2
    ld hl, PH_OUT
    ld de, ti.appErr1
    ld bc, 26
    ldir
    ld a, (0D000B5h)                    ; the localize hook puts the line under the title; armed for this screen only
    bit 1, a
    jq nz, .armed
    or a, 2
    ld (0D000B5h), a
    ld hl, symce_lhook
    ld (0D02611h), hl
    ld a, 1
    ld (0D0EE06h), a                    ; tempArm
.armed:
    call phErrFix                       ; an error while a copy runs: the Goto must open the original
    jq nz, .quit
.not_wrapped_err:
    ld a, (PH_OUT + 2)                  ; third letter N (SYNTAX) or G (ARGUMENT): 2:Goto too (0xAB), else Quit only (0x2B)
    cp a, 4Eh
    jq z, .goto
    cp a, 47h
    jq z, .goto
.quit:
    ld a, 2Bh
    jp ti.JError
.goto:
    ld a, 0ABh
    jp ti.JError

; look(letter), as hook.c's .Llook: the real stored in that letter (an address, 0 if none), for SOLVE's other letters.
phLook:
    push ix
    ld hl, (ti.OP1)
    push hl
    ld hl, (ti.OP1 + 3)
    push hl
    ld hl, (ti.OP1 + 6)
    push hl
    ld hl, 0
    push hl                             ; the type to try
    ld iy, ti.flags
.try:
    ld ix, 0
    add ix, sp
    ld a, (ix + 0)
    ld hl, ti.OP1
    ld (hl), a
    inc hl
    ld a, (ix + 18)
    ld (hl), a
    inc hl
    ld (hl), 0
    call ti.ChkFindSym
    jq nc, .found
    ld ix, 0
    add ix, sp
    ld a, (ix + 0)
    or a, a
    ld de, 0
    jq nz, .found
    ld a, 0Ch
    ld (ix + 0), a
    jq .try
.found:
    pop hl
    pop hl
    ld (ti.OP1 + 6), hl
    pop hl
    ld (ti.OP1 + 3), hl
    pop hl
    ld (ti.OP1), hl
    ex de, hl
    pop ix
    ret
a2name:
    db "AsmHook2", 0

hook_rel := symce_hook - appMain
phook_rel := symce_phook - appMain
mhook_rel := symce_mhook - appMain
fhook_rel := symce_fhook - appMain
lhook_rel := symce_lhook - appMain
khook_rel := symce_khook - appMain

; src/engine.c and src/menu.c, each linked on its own; every absolute address
; in them is a `dl` that app_create relocates, generated by tools/relocs.py.
include '../obj/engine.inc'
include '../obj/menu.inc'
include '../obj/graph.inc'

; make PAD=n: bytes nothing runs, enough to make the image n bytes long. They are the image
; so far, over and over from its first byte: real code, so the padded app packs as the real one
; does (the installer's packed parts would otherwise be a test of nothing), and a misplaced
; chunk shows.
if defined PAD
    image.padStart := $%
    image.pad = PAD - image.padStart - image.tail
    if image.pad > 0
        repeat image.pad
            load image.pb: byte from : (% - 1) mod image.padStart
            db image.pb
        end repeat
    end if
    image.padEnd := $%
end if

app_data
