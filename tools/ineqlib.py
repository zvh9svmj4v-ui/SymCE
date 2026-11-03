"""Tiny encoder/decoder for the host engine_cli: text <-> TI tokens (the subset
the inequality, PARTFRAC and their tests use)."""
import subprocess, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
_c = open(os.path.join(HERE, '..', 'symce', 'src', 'engine.c')).read().split('static const char cmds[] =')[1].split(';')[0]
NAMES = set(re.findall(r'[A-Z]+', _c.replace('\\0', ' ')))   # a command is typed as lowercase tokens (BB B0 a .. j, BB BB k .. z)

CLI = os.path.join(HERE, '..', 'symce', 'bin', 'host', 'engine_cli')

ENC = {'(': 0x10, ')': 0x11, '+': 0x70, '-': 0x71, '*': 0x82, '/': 0x83, '^': 0xF0,
       '=': 0x6A, '<': 0x6B, '>': 0x6C, '≤': 0x6D, '≥': 0x6E, '≠': 0x6F,
       '²': 0x0D, '³': 0x0F, ',': 0x2B, '.': 0x3A, '~': 0xB0, '√': 0xBC, '|': 0xB2, '{': 0x08, '}': 0x09, "'": 0xAE, '&': 0x40, '!': 0x2D}
DEC = {0x28: '(', 0x10: '(', 0x11: ')', 0x70: '+', 0x71: '-', 0x82: '*', 0x83: '/', 0xF0: '^',
       0x6A: '=', 0x6B: '<', 0x6C: '>', 0x6D: '≤', 0x6E: '≥', 0x6F: '≠', 0x0D: '²', 0x0F: '³',
       0x2B: ',', 0x2C: ',', 0x3A: '.', 0xB0: '-', 0xBC: '√(', 0xB2: 'abs(', 0x3C: ' or ',
       0xD7: 'i', 0x2D: '!', 0x08: '{', 0x09: '}'}


def enc(s):
    out = bytearray()
    i = 0
    while i < len(s):
        c = s[i]
        m = re.match(r'([A-Z]+)\(', s[i:])
        if m and m.group(1) in NAMES:
            for ch in m.group(1):
                out += bytes([0xBB, 0xB0 + ord(ch) - 65 + (ch >= 'K')])
            i += len(m.group(1)); continue
        if s.startswith('abs(', i):
            out.append(0xB2); i += 4; continue
        if c == '~': out.append(0xB0)
        elif c in ENC: out.append(ENC[c])
        elif c.isdigit(): out.append(ord(c))
        elif c.isalpha() and c.isupper(): out.append(ord(c))
        else: raise ValueError(c)
        i += 1
    return bytes(out)


def dec(b):
    s = ''
    for x in b:
        if x in DEC: s += DEC[x]
        elif 0x30 <= x <= 0x39 or 0x41 <= x <= 0x5A: s += chr(x)
        else: s += '<%02x>' % x
    return s


class Eng:
    def __init__(self, mode=''):
        self.p = subprocess.Popen([CLI], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        self.mode = mode

    def ask(self, entry, mode=None):
        """entry text -> answer text, 'PASS', or 'E:message'"""
        m = self.mode if mode is None else mode
        self.p.stdin.write(m + enc(entry).hex() + '\n'); self.p.stdin.flush()
        r = self.p.stdout.readline().strip()
        if r == 'PASS': return r
        if r.startswith('E'):
            raw = bytes.fromhex(r[1:])
            return 'E:' + raw.split(b'\0')[0].decode() + '|' + raw.split(b'\0')[1].decode()
        return dec(bytes.fromhex(r))
