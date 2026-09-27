/* The engine (or menu.c, linked with -e _symce_menu) as one flat, read-only
   blob: entry first, then code and constants. Linked at three bases and diffed
   by tools/relocs.py to find what to relocate; -Ttext picks the base.
   Writable data would land in flash, where stores do nothing, so any is a
   link error. */
ENTRY(_symce_engine)
SECTIONS
{
    .text : {
        *(.text._symce_engine)
        *(.text._symce_menu)
        *(.text .text.*)
        *(.rodata .rodata.*)
    }
    .data : { *(.data .data.*) *(.bss .bss.*) *(COMMON) }
    /DISCARD/ : { *(.note.*) *(.comment) }
}
ASSERT(SIZEOF(.data) == 0, "engine.c/menu.c must not have writable statics: it runs from flash")
