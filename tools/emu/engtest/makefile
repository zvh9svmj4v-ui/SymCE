# On-device check of the shipped engine; tools/emu/engine_device.py generates
# src/vectors.h, builds this, and runs it on the real ROM.
#
# The engine is linked as the exact assembly the app carries -- symce's
# obj/engine/engine.s -- not recompiled with this makefile's flags or LTO.
NAME = ENGTEST
COMPRESSED = NO
ARCHIVED = NO
HAS_PRINTF = NO
CFLAGS = -Wall -Wextra -Oz
EXTRA_ASMSOURCES = ../../../symce/obj/engine/engine.s

include $(shell cedev-config --makefile)
