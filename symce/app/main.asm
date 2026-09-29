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
; appvars), 2 picks the insert or TI cursor, 3 the Evo or TI font, 4 runs the
; grapher, 5 runs prgmSYMCE, so an upgrade needs no AsmHook2.
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
    ld de, nullVal
    ld hl, graphLbl
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
    call graphMain
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
    ld hl, (ti.menuHookPtr)
    ld de, symce_mhook
    or a, a
    sbc hl, de
    call z, ti.ClrMenuHook
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

; The grapher (src/graph.c). Its work struct lives in the engine's RAM (see the
; layout at `struct gw`); this fills the inputs from the OS, then calls
; symce_graph(w, key) with key 0 and after each key until it returns 1. Keys come from
; GetCSC (GetKey would draw the battery icon over the plot); a held arrow repeats.
GW := 0D0EF00h
GWN := GW + 900h                        ; loop scratch, past the struct
GWD := GW + 901h                        ; also the last key sent, once the setup is over
GWS := GW + 904h                        ; 1 after 2nd, for 2nd MODE
GWR := GW + 905h                        ; 1 once the held arrow is repeating, 0 on a fresh press

graphMain:
    ld hl, GW
    ld (hl), 0
    ld de, GW + 1
    ld bc, 2047
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
    ld hl, ti.y1LineColor
    ld de, GW + 37
    ld bc, 10
    ldir
    xor a, a
    ld (GWN), a
    ld hl, GW + 300
    ld (GWD), hl
.eq:                                    ; Y1..Y0 are 5E 10..19
    ld a, (GWN)
    add a, 10h
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
    ld hl, GW + 47
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
    cp a, 10
    jq nz, .eq
    xor a, a
    ld (GWN), a
    ld hl, GW + 57
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
    xor a, a
    ld (GWS), a
.loop:                                  ; A = key: 0 first draw, else graph.c's K_ code
    ld (GWD), a
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
    ret nz
    ld hl, ti.vRam + 220 * 640          ; the bar below the plot: white, black text
    ld (hl), 0FFh
    push hl
    pop de
    inc de
    ld bc, 20 * 640 - 1
    ldir
    xor a, a
    ld (ti.drawFGColor), a
    ld (ti.drawFGColor + 1), a
    ld (ti.penRow + 1), a
    ld (ti.penCol + 1), a
    dec a
    ld (ti.drawBGColor), a
    ld (ti.drawBGColor + 1), a
    ld a, 224
    ld (ti.penRow), a
    ld a, 2
    ld (ti.penCol), a
    ld hl, GW + 1260
    call ti.VPutS
    call .held                          ; a held arrow (K_ 1..4) pans again
    jq z, .poll
    ld a, (GWR)                         ; a fresh press waits 300 ms first, so a tap pans once
    or a, a
    jq nz, .again
    inc a
    ld (GWR), a
    ld b, 30
.wait:
    push bc                             ; Delay10ms may clobber BC
    call ti.Delay10ms
    call .held
    pop bc
    jq z, .poll                         ; let go: no pan
    djnz .wait
.again:
    call ti.GetCSC                      ; drop the press this repeat stands for
    ld a, (GWD)
    jq .loop
.poll:                                  ; ponytail: no APD in here, like the menu's loop
    call ti.GetCSC
    or a, a
    jq z, .poll
    ld c, a
    xor a, a
    ld (GWR), a                         ; a new press: its repeat starts with the wait
    ld a, c
    cp a, ti.sk2nd
    jq nz, .not2nd
    ld a, 1
    ld (GWS), a
    jq .poll
.not2nd:
    ld b, a
    ld a, (GWS)
    ld c, a
    xor a, a
    ld (GWS), a
    ld a, b
    cp a, ti.skMode
    jq nz, .map
    ld a, c                             ; MODE alone is nothing; 2nd MODE is QUIT
    or a, a
    jq z, .poll
    ld a, 40h
    jq .loop
.map:
    ld hl, scanMap
.m:
    ld a, (hl)
    or a, a
    jq z, .poll                         ; not a viewer key
    inc hl
    cp a, b
    ld a, (hl)
    inc hl
    jq nz, .m
    jq .loop

.held:                                  ; NZ if the arrow in GWD is down
    ld a, (GWD)
    dec a
    cp a, 4
    jq c, .arrow
    xor a, a                            ; not an arrow: Z
    ret
.arrow:
    ld hl, arrowBit
    ld de, 0
    ld e, a
    add hl, de
    ld a, (0F5001Eh)                    ; keypad group 7: bit set = down
    and a, (hl)
    ret

; GetCSC scan code, graph.c K_ code
scanMap:
    db ti.skRight, 1, ti.skLeft, 2, ti.skUp, 3, ti.skDown, 4, ti.skClear, 9
    db ti.skAdd, 80h, ti.skSub, 81h, ti.sk0, 8Eh, 0
arrowBit:                               ; group 7 bits for K_RIGHT, LEFT, UP, DOWN
    db 4, 2, 8, 1

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
graphLbl:
    db "4: Graph", 0
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
; The menu hook, src/mhook.c, for the SymCE tab in 2nd MATH: the same, and its
; word is menu.c's second entry point, menu_tab.
symce_mhook:
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

hook_rel := symce_hook - appMain
mhook_rel := symce_mhook - appMain
fhook_rel := symce_fhook - appMain
lhook_rel := symce_lhook - appMain

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
