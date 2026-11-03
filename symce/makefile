# SymCE: a flash app carrying the hook body (src/hook.c, hand-written eZ80) and
# the engine it calls (src/engine.c, compiled C).
#
#   make          bin/SYMCE.8xp, the installer, and bin/SYMCE1.8xv .., the app
#                 it installs, ZX0-packed (tools/zpack.py). Send them all, run prgmSYMCE once.
#   make PAD=n    the same with the app padded to n bytes, in bin/pad<n>
#   make check    engine fuzzed on the host; hook body under tools/ez80sim.py;
#                 the grapher's compiler and evaluator against Python's math
#   make emu      real-ROM tests in tools/emu, the shipped engine included,
#                 and the installer with a PAD=$(BIG) app (needs
#                 ~/CEdev/ti84pce.rom)
#
# Needs CEdev (ez80-clang, binutils, libcrt) and fasmg for the app;
# ../tools/get-fasmg.sh fetches fasmg into tools/bin. make check needs sympy.

CEDEV ?= $(HOME)/CEdev
FASMG ?= $(abspath ../tools/bin/fasmg)
BINUTILS = $(CEDEV)/binutils/bin/z80-none-elf
CLANG = $(CEDEV)/bin/ez80-clang -S -nostdinc -isystem $(CEDEV)/include -fno-addrsig \
        -fno-autolink -ffunction-sections -mllvm -z80-gas-style -Oz $(XFLAGS)
# Three link addresses for tools/relocs.py, every byte of each difference nonzero.
BASES = 0x100000 0x223456 0x0ABCDE

# The app padded to this many bytes for make emu: the shipped parts' size plus 8 KB, so the
# padded build is a different, bigger one at any app size, and at least 530,000 so the installer
# is always tried with many appvars. The installer takes up to 35 packed parts (SYMCE1..9, SYMCEA..Z): the same
# tests with make PAD=700000 / PAD=1000000 are in docs/TI84CE-KNOWLEDGE.md section 8 (env vars in bigapp.py/screen.py).
# bigapp.py sizes its ROMs from it.
BIG = $(shell python3 ../tools/zpack.py --size bin 2>/dev/null | awk '{n = $$1 + 8192; print (n < 530000 ? 530000 : n)}')   # 530000 on a clean tree, before bin/ exists
# a failing script behind `| tail` must fail the target
SHELL = /bin/bash
.SHELLFLAGS = -o pipefail -ec
OUT = bin$(if $(PAD),/pad$(PAD))

# One fasmg run writes the image (obj/SYMCE.img) and the installer (obj/SYMCE.8xp); zpack.py
# cuts the image into ZX0-packed parts SYMCE1.8xv .. and checks the installer carries their build id.
$(OUT)/SYMCE.8xp: bin/host/salvador ../tools/zpack.py obj/hook/hook.bin obj/hook/mhook.bin obj/hook/fhook.bin obj/hook/lhook.bin obj/font.inc obj/engine.inc obj/menu.inc obj/graph.inc app/main.asm app/installer.asm app/ports.asm $(wildcard app/include/*.inc)
	@mkdir -p $(OUT)
	rm -f obj/SYMCE.* $(OUT)/SYMCE*.8xv
	cd app && $(FASMG) $(if $(PAD),-i 'PAD := $(PAD)') main.asm ../obj/SYMCE.img
	cp obj/SYMCE.8xp $@
	python3 ../tools/zpack.py obj/SYMCE.img $@ $(OUT) --salvador bin/host/salvador

# The hook is all inline asm, so it goes straight to raw bytes: no C runtime,
# no linker. -fno-addrsig: GNU as rejects clang's .addrsig directive.
obj/hook/hook.bin obj/hook/mhook.bin obj/hook/fhook.bin obj/hook/lhook.bin: obj/hook/%.bin: src/%.c
	@mkdir -p obj/hook
	$(CLANG) $< -o $(@:.bin=.s)
	$(BINUTILS)-as -march=ez80+full $(@:.bin=.s) -o $(@:.bin=.o)
	$(BINUTILS)-objcopy -O binary --only-section=.text $(@:.bin=.o) $@

# The Evo font tables the font and localize hooks answer from, generated from
# the hand-edited glyph files.
obj/font.inc: font/large.txt font/small.txt ../tools/mkfont.py
	python3 ../tools/mkfont.py build

# The engine (and the ALPHA+DOWN menu, src/menu.c, entry symce_menu, and the
# grapher, src/graph.c, entry symce_graph, which also wants libc's sinf and
# powf) is ordinary C plus CEdev's runtime helpers (__ldivs and friends), each linked
# on its own at three bases: the first two differ exactly at its absolute
# addresses, which relocs.py turns into app relocations; the third checks them.
# The engine and the menu, not the grapher (its speed): nothing is inlined unless that is smaller, no jump tables (each
# entry is a relocation, 6 bytes), common code hoisted and sunk, spills deferred and split for size: -8 KB
obj/engine.inc obj/menu.inc: CLANG += -mllvm -inline-threshold=-20 -fno-jump-tables -mllvm -enable-gvn-hoist -mllvm -enable-gvn-sink -mllvm -enable-deferred-spilling -mllvm -split-spill-mode=size
obj/graph.inc: src/graph_stat.c src/graph_draw.c src/graph_draw.h src/graph_table.c src/graph_table.h
obj/%.inc: src/%.c src/engine.ld ../tools/relocs.py
	@mkdir -p obj/$*
	$(CLANG) $< -o obj/$*/$*.s
	$(BINUTILS)-as -march=ez80+full obj/$*/$*.s -o obj/$*/$*.o
	for b in $(BASES); do \
	    $(BINUTILS)-ld -T src/engine.ld -e _symce_$* -Ttext=$$b --gc-sections --no-warn-execstack \
	        obj/$*/$*.o $(CEDEV)/lib/crt/libcrt.a $(CEDEV)/lib/libc/libc.a $(CEDEV)/lib/crt/libcrt.a -o obj/$*/$$b.elf && \
	    $(BINUTILS)-objcopy -O binary obj/$*/$$b.elf obj/$*/$$b.bin || exit 1; \
	done
	python3 ../tools/relocs.py symce_$* $@ $(foreach b,$(BASES),$(b) obj/$*/$(b).bin)

# salvador (Emmanuel Marty, zlib licence, tools/salvador): the ZX0 compressor zpack.py runs.
bin/host/salvador: $(wildcard ../tools/salvador/src/*.c ../tools/salvador/src/libdivsufsort/lib/*.c)
	@mkdir -p $(@D)
	cc -O2 -I../tools/salvador/src/libdivsufsort/include -I../tools/salvador/src -o $@ \
	    ../tools/salvador/src/*.c ../tools/salvador/src/libdivsufsort/lib/*.c

# The same engine.c built for the host, for engine_check.py and the hook sim,
# and minipoly, the engine it replaced, which engine_check.py diffs it against.
bin/host/engine_cli: src/engine.c ../tools/engine_cli.c
	@mkdir -p $(@D)
	cc -O2 -o $@ ../tools/engine_cli.c

bin/host/minipoly_cli: ../tools/minipoly_cli.c ../tools/minipoly.c
	@mkdir -p $(@D)
	cc -O2 -o $@ $^

# The grapher's compiler and evaluator, for graph_check.py.
bin/host/graph_cli: src/graph.c src/graph_stat.c ../tools/graph_cli.c
	@mkdir -p $(@D)
	cc -O2 -o $@ ../tools/graph_cli.c -lm

# idraw (implicit equations and inequalities) on the host, for graph_check.py.
bin/host/implicit_cli: src/graph.c src/graph_stat.c ../tools/implicit_cli.c
	@mkdir -p $(@D)
	cc -O2 -w -o $@ ../tools/implicit_cli.c -lm

# Run the assembled hook body under tools/ez80sim.py. These test the SHIPPED
# BYTES -- hook.bin, checked to sit verbatim in bin/SYMCE1.8xv -- not a C model
# of them, and they need no ROM, so they run on every build.
#
#   engine_check.py   engine.c against sympy, an independent TI parser, and
#                     tools/minipoly.c (the engine it replaced), byte for byte
#   hook_sim_test.py  gates, the pending mark, the engine call and what it must
#                     restore, the display hand-off, insert mode, bad pointers
check: bin/SYMCE.8xp bin/host/engine_cli bin/host/minipoly_cli bin/host/graph_cli bin/host/implicit_cli
	@python3 ../tools/engine_check.py 4000 | tail -6
	@python3 ../tools/ineq_check.py 400 | tail -3
	@python3 ../tools/partfrac_check.py 300 | tail -3
	@python3 ../tools/hook_sim_test.py | tail -1
	@python3 ../tools/graph_check.py | tail -2
	@python3 ../tools/steps_check.py | tail -1
	@python3 ../tools/width_check.py | tail -1
	@python3 ../tools/mkfont.py check

# bigapp.py's no-room case installs tools/emu/old/SYMCE1.8xv, an early build
# kept as a fixture: under the 61 KB the ROM has free below its lowest app.
emu: bin/SYMCE.8xp
	$(MAKE) PAD=$(BIG)
	rm -f ../tools/emu/SYMCE*.8xv ../tools/emu/_pad/SYMCE*
	@mkdir -p ../tools/emu/_pad
	cp bin/SYMCE.8xp bin/SYMCE*.8xv ../tools/emu/
	cp bin/pad$(BIG)/SYMCE* ../tools/emu/_pad/
	cd ../tools/emu && python3 crash.py && python3 e2e.py && python3 help.py && python3 errs.py && python3 hist.py && python3 cursor.py && python3 lifecycle.py && \
	    python3 engine_device.py && python3 bigapp.py

clean:
	rm -rf bin obj

# The installer's ceiling: a 1,630,000-byte app = 35 packed parts (SYMCE1..9, A..Z). The fixture erases every TI app
# below 0x33A1CF to make room, which leaves 7 apps in the APPS menu (SymCE the 7th) and 7 in Mem Mgmt; the OS takes
# a minute to move a 1.6 MB app after a delete, so lifecycle waits 40 s there and 18 s for the install.
BIGMAX = 1630000
emubig:
	$(MAKE) PAD=$(BIGMAX)
	rm -f ../tools/emu/SYMCE*
	cp bin/pad$(BIGMAX)/SYMCE* ../tools/emu/
	cd ../tools/emu && export SYMCE_ROOMY_TO=0x33a1cf SYMCE_MENU_SKIP=10 SYMCE_MEM_SKIP=15 SYMCE_RELOC_MS=40000 SYMCE_WAIT_EXTRA=14 && \
	    python3 crash.py && python3 e2e.py && python3 lifecycle.py
	rm -f ../tools/emu/SYMCE*

.PHONY: check emu emubig clean
