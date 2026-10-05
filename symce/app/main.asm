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
;   SYMCE1.8xv ..   the image cut into archived appvars of at most PART_SIZE
;                   bytes, each after a 4-byte build id (a hash of the image)
;
; A TI variable holds under 64 KB, so the image rides in appvars and the
; installer reads them where they lie, archive or RAM. Before it touches
; anything it checks every part is there, the right size, and carries its own
; build id; if not it names the part and writes nothing. Then it checks there
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

; The finished image is this output, byte for byte: hash it, cut it into
; parts. Declared before app.inc, as fasmg runs postponed blocks last declared
; first: this one runs after app.inc's, which end the image with its
; signature and size trailer.
postpone
    IMAGE_SIZE := $%
    if defined PAD
        image.tail := IMAGE_SIZE - image.padEnd
    end if
    PARTS := (IMAGE_SIZE + PART_SIZE - 1) / PART_SIZE
    LAST_SIZE := IMAGE_SIZE - (PARTS - 1) * PART_SIZE
    assert PARTS <= 9
    image.id = 2166136261               ; FNV-1a
    repeat IMAGE_SIZE
        load image.byte: byte from : % - 1
        image.id = ((image.id xor image.byte) * 16777619) and $FFFFFFFF
    end repeat
    BUILD_ID := image.id
    ; Written as SYMCE.v1 ..; the makefile renames them SYMCE1.8xv ..
    irp ext, 'v1','v2','v3','v4','v5','v6','v7','v8','v9'
        image.part = %
        if image.part <= PARTS
            image.name = 'SYMCE' + ('0' + image.part) shl 40
            image.from = (image.part - 1) * PART_SIZE
            image.size = PART_SIZE
            if image.part = PARTS
                image.size = LAST_SIZE
            end if
            tifile ext, ti.AppVarObj, image.name, , image.from, image.size, 1, BUILD_ID
        end if
    end irp
end postpone

include 'include/app.inc'
include 'include/ti84pceg.inc'

; Image bytes per appvar part: under the 65,512-byte ceiling of one archived
; variable with room to spare.
PART_SIZE := 65000

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
    call ti.NewLine
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
    cp a, ti.k4
    jq nz, .exit
    call gkeyIsOn
    jq nz, .gkeySet
    call gkeyClr
    call casIsOn
    call nz, mhookClr                   ; the menu hook stays while the CAS needs it
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
    jq .screen
.casOff:
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
    call gkeyIsOn
    call nz, mhookClr                   ; the menu hook stays while the GRAPH key needs it
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
    ld hl, (ti.fontHookPtr)
    ld de, symce_fhook
    or a, a
    sbc hl, de
    call z, ti.ClrFontHook
    ld hl, (ti.localizeHookPtr)
    ld de, symce_lhook
    or a, a
    sbc hl, de
    ret nz
    jp ti.ClrLocalizeHook

fontSet:
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
    db "Press 1-5, CLEAR exits.", 0
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
; The same body again for the TI cursor; the settings screen arms one or the other.
symce_hook_ti:
    file '../obj/hook/hook.bin'
    dl symce_engine
    dl symce_menu
    db 1
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

; A = 3, B = the key. CLEAR in a menu opened from the grapher goes back to it; a pick in the ZOOM
; menu (ENTER, a digit, a letter) is done on the grapher unless TI's graph has to
; draw (mode, stat plot ...), the item is ZoomStat or SetFactors, or the GRAPH key is TI's.
; Never returns NZ: the grapher ends in JForceCmd, the rest is the OS's (Z).
menuKey:
    push hl
    push de
    push bc
    push af
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

; DRAW menu item -> the grapher's pick (D_* in graph_draw.h), 0 = not on the grapher: DRAW tab, POINTS tab
dtab0:
    db 1, 2, 3, 4, 0, 0, 0, 0, 0, 0, 0
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
symce_lhook:
    file '../obj/hook/lhook.bin'
    dl font_map
    dl font_small - 25
include '../obj/font.inc'

; The raw key hook body. Like the others it lives in the app (the OS resets the
; calculator at a context switch if an armed hook points outside flash). The OS
; jumps to the byte after the 83h with A = the key and takes A back as the key
; to process. GRAPH opens SymCE's grapher in place of TI's; the rest goes on.
symce_khook:
    db 83h
    push bc
    ld b, a                             ; the key
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
    or a, a
    jq nz, .nomenu
    ld a, b
    cp a, ti.kCalc                      ; the menu is about to open: the home cursor, before it moves it (the menu hook's event 4 does this for ZOOM)
    jq nz, .nomenu
    push hl
    ld hl, (ti.curRow)
    ld (GWC), hl
    pop hl
    jq .nomenu
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
    xor a, a
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

hook_rel := symce_hook - appMain
mhook_rel := symce_mhook - appMain
fhook_rel := symce_fhook - appMain
lhook_rel := symce_lhook - appMain
khook_rel := symce_khook - appMain

; src/engine.c and src/menu.c, each linked on its own; every absolute address
; in them is a `dl` that app_create relocates, generated by tools/relocs.py.
include '../obj/engine.inc'
include '../obj/menu.inc'
include '../obj/graph.inc'

; make PAD=n: bytes nothing runs, enough to make the image n bytes long,
; patterned so a misplaced chunk shows.
if defined PAD
    image.padStart := $%
    image.pad = PAD - image.padStart - image.tail
    if image.pad > 0
        repeat image.pad
            db (% xor % shr 8) and $FF
        end repeat
    end if
    image.padEnd := $%
end if

app_data
