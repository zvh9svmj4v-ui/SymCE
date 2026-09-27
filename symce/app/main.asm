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
; After that, opening SymCE from APPS toggles it on/off, which is also how to
; turn it back on after a RAM clear (which keeps the app and the appvars).
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
    ld hl, (ti.homescreenHookPtr)
    ld de, symce_hook
    or a, a
    sbc hl, de
    jq nz, .on
    bit ti.homescreenHookActive, (iy + ti.hookflags2)
    jq z, .on
    call ti.ClrHomescreenHook
    ld hl, (ti.menuHookPtr)
    ld de, symce_mhook
    or a, a
    sbc hl, de
    call z, ti.ClrMenuHook
    ld hl, offStr
    jq .show
.on:
    call osOk
    ld hl, osStr
    jq nz, .show
    xor a, a
    ld (HOOK_OK), a
    ld (HOOK_PENDING), a
    ld (HOOK_ANS), a
    ld hl, symce_hook
    call ti.SetHomescreenHook
    ld hl, symce_mhook
    call ti.SetMenuHook
    ld hl, onStr
.show:
    push hl
    call ti.ClrScrn
    call ti.HomeUp
    pop hl
    call appLines
    call ti.GetKey
    ld a, ti.kClear
    jp ti.JForceCmd

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
onStr:
    db "SymCE is ON", 0
    db " ", 0
    db "2X+2X ENTER now shows 4X.", 0
    db " ", 0
    db "Open SymCE from APPS", 0
    db "again to turn it off.", 0
    db "Turn it off before you", 0
    db "delete the app.", 0
    db 0
offStr:
    db "SymCE is OFF", 0
    db " ", 0
    db "Calculator is stock.", 0
    db "Open SymCE to turn it on.", 0
    db 0

; Built from src/hook.c by the makefile. Position independent: it finds itself
; through homescreenHookPtr, so it needs no relocation entries.
symce_hook:
    file '../obj/hook/hook.bin'
; The hook reads these words, just past its own last byte, to find the engine
; and the ALPHA+DOWN menu. They are relative to app.base, so app_create
; relocates them.
    dl symce_engine
    dl symce_menu
; The menu hook, src/mhook.c, for the SymCE tab in 2nd MATH: the same, and its
; word is menu.c's second entry point, menu_tab.
symce_mhook:
    file '../obj/hook/mhook.bin'
    dl symce_menu + 4
hook_rel := symce_hook - appMain
mhook_rel := symce_mhook - appMain

; src/engine.c and src/menu.c, each linked on its own; every absolute address
; in them is a `dl` that app_create relocates, generated by tools/relocs.py.
include '../obj/engine.inc'
include '../obj/menu.inc'

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
