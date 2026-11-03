;----------------------------------------
;
; SymCE installer (prgmSYMCE): writes the app, carried ZX0-packed in appvars
; SYMCE1.. (tools/zpack.py), to flash, then arms the hook in it. Each part
; unpacks into free RAM (the gap between the FPS and the OPS), is checked
; there, and is written from there. Every check, the unpacking and summing of
; every part included, comes before the first write or delete, so a refusal
; leaves the calculator as it was.
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

    ; Every part there, packed, from this build, and unpacking to what it says
    ; (RAM enough, sums right); together they must be the whole image.
    ld hl, 0
    ld (total), hl
    ld a, 1
.check:
    ld (partNo), a
    cp a, MAX_PARTS + 1
    ld hl, sizeStr
    jq nc, .done                ; no part says it is the last
    call findPart
    jq c, .done
    call unpack
    jq c, .done
    ld hl, (total)
    add hl, bc
    ld (total), hl
    ld hl, (foundAt)
    ld de, H_LAST
    add hl, de
    ld a, (hl)
    or a, a
    ld a, (partNo)
    jq z, .more
    ld (nParts), a
    ld hl, (total)
    ld de, IMAGE_SIZE
    or a, a
    sbc hl, de
    ld hl, sizeStr
    jq nz, .done
    jq .checked
.more:
    inc a
    jq .check
.checked:

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
    push hl
    ld de, phook_rel
    add hl, de
    ld de, (ti.parserHookPtr)
    or a, a
    sbc hl, de
    call z, giveBack            ; the old SymCE holds the parser slot: AsmHook2 gets it back before the delete clears it
    pop hl
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
    push hl
    ld de, mhook_rel
    add hl, de
    call ti.SetMenuHook
    pop hl
    push hl
    ld de, fhook_rel
    add hl, de
    call ti.SetFontHook
    pop hl
    push hl
    ld de, lhook_rel
    add hl, de
    call ti.SetLocalizeHook
    pop hl
    push hl
    ld de, phook_rel
    add hl, de
    call ti.SetParserHook               ; ahead of AsmHook2's: expr("cmd(") in programs; asm programs are chained to it
    pop hl
    ld de, khook_rel
    add hl, de
    call ti.SetGetKeyHook               ; arms the RAW KEY hook (0xD025DB) despite its name
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

; The parser slot back to AsmHook2, as symce_phook's phookClr does for CAS off (main.asm): AsmHook2's hook is the
; `83 B7 28` (db 83h, or a, a, jr z) in its app. Without AsmHook2 the slot is just cleared.
giveBack:
    call ti.ClrParserHook
    ld hl, a2Name
    push hl
    call $21100                 ; FindAppStart
    pop de
    ret z
    ld bc, 800h
.f:
    ld a, 83h
    cpir
    ret nz
    ld a, (hl)
    cp a, 0B7h
    jq nz, .f
    inc hl
    ld a, (hl)
    cp a, 28h
    jq nz, .f
    dec hl
    dec hl
    jp ti.SetParserHook
a2Name:
    db "AsmHook2", 0

; NZ and HL = the installed SymCE's start, or Z.
findApp:
    ld hl, appName
    push hl
    call $21100                 ; FindAppStart
    pop bc
    ret

; In: A = part number. Out: NC, HL = the part's header (header.txt in
; docs/TI84CE-KNOWLEDGE.md section 8: build id, "ZX", unpacked length, last
; flag, sums), read where it lies, archived or in RAM; or C and HL = what to
; tell the user.
findPart:
    add a, '0'
    cp a, '9' + 1
    jr c, .name
    add a, 'A' - '9' - 1        ; part 10 is SYMCEA ..
.name:
    ld (partVar + 6), a
    ld (missingStr.digit), a
    ld (otherStr.digit), a
    ld (emptyStr.digit), a
    ld (oldStr.digit), a
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
    ld de, 0
    ld e, (hl)
    inc hl
    ld d, (hl)
    inc hl                      ; HL = the header, DE = the appvar's length
    ld (foundAt), hl
    ld a, d                     ; empty: what a transfer leaves, in RAM, when
    or a, e                     ; the archive has no room for the part
    jr nz, .sized
    ld hl, emptyStr
    scf
    ret
.sized:
    ld hl, HEADER_SIZE
    or a, a
    sbc hl, de
    jr nc, .old                 ; too short for a header
    ld hl, (foundAt)
    ld de, H_MAGIC
    add hl, de
    ld a, (hl)
    cp a, 'Z'
    jr nz, .old
    inc hl
    ld a, (hl)
    cp a, 'X'
    jr nz, .old
    ld hl, (foundAt)
    ld de, buildId
    ld b, 4
.id:
    ld a, (de)
    cp a, (hl)
    jr nz, .other
    inc de
    inc hl
    djnz .id
    ld hl, (foundAt)
    ld de, H_ULEN
    add hl, de
    ld bc, 0
    ld c, (hl)
    inc hl
    ld b, (hl)
    ld a, b
    or a, c
    jr z, .other
    ld hl, MAX_SLICE
    or a, a
    sbc hl, bc
    jr c, .other
    ld hl, (foundAt)
    or a, a
    ret
.old:
    ld hl, oldStr               ; an old uncompressed part, or not a SymCE part
    scf
    ret
.other:
    ; Show what it found and what it wants: the build id, byte by byte.
    ld de, otherStr.has
    ld hl, (foundAt)
    ld b, 4
    call hexBytes
    ld hl, buildId
    ld b, 4
    ld de, otherStr.want
    call hexBytes
    ld hl, otherStr
    scf
    ret

; Unpack the part whose header is at (foundAt) into free RAM at the FPS and
; check it. Out: NC, BC = its length, HL = where; or C and HL = a message.
; The unpacker is a C routine to the OS (it leaves IY wrong): IY is kept.
unpack:
    ld hl, (foundAt)
    ld de, H_ULEN
    add hl, de
    ld bc, 0
    ld c, (hl)
    inc hl
    ld b, (hl)
    ld (ulen), bc
    ld hl, (ti.OPS)
    ld de, (ti.FPS)
    or a, a
    sbc hl, de
    ld (freeRam), hl            ; the gap nothing owns
    ld hl, RAM_MARGIN
    add hl, bc
    ld (needRam), hl
    ld de, (freeRam)
    ex de, hl
    or a, a
    sbc hl, de                  ; free - need
    jr nc, .ram
    ld de, noRamStr.has
    ld hl, (freeRam)
    call hexHL
    ld de, noRamStr.want
    ld hl, (needRam)
    call hexHL
    ld hl, noRamStr
    scf
    ret
.ram:
    ld hl, (foundAt)
    ld de, HEADER_SIZE
    add hl, de
    ld de, (ti.FPS)
    push iy
    call zx0
    pop iy
    ld hl, (ti.FPS)
    ex de, hl                   ; DE = FPS, HL = the end
    or a, a
    sbc hl, de
    ld bc, (ulen)
    or a, a
    sbc hl, bc
    jr nz, .bad                 ; the stream was not the slice
    ld hl, (ti.FPS)
    push bc
    call checksum
    ld (sums), de
    ld (sums + 3), ix
    ld hl, (foundAt)
    ld de, H_SUMS
    add hl, de
    ld de, sums
    ld b, 6
.cmp:
    ld a, (de)
    cp a, (hl)
    jr nz, .badPop
    inc de
    inc hl
    djnz .cmp
    pop bc
    ld hl, (ti.FPS)
    or a, a
    ret
.badPop:
    pop bc
.bad:
    ld hl, badStr
    scf
    ret

; Fletcher-style sums of BC bytes at HL: DE = sum of the bytes (16 bits),
; IX = sum of DE after each byte (24 bits). tools/zpack.py sums().
checksum:
    ld de, 0
    ld ix, 0
.sum:
    ld a, (hl)
    inc hl
    add a, e
    ld e, a
    jr nc, .carried
    inc d
.carried:
    add ix, de
    dec bc
    ld a, b
    or a, c
    jr nz, .sum
    ret

; The toolchain's zx0_Decompress ("turbo" ZX0 v2 decoder by Einar Saukas and
; introspec, in CEdev's libce zx0.o): HL = packed, DE = where to; returns
; DE = the end. Clobbers A, BC, HL, IY; the caller keeps IY.
zx0:
    ld iy, -1                   ; last offset 1
    lea bc, iy + 2
    scf
    jr .start
.newOffset:
    dec bc
    dec bc
    add a, a
    jr nz, .newOffsetSkip
    ld a, (hl)
    inc hl
    rla
.newOffsetSkip:
    call nc, .elias
    inc c
    ret z                       ; the end marker
    ld b, (hl)
    inc hl
    rr c
    rr b
    ld iyl, b
    ld iyh, c
    ld bc, 1
    call nc, .elias
    inc bc
.copy:
    push hl
    lea hl, iy + 0
    add hl, de
    ldir
    pop hl
    add a, a
    jr c, .newOffset
.literals:
    inc c
    add a, a
    jr nz, .literalsSkip
.start:
    ld a, (hl)
    inc hl
    rla
.literalsSkip:
    call nc, .elias
    ldir
    add a, a
    jr c, .newOffset
    inc c
    add a, a
    jr nz, .lastOffsetSkip
    ld a, (hl)
    inc hl
    rla
.lastOffsetSkip:
    call nc, .elias
    jr .copy
.elias:
    add a, a
    rl c
    add a, a
    jr nc, .elias
    ret nz
    ld a, (hl)
    inc hl
    rla
    ret c
    add a, a
    rl c
    add a, a
    ret c
    add a, a
    rl c
    add a, a
    ret c
    add a, a
    rl c
    add a, a
    ret c
.eliasLoop:
    add a, a
    rl c
    rl b
    add a, a
    jr nc, .eliasLoop
    ret nz
    ld a, (hl)
    inc hl
    rla
    jr nc, .eliasLoop
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

; HL as six hex digits at DE.
hexHL:
    ld (hexTmp), hl
    ld hl, hexTmp + 2
    ld b, 1
    call hexBytes
    ld hl, hexTmp + 1
    ld b, 1
    call hexBytes
    ld hl, hexTmp
    ld b, 1
    jr hexBytes

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

; Unpack the parts one at a time and write them to flash at HL, then
; relocate the app there. Out: NC and DE = its code; or C and HL = a message.
app_create:
    ld (dest), hl
    ld (writeAt), hl
    ld a, 1
.part:
    ld (partNo), a
    call findPart
    ret c
    call unpack                 ; again: the GC moves the appvars, and RAM was reused
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
    ld de, (writeAt)
    call installer.portUnlock
    call $0002e0                ; WriteFlash HL -> DE, BC bytes
    call installer.portLock
    pop bc
    ld hl, (readAt)
    add hl, bc
    ld (readAt), hl
    ld hl, (writeAt)
    add hl, bc
    ld (writeAt), hl
    ld hl, (left)
    ld de, 0
    or a, a
    sbc hl, de
    jr nz, .chunk
    ld a, (partNo)
    ld hl, nParts
    cp a, (hl)
    jr z, .written
    inc a
    jr .part
.written:

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

CHUNK := 8192                   ; flash is written this much at a time

; A part (tools/zpack.py): build id (4), "ZX", unpacked length (2), last flag (1),
; sums (3 + 3), then the ZX0 stream.
HEADER_SIZE := 15
H_MAGIC := 4
H_ULEN := 6
H_LAST := 8
H_SUMS := 9
MAX_PARTS := 35                 ; SYMCE1..9, SYMCEA..Z
RAM_MARGIN := 1024              ; left alone above the unpacked slice

appName:
    dq 'SymCE'
buildId:
    dd BUILD_ID
partVar:
    db ti.AppVarObj, "SYMCE?", 0, 0
partNo:
    db 0
nParts:
    db 0
total:
    dl 0
ulen:
    dl 0
freeRam:
    dl 0
needRam:
    dl 0
hexTmp:
    dl 0
sums:
    dl 0, 0
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
    db "SymCE from APPS, press 1.", 0
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
    db "????????", 0
    db "Want "
.want:
    db "????????", 0
    db 0

oldStr:
    db "Appvar SYMCE"
.digit:
    db "? is not a", 0
    db "packed part (an old", 0
    db "SymCE file?). Delete all", 0
    db "SYMCE# appvars, send the", 0
    db "new SYMCE files again,", 0
    db "run prgmSYMCE. Nothing", 0
    db "was written.", 0
    db 0

badStr:
    db "A SYMCE appvar is damaged:", 0
    db "it does not unpack to", 0
    db "what it should. Delete", 0
    db "the SYMCE# appvars, send", 0
    db "them again, run prgmSYMCE.", 0
    db "Nothing was written.", 0
    db 0

sizeStr:
    db "The SYMCE appvars do not", 0
    db "add up to one whole app:", 0
    db "a part is missing or is", 0
    db "from another build. Send", 0
    db "all the SYMCE files and", 0
    db "run prgmSYMCE. Nothing", 0
    db "was written.", 0
    db 0

noRamStr:
    db "Not enough RAM: SymCE", 0
    db "unpacks its app in free", 0
    db "RAM. Delete or archive", 0
    db "variables (2nd MEM >", 0
    db "Mem Mgmt), then run", 0
    db "prgmSYMCE again. Nothing", 0
    db "was written.", 0
    db "Free "
.has:
    db "??????", 0
    db "Need "
.want:
    db "?????? (hex bytes)", 0
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
    db "archived files (the SYMCE", 0
    db "files count too), then", 0
    db "run prgmSYMCE again.", 0
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
