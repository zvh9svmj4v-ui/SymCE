"""Keys that make the home screen type a command, from the menus themselves.

A command is lowercase tokens that only the menus type (typing S O L V E by hand is a
product of variables), so the tests pick them: ALPHA+DOWN opens the popup (Algebra), RIGHT
moves to the next tab, a digit (0 = tenth, then MATH APPS PRGM x^-1) picks an item, and
an item that opens a submenu is picked the same way. The menus are read from symce/src/menu.c."""
import os, re

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'symce', 'src', 'menu.c')
ITEMKEYS = "1 2 3 4 5 6 7 8 9 0 math apps prgm -1".split()
def pick(i):
    """Keys that pick item i: a digit key for the first 14, DOWN then ENTER beyond."""
    return [ITEMKEYS[i]] if i < len(ITEMKEYS) else ['down'] * i + ['enter', '.']


TABS = (0, 6, 9)           # menus shown as tabs: Algebra, Calculus, Geometry (menu.c tops[])


def _menus():
    src = open(SRC).read().split('static const char M[] =')[1].split(';')[0]
    raw = ''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', src))
    buf, i = bytearray(), 0
    while i < len(raw):
        c = raw[i]
        if c != '\\':
            buf += c.encode('latin-1'); i += 1; continue
        i += 1
        if raw[i] == 'x':
            m = re.match(r'[0-9a-fA-F]+', raw[i + 1:]); buf.append(int(m.group(), 16) & 255); i += 1 + len(m.group())
        elif raw[i] in '01234567':
            m = re.match(r'[0-7]{1,3}', raw[i:]); buf.append(int(m.group(), 8)); i += len(m.group())
        else:
            buf += raw[i].encode(); i += 1
    parts = bytes(buf).split(b'\0')
    menus, k = [], 0
    while k < len(parts) - 1:
        title, items = parts[k].decode('latin-1'), []
        k += 1
        while parts[k]:
            items.append((parts[k].decode('latin-1'), parts[k + 1])); k += 2
        k += 1
        menus.append((title, items))
    return menus


def path(name):
    """Keys, after ALPHA+DOWN, that type `name(`."""
    menus = _menus()
    want = name.lower() + '('

    def find(m, keys):
        for i, (label, typed) in enumerate(menus[m][1]):
            if typed and typed[0] < 32:
                r = find(typed[0], keys + pick(i))
                if r: return r
            elif (typed.decode('latin-1') if typed else label).lower() == want:
                return keys + pick(i)
    for t, m in enumerate(TABS):
        r = find(m, ['right'] * t)
        if r: return r
    raise KeyError(name)


def spell(name):
    """Key names that type a command's name and its '(' through the popup."""
    return ' '.join(['alpha', 'down'] + path(name))


def tokens(name):
    """A command's name as the menus type it: lowercase tokens (BB B0 a .. j, BB BB k .. z)."""
    return b"".join(bytes([0xBB, 0xB0 + ord(c) - 65 + (c >= "K")]) for c in name)
