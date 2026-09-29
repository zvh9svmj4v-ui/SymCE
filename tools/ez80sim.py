#!/usr/bin/env python3
"""Tiny eZ80 (ADL mode) interpreter, just big enough to run the SymCE hook body.

Why this exists: the hook does not need an OS to be tested -- it reads a few
flags and pointers, calls the engine, and hands the answer to one OS routine.
That is emulatable in a few hundred lines, needs no ROM, and tests the ACTUAL
ASSEMBLED BYTES rather than a C model of them. tools/emu covers the real OS.

There is no ROM here, so `call nn` into the OS does not run anything: the call
is recorded (address and registers) and returns at once. The tests assert on
the record.

The engine is compiled C this interpreter cannot run, so its entry point is a
stub: it reads the C arguments off the stack, asks the host build of the same
engine.c (symce/bin/host/engine_cli) for the answer, writes it where the hook
asked, and returns with the registers compiled C would leave -- A holding the
length, ix kept, everything else trashed, iy included.

Only the opcodes the hook actually uses are implemented. Anything else raises,
so a hook change that emits a new instruction fails loudly instead of being
silently mis-executed.

Not a general eZ80 emulator and not trying to be. Once the ROM exists,
cemu-autotester takes over for end-to-end behaviour; this keeps testing the
hook's logic in isolation, which is the part that is hard to see on hardware.
"""
import os, subprocess

# CE addresses the hook depends on.
EDIT_TOP   = 0xD02437      # only for the insert-mode baseline
CX_CUR_APP = 0xD007E0
EDIT_FLAGS = 0xD00081
HOOK_PTR = 0xD025E1        # holds the hook's own base address
# OS flag groups, from CEdev ti/flags.h. The block starts at iy = 0xD00080 and
# each group is one byte on from there.
TEXT_FLAGS = 0xD00085      # bit 4 insMode     -- insert, not overwrite
MP_FLAGS   = 0xD000C4      # bit 5             -- MathPrint on
ANS_FLAGS  = 0xD0009A      # bit 0             -- MODE ANSWERS: DEC (measured)
APD_FLAGS  = 0xD00088      # bit 4 warmStart   -- waking from APD or power loss
ON_FLAGS   = 0xD00089      # bit 1 parseInp    -- BASIC Input/Prompt is open
CMD_FLAGS  = 0xD0008C      # bit 5 virgin      -- nothing typed on this line
API_FLG1   = 0xD000A8      # bit 4 appRunning  -- an app owns the screen
BASE_TOP   = 0xD0EE00      # editTop as it was when this entry line was empty
BASE_OK    = 0xD0EE03      # 0xA5 once BASE_TOP belongs to the current entry
PENDING    = 0xD0EE04      # 0xFF: ENTER, for A=2 to try; 0xFE: answered, A=0 shows it
ANS_BUF    = 0xD0EE80      # the answer, as tokens; then the text the OS draws
LAST_ANS   = 0xD0EE20      # what Ans is: length and tokens, or 0xFF and the OS's real
ENGINE_WORK = 0xD0EF00     # the engine's scratch
OP1        = 0xD005F8      # a variable's name for ChkFindSym; the OS's own result at A=0
DISP_RESULT = 0x091FC2     # OS: draw the text at HL as a result of type A, file it in history
CHK_FIND_SYM = 0x02050C    # OS: find the variable OP1 names; DE = its data, carry if none
JERROR     = 0x020790      # OS: throw error A; never returns
CX_MAIN_PTR = 0xD007CA     # (cxMain): the home screen's key handler, A = key
CX_MAIN    = 0x058680      # where it points on the home screen (OS 5.8.4)
IN_KEY     = 0xD0EE05      # nonzero while the hook runs a key through cxMain
APP_ERR1   = 0xD025A9      # error 0x2B's message, then its description line
IY_OS      = 0xD00080      # what the OS keeps in iy: its flags block
ENTRY_AT   = 0xD30000      # the tokens of prog #, which holds the entry by A=2

ENGINE_CLI = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                          'symce/bin/host/engine_cli')
_cli = None


class Trap(Exception):
    pass


class Err(bytes):
    """What engine() gives for an error screen: "MESSAGE",0,"LINE",0,0."""


class Answer(bytes):
    """What engine() gives for an answer: its tokens, and in .cols the Classic
    columns the engine counted, which it leaves at out[64] for the hook."""


def engine(entry, last=b'', mode=0):
    """The host build of symce/src/engine.c: the answer tokens, or b'' when it
    refuses. `last` is what Ans stands for, as the hook keeps it at LAST_ANS:
    a length and tokens, or 0xFF and a TI real. `mode`: 0x20 answers as for
    MathPrint, 0x01 as for MODE ANSWERS: DEC, 0x04 in degrees, as the hook
    passes them. One process for the whole run."""
    global _cli
    if _cli is None:
        _cli = subprocess.Popen([ENGINE_CLI], stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, text=True, bufsize=1)
    _cli.stdin.write('C' + ('M' if mode & 0x20 else '') + ('D' if mode & 1 else '') +
                     ('G' if mode & 4 else '') + entry.hex() + (',' + last.hex() if last else '') + '\n')
    _cli.stdin.flush()
    r = _cli.stdout.readline().strip()
    if r == 'PASS':
        return b''
    if r[0] == 'E':
        return Err(bytes.fromhex(r[1:]))
    h, c = r.split(':')
    a = Answer(bytes.fromhex(h))
    a.cols = int(c, 16)
    return a


class Sim:
    def __init__(self, code, org=0x100000, tail=0):
        self.mem = {}
        self.org = org
        for i, b in enumerate(code):
            self.mem[org + i] = b
        self.a = self.b = self.c = 0
        self.hl = self.de = self.ix = 0
        self.iy = IY_OS
        self.bc_hi = 0            # bc's third byte has no 8-bit register name
        self.sp = 0xD1A000
        self.fz = self.fc = False
        self.pc = org
        self.end = org + len(code)
        self.jp_in = False        # jp nn to its own labels: only the relocated menu blob may
        self.steps = 0
        self.writes = []          # (addr, value) for every store, in order
        self.calls = []           # (target, a, hl, de, bc) for every call nn
        # The words just past the body point at the engine and the menu, as in
        # the app; the stubs sit right after them, where main.asm puts the
        # real ones. Then a byte: 0 is the insert-cursor body, 1 is TI's own.
        self.engine_at = org + len(code) + 7
        self.menu_at = org + len(code) + 8
        self.w24(org + len(code), self.engine_at)
        self.w24(org + len(code) + 3, self.menu_at)
        self.w8(org + len(code) + 6, tail)
        self.engine_calls = []    # (in, len, out, work, ans, mode) for every engine call
        self.menu_calls = 0       # symce_menu() calls
        self.menu_ret = 0xBADBAD  # what it returns in HL
        self.prog = None          # where prog #'s data is, if it exists
        self.thrown = None        # the error A the hook threw through _JError
        self.cx_keys = []         # the key of every (cxMain) call
        self.cx_inner = []        # what the hook said (fz) when cxMain called it back
        self.nest = 0             # hook calls cxMain made, still to return
        self.w24(CX_MAIN_PTR, CX_MAIN)

    def _engine(self):
        """symce_engine(in, len, out, work, ans, mode, look), with the C calling convention:
        the return address on top, then the arguments first to last."""
        ret, src, n, out, work, ansp, mode, look = (self.r24(self.sp + 3 * i) for i in range(8))
        mode &= 0xFF              # a uint8_t: C reads the slot's low byte
        self.engine_calls.append((src, n, out, work, ansp, mode))
        last = bytes(self.r8(ansp + i) for i in range(65)) if ansp else b''
        # engine.c refuses anything past 64 tokens; 129 keeps that true here
        # without reading megabytes when a broken range makes n huge.
        ans = engine(bytes(self.r8(src + i) for i in range(min(n, 129))), last, mode)
        for i, t in enumerate(ans):
            self.w8(out + i, t)
        if isinstance(ans, Answer):
            self.w8(out + 64, ans.cols)
        self.a = 0xFF if isinstance(ans, Err) else len(ans)
        self.hl = self.de = self.iy = 0xBADBAD
        self.bcset(0xBADBAD)
        self.fz = self.fc = True
        self.pc = self.pop()

    def _cxmain(self):
        """(cxMain)(A = key), which the hook calls to run a key itself. The OS
        calls the hook at A=1 first, the key in B, and that call must pass it
        on (Z); then it handles the key. Every key the hook runs this way ends
        insert mode, as the OS does. Registers come back trashed."""
        if self.pc == CX_MAIN:
            self.cx_keys.append(self.a)
            self.push(CX_MAIN + 1)
            self.nest += 1
            self.b, self.a = self.a, 1
            self.pc = self.org + 1
            return
        self.cx_inner.append(self.fz)
        self.w8(TEXT_FLAGS, self.r8(TEXT_FLAGS) & ~0x10)
        self.hl = self.de = self.ix = self.iy = 0xBADBAD
        self.bcset(0xBADBAD)
        self.a = 0x5A
        self.pc = self.pop()

    def _menu(self):
        """symce_menu(bc): compiled C too, so it returns with ix kept and every
        other register trashed, iy included. What it draws and types is the
        real OS's business (tools/emu/e2e.py); here only the call and the key,
        B of its argument, count. Called through mhook.bin it is menu_tab(event,
        index) instead: menu_args keeps both, and it returns menu_ret."""
        self.menu_calls += 1
        self.menu_key = self.r8(self.sp + 4)
        self.menu_args = (self.r8(self.sp + 3), self.r8(self.sp + 6))
        self.a = 0x5A
        self.hl = self.de = self.iy = 0xBADBAD
        self.hl = self.menu_ret
        self.bcset(0xBADBAD)
        self.fz = self.fc = True
        self.pc = self.pop()

    # ---- memory ----
    def r8(self, a):
        return self.mem.get(a & 0xFFFFFF, 0)

    def w8(self, a, v):
        a &= 0xFFFFFF
        self.mem[a] = v & 0xFF
        self.writes.append((a, v & 0xFF))

    def r24(self, a):
        return self.r8(a) | (self.r8(a + 1) << 8) | (self.r8(a + 2) << 16)

    def w24(self, a, v):
        self.w8(a, v & 0xFF)
        self.w8(a + 1, (v >> 8) & 0xFF)
        self.w8(a + 2, (v >> 16) & 0xFF)

    # ---- helpers ----
    def imm8(self):
        v = self.r8(self.pc); self.pc += 1; return v

    def imm24(self):
        v = self.r24(self.pc); self.pc += 3; return v

    def bc(self):
        return (self.bc_hi << 16) | (self.b << 8) | self.c

    def bcset(self, v):
        self.c = v & 0xFF
        self.b = (v >> 8) & 0xFF
        self.bc_hi = (v >> 16) & 0xFF

    def disp(self):
        """The signed 8-bit displacement of an (ix+d) operand."""
        d = self.imm8()
        return d - 256 if d >= 128 else d

    def rel(self):
        d = self.imm8()
        return d - 256 if d >= 128 else d

    def cp(self, n):
        """cp a,n -- sets Z on equal, C when a < n (unsigned)."""
        self.fz = (self.a == n)
        self.fc = (self.a < n)

    def push(self, v):
        self.sp -= 3
        n = len(self.writes)
        self.w24(self.sp, v)
        del self.writes[n:]          # stack traffic is not an observable write

    def pop(self):
        v = self.r24(self.sp)
        self.sp += 3
        return v

    # ---- 8-bit ALU helpers ----
    def _add8(self, n):
        r = self.a + n
        self.fc = r > 0xFF
        self.a = r & 0xFF
        self.fz = (self.a == 0)

    def _sub8(self, n):
        r = self.a - n
        self.fc = r < 0
        self.a = r & 0xFF
        self.fz = (self.a == 0)

    def _logic(self, v):
        self.a = v & 0xFF
        self.fz = (self.a == 0)
        self.fc = False

    def _sbc16(self, rr):
        r = self.hl - rr - (1 if self.fc else 0)
        self.fc = r < 0
        self.hl = r & 0xFFFFFF
        self.fz = (self.hl == 0)

    def _add16(self, rr):
        r = self.hl + rr
        self.fc = r > 0xFFFFFF
        self.hl = r & 0xFFFFFF

    def _rot_mem(self, addr, op):
        """sla / rl on a memory byte. Returns nothing; sets Z and C."""
        v = self.r8(addr)
        cin = 1 if (self.fc and op == 'rl') else 0
        self.fc = bool(v & 0x80)
        v = ((v << 1) | cin) & 0xFF
        self.w8(addr, v)
        self.fz = (v == 0)

    def step(self):
        if self.pc == self.engine_at:
            self._engine()
            return True
        if self.pc == self.menu_at:
            self._menu()
            return True
        if self.pc in (CX_MAIN, CX_MAIN + 1):
            self._cxmain()
            return True
        op = self.imm8()

        # ---- 8-bit loads ----
        if   op == 0x3E: self.a = self.imm8()                      # ld a,n
        elif op == 0x06: self.b = self.imm8()                      # ld b,n
        elif op == 0x0E: self.c = self.imm8()                      # ld c,n
        elif op == 0x78: self.a = self.b                           # ld a,b
        elif op == 0x79: self.a = self.c                           # ld a,c
        elif op == 0x47: self.b = self.a                           # ld b,a
        elif op == 0x4F: self.c = self.a                           # ld c,a
        elif op == 0x41: self.b = self.c                           # ld b,c
        elif op == 0x5F: self.de = (self.de & 0xFFFF00) | self.a   # ld e,a
        elif op == 0x59: self.de = (self.de & 0xFFFF00) | self.c   # ld e,c
        elif op == 0x6F: self.hl = (self.hl & 0xFFFF00) | self.a   # ld l,a
        elif op == 0x7D: self.a = self.hl & 0xFF                   # ld a,l
        elif op == 0x7E: self.a = self.r8(self.hl)                 # ld a,(hl)
        elif op == 0x4E: self.c = self.r8(self.hl)                 # ld c,(hl)
        elif op == 0x46: self.b = self.r8(self.hl)                 # ld b,(hl)
        elif op == 0x5E: self.de = (self.de & 0xFFFF00) | self.r8(self.hl)         # ld e,(hl)
        elif op == 0x56: self.de = (self.de & 0xFF00FF) | self.r8(self.hl) << 8    # ld d,(hl)
        elif op == 0x3A: self.a = self.r8(self.imm24())            # ld a,(nn)
        elif op == 0x32: self.w8(self.imm24(), self.a)             # ld (nn),a
        elif op == 0x36: self.w8(self.hl, self.imm8())             # ld (hl),n
        elif op == 0x71: self.w8(self.hl, self.c)                  # ld (hl),c
        elif op == 0x12: self.w8(self.de, self.a)                  # ld (de),a
        elif op == 0x77: self.w8(self.hl, self.a)                  # ld (hl),a
        elif op == 0x72: self.w8(self.hl, self.de >> 8)            # ld (hl),d
        elif op == 0x73: self.w8(self.hl, self.de)                 # ld (hl),e
        elif op == 0x7A: self.a = (self.de >> 8) & 0xFF            # ld a,d
        elif op == 0x7B: self.a = self.de & 0xFF                   # ld a,e
        elif op == 0x37: self.fc = True                            # scf
        elif op == 0x57: self.de = (self.de & 0xFF00FF) | self.a << 8  # ld d,a
        elif op == 0x58: self.de = (self.de & 0xFFFF00) | self.b       # ld e,b
        elif op == 0x1E: self.de = (self.de & 0xFFFF00) | self.imm8()  # ld e,n

        # ---- 24-bit loads ----
        elif op == 0x01: self.bcset(self.imm24())                  # ld bc,nn
        elif op == 0x11: self.de = self.imm24()                    # ld de,nn
        elif op == 0x21: self.hl = self.imm24()                    # ld hl,nn
        elif op == 0x2A: self.hl = self.r24(self.imm24())          # ld hl,(nn)
        elif op == 0x22: self.w24(self.imm24(), self.hl)           # ld (nn),hl
        elif op == 0xF9: self.sp = self.hl                         # ld sp,hl

        # ---- arithmetic ----
        elif op == 0xC6: self._add8(self.imm8())                   # add a,n
        elif op == 0xD6: self._sub8(self.imm8())                   # sub a,n
        elif op == 0x91: self._sub8(self.c)                        # sub a,c
        elif op == 0x80: self._add8(self.b)                        # add a,b
        elif op == 0x81: self._add8(self.c)                        # add a,c
        elif op == 0x87: self._add8(self.a)                        # add a,a
        elif op == 0x3C:                                           # inc a
            self.a = (self.a + 1) & 0xFF; self.fz = (self.a == 0)
        elif op == 0x3D:                                           # dec a
            self.a = (self.a - 1) & 0xFF; self.fz = (self.a == 0)
        elif op == 0x2C:                                           # inc l
            # 8-bit: touches only the low byte of HL, never carries into h.
            self.hl = (self.hl & 0xFFFF00) | ((self.hl + 1) & 0xFF)
        elif op == 0x23: self.hl = (self.hl + 1) & 0xFFFFFF        # inc hl
        elif op == 0x13: self.de = (self.de + 1) & 0xFFFFFF        # inc de
        elif op == 0x09: self._add16(self.bc())                 # add hl,bc
        elif op == 0x19: self._add16(self.de)                      # add hl,de
        elif op == 0x29: self._add16(self.hl)                      # add hl,hl
        elif op == 0x39: self._add16(self.sp)                      # add hl,sp
        elif op == 0x17:                                           # rla
            cin = 1 if self.fc else 0
            self.fc = bool(self.a & 0x80)
            self.a = ((self.a << 1) | cin) & 0xFF

        # ---- logic / compare ----
        elif op == 0xB7: self._logic(self.a)                       # or a,a
        elif op == 0xB1: self._logic(self.a | self.c)              # or a,c
        elif op == 0xAF: self._logic(0)                            # xor a,a
        elif op == 0xE6: self._logic(self.a & self.imm8())         # and a,n
        elif op == 0xEE: self._logic(self.a ^ self.imm8())         # xor a,n
        elif op == 0xFE: self.cp(self.imm8())                      # cp a,n
        elif op == 0xBF: self.cp(self.a)                           # cp a,a
        elif op == 0xB8: self.cp(self.b)                           # cp a,b
        elif op == 0xB9: self.cp(self.c)                           # cp a,c
        elif op == 0xBA: self.cp((self.de >> 8) & 0xFF)            # cp a,d
        elif op == 0xBB: self.cp(self.de & 0xFF)                   # cp a,e

        # ---- stack ----
        elif op == 0xE5: self.push(self.hl)
        elif op == 0xD5: self.push(self.de)
        elif op == 0xC5: self.push(self.bc())
        elif op == 0xF5: self.push((self.a << 8) | (0x40 if self.fz else 0)
                                                 | (0x01 if self.fc else 0))
        elif op == 0xE1: self.hl = self.pop()
        elif op == 0xD1: self.de = self.pop()
        elif op == 0xC1: self.bcset(self.pop())
        elif op == 0xF1:
            v = self.pop()
            self.a = (v >> 8) & 0xFF
            self.fz = bool(v & 0x40); self.fc = bool(v & 0x01)
        elif op == 0xEB: self.de, self.hl = self.hl, self.de       # ex de,hl
        elif op == 0xE3:                                            # ex (sp),hl
            t = self.r24(self.sp); self.w24(self.sp, self.hl); self.hl = t

        # ---- control ----
        elif op == 0x18:
            # `self.pc += self.rel()` is WRONG here: Python loads self.pc for
            # the augmented assignment before rel() advances it past the
            # displacement byte, so the jump lands one byte short. Sequence it.
            d = self.rel(); self.pc += d
        elif op == 0x20: d = self.rel(); self.pc += d if not self.fz else 0
        elif op == 0x28: d = self.rel(); self.pc += d if self.fz else 0
        elif op == 0x30: d = self.rel(); self.pc += d if not self.fc else 0
        elif op == 0x38: d = self.rel(); self.pc += d if self.fc else 0
        elif op == 0x10:                                           # djnz
            d = self.rel()
            self.b = (self.b - 1) & 0xFF
            if self.b: self.pc += d
        elif op == 0xE9: self.pc = self.hl                         # jp (hl)
        elif op == 0xC0:                                           # ret nz
            if not self.fz:
                if not self.nest: return False
                self.nest -= 1
                self.pc = self.pop()
        elif op == 0xC9:                                           # ret
            if not self.nest: return False
            self.nest -= 1            # the hook's call back from cxMain returning to it
            self.pc = self.pop()
        elif op == 0xC3:                                           # jp nn
            # Only ever out of the body, to throw: the OS unwinds from there.
            t = self.imm24()
            if self.jp_in and self.org <= t < self.end:
                self.pc = t
                return True
            if t != JERROR:
                raise Trap("jp %06X at %06X" % (t, self.pc - 4))
            self.thrown = self.a
            return False
        elif op == 0xCD:                                           # call nn
            # Into the OS. Nothing to run, so record it and come straight
            # back; the flags are whatever the hook sets afterwards.
            self.calls.append((self.imm24(), self.a, self.hl, self.de, self.bc()))
            if self.calls[-1][0] == CHK_FIND_SYM:
                # Only prog # exists: a size word, then the entry's tokens.
                name = bytes(self.r8(OP1 + i) for i in range(3))
                self.fc = name != b'\x05#\x00' or self.prog is None
                self.de = 0xBADBAD if self.fc else self.prog
                self.hl, self.a = 0xBADBAD, 5
                self.bcset(0xBADBAD)

        # ---- DD (ix) prefix ----
        elif op == 0xDD:
            o2 = self.imm8()
            if   o2 == 0xE5: self.push(self.ix)
            elif o2 == 0xE1: self.ix = self.pop()
            elif o2 == 0x21: self.ix = self.imm24()                # ld ix,nn
            elif o2 == 0x39: self.ix = (self.ix + self.sp) & 0xFFFFFF  # add ix,sp
            elif o2 == 0xCB:
                d = self.disp(); o3 = self.imm8()
                if   o3 == 0x26: self._rot_mem(self.ix + d, 'sla')
                elif o3 == 0x16: self._rot_mem(self.ix + d, 'rl')
                else: raise Trap("unimplemented DD CB %02X" % o3)
            else:
                d = self.disp()
                if   o2 == 0x7E: self.a = self.r8(self.ix + d)     # ld a,(ix+d)
                elif o2 == 0x77: self.w8(self.ix + d, self.a)      # ld (ix+d),a
                elif o2 == 0x36: self.w8(self.ix + d, self.imm8()) # ld (ix+d),n
                elif o2 == 0x86: self._add8(self.r8(self.ix + d))  # add a,(ix+d)
                elif o2 == 0xBE: self.cp(self.r8(self.ix + d))     # cp a,(ix+d)
                elif o2 == 0x07: self.bcset(self.r24(self.ix + d)) # ld bc,(ix+d)
                elif o2 == 0x17: self.de = self.r24(self.ix + d)   # ld de,(ix+d)
                elif o2 == 0x27: self.hl = self.r24(self.ix + d)   # ld hl,(ix+d)
                elif o2 == 0x1F: self.w24(self.ix + d, self.de)    # ld (ix+d),de
                elif o2 == 0x2F: self.w24(self.ix + d, self.hl)    # ld (ix+d),hl
                elif o2 == 0x31: self.iy = self.r24(self.ix + d)   # ld iy,(ix+d)
                elif o2 in (0x34, 0x35):                           # inc/dec (ix+d)
                    v = (self.r8(self.ix + d) + (1 if o2 == 0x34 else -1)) & 0xFF
                    self.w8(self.ix + d, v); self.fz = (v == 0)
                else: raise Trap("unimplemented DD %02X" % o2)

        # ---- FD (iy) prefix: saving it round the engine call; the menu's pixel codec ----
        elif op == 0xFD:
            o2 = self.imm8()
            if   o2 == 0xE5: self.push(self.iy)
            elif o2 == 0xE1: self.iy = self.pop()
            elif o2 == 0x09:                                       # add iy,bc
                r = self.iy + self.bc(); self.fc = r > 0xFFFFFF; self.iy = r & 0xFFFFFF
            else:
                a = self.iy + self.disp()
                if   o2 == 0x27: self.hl = self.r24(a)             # ld hl,(iy+d)
                elif o2 == 0x17: self.de = self.r24(a)             # ld de,(iy+d)
                elif o2 == 0x1F: self.w24(a, self.de)              # ld (iy+d),de
                elif o2 == 0x0F: self.w24(a, self.bc())            # ld (iy+d),bc
                elif o2 == 0x73: self.w8(a, self.de)               # ld (iy+d),e
                elif o2 == 0x72: self.w8(a, self.de >> 8)          # ld (iy+d),d
                else: raise Trap("unimplemented FD %02X" % o2)

        # ---- CB prefix ----
        elif op == 0xCB:
            o2 = self.imm8()
            # set b,(hl) is 11 bbb 110, res b,(hl) is 10 bbb 110, bit b,(hl) is
            # 01 bbb 110; bit b,a is 01 bbb 111. bit leaves Z set when
            # the bit is CLEAR, which is what the mode gate branches on.
            if o2 & 0xC7 == 0xC6:
                self.w8(self.hl, self.r8(self.hl) | (1 << ((o2 >> 3) & 7)))
            elif o2 & 0xC7 == 0x86:
                self.w8(self.hl, self.r8(self.hl) & ~(1 << ((o2 >> 3) & 7)))
            elif o2 & 0xC7 == 0x46:
                self.fz = not (self.r8(self.hl) & (1 << ((o2 >> 3) & 7)))
            elif o2 & 0xC7 == 0x47:                                # bit b,a
                self.fz = not (self.a & (1 << ((o2 >> 3) & 7)))
            elif o2 in (0x19, 0x39):                               # rr c / srl c
                v = self.c >> 1 | (0x80 if o2 == 0x19 and self.fc else 0)
                self.fc = bool(self.c & 1); self.c = v; self.fz = (v == 0)
            else: raise Trap("unimplemented CB %02X at %06X" % (o2, self.pc - 2))

        # ---- ED prefix ----
        elif op == 0xED:
            o2 = self.imm8()
            if   o2 == 0x52: self._sbc16(self.de)                  # sbc hl,de
            elif o2 == 0x42: self._sbc16(self.bc())                # sbc hl,bc
            elif o2 == 0x5B: self.de = self.r24(self.imm24())      # ld de,(nn)
            elif o2 == 0x53: self.w24(self.imm24(), self.de)       # ld (nn),de
            elif o2 == 0x17: self.de = self.r24(self.hl)           # ld de,(hl)
            elif o2 == 0x27: self.hl = self.r24(self.hl)           # ld hl,(hl)
            elif o2 == 0x1F: self.w24(self.hl, self.de)            # ld (hl),de
            elif o2 == 0x12: self.de = (self.ix + self.disp()) & 0xFFFFFF  # lea de,ix+d
            elif o2 == 0x22: self.hl = (self.ix + self.disp()) & 0xFFFFFF  # lea hl,ix+d
            elif o2 == 0x33: self.iy = (self.iy + self.disp()) & 0xFFFFFF  # lea iy,iy+d
            elif o2 == 0x4B: self.bcset(self.r24(self.imm24()))    # ld bc,(nn)
            elif o2 == 0x62: self._sbc16(self.hl)                  # sbc hl,hl
            elif o2 == 0x5C:                                       # mlt de
                self.de = ((self.de >> 8) & 0xFF) * (self.de & 0xFF)
            elif o2 == 0xB0:                                       # ldir
                n = self.bc()
                if n == 0: n = 0x1000000       # what the hardware really does
                for _ in range(n):
                    self.w8(self.de, self.r8(self.hl))
                    self.hl = (self.hl + 1) & 0xFFFFFF
                    self.de = (self.de + 1) & 0xFFFFFF
                self.bcset(0)
            else: raise Trap("unimplemented ED %02X at %06X" % (o2, self.pc - 2))
        else:
            raise Trap("unimplemented opcode %02X at %06X" % (op, self.pc - 1))
        return True

    def run(self, limit=400000):
        while self.step():
            self.steps += 1
            if self.steps > limit:
                raise Trap("ran away: %d steps without RET" % self.steps)
        return self


def load_hook(bin_path, xp_path):
    """The assembled hook body, checked to be byte-for-byte what the app ships.

    The app carries hook.bin verbatim (it is position independent, so it has no
    relocation entries), so the body must appear unchanged in the file that
    carries it: SYMCE1.8xv, the first part of the app image."""
    body = open(bin_path, 'rb').read()
    if not body or body[0] != 0x83:
        raise SystemExit("hook body missing its 0x83 magic byte")
    if body not in open(xp_path, 'rb').read():
        raise SystemExit("%s does not carry %s -- run make" % (xp_path, bin_path))
    return body


def run_hook(body, entry, a=1, b=0x05, cx=0x40, edit_open=True, org=0x100000,
             warm=False, parse_inp=False, virgin=False, app_running=False,
             text_flags=0x02, base_ok=True, base_delta=0, last_ans=b'', op1=b'',
             mathprint=False, dec=False, deg=False, cmd=None, tail=0):
    """Runs one hook call -- the KEY call by default, ENTER on `entry`.

    `entry` goes where the OS leaves it flattened by A=2, the temp program #
    (a size word, then the tokens), and must come back untouched. Returns the
    sim; PENDING reads 0xFF if this ENTER was marked. Run evaluate() and then
    display() on it for the rest.
    """
    s = Sim(body, org=org, tail=tail)
    s.prog = ENTRY_AT - 2
    s.w8(ENTRY_AT - 2, len(entry) & 0xFF)
    s.w8(ENTRY_AT - 1, len(entry) >> 8)
    for i, t in enumerate(entry):
        s.w8(ENTRY_AT + i, t)
    s.w8(CX_CUR_APP, cx)
    s.w8(EDIT_FLAGS, 0x04 if edit_open else 0x00)
    # Classic reports 0x02 on an open entry line, MathPrint 0x20; insert (bit
    # 4) is clear because the OS opens every entry in overwrite. The hook is
    # supposed to turn insert on; the caller reads it back off the sim.
    s.w8(TEXT_FLAGS, text_flags)
    s.w8(MP_FLAGS, 0x20 if mathprint else 0x00)
    s.w8(ANS_FLAGS, 0x01 if dec else 0x00)
    s.w8(IY_OS, 0x04 if deg else 0x00)   # trigFlags bit 2: MODE DEGREE
    # The per-entry baseline: editTop as it was while the line was still empty.
    # base_delta != 0 is the cursor inside a MathPrint box, where editTop has
    # moved (measured on OS 5.8.4: virgin and flat D1A8CA, cube root D1A90F).
    top = 0xD1A8CA
    s.w24(EDIT_TOP, top)
    s.w24(BASE_TOP, top - base_delta)
    s.w8(BASE_OK, 0xA5 if base_ok else 0x00)
    s.w8(APD_FLAGS, 0x10 if warm else 0x00)
    s.w8(ON_FLAGS,  0x02 if parse_inp else 0x00)
    s.w8(CMD_FLAGS, (0x20 if virgin else 0x00) if cmd is None else cmd)
    s.w8(API_FLG1,  0x10 if app_running else 0x00)
    # The OS is calling us through the hook pointer, so it holds our base.
    s.w24(HOOK_PTR, org)
    # A key must clear whatever was left pending; seed a stale mark to check.
    # Nothing else may touch it: A=2 lands between ENTER and its A=0.
    s.w8(PENDING, 0x5A if a == 1 else 0)
    # What Ans stands for: tokens SymCE showed (length first), the OS's real
    # (0xFF first), or nothing. op1 is the OS's own result, there at A=0.
    if last_ans[:1] != b'\xff':
        last_ans = bytes([len(last_ans)]) + last_ans
    for i, t in enumerate(last_ans):
        s.w8(LAST_ANS + i, t)
    for i, t in enumerate(op1):
        s.w8(OP1 + i, t)

    s.writes = []
    s.a, s.b = a, b
    s.pc = org + 1                  # the OS enters just past the magic byte
    s.run()

    if bytes(s.r8(ENTRY_AT + i) for i in range(len(entry))) != bytes(entry):
        raise Trap("the entry was rewritten")
    if a == 1 and s.r8(PENDING) not in (0, 0xFF):
        raise Trap("a key event left a stale pending mark")
    return s


def evaluate(s, cx=0x40):
    """The OS is about to evaluate the entry and asks the hook first, with A=2.
    Returns nz: the hook answered it, and the OS must skip evaluating it. Like
    display(), it must give back iy, hl, de, bc, the stack, and OP1."""
    op1 = bytes(s.r8(OP1 + i) for i in range(9))
    nz = _call(s, 2, cx)
    if bytes(s.r8(OP1 + i) for i in range(9)) != op1:
        raise Trap("A=2 did not give the OS its OP1 back")
    return nz


def display(s, cx=0x40):
    """The last call: the OS asks the hook, with A=0, whether it wants to
    display the result itself.

    Returns (call, nz). call is the recorded (target, a, hl, de, bc) of the
    one OS call the hook may make, or None; nz is what the hook returned,
    which is what tells the OS to draw nothing of its own. Whatever it
    decides, it must hand the OS back its iy, the registers it came in with,
    and a balanced stack.
    """
    s.calls = []
    nz = _call(s, 0, cx)
    if len(s.calls) > 1:
        raise Trap("A=0 made %d OS calls" % len(s.calls))
    return (s.calls[0] if s.calls else None), nz


def _call(s, a, cx):
    s.w8(CX_CUR_APP, cx)
    s.writes = []
    s.a = a
    s.hl, s.de, s.iy, sp = 0x123456, 0x234567, IY_OS, s.sp
    s.bcset(0x345678)
    s.pc = s.org + 1
    s.run()
    if s.thrown is not None:
        return True               # the OS's error handler takes it from here
    if s.iy != IY_OS:
        raise Trap("A=%d left iy at %06X; the OS needs its flags pointer back" % (a, s.iy))
    if s.sp != sp:
        raise Trap("A=%d left the stack %+d bytes off" % (a, s.sp - sp))
    if (s.hl, s.de, s.bc()) != (0x123456, 0x234567, 0x345678):
        raise Trap("A=%d did not restore hl, de, bc" % a)
    return not s.fz


def drawn_text(s):
    """The NUL-terminated text the hook left at ANS_BUF for the OS to draw."""
    out = bytearray()
    a = ANS_BUF
    while s.r8(a) and len(out) < 80:
        out.append(s.r8(a)); a += 1
    return bytes(out)


# 0x91FC2 reads the text as glyphs and files (then draws) them as tokens: 0xB19FB,
# table 0xB1AB2, OS 5.8.4. The 20 one-to-one pairs, glyph first; the table's
# five two-byte ones are glyphs no answer holds. Glyph 0x10 is the root sign:
# it becomes sqrt( and takes the "(" drawn after it along.
GLYPH_TOKEN = dict(zip(*[iter(bytes.fromhex(
    "2e3a 1b3b 2029 1ab0 2d71 2b70 7b08 7d09 c106 5d07"
    "2c2b 2810 2911 222a 140b 5ef0 d72c 27ae 140b c4ac"))] * 2))


def os_tokens(glyphs):
    """What the OS makes of the text: the tokens it files and draws."""
    out, i = bytearray(), 0
    while i < len(glyphs):
        if glyphs[i] == 0x10:
            out.append(0xBC); i += 2
        elif glyphs[i] == 0xF6:                 # the n/d bar
            out += b'\xef\x2e'; i += 1
        else:
            out.append(GLYPH_TOKEN.get(glyphs[i], glyphs[i])); i += 1
    return bytes(out)
