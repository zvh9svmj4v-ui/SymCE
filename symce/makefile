# SymCE: a flash app carrying the hook body (src/hook.c, hand-written eZ80) and
# the engine it calls (src/engine.c, compiled C).
#
#   make          bin/SYMCE.8xp, the installer, and bin/SYMCE1.8xv .., the app
#                 it installs. Send them all, run prgmSYMCE once.
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
        -fno-autolink -ffunction-sections -mllvm -z80-gas-style -Oz
# Three link addresses for tools/relocs.py, every byte of each difference nonzero.
BASES = 0x100000 0x223456 0x0ABCDE

# The app padded to this many bytes for make emu: over two appvars' worth.
BIG = 132000
OUT = bin$(if $(PAD),/pad$(PAD))

# One fasmg run writes the image (obj/SYMCE.img), the installer and the parts
# (obj/SYMCE.8xp, obj/SYMCE.v1 ..); the parts are renamed SYMCE1.8xv ..
$(OUT)/SYMCE.8xp: obj/hook/hook.bin obj/hook/mhook.bin obj/hook/fhook.bin obj/hook/lhook.bin obj/font.inc obj/engine.inc obj/menu.inc obj/graph.inc app/main.asm app/installer.asm app/ports.asm $(wildcard app/include/*.inc)
	@mkdir -p $(OUT)
	rm -f obj/SYMCE.* $(OUT)/SYMCE*.8xv
	cd app && $(FASMG) $(if $(PAD),-i 'PAD := $(PAD)') main.asm ../obj/SYMCE.img
	cp obj/SYMCE.8xp $@
	for f in obj/SYMCE.v*; do cp $$f $(OUT)/SYMCE$${f##*.v}.8xv; done

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

# The same engine.c built for the host, for engine_check.py and the hook sim,
# and minipoly, the engine it replaced, which engine_check.py diffs it against.
bin/host/engine_cli: src/engine.c ../tools/engine_cli.c
	@mkdir -p $(@D)
	cc -O2 -o $@ ../tools/engine_cli.c

bin/host/minipoly_cli: ../tools/minipoly_cli.c ../tools/minipoly.c
	@mkdir -p $(@D)
	cc -O2 -o $@ $^

# The grapher's compiler and evaluator, for graph_check.py.
bin/host/graph_cli: src/graph.c ../tools/graph_cli.c
	@mkdir -p $(@D)
	cc -O2 -o $@ ../tools/graph_cli.c -lm

# Run the assembled hook body under tools/ez80sim.py. These test the SHIPPED
# BYTES -- hook.bin, checked to sit verbatim in bin/SYMCE1.8xv -- not a C model
# of them, and they need no ROM, so they run on every build.
#
#   engine_check.py   engine.c against sympy, an independent TI parser, and
#                     tools/minipoly.c (the engine it replaced), byte for byte
#   hook_sim_test.py  gates, the pending mark, the engine call and what it must
#                     restore, the display hand-off, insert mode, bad pointers
check: bin/SYMCE.8xp bin/host/engine_cli bin/host/minipoly_cli bin/host/graph_cli
	@python3 ../tools/engine_check.py 4000 | tail -6
	@python3 ../tools/hook_sim_test.py | tail -1
	@python3 ../tools/graph_check.py | tail -2
	@python3 ../tools/mkfont.py check

# bigapp.py's no-room case installs tools/emu/old/SYMCE1.8xv, an early build
# kept as a fixture: under the 61 KB the ROM has free below its lowest app.
emu: bin/SYMCE.8xp
	$(MAKE) PAD=$(BIG)
	rm -f ../tools/emu/SYMCE*.8xv ../tools/emu/_pad/SYMCE*
	@mkdir -p ../tools/emu/_pad
	cp bin/SYMCE.8xp bin/SYMCE*.8xv ../tools/emu/
	cp bin/pad$(BIG)/SYMCE* ../tools/emu/_pad/
	cd ../tools/emu && python3 crash.py && python3 e2e.py && python3 lifecycle.py && \
	    python3 engine_device.py && python3 bigapp.py

clean:
	rm -rf bin obj

.PHONY: check emu clean
