#!/usr/bin/env python3
"""Turns an autotester CRC back into the bytes it was computed over.

The autotester will hash any address range, but only ever reports the CRC. For
a range no longer than four bytes that is not a limitation: CRC-32C is affine
in the message, so the map from a fixed-length message to its CRC is invertible
by linear algebra over GF(2) -- 8*n+1 CRC evaluations, not 2^(8n) guesses.

    ./crcread.py 6064A37A 3     ->  the three bytes at the hashed address
"""
import sys

POLY_REV = 0x82F63B78       # CRC-32C (0x1EDC6F41) reflected, the usual form

TABLE = []
for _b in range(256):
    _c = _b
    for _ in range(8):
        _c = (_c >> 1) ^ (POLY_REV if _c & 1 else 0)
    TABLE.append(_c)


def crc32c(data):
    c = 0xFFFFFFFF
    for b in data:
        c = TABLE[(c ^ b) & 0xFF] ^ (c >> 8)
    return c ^ 0xFFFFFFFF


def invert(target, n):
    """Solve for the n-byte message whose CRC is `target`.

    CRC is affine: crc(a^b) == crc(a) ^ crc(b) ^ crc(0). So crc(m) is crc(0)
    xored with one fixed column per set bit of m, and recovering m is an
    XOR-basis solve over the 8n columns.
    """
    base = crc32c(bytes(n))
    cols = []
    for bit in range(8 * n):
        m = bytearray(n)
        m[bit // 8] = 1 << (bit % 8)
        cols.append(crc32c(bytes(m)) ^ base)

    basis = {}                                  # pivot bit -> (vector, mask)
    for i, v in enumerate(cols):
        msk = 1 << i
        for b in range(31, -1, -1):
            if not (v >> b) & 1:
                continue
            if b in basis:
                pv, pm = basis[b]
                v ^= pv
                msk ^= pm
            else:
                basis[b] = (v, msk)
                break

    rem, sol = target ^ base, 0
    for b in range(31, -1, -1):
        if (rem >> b) & 1:
            if b not in basis:
                raise SystemExit("no %d-byte message has CRC %08X" % (n, target))
            pv, pm = basis[b]
            rem ^= pv
            sol ^= pm
    if rem:
        raise SystemExit("no %d-byte message has CRC %08X" % (n, target))

    out = bytes((sol >> (8 * i)) & 0xFF for i in range(n))
    assert crc32c(out) == target, "solved message does not reproduce the CRC"
    return out


if __name__ == '__main__':
    got = invert(int(sys.argv[1], 16), int(sys.argv[2]))
    print("bytes %s   little-endian 0x%06X" %
          (got.hex(' '), int.from_bytes(got, 'little')))
