;----------------------------------------
;
; SymCE installer (prgmSYMCE): writes the app, carried in appvars SYMCE1..,
; to flash, then arms the hook in it. Every check comes before the first
; write or delete, so a refusal leaves the calculator as it was.
; Adapted from AsmHook2's installer.asm by RoccoLox Programs and jacobly.
;
;----------------------------------------

installApp:
    call ti.RunIndicOff
    call clearScreen
    ld hl, installingStr
    call ti.PutS

    call installerOsOk
    ld hl, osUnknownStr
    jq nz, .done

    ; Every part there, the right size, from this build.
    ld a, 1
.check:
    push af
    call findPart
    pop bc
    jq c, .done
    ld a, b
    inc a
    cp a, PARTS + 1
    jq nz, .check

    call ports
.badPorts:
    ld hl, osInvalidStr
    jq nz, .done

    ; Room for the new app once any old one is gone? If not, garbage collect
    ; the archive as the OS does before it receives an app (no prompt), which
    ; can hand its emptied sectors to apps, and ask again.
    call room
    jq nc, .fits
    ld a, 2
    call $035bb9
    di
    call room
    ld hl, noRoomStr
    jq c, .done
.fits:
    ; Replace, never keep: a RAM clear leaves an installed SymCE with its hook
    ; off, so refusing would strand users on an old build. DeleteApp on
    ; FindAppStart's HL (ROM 0x5CE6A), then Mem Mgmt's pack (ROM 0x5CDF7
    ; calls 0x035E5D, OS 5.8.4 only, as is SymCE), which moves the apps below
    ; up into the hole and ends with ei.
    call findApp
    jq z, .fresh
    call $2126c                 ; DeleteApp
    call $035e5d
    di
.fresh:
    call room                   ; HL = where it goes now, the old app gone
    jq nc, .write
    ld hl, lostStr              ; not after the check above said it fits
    jq .done
.write:
    push hl
    call ports                  ; again: the GC or pack may reuse saveSScreen
    pop hl
    jq nz, .badPorts
    call app_create
    jq c, .done                 ; a part vanished mid-write: HL says which
    res 7, (iy + 36)            ; ArcChk's cached free space: stale now

.installed:
    push de
    ex de, hl
    ld de, hook_rel
    add hl, de
    xor a, a
    ld (HOOK_OK), a
    ld (HOOK_PENDING), a
    ld (HOOK_ANS), a
    call ti.SetHomescreenHook
    pop hl
    ld de, mhook_rel
    add hl, de
    call ti.SetMenuHook
    ; v15 and earlier kept the body in these appvars; nothing reads them now.
    ld hl, oldHookVar
    call delVar
    ld hl, oldPrevVar
    call delVar
    ld hl, installedStr

.done:
    push hl
    call clearScreen
    pop hl
    call putLines
    call ti.GetKey

clearScreen:
    call ti.ClrScrn
    jp ti.HomeUp

; Print zero-terminated lines until an empty one.
putLines:
    ld a, (hl)
    or a, a
    ret z
    call ti.PutS
    call ti.NewLine
    jq putLines

delVar:
    call ti.Mov9ToOP1
    call ti.ChkFindSym
    ret c
    jp ti.DelVarArc

; Copy the port unlock to saveSScreen and set it up. NZ: unusable boot code.
ports:
    ld hl, portsCode
    ld de, ti.saveSScreen
    ld bc, portsLength
    ldir
    jp installer.portSetup

; NZ and HL = the installed SymCE's start, or Z.
findApp:
    ld hl, appName
    push hl
    call $21100                 ; FindAppStart
    pop bc
    ret

; In: A = part number. Out: NC, HL = the part's slice of the image, BC = its
; length; or C and HL = what to tell the user. Read where it lies, archived
; or in RAM.
findPart:
    ld bc, PART_SIZE + 4
    cp a, PARTS
    jr nz, .notLast
    ld bc, LAST_SIZE + 4
.notLast:
    ld (partSize), bc
    add a, '0'
    ld (partVar + 6), a
    ld (missingStr.digit), a
    ld (otherStr.digit), a
    ld (emptyStr.digit), a
    ld hl, partVar
    call ti.Mov9ToOP1
    call ti.ChkFindSym
    ld hl, missingStr
    ret c
    call ti.ChkInRam
    ex de, hl
    jr z, .inRam
    ld de, 9                    ; skip the archive entry's header
    add hl, de
    ld e, (hl)
    add hl, de
    inc hl
.inRam:
    ld (foundAt), hl
    ld de, 0
    ld e, (hl)
    inc hl
    ld d, (hl)
    inc hl                      ; HL = the build id, then the slice
    push hl
    ld hl, (partSize)
    or a, a
    sbc hl, de
    pop hl
    jr z, .sized
    ld a, d                     ; empty: what a transfer leaves, in RAM, when
    or a, e                     ; the archive has no room for the part
    jr nz, .other
    ld hl, emptyStr
    scf
    ret
.sized:
    ld de, buildId
    ld b, 4
.id:
    ld a, (de)
    cp a, (hl)
    jr nz, .other
    inc de
    inc hl
    djnz .id
    push hl
    ld hl, (partSize)
    ld bc, -4
    add hl, bc
    push hl
    pop bc
    pop hl
    or a, a
    ret
.other:
    ; Show what it found and what it wants, byte by byte: size word, then id.
    ld de, otherStr.has
    ld hl, (foundAt)
    ld b, 6
    call hexBytes
    ld de, otherStr.want
    ld hl, partSize
    ld b, 2
    call hexBytes
    ld hl, buildId
    ld b, 4
    call hexBytes
    ld hl, otherStr
    scf
    ret

; B bytes at HL as hex digits at DE.
hexBytes:
    ld a, (hl)
    rrca
    rrca
    rrca
    rrca
    call .nibble
    ld a, (hl)
    call .nibble
    inc hl
    djnz hexBytes
    ret
.nibble:
    and a, $0f
    add a, '0'
    cp a, '9' + 1
    jr c, .digit
    add a, 'A' - '9' - 1
.digit:
    ld (de), a
    inc de
    ret

; Where the new app goes, and whether it may: NC and HL = its start, or C.
;
; Measured from OS 5.8.4 (docs/TI84CE-KNOWLEDGE.md section 8): apps sit end to
; end below $3B0000, each followed by a 3-byte trailer holding its size, and
; the walk down them stops at a trailer slot reading $FFFFFF. So the app goes
; right below the lowest one -- lower by the span of the installed SymCE,
; which the pack will close -- with the blank slot under it kept blank. And
; like the OS's own allocator (0x23EC5), that slot must lie above the
; archive's last sector in use (0x27EB8), which the archive never gives up.
room:
    call findApp
    jr nz, .old
    ld hl, 0                    ; no SymCE: nothing starts at 0
.old:
    ld (oldApp), hl
    or a, a
    sbc hl, hl
    ld (oldSpan), hl
    ld hl, $3b0000
.walk:
    push hl
    call $22044                 ; HL = the next app down; Z past the last
    pop de                      ; DE = the one above it
    jr z, .walked
    push hl
    ld bc, (oldApp)
    or a, a
    sbc hl, bc
    jr nz, .notOld
    ex de, hl
    sbc hl, bc                  ; (no carry) the old SymCE's span, trailer too
    ld (oldSpan), hl
.notOld:
    pop hl
    jr .walk
.walked:
    ld (lowest), de
    ld hl, (oldSpan)
    add hl, de
    ld de, IMAGE_SIZE + 3
    or a, a
    sbc hl, de                  ; HL = the blank slot under the new app
    push hl
    call $027eb8                ; HL = the archive's last sector in use
    ld de, $010000
    add hl, de
    ex de, hl
    pop hl
    or a, a
    sbc hl, de
    ret c                       ; inside the archive's sectors
    add hl, de
    push hl
    ex de, hl
    ld hl, (lowest)
    or a, a
    sbc hl, de                  ; blank from the slot up to the lowest app
    jr c, .blank                ; (none of it below: the pack checks the rest)
    jr z, .blank
    push hl
    pop bc
    ex de, hl
    ld a, $ff
.cpi:
    cpi                         ; flash writes only clear bits, so anything
    jr nz, .full                ; but erased flash there would corrupt both
    jp pe, .cpi
.blank:
    pop hl
    inc hl
    inc hl
    inc hl
    or a, a
    ret
.full:
    pop hl
    scf
    ret

; Copy the parts to flash at HL through a RAM buffer, then relocate the app
; there. Out: NC and DE = its code; or C and HL = a message.
app_create:
    ld (dest), hl
    ld (writeAt), hl
    ld a, 1
.part:
    ld (partNo), a
    call findPart
    ret c
    ld (readAt), hl
    ld (left), bc
.chunk:
    ld bc, (left)
    ld hl, CHUNK
    or a, a
    sbc hl, bc
    jr nc, .last                ; BC = what is left, at most a chunk
    ld bc, CHUNK
.last:
    ld hl, (left)
    or a, a
    sbc hl, bc
    ld (left), hl
    push bc
    ld hl, (readAt)
    ld de, ti.pixelShadow
    ldir                        ; the archive is flash too: go through RAM
    ld (readAt), hl
    pop bc
    push bc
    ld hl, ti.pixelShadow
    ld de, (writeAt)
    call installer.portUnlock
    call $0002e0                ; WriteFlash HL -> DE, BC bytes
    call installer.portLock
    pop bc
    ld hl, (writeAt)
    add hl, bc
    ld (writeAt), hl
    ld hl, (left)
    ld de, 0
    or a, a
    sbc hl, de
    jr nz, .chunk
    ld a, (partNo)
    inc a
    cp a, PARTS + 1
    jr nz, .part

    ; Each table entry is two words relative to the code: where, and what.
    ld hl, (dest)
    ld de, APP_CODE
    add hl, de
    ex de, hl                   ; DE = the code
    ld hl, (dest)
    ld bc, APP_TABLE
    add hl, bc
.relocate:
    or a, a
    sbc hl, de
    add hl, de
    jr z, .relocated
    ld ix, (hl)
    add ix, de                  ; location to overwrite
    inc hl
    inc hl
    inc hl
    push hl
    push de
    ld hl, (hl)
    add hl, de
    lea de, ix
    ld (ti.OP6), hl
    ld hl, ti.OP6
    ld bc, 3
    call installer.portUnlock
    call $0002e0
    call installer.portLock
    pop de
    pop hl
    inc hl
    inc hl
    inc hl
    jr .relocate
.relocated:
    or a, a
    ret

os_check installerOsOk

CHUNK := 8192                   ; pixelShadow holds 8400

appName:
    dq 'SymCE'
buildId:
    dd BUILD_ID
partVar:
    db ti.AppVarObj, "SYMCE?", 0, 0
partNo:
    db 0
partSize:
    dl 0
foundAt:
    dl 0
oldApp:
    dl 0
oldSpan:
    dl 0
lowest:
    dl 0
dest:
    dl 0
writeAt:
    dl 0
readAt:
    dl 0
left:
    dl 0

osUnknownStr:
    db "SymCE was built for OS", 0
    db "5.8.4 and does not know", 0
    db "this one. Nothing was", 0
    db "written.", 0
    db 0

oldHookVar:
    db ti.AppVarObj, "SYMCEHK", 0
oldPrevVar:
    db ti.AppVarObj, "SYMCEPV", 0

installingStr:
    db "Installing SymCE...", 0

osInvalidStr:
    db "Cannot use this boot", 0
    db "code. Nothing written.", 0
    db 0

installedStr:
    db "SymCE app installed.", 0
    db " ", 0
    db "SymCE is ON:", 0
    db "2X+2X ENTER shows 4X.", 0
    db " ", 0
    db "After a RAM clear, open", 0
    db "SymCE from APPS.", 0
    db 0

missingStr:
    db "Appvar SYMCE"
.digit:
    db "? is missing.", 0
    db "Send SYMCE.8xp and every", 0
    db "SYMCE#.8xv file, then run", 0
    db "prgmSYMCE again. Nothing", 0
    db "was written.", 0
    db 0

otherStr:
    db "Appvar SYMCE"
.digit:
    db "? is from", 0
    db "another SymCE build. Send", 0
    db "all the SYMCE files again", 0
    db "and run prgmSYMCE. Nothing", 0
    db "was written.", 0
    db "Has  "
.has:
    db "????????????", 0
    db "Want "
.want:
    db "????????????", 0
    db 0

emptyStr:
    db "Appvar SYMCE"
.digit:
    db "? is empty:", 0
    db "the archive had no room", 0
    db "for it. Delete it, free", 0
    db "archive space (Mem Mgmt),", 0
    db "send it again and run", 0
    db "prgmSYMCE. Nothing was", 0
    db "written.", 0
    db 0

noRoomStr:
    db "No blank flash below the", 0
    db "last app. Nothing was", 0
    db "changed. Delete an app or", 0
    db "archived files, then run", 0
    db "prgmSYMCE again.", 0
    db 0

lostStr:
    db "No blank flash where the", 0
    db "old SymCE was. Run", 0
    db "prgmSYMCE again.", 0
    db 0

; The port unlock runs from saveSScreen, as AsmHook2's does: these are its
; bytes, assembled there.
virtual at ti.saveSScreen
    portsArea::
    define installer
    namespace installer
        include 'ports.asm'
    end namespace
    portsLength := $ - $$
end virtual
portsCode:
    load portsBytes: portsLength from portsArea: ti.saveSScreen
    emit portsLength: portsBytes
