/*
 * init::start will pass stacktop to setup() as the start of free memory.
 * setup() will then move the FDT and initrd to the base of that
 * free memory before calling mem_init(). With those movements and
 * this linker script, we'll end up having the following memory layout:
 *
 *    +----------------------+   <-- top of physical memory
 *    |                      |
 *    ~                      ~
 *    |                      |
 *    +----------------------+   <-- top of initrd
 *    |                      |
 *    +----------------------+   <-- top of FDT
 *    |                      |
 *    +----------------------+   <-- top of cpu0's stack
 *    |                      |
 *    +----------------------+   <-- top of text/data/bss sections,
 *    |                      |       see arm/flat.lds
 *    |                      |
 *    +----------------------+   <-- load address
 *    |                      |
 *    +----------------------+
 */

SECTIONS
{
    .text : {
      _text = .;
      *(.init) *(.text) *(.text.*)
    }
    . = ALIGN(64K);
    PROVIDE(_etext = .);

    PROVIDE(reloc_start = .);
    .rela.dyn : { *(.rela.dyn) }
    PROVIDE(reloc_end = .);
    .dynsym   : { *(.dynsym) }
    .dynstr   : { *(.dynstr) }
    .hash     : { *(.hash) }
    .gnu.hash : { *(.gnu.hash) }
    .got      : { *(.got) *(.got.plt) }
    .eh_frame : { *(.eh_frame) }

    .rodata   : { *(.rodata*) }
    .data     : { *(.data) }
    .bss      : { *(.bss) }
    . = ALIGN(64K);
    PROVIDE(edata = .);

    /*
     * stack depth is 16K for arm and PAGE_SIZE for arm64, see THREAD_SIZE
     * sp must be 16 byte aligned for arm64, and 8 byte aligned for arm
     * sp must always be strictly less than the true stacktop
     */
    . += 64K;
    . = ALIGN(64K);
    PROVIDE(stackptr = . - 16);
    PROVIDE(stacktop = .);

    /DISCARD/ : {
        *(.note*)
        *(.interp)
        *(.debug*)
        *(.comment)
        *(.dynamic)
    }
}

ENTRY(start)
