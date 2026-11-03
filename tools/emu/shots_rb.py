#!/usr/bin/env python3
"""Colour screenshots of nested parentheses, Classic and MathPrint: python3 shots_rb.py out_dir"""
import os, sys, e2e, vram
KEYS = "( ( 1 + 2 ) * ( 3 + ( 4 - 5 ) ) ) + sin ( ( 6 ) )"
for name, pre in (("classic", "C "), ("mathprint", "")):
    px, _ = vram.grab(vram.SYMCE + pre + KEYS + " enter . . ( ( ( ( ( ( ( 1 ) ) ) ) ) ) ) w300", 0, 240, 1, 1, files=vram.FILES, launch=e2e.LAUNCH, lead=False)
    vram.png(px, os.path.join(sys.argv[1], "rainbow_%s.png" % name), 3)
